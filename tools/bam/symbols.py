"""Parse decomp-toolkit symbols.txt / splits.txt for GALE01 and emit linker inputs.

The retail DOL is never relinked. Overlay code is linked against *absolute*
symbols for every retail function and object, so a call to Fighter_ChangeMotionState
from overlay code resolves to its retail address 0x8006xxxx.
"""
from __future__ import annotations
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Tuple

SYM_RE = re.compile(
    r'^(?P<name>\S+) = (?P<section>\S+):0x(?P<addr>[0-9A-Fa-f]+);'
    r'\s*//\s*(?P<attrs>.*)$')


@dataclass(frozen=True)
class Symbol:
    name: str
    section: str
    addr: int
    size: int
    kind: str      # function | object | label
    scope: str     # global | local | weak
    attrs: Dict[str, str]

    @property
    def end(self) -> int:
        return self.addr + self.size


def parse_symbols(path: Path) -> List[Symbol]:
    out: List[Symbol] = []
    for line in path.read_text(encoding='utf-8').splitlines():
        m = SYM_RE.match(line)
        if not m:
            continue
        attrs: Dict[str, str] = {}
        for tok in m.group('attrs').split():
            if ':' in tok:
                k, v = tok.split(':', 1)
                attrs[k] = v
            else:
                attrs[tok] = '1'
        size = int(attrs.get('size', '0x0'), 16)
        out.append(Symbol(m.group('name'), m.group('section'),
                          int(m.group('addr'), 16), size,
                          attrs.get('type', 'label'), attrs.get('scope', 'global'), attrs))
    return out


@dataclass(frozen=True)
class Split:
    unit: str           # e.g. melee/ft/fighter.c
    section: str
    start: int
    end: int


def parse_splits(path: Path) -> List[Split]:
    out: List[Split] = []
    unit: Optional[str] = None
    for line in path.read_text(encoding='utf-8').splitlines():
        m = re.match(r'^(\S+\.(?:c|cpp|s)):', line)
        if m:
            unit = m.group(1)
            continue
        m = re.match(r'^\s+(\.\w+|extab\w*)\s+start:0x([0-9A-Fa-f]+)\s+end:0x([0-9A-Fa-f]+)', line)
        if m and unit:
            out.append(Split(unit, m.group(1), int(m.group(2), 16), int(m.group(3), 16)))
    return out


class SymbolTable:
    def __init__(self, symbols: Iterable[Symbol], splits: Iterable[Split] = ()):
        self.symbols = sorted(symbols, key=lambda s: s.addr)
        self.by_name: Dict[str, Symbol] = {}
        for s in self.symbols:
            # Local symbols can repeat across units; keep the first, callers
            # that need locals should go through lookup_in_unit().
            self.by_name.setdefault(s.name, s)
        self._addrs = [s.addr for s in self.symbols]
        self.splits = sorted(splits, key=lambda s: s.start)
        self._split_starts = [s.start for s in self.splits]

    def function_at(self, addr: int) -> Optional[Symbol]:
        import bisect
        i = bisect.bisect_right(self._addrs, addr) - 1
        while i >= 0:
            s = self.symbols[i]
            if s.kind == 'function' and s.addr <= addr < s.end:
                return s
            if s.addr + max(s.size, 4) <= addr and s.kind == 'function':
                break
            i -= 1
        return None

    def unit_at(self, addr: int) -> Optional[Split]:
        import bisect
        i = bisect.bisect_right(self._split_starts, addr) - 1
        if i >= 0 and self.splits[i].start <= addr < self.splits[i].end:
            return self.splits[i]
        return None

    def unit_functions(self, unit: str) -> List[Symbol]:
        rs = [s for s in self.splits if s.unit == unit and s.section == '.text']
        out = []
        for r in rs:
            out.extend(s for s in self.symbols
                       if s.kind == 'function' and r.start <= s.addr < r.end)
        return out

    def write_lcf_absolutes(self, path: Path, exclude: Iterable[str] = (),
                            only_global: bool = True) -> int:
        """Emit `name = 0xADDR;` assignments for the overlay link.

        MWLD treats assignments in an LCF as absolute symbols, so unresolved
        references in overlay objects bind to retail addresses. Symbols the
        overlay defines itself must be excluded or the linker reports a
        duplicate definition.
        """
        ex = set(exclude)
        seen = set()
        lines = ['/* generated: absolute retail symbols for GALE01 (NTSC 1.02) */']
        n = 0
        for s in self.symbols:
            if s.name in ex or s.name in seen:
                continue
            if only_global and s.scope != 'global':
                continue
            if not re.match(r'^[A-Za-z_@$][A-Za-z0-9_@$.]*$', s.name):
                continue
            seen.add(s.name)
            lines.append(f'{s.name} = 0x{s.addr:08X};')
            n += 1
        path.write_text('\n'.join(lines) + '\n', encoding='utf-8')
        return n
