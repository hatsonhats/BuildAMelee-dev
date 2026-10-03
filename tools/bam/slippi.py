"""Slippi gecko code list analysis.

Slippi Dolphin applies Data/Sys/GameSettings/GALE01r2.ini at boot. Every C2
(code injection) and 04 (word write) entry targets a retail address. Our
patches must never land on the same instruction, or either Slippi's code or
ours silently disappears. `injection_addresses` extracts them; the build
refuses colliding patches.
"""
from __future__ import annotations
import re
from pathlib import Path
from typing import Dict, Iterable, List, Set


def parse_codes(ini: Path, enabled_only: bool = True) -> Dict[str, List[tuple]]:
    """{code name: [(kind, address, data), ...]} for the [Gecko] section.
    With enabled_only, only codes listed under [Gecko_Enabled] plus the
    Required/Recommended families are returned (what Slippi netplay runs)."""
    text = Path(ini).read_text(encoding='utf-8', errors='replace').splitlines()
    enabled: Set[str] = set()
    section = ''
    codes: Dict[str, List[tuple]] = {}
    cur = None
    for line in text:
        if line.startswith('['):
            section = line.strip()
            continue
        if section == '[Gecko_Enabled]' and line.startswith('$'):
            enabled.add(line[1:].strip())
            continue
        if section != '[Gecko]':
            continue
        if line.startswith('$'):
            cur = line[1:].split(' [')[0].strip()
            codes.setdefault(cur, [])
            continue
        m = re.match(r'^\s*([0-9A-Fa-f]{8})\s+([0-9A-Fa-f]{8})', line)
        if m and cur is not None:
            codes[cur].append((int(m.group(1), 16), int(m.group(2), 16)))
    if enabled_only:
        keep = {}
        for name, entries in codes.items():
            if name in enabled or name.startswith('Required') or name.startswith('Recommended'):
                keep[name] = entries
        codes = keep
    return codes


def injection_addresses(ini: Path, enabled_only: bool = True) -> Dict[int, List[str]]:
    """{retail address: [code names]} for C2/C3 injections and 04/05 writes.
    Multi-line C2 payloads are skipped correctly by honouring the line count."""
    out: Dict[int, List[str]] = {}
    for name, entries in parse_codes(ini, enabled_only).items():
        i = 0
        while i < len(entries):
            w, d = entries[i]
            t = (w >> 24) & 0xFF
            addr = 0x80000000 | (w & 0x01FFFFFF)
            if t in (0xC2, 0xC3):
                out.setdefault(addr, []).append(name)
                i += 1 + d          # d = number of 8-byte lines of payload
                continue
            if t in (0x04, 0x05):
                out.setdefault(addr, []).append(name)
            elif t in (0x06, 0x07):   # string write: covers d bytes
                for k in range(0, d, 4):
                    out.setdefault(addr + k, []).append(name)
                i += 1 + (d + 7) // 8
                continue
            i += 1
    return out


def functions_hooked(ini: Path, symtab, enabled_only: bool = True) -> Dict[str, List[tuple]]:
    """Map injection addresses onto retail functions: {fn: [(addr, offset, codes)]}."""
    res: Dict[str, List[tuple]] = {}
    for addr, names in sorted(injection_addresses(ini, enabled_only).items()):
        fn = symtab.function_at(addr)
        if fn:
            res.setdefault(fn.name, []).append((addr, addr - fn.addr, names))
    return res


def gct_injection_addresses(gct: Path) -> Dict[int, List[str]]:
    """Injection addresses of a binary .gct (Slippi's Sys/bootloader.gct)."""
    import struct
    data = Path(gct).read_bytes()
    words = struct.unpack('>%dI' % (len(data) // 4), data[: len(data) // 4 * 4])
    out: Dict[int, List[str]] = {}
    name = Path(gct).name
    i = 0
    while i + 1 < len(words):
        w, d = words[i], words[i + 1]
        t = (w >> 24) & 0xFF
        addr = 0x80000000 | (w & 0x01FFFFFF)
        if w == 0x00D0C0DE or t == 0xF0:
            i += 2
            continue
        if t in (0xC2, 0xC3):
            out.setdefault(addr, []).append(name)
            i += 2 + 2 * d
            continue
        if t in (0x04, 0x05):
            out.setdefault(addr, []).append(name)
        elif t in (0x06, 0x07):
            for k in range(0, d, 4):
                out.setdefault(addr + k, []).append(name)
            i += 2 + 2 * ((d + 7) // 8)
            continue
        i += 2
    return out
