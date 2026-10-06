"""Layout-independent fingerprint of the overlay's code, for refactors.

Moving code between files, renaming functions or reordering them changes
where everything lands in the overlay, so the built DOL changes even when no
instruction does. This hashes each function's instructions with every
address-dependent field masked (branch displacements, `lis` halves and the
immediates that pair with them, small-data offsets), so two builds can be
compared function by function:

    python3 tools/bam.py fingerprint save before.json
    ... refactor, build ...
    python3 tools/bam.py fingerprint diff before.json

Functions are matched by name after `rename` (old prefix -> new prefix).
"""
from __future__ import annotations
import hashlib
import json
import struct
from pathlib import Path
from typing import Dict, Iterable, Tuple

from .elf import Elf, function_bytes

# D-form opcodes with a 16-bit immediate or displacement and an rA field.
_D_FORM = {7, 8, 12, 13, 14, 15, 24, 25, 26, 27, 28, 29, 32, 33, 34, 35, 36, 37, 38, 39,
           40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55}


def _masked(words: Iterable[int]) -> Tuple[int, ...]:
    out = []
    # Registers that held an address built from `lis` anywhere in the
    # function (sticky: a straight scan cannot follow branches, and masking
    # a little too much only makes the check looser).
    based = set()
    for w in words:
        op = w >> 26
        rt, ra = (w >> 21) & 31, (w >> 16) & 31
        if op == 18:                      # b / bl
            w &= 0xFC000003
        elif op == 16:                    # bc
            w &= 0xFFFF0003
        elif op == 15 and ra == 0:        # lis rT, hi
            w &= 0xFFFF0000
            based.add(rt)
        elif op in (14, 15, 24) and ra in based:   # addi / addis / ori from an address
            w &= 0xFFFF0000
            based.add(rt if op != 24 else ra)
        elif op == 31 and ((w >> 1) & 0x3FF) == 444 and rt == ((w >> 11) & 31) and rt in based:  # mr
            based.add(ra)
        elif op in _D_FORM and (ra in (2, 13) or ra in based):
            w &= 0xFFFF0000
        out.append(w)
    return tuple(out)


def fingerprint(elf_path: Path) -> Dict[str, str]:
    elf = Elf(elf_path)
    out = {}
    for name, (sec, off, size) in function_bytes(elf).items():
        start = off - sec.addr if off >= sec.addr else off
        data = sec.data[start:start + size]
        words = struct.unpack('>%dI' % (len(data) // 4), data[: len(data) // 4 * 4])
        out[name] = hashlib.sha1(struct.pack('>%dI' % len(words), *_masked(words))).hexdigest()[:16]
    return out


def save(elf_path: Path, dest: Path) -> int:
    fp = fingerprint(elf_path)
    Path(dest).write_text(json.dumps(fp, indent=0, sort_keys=True))
    return len(fp)


def diff(elf_path: Path, before: Path, rename: Dict[str, str] = None) -> Tuple[list, list, list]:
    """(changed, only before, only after) function names."""
    rename = rename or {}

    def norm(n: str) -> str:
        for a, b in rename.items():
            if n.startswith(a):
                return b + n[len(a):]
        return n
    old = {norm(k): v for k, v in json.loads(Path(before).read_text()).items()}
    new = {norm(k): v for k, v in fingerprint(elf_path).items()}
    changed = sorted(n for n in old.keys() & new.keys() if old[n] != new[n])
    return changed, sorted(old.keys() - new.keys()), sorted(new.keys() - old.keys())
