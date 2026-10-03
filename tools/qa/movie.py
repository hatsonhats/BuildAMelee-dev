"""Write Dolphin DTM input movies (Ishiiruka/Slippi 3.x header layout) from a
simple timeline, for headless QA. One input record per controller poll.

timeline entries: (start_poll, end_poll, buttons_mask, stick_x, stick_y)
"""
from __future__ import annotations
import hashlib
import struct
from pathlib import Path

BTN = dict(START=1, A=2, B=4, X=8, Y=0x10, Z=0x20, DUP=0x40, DDOWN=0x80,
           DLEFT=0x100, DRIGHT=0x200, L=0x400, R=0x800)


def md5_of(path: Path) -> bytes:
    h = hashlib.md5()
    with Path(path).open('rb') as f:
        for c in iter(lambda: f.read(1 << 20), b''):
            h.update(c)
    return h.digest()


def write(image: Path, out: Path, timeline, count: int) -> Path:
    hdr = bytearray(256)
    hdr[:4] = b'DTM\x1a'
    hdr[4:10] = Path(image).open('rb').read(6)
    hdr[11] = 1                                    # GC controller 1
    struct.pack_into('<Q', hdr, 13, count)         # frameCount
    struct.pack_into('<Q', hdr, 21, count)         # inputCount
    a = b'BuildAMelee QA'
    hdr[49:49 + len(a)] = a
    hdr[113:129] = md5_of(image)
    struct.pack_into('<Q', hdr, 129, 1790000000)
    body = bytearray()
    for i in range(count):
        buttons, x, y = 0, 128, 128
        for s, e, m, sx, sy in timeline:
            if s <= i < e:
                buttons |= m
                if (sx, sy) != (128, 128):
                    x, y = sx, sy
        body += struct.pack('<H6B', buttons, 0, 0, x, y, 128, 128)
    out = Path(out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(bytes(hdr) + bytes(body))
    return out


def parse(script: str):
    """'at POLL press A [hold N]' | 'at POLL stick X Y hold N' -> timeline"""
    tl = []
    for line in script.splitlines():
        p = line.split('#')[0].split()
        if not p:
            continue
        assert p[0] == 'at', line
        at = int(p[1])
        hold = int(p[p.index('hold') + 1]) if 'hold' in p else 20
        if p[2] == 'press':
            m = 0
            for k in p[3].split('+'):
                m |= BTN[k.upper()]
            tl.append((at, at + hold, m, 128, 128))
        elif p[2] == 'stick':
            tl.append((at, at + hold, 0, int(p[3]), int(p[4])))
    return tl
