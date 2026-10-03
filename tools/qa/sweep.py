#!/usr/bin/env python3
"""Headless move sweep runner (QA builds: `bam.py build --qa sweep`).

Runs the QA DOL in headless Dolphin (unthrottled, null video, no audio),
collects the sweep log (src/qa/qa_moves.c) and restarts the emulator after a
crash or freeze, skipping the step that broke.

  sweep.py iso                         build/qa/base.iso from build/output/main.dol
  sweep.py run --workers 2 [--start M] [--end M] [--tag NAME]
  sweep.py report [--tag NAME]         summary of build/qa/NAME/*.log

Matches: 0..25 native baselines, then recipient x donor pairs (674 total).
"""
from __future__ import annotations
import argparse
import os
import re
import select
import shutil
import signal
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
QA = ROOT / 'build' / 'qa'
# A headless Dolphin build (docs/QA.md) and the decomp's symbol map.
DOLPHIN = Path(os.environ.get('DOLPHIN', 'dolphin-emu-nogui'))
SYMBOLS = Path(os.environ.get('BAM_SYMBOLS', str(ROOT / '.cache/melee/config/GALE01/symbols.txt')))
TOTAL_MATCHES = 26 + 26 * 25 - 2
MARKER = b'QARS'
VIDEO = False

DOLPHIN_INI = """[Core]
CPUThread = True
EnableCheats = False
SlotA = 255
SlotB = 255
SerialPort1 = 255
SIDevice0 = 6
SIDevice1 = 0
SIDevice2 = 0
SIDevice3 = 0
EmulationSpeed = 0.00000000
GFXBackend = Null
DSPHLE = True
SkipIPL = True
OverclockEnable = False
[DSP]
Backend = No Audio Output
EnableJIT = True
[Interface]
UsePanicHandlers = False
ConfirmStop = False
OnScreenDisplayMessages = False
DebugModeEnabled = False
[Display]
RenderToMain = False
[Analytics]
Enabled = False
PermissionAsked = True
[AutoUpdate]
UpdateTrack =
"""
GFX_INI = """[Settings]
ShowFPS = False
[Hacks]
EFBAccessEnable = False
"""
LOGGER_INI = """[Options]
Verbosity = 4
WriteToConsole = True
WriteToFile = False
[Logs]
OSREPORT = True
OSREPORT_HLE = False
MI = True
POWERPC = True
BOOT = True
CORE = True
"""


def make_map(path: Path):
    """Dolphin symbol map with the functions it hooks (OSReport -> log)."""
    want = {'OSReport', 'OSPanic', 'vprintf', 'printf'}
    lines = ['.text section layout']
    for ln in SYMBOLS.read_text().splitlines():
        m = re.match(r'(\w+) = \.text:0x([0-9A-Fa-f]+); // type:function size:0x([0-9A-Fa-f]+)', ln)
        if m and m.group(1) in want:
            a, s = int(m.group(2), 16), int(m.group(3), 16)
            lines.append(f'  {a:08x} {s:06x} {a:08x}  4 {m.group(1)} \tqa.o')
    path.write_text('\n'.join(lines) + '\n')


def user_dir(n: int) -> Path:
    u = QA / f'user{n}'
    (u / 'Config').mkdir(parents=True, exist_ok=True)
    (u / 'Maps').mkdir(parents=True, exist_ok=True)
    (u / 'Config' / 'Dolphin.ini').write_text(DOLPHIN_INI)
    (u / 'Config' / 'GFX.ini').write_text(GFX_INI)
    (u / 'Config' / 'Logger.ini').write_text(LOGGER_INI)
    make_map(u / 'Maps' / 'GALE01.map')
    return u


def build_iso():
    from bam.iso import assemble
    QA.mkdir(parents=True, exist_ok=True)
    src = ROOT / '.iso' / 'GALE01.iso'
    off = assemble(src, ROOT / 'build-qa/output/main.dol', QA / 'base.iso')
    print('base iso written, DOL at', hex(off))


def update_isos():
    """Put build/output/main.dol into base.iso and every worker ISO in place
    (same extent as `iso`; no 1.4 GB copy)."""
    from bam.iso import layout
    src = ROOT / '.iso' / 'GALE01.iso'
    dol = (ROOT / 'build-qa/output/main.dol').read_bytes()
    with src.open('rb') as f:
        _, ranges = layout(f)
    cursor, dest = 0, None
    for start, end in ranges + [(src.stat().st_size, src.stat().st_size)]:
        cursor = (cursor + 31) & ~31
        if start - cursor >= len(dol):
            dest = cursor
            break
        cursor = max(cursor, end)
    only = os.environ.get('QA_ISOS')
    isos = [QA / n for n in only.split(',')] if only else [QA / 'base.iso'] + sorted(QA.glob('w*.iso'))
    for iso in isos:
        with iso.open('r+b') as f:
            f.seek(dest)
            f.write(dol)
            f.seek(0x420)
            f.write(struct.pack('>I', dest))
        os.utime(iso)
        print('updated', iso.name, hex(dest))


def marker_offset(iso: Path) -> int:
    data = iso.read_bytes()[:0x1000000] if iso.stat().st_size > 0x1000000 else iso.read_bytes()
    # The DOL sits early in the image; search the whole file if needed.
    hits = [m.start() for m in re.finditer(re.escape(MARKER), data)]
    if not hits:
        with iso.open('rb') as f:
            whole = f.read()
        hits = [m.start() for m in re.finditer(re.escape(MARKER), whole)]
    good = []
    with iso.open('rb') as f:
        for h in hits:
            f.seek(h)
            b = f.read(12)
            if b[4:8] == b'\0\0\0\0' and b[8:12] == struct.pack('>I', 0xFFFF):
                good.append(h)
    if len(good) != 1:
        raise SystemExit(f'resume marker not unique in {iso}: {good}')
    return good[0]


def patch(iso: Path, off: int, resume: int, end: int):
    with iso.open('r+b') as f:
        f.seek(off + 4)
        f.write(struct.pack('>II', resume, end))


class Worker:
    def __init__(self, n, start, end, tag, timeout):
        self.n, self.start, self.end, self.tag, self.timeout = n, start, end, tag, timeout
        self.iso = QA / f'w{n}.iso'
        if not self.iso.exists():
            shutil.copyfile(QA / 'base.iso', self.iso)
            # The marker must start at resume 0, end 0xFFFF before searching.
        self.off = marker_offset_fresh(self.iso)
        self.user = user_dir(n)
        self.log = (QA / tag)
        self.log.mkdir(parents=True, exist_ok=True)
        self.out = (self.log / f'w{n}.log').open('a')
        self.raw = (self.log / f'w{n}.raw').open('a')
        self.resume = start * 32
        self.proc = None
        self.done = False
        self.fail_streak = {}

    def launch(self):
        self.off = marker_offset_fresh(self.iso)  # the DOL may have been replaced
        patch(self.iso, self.off, self.resume, self.end)
        self.out.write(f'[qa] LAUNCH {self.resume >> 5} {self.resume & 31} {time.time():.0f}\n')
        self.out.flush()
        env = dict(os.environ, DOLPHIN_NO_ALERTS='1')
        extra = []
        if VIDEO:
            extra = ['-C', 'Dolphin.Core.GFXBackend=Software Renderer', '-C', 'Dolphin.Movie.DumpFrames=True',
                     '-C', 'GFX.Settings.DumpFramesAsImages=True']
        self.proc = subprocess.Popen([str(DOLPHIN), '-u', str(self.user), '-p', 'headless'] + extra + ['-e', str(self.iso)],
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=0,
                                     start_new_session=True, env=env, stdin=subprocess.DEVNULL)
        self.buf = b''
        self.last = time.time()
        self.boot = time.time()
        self.m = self.s = None
        self.open_step = False
        self.after_r = None
        self.tail = []

    def kill(self):
        if self.proc and self.proc.poll() is None:
            try:
                os.killpg(self.proc.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            self.proc.wait()
        self.proc = None

    def handle(self, line: str):
        self.raw.write(line + '\n')
        if 'QA-CRASH' in line or 'UNHANDLED EXCEPTION' in line or 'assertion' in line:
            self.crashinfo = (getattr(self, 'crashinfo', '') + ' ' + line[line.find('QA-CRASH') if 'QA-CRASH' in line else 0:].strip())[-600:]
        self.tail.append(line)
        self.tail = self.tail[-12:]
        if '[qa]' not in line:
            if '[bam]' in line:
                self.out.write(line[line.index('[bam]'):] + '\n')
            return
        q = line[line.index('[qa]'):]
        self.out.write(q + '\n')
        self.last = time.time()
        p = q.split()
        if p[1] == 'M':
            self.m, self.s, self.open_step, self.after_r = int(p[2]), None, False, None
        elif p[1] == 'S':
            self.m, self.s, self.open_step = int(p[2]), int(p[3]), True
        elif p[1] == 'R':
            self.open_step = False
            self.after_r = int(p[3])
        elif p[1] == 'E':
            self.after_r = None
        elif p[1] == 'DONE':
            self.done = True

    def failure(self, why: str):
        """Record the failure where the sweep was and pick the resume point."""
        tail = getattr(self, 'crashinfo', '') or ' | '.join(t.strip()[:120] for t in self.tail[-6:])
        self.crashinfo = ''
        if self.m is None:
            key = ('boot', self.resume)
            self.fail_streak[key] = self.fail_streak.get(key, 0) + 1
            self.out.write(f'[qa] X boot {self.resume >> 5} {self.resume & 31} {why} :: {tail}\n')
            if self.fail_streak[key] >= 2:
                # The first match itself never got going: skip it.
                self.out.write(f'[qa] X {self.resume >> 5} -1 LOAD{why} :: {tail}\n')
                self.resume = ((self.resume >> 5) + 1) * 32
        elif self.open_step:
            self.out.write(f'[qa] X {self.m} {self.s} {why} :: {tail}\n')
            self.resume = self.m * 32 + self.s + 1
        elif self.after_r is not None:
            self.out.write(f'[qa] X {self.m} {self.after_r + 1} PREP{why} :: {tail}\n')
            self.resume = self.m * 32 + self.after_r + 2
        else:
            self.out.write(f'[qa] X {self.m} -1 LOAD{why} :: {tail}\n')
            self.resume = (self.m + 1) * 32
        self.out.flush()
        if (self.resume >> 5) >= self.end:
            self.done = True

    def poll(self):
        """Read output; returns False once this worker is finished."""
        if self.done:
            return False
        if self.proc is None:
            self.launch()
        r, _, _ = select.select([self.proc.stdout], [], [], 0.2)
        if r:
            chunk = os.read(self.proc.stdout.fileno(), 65536)
            if chunk:
                self.buf += chunk
                *lines, self.buf = self.buf.split(b'\n')
                for ln in lines:
                    self.handle(ln.decode('utf-8', 'replace'))
            elif self.proc.poll() is not None:
                self.kill()
                if self.done:
                    return False
                self.failure('EXIT')
                return not self.done
        if self.done:
            self.kill()
            self.out.flush()
            return False
        limit = self.timeout if self.m is not None else max(self.timeout, 90)
        if time.time() - self.last > limit:
            crash = bool(getattr(self, 'crashinfo', '')) or any(
                k in t for t in self.tail for k in ('Invalid read', 'Invalid write', 'exception', 'Exception',
                                                    'OSPanic', 'PANIC', 'Panic'))
            self.kill()
            self.failure('CRASH' if crash else 'FREEZE')
        self.out.flush()
        return not self.done


def marker_offset_fresh(iso: Path) -> int:
    # A worker ISO may carry a patched resume from an earlier run: reset it.
    data = iso.open('rb')
    off = None
    chunk = 1 << 24
    pos = 0
    while True:
        b = data.read(chunk + 12)
        if not b:
            break
        i = b.find(MARKER)
        while i >= 0:
            if i + 12 <= len(b) and b[i + 8:i + 12] in (struct.pack('>I', 0xFFFF),) or (i + 12 <= len(b) and True):
                cand = pos + i
                tail = b[i + 4:i + 12]
                if len(tail) == 8:
                    r, e = struct.unpack('>II', tail)
                    if r < 0x10000 and e <= 0xFFFF:
                        if off is not None and off != cand:
                            raise SystemExit(f'two resume markers in {iso}')
                        off = cand
            i = b.find(MARKER, i + 1)
        pos += chunk
        data.seek(pos)
        if pos > 0x8000000:
            break
    data.close()
    if off is None:
        raise SystemExit('no resume marker in ' + str(iso))
    return off


def cmd_run(a):
    global VIDEO
    VIDEO = a.video
    end = min(a.end, TOTAL_MATCHES)
    n = a.workers
    span = (end - a.start + n - 1) // n
    ws = []
    for i in range(n):
        lo, hi = a.start + i * span, min(end, a.start + (i + 1) * span)
        if lo < hi:
            w = Worker(a.slot + i, lo, hi, a.tag, a.timeout)
            if i == 0 and a.step:
                w.resume = lo * 32 + a.step
            ws.append(w)
    t0 = time.time()
    try:
        while any([w.poll() for w in ws]):
            pass
    finally:
        for w in ws:
            w.kill()
    print(f'sweep {a.tag} finished in {time.time() - t0:.0f}s')


def cmd_report(a):
    import collections
    res = collections.Counter()
    bad = []
    for f in sorted((QA / a.tag).glob('w*.log')):
        for ln in f.read_text().splitlines():
            p = ln.split()
            if len(p) > 2 and p[1] == 'R':
                res[p[4]] += 1
                if p[4] != 'OK':
                    bad.append(ln)
            elif len(p) > 2 and p[1] == 'X':
                res['X'] += 1
                bad.append(ln)
    print(dict(res))
    for b in bad[:200]:
        print(b)


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest='cmd', required=True)
    s = sub.add_parser('iso')
    s.set_defaults(fn=lambda a: build_iso())
    s = sub.add_parser('update')
    s.set_defaults(fn=lambda a: update_isos())
    s = sub.add_parser('run')
    s.add_argument('--workers', type=int, default=2)
    s.add_argument('--start', type=int, default=0)
    s.add_argument('--end', type=int, default=TOTAL_MATCHES)
    s.add_argument('--tag', default='sweep')
    s.add_argument('--timeout', type=float, default=40)
    s.add_argument('--slot', type=int, default=0, help='first worker number (own ISO and user dir)')
    s.add_argument('--step', type=int, default=0, help='first step of the first match')
    s.add_argument('--video', action='store_true', help='software renderer, every frame dumped as PNG')
    s.set_defaults(fn=cmd_run)
    s = sub.add_parser('report')
    s.add_argument('--tag', default='sweep')
    s.set_defaults(fn=cmd_report)
    a = ap.parse_args()
    a.fn(a)


if __name__ == '__main__':
    main()
