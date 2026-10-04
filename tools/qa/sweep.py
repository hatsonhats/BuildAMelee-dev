#!/usr/bin/env python3
"""Automated move sweep: every move slot of every character with every
donor, played in Dolphin as fast as the machine allows, many instances at
once, then checked against each donor's own moves.

One command does everything (Windows, Linux, macOS):

    python tools/qa/sweep.py

builds the QA executable (`bam.py build --qa sweep`), puts it in a QA copy
of your Melee ISO, runs the 674 sweep matches spread over parallel Dolphin
instances, and writes a report:

    build-qa/sweep/<tag>/report.txt     summary and every problem found
    build-qa/sweep/<tag>/results.csv    one row per move tested
    build-qa/sweep/<tag>/logs/          raw logs (one per batch of matches)

Options:
    --workers N        parallel Dolphin instances (default: half the CPU threads)
    --matches A-B      only matches A..B-1 (0-25 are each character's own moves)
    --pair R:D         one recipient with one donor, e.g. --pair Jigglypuff:Marth
    --tag NAME         output folder name (default: a timestamp)
    --no-build         reuse the last QA build
    --timeout S        seconds without progress before a freeze is declared
    sweep.py report TAG    re-run the analysis of an earlier sweep

Dolphin: a stock Dolphin build (not Slippi Dolphin: the QA executable is
laid out for a stock disc boot). DolphinNoGUI runs without windows and is
preferred; Dolphin.exe works too. Point at it with --dolphin, the DOLPHIN
environment variable, or `qa_dolphin = "..."` under [paths] in
config/local.toml.

How a run works: each worker owns a Dolphin user folder whose game settings
carry a patch telling the QA executable which matches to play. The worker
reads Dolphin's log file; a match that crashes or freezes is recorded, and
the worker relaunches Dolphin from the step after the one that broke.
"""
from __future__ import annotations

import argparse
import os
import queue
import shutil
import struct
import subprocess
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'tools' / 'qa'))
OUT = ROOT / 'build-qa' / 'sweep'
QA_DOL = ROOT / 'build-qa' / 'output' / 'main.dol'
QA_ELF = ROOT / 'build-qa' / 'overlay' / 'overlay.elf'
ISO = ROOT / 'build-qa' / 'sweep.iso'

CHARS = ['Falcon', 'DK', 'Fox', 'G&W', 'Kirby', 'Bowser', 'Link', 'Luigi', 'Mario', 'Marth', 'Mewtwo', 'Ness',
         'Peach', 'Pikachu', 'ICs', 'Jigglypuff', 'Samus', 'Yoshi', 'Zelda', 'Sheik', 'Falco', 'YLink', 'Doc',
         'Roy', 'Pichu', 'Ganon']
ALIASES = {'captainfalcon': 'Falcon', 'donkeykong': 'DK', 'gameandwatch': 'G&W', 'gw': 'G&W', 'mrgameandwatch': 'G&W',
           'bowser': 'Bowser', 'iceclimbers': 'ICs', 'puff': 'Jigglypuff', 'younglink': 'YLink', 'drmario': 'Doc',
           'ganondorf': 'Ganon'}
NATIVE = len(CHARS)
TOTAL_MATCHES = NATIVE + NATIVE * (NATIVE - 1) - 2   # Zelda and Sheik never borrow from each other
BATCH = 4                                            # matches per Dolphin launch

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
FastDiscSpeed = True
GFXBackend = Null
DSPHLE = True
SkipIPL = True
OverclockEnable = False
MMU = False
[DSP]
Backend = No Audio Output
EnableJIT = True
[Interface]
UsePanicHandlers = False
ConfirmStop = False
OnScreenDisplayMessages = False
DebugModeEnabled = False
ShowActiveTitle = False
[Display]
RenderToMain = False
[Analytics]
Enabled = False
PermissionAsked = True
[AutoUpdate]
UpdateTrack =
[General]
ShowFrameCount = False
"""
GFX_INI = """[Settings]
ShowFPS = False
[Hacks]
EFBAccessEnable = False
"""
LOGGER_INI = """[Options]
Verbosity = 4
WriteToConsole = False
WriteToFile = True
WriteToWindow = False
[Logs]
OSREPORT = True
OSREPORT_HLE = False
MI = True
POWERPC = True
BOOT = True
CORE = True
"""


# ---- setup ----------------------------------------------------------------

def local_paths() -> dict:
    from bam.project import Project
    return Project.load(ROOT).local.get('paths', {})


def find_dolphin(arg: str | None) -> Path:
    cands = [arg, os.environ.get('DOLPHIN'), local_paths().get('qa_dolphin')]
    for c in cands:
        if c:
            p = Path(c)
            if p.is_dir():
                for name in ('DolphinNoGUI.exe', 'dolphin-emu-nogui', 'Dolphin.exe', 'dolphin-emu'):
                    if (p / name).is_file():
                        return p / name
            if p.is_file():
                return p
            # Dolphin's Windows download may not include DolphinNoGUI.exe:
            # fall back to the Dolphin.exe next to where it was expected.
            for name in ('DolphinNoGUI.exe', 'Dolphin.exe', 'dolphin-emu-nogui', 'dolphin-emu'):
                if (p.parent / name).is_file():
                    print(f'{p.name} not found; using {name}')
                    return p.parent / name
            sys.exit(f'Dolphin not found at {c}')
    for name in ('dolphin-emu-nogui', 'DolphinNoGUI', 'dolphin-emu'):
        w = shutil.which(name)
        if w:
            return Path(w)
    sys.exit('Set the stock Dolphin to use: --dolphin PATH, the DOLPHIN environment variable, or\n'
             '  [paths]\n  qa_dolphin = "C:/Dolphin-x64/DolphinNoGUI.exe"\nin config/local.toml')


def build_qa():
    print('Building the QA executable...')
    subprocess.run([sys.executable, str(ROOT / 'tools' / 'bam.py'), 'build', '--qa', 'sweep'], check=True)


def prepare_iso():
    """The QA ISO: a copy of the retail image with the QA executable in it.
    Made once; later runs only rewrite the executable in place."""
    from bam.iso import assemble, layout
    dol = QA_DOL.read_bytes()
    if ISO.is_file():
        with ISO.open('r+b') as f:
            f.seek(0x420)
            at = struct.unpack('>I', f.read(4))[0]
            _, ranges = layout(f)
            nxt = min([s for s, _ in ranges if s > at] + [ISO.stat().st_size])
            if at + len(dol) <= nxt:
                f.seek(at)
                f.write(dol)
                return
    src = local_paths().get('melee_iso')
    if not src:
        sys.exit('Set paths.melee_iso in config/local.toml (a clean NTSC 1.02 Melee ISO).')
    print('Making the QA copy of your ISO (once)...')
    ISO.parent.mkdir(parents=True, exist_ok=True)
    assemble(Path(src), QA_DOL, ISO)


def resume_address() -> int:
    from bam.elf import Elf
    for s in Elf(QA_ELF).symbols():
        if s.name == 'qa_resume_block':
            return s.value
    sys.exit('qa_resume_block not found: rebuild with `bam.py build --qa sweep`')


def symbol_map() -> str:
    """Dolphin symbol map with the functions it hooks (OSReport -> log)."""
    import re
    syms = Path(os.environ.get('BAM_SYMBOLS', '')) if os.environ.get('BAM_SYMBOLS') else None
    if not syms or not syms.is_file():
        from bam.project import Project
        syms = Path(Project.load(ROOT, qa=True).decomp) / 'config' / 'GALE01' / 'symbols.txt'
    lines = ['.text section layout']
    for ln in syms.read_text().splitlines():
        m = re.match(r'(\w+) = \.text:0x([0-9A-Fa-f]+); // type:function size:0x([0-9A-Fa-f]+)', ln)
        if m and m.group(1) in ('OSReport', 'OSPanic', 'vprintf', 'printf'):
            a, n = int(m.group(2), 16), int(m.group(3), 16)
            lines.append(f'  {a:08x} {n:06x} {a:08x}  4 {m.group(1)} \tqa.o')
    return '\n'.join(lines) + '\n'


# ---- one Dolphin instance --------------------------------------------------

class Worker(threading.Thread):
    def __init__(self, n, jobs, run, dolphin, timeout, addr, smap, progress):
        super().__init__(daemon=True)
        self.n, self.jobs, self.run_dir, self.dolphin = n, jobs, run, dolphin
        self.timeout, self.addr, self.progress = timeout, addr, progress
        self.user = OUT / 'users' / f'u{n}'
        for sub in ('Config', 'Maps', 'GameSettings', 'Logs'):
            (self.user / sub).mkdir(parents=True, exist_ok=True)
        (self.user / 'Config' / 'Dolphin.ini').write_text(DOLPHIN_INI)
        (self.user / 'Config' / 'GFX.ini').write_text(GFX_INI)
        (self.user / 'Config' / 'Logger.ini').write_text(LOGGER_INI)
        (self.user / 'Maps' / 'GALE01.map').write_text(smap)
        self.logfile = self.user / 'Logs' / 'dolphin.log'
        self.proc = None
        self.stop = False

    # The QA executable reads where to start and stop from qa_resume_block
    # (src/qa/qa_moves.c); a game patch rewrites it every frame.
    def set_range(self, resume: int, end: int):
        a = self.addr
        (self.user / 'GameSettings' / 'GALE01.ini').write_text(
            '[OnFrame]\n$BAM QA range\n'
            f'0x{a + 4:08X}:dword:0x{resume:08X}\n0x{a + 8:08X}:dword:0x{end:08X}\n'
            '[OnFrame_Enabled]\n$BAM QA range\n')

    def launch(self):
        self.kill()
        # Start each launch with an empty log; if the file is still held
        # (Windows, briefly after a kill), read only what this launch appends.
        start = 0
        try:
            self.logfile.unlink()
        except FileNotFoundError:
            pass
        except OSError:
            start = self.logfile.stat().st_size
        nogui = 'nogui' in self.dolphin.name.lower()
        cmd = [str(self.dolphin), '-u', str(self.user)]
        cmd += ['-p', 'headless'] if nogui else ['-b']
        cmd += ['-e', str(ISO)]
        flags = subprocess.CREATE_NEW_PROCESS_GROUP if os.name == 'nt' else 0
        self.proc = subprocess.Popen(cmd, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                     stderr=subprocess.DEVNULL, creationflags=flags)
        self.pos = start
        self.part = b''

    def kill(self):
        if self.proc and self.proc.poll() is None:
            self.proc.kill()
            try:
                self.proc.wait(10)
            except subprocess.TimeoutExpired:
                pass
        self.proc = None

    def lines(self):
        try:
            with self.logfile.open('rb') as f:
                f.seek(self.pos)
                data = f.read()
        except FileNotFoundError:
            return []
        self.pos += len(data)
        data = self.part + data
        *out, self.part = data.split(b'\n')
        return [x.decode('utf-8', 'replace').rstrip('\r') for x in out]

    def run(self):
        while not self.stop:
            try:
                first, last = self.jobs.get_nowait()
            except queue.Empty:
                return
            self.batch(first, last)
            self.jobs.task_done()

    def batch(self, first: int, last: int):
        out = (self.run_dir / 'logs' / f'm{first:03d}.log').open('w', encoding='utf-8')
        resume = first * 32
        m = s = after_r = None
        open_step = False
        boot_fails = 0
        tail: list[str] = []
        while not self.stop:
            self.set_range(resume, last)
            self.launch()
            t_last = t0 = time.time()
            state = 'run'
            while not self.stop:
                time.sleep(0.25)
                for ln in self.lines():
                    tail = (tail + [ln])[-12:]
                    if '[qa]' not in ln:
                        if '[bam]' in ln:
                            out.write(ln[ln.index('[bam]'):] + '\n')
                        elif any(k in ln for k in ('Unhandled', 'exception', 'Invalid read', 'Invalid write',
                                                   'OSPanic', 'PANIC', 'assert')):
                            out.write('[dolphin] ' + ln.strip()[-300:] + '\n')
                        continue
                    q = ln[ln.index('[qa]'):]
                    out.write(q + '\n')
                    t_last = time.time()
                    p = q.split()
                    k = p[1] if len(p) > 1 else ''
                    if k == 'M':
                        m, s, open_step, after_r = int(p[2]), None, False, None
                    elif k == 'S':
                        m, s, open_step = int(p[2]), int(p[3]), True
                    elif k == 'R':
                        open_step, after_r = False, int(p[3])
                    elif k == 'E':
                        after_r = None
                        self.progress(1)
                    elif k == 'DONE':
                        state = 'done'
                if state == 'done':
                    break
                if self.proc.poll() is not None:
                    state = 'EXIT'
                    break
                limit = self.timeout if m is not None else max(self.timeout, 120)
                if time.time() - t_last > limit:
                    crashed = any(k in t for t in tail for k in ('Invalid read', 'Invalid write', 'xception',
                                                               'OSPanic', 'PANIC', 'FREEZE'))
                    state = 'CRASH' if crashed else 'FREEZE'
                    break
            self.kill()
            if state == 'done' or self.stop:
                break
            why = ' | '.join(t.strip()[-120:] for t in tail[-4:])
            # Skip past what broke and relaunch.
            if m is None:
                boot_fails += 1
                out.write(f'[qa] X boot {resume >> 5} {resume & 31} {state} :: {why}\n')
                if boot_fails >= 2:
                    out.write(f'[qa] X {resume >> 5} -1 LOAD{state} :: {why}\n')
                    resume = ((resume >> 5) + 1) * 32
                    self.progress(1)
                    boot_fails = 0
            elif open_step:
                out.write(f'[qa] X {m} {s} {state} :: {why}\n')
                resume = m * 32 + s + 1
            elif after_r is not None:
                out.write(f'[qa] X {m} {after_r + 1} PREP{state} :: {why}\n')
                resume = m * 32 + after_r + 2
            else:
                out.write(f'[qa] X {m} -1 LOAD{state} :: {why}\n')
                resume = (m + 1) * 32
                self.progress(1)
            out.flush()
            m = s = after_r = None
            open_step = False
            if (resume >> 5) >= last:
                break
        out.close()


# ---- commands ---------------------------------------------------------------

def match_range(a) -> tuple[int, int]:
    if a.pair:
        def ck(name):
            key = name.lower().replace(' ', '').replace('.', '').replace('&', 'and')
            for i, c in enumerate(CHARS):
                if c.lower().replace('&', 'and') == key or ALIASES.get(key) == c:
                    return i
            sys.exit(f'unknown character {name!r}; one of: {", ".join(CHARS)}')
        r, d = (ck(x) for x in a.pair.split(':'))
        if r == d:
            return r, r + 1
        n = NATIVE
        for rr in range(NATIVE):
            for dd in range(NATIVE):
                if rr == dd or {rr, dd} == {18, 19}:
                    continue
                if (rr, dd) == (r, d):
                    return n, n + 1
                n += 1
        sys.exit('that pair is not swept (Zelda and Sheik share moves)')
    if a.matches:
        lo, _, hi = a.matches.partition('-')
        return int(lo), int(hi) if hi else int(lo) + 1
    return 0, TOTAL_MATCHES


def cmd_run(a):
    dolphin = find_dolphin(a.dolphin)
    if not a.no_build:
        build_qa()
    prepare_iso()
    addr = resume_address()
    smap = symbol_map()
    first, last = match_range(a)
    tag = a.tag or time.strftime('%Y%m%d-%H%M%S')
    run = OUT / tag
    if run.exists():
        shutil.rmtree(run)
    (run / 'logs').mkdir(parents=True)
    jobs: queue.Queue = queue.Queue()
    for lo in range(first, last, BATCH):
        jobs.put((lo, min(last, lo + BATCH)))
    total = last - first
    workers = max(1, min(a.workers or max(1, (os.cpu_count() or 4) // 2), jobs.qsize()))
    done = [0]
    lock = threading.Lock()

    def progress(n):
        with lock:
            done[0] += n

    print(f'Sweep {tag}: matches {first}-{last - 1} on {workers} Dolphin instances ({dolphin.name})')
    ws = [Worker(i, jobs, run, dolphin, a.timeout, addr, smap, progress) for i in range(workers)]
    t0 = time.time()
    for w in ws:
        w.start()
    try:
        while any(w.is_alive() for w in ws):
            time.sleep(1)
            el = time.time() - t0
            d = min(done[0], total)
            eta = (el / d * (total - d)) if d else 0
            print(f'\r  {d}/{total} matches  {el / 60:5.1f} min elapsed  ~{eta / 60:5.1f} min left ', end='', flush=True)
    except KeyboardInterrupt:
        print('\nStopping...')
        for w in ws:
            w.stop = True
        for w in ws:
            w.kill()
        raise SystemExit(1)
    print(f'\nFinished in {(time.time() - t0) / 60:.1f} min.')
    report(run)


def report(run: Path):
    import analyze
    rows, counts = analyze.analyze(run / 'logs')
    bad = [r for r in rows if r[5] != 'OK']
    lines = [f'Sweep {run.name}: {len(rows)} moves tested',
             'results: ' + ', '.join(f'{k} {v}' for k, v in sorted(counts.items(), key=lambda kv: -kv[1])), '',
             'OK       the move ran and its hitboxes match the donor\'s own (scaled)',
             'HITBOX   hitboxes differ in size, place or damage from the donor\'s own',
             'NOHIT    the dummy took no damage although the donor\'s own move hits it',
             'NOMOVE   the move never started    STUCK  the move never ended',
             'CRASH / FREEZE   the emulator crashed or hung on this move (skipped)', '']
    for r in bad:
        lines.append('%4d %2d %-10s %-10s %-9s %-8s %s' % r)
    (run / 'report.txt').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    import csv
    with (run / 'results.csv').open('w', newline='', encoding='utf-8') as f:
        w = csv.writer(f)
        w.writerow(['match', 'step', 'recipient', 'donor', 'move', 'result', 'note'])
        w.writerows(rows)
    print('\n'.join(lines[:2]))
    print(f'Report: {run / "report.txt"}')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd')
    r = sub.add_parser('report', help='re-run the analysis of an earlier sweep')
    r.add_argument('tag')
    for p in (ap,):
        p.add_argument('--workers', type=int, default=0)
        p.add_argument('--matches')
        p.add_argument('--pair')
        p.add_argument('--tag')
        p.add_argument('--no-build', action='store_true')
        p.add_argument('--timeout', type=float, default=45)
        p.add_argument('--dolphin')
    a = ap.parse_args()
    if a.cmd == 'report':
        report(OUT / a.tag)
    else:
        cmd_run(a)


if __name__ == '__main__':
    main()
