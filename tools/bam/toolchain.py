"""Locate and drive the Metrowerks toolchain shipped with the Melee decomp.

The decomp checkout (pinned in deps/manifest.toml) provides:
  - headers and sources            (<decomp>/src, libs/dolphin/include, build/GALE01/include)
  - compilers                      (<decomp>/build/compilers/<ver>/mwcceppc.exe ...)
  - the exact per-unit flags       (<decomp>/build.ninja)
  - retail symbol/split tables     (<decomp>/config/GALE01/{symbols,splits}.txt)
  - binutils (nm/objdump)          (<decomp>/build/binutils)
On Linux/macOS the Windows executables run through wibo.
"""
from __future__ import annotations
import os
import platform
import re
import shlex
import subprocess
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Sequence


class ToolError(RuntimeError):
    pass


@dataclass
class UnitFlags:
    unit: str
    mw_version: str
    cflags: List[str]
    sjis: bool
    source: str = ''


@dataclass
class Toolchain:
    decomp: Path
    wrapper: Optional[Path] = None
    compilers: Path = field(default=None)  # type: ignore[assignment]
    binutils: Optional[Path] = None
    sjiswrap: Optional[Path] = None

    def __post_init__(self):
        self.decomp = Path(self.decomp).resolve()
        if self.compilers is None:
            self.compilers = self.decomp / 'build/compilers'
        if self.binutils is None and (self.decomp / 'build/binutils').is_dir():
            self.binutils = self.decomp / 'build/binutils'
        if self.sjiswrap is None:
            for cand in (self.decomp / 'build/tools/sjiswrap.exe',):
                if cand.is_file():
                    self.sjiswrap = cand
        if self.wrapper is None and platform.system() != 'Windows':
            for cand in (self.decomp / 'build/tools/wibo',):
                if cand.is_file():
                    self.wrapper = cand
            if self.wrapper is None:
                raise ToolError('wibo not found; run the decomp configure first or pass --wrapper')
        self._unit_flags: Optional[Dict[str, UnitFlags]] = None

    # ---- discovery ---------------------------------------------------------

    def compiler(self, mw_version: str, tool: str = 'mwcceppc.exe') -> Path:
        p = self.compilers / mw_version / tool
        if not p.is_file():
            raise ToolError(f'compiler missing: {p}')
        return p

    def include_flags(self, build_dir: str = 'build/GALE01') -> List[str]:
        return ['-i', str(self.decomp / 'src'), '-i', str(self.decomp / 'src/MSL'),
                '-i', str(self.decomp / 'libs/dolphin/include'),
                '-i', str(self.decomp / build_dir / 'include')]

    def unit_flags(self) -> Dict[str, UnitFlags]:
        """Parse the decomp's generated build.ninja for each unit's compiler
        version and cflags so overrides are compiled exactly like retail."""
        if self._unit_flags is not None:
            return self._unit_flags
        ninja = self.decomp / 'build.ninja'
        if not ninja.is_file():
            raise ToolError(f'{ninja} missing; run configure.py in the decomp first')
        text = ninja.read_text(encoding='utf-8')
        # Join ninja line continuations.
        text = re.sub(r'\$\n\s*', ' ', text)
        out: Dict[str, UnitFlags] = {}
        block_re = re.compile(r'^build (\S+)\.o:\s+(mwcc\w*)\s+(\S+)[^\n]*\n((?:  .*\n)*)', re.M)
        for m in block_re.finditer(text):
            obj, rule, src = m.group(1), m.group(2), m.group(3)
            prefix = 'build/GALE01/src/'
            if not obj.startswith(prefix):
                continue
            unit = obj[len(prefix):] + Path(src).suffix
            vars_ = dict(re.findall(r'^  (\w+) = (.*)$', m.group(4), re.M))
            if 'cflags' not in vars_ or 'mw_version' not in vars_:
                continue
            flags = shlex.split(vars_['cflags'].replace('$', ''), posix=True)
            out[unit] = UnitFlags(unit, vars_['mw_version'], flags, 'sjis' in rule, src)
        self._unit_flags = out
        return out

    # ---- running -----------------------------------------------------------

    def _wrap(self, exe: Path, args: Sequence[str], sjis: bool = False) -> List[str]:
        cmd: List[str] = []
        if self.wrapper:
            cmd.append(str(self.wrapper))
        if sjis and self.sjiswrap:
            cmd.append(str(self.sjiswrap))
        cmd.append(str(exe))
        cmd.extend(args)
        return cmd

    def run(self, cmd: List[str], cwd: Optional[Path] = None) -> subprocess.CompletedProcess:
        proc = subprocess.run(cmd, cwd=str(cwd) if cwd else None, capture_output=True, text=True)
        if proc.returncode != 0:
            raise ToolError('command failed (%d): %s\n%s%s' % (proc.returncode, ' '.join(cmd), proc.stdout, proc.stderr))
        return proc

    def compile(self, source: Path, obj: Path, mw_version: str, cflags: Sequence[str],
                extra: Sequence[str] = (), sjis: bool = True, cwd: Optional[Path] = None) -> None:
        obj.parent.mkdir(parents=True, exist_ok=True)
        args = [*cflags, *extra, '-c', str(source), '-o', str(obj)]
        self.run(self._wrap(self.compiler(mw_version), args, sjis), cwd=cwd or self.decomp)

    def link(self, objects: Sequence[Path], lcf: Path, out_elf: Path, out_map: Path,
             mw_version: str = 'GC/1.3.2', extra: Sequence[str] = ()) -> None:
        out_elf.parent.mkdir(parents=True, exist_ok=True)
        rsp = out_elf.with_suffix('.rsp')
        rsp.write_text('\n'.join(str(o) for o in objects) + '\n', encoding='utf-8')
        args = ['-fp', 'hardware', '-nodefaults', '-warn', 'off', '-mapunused', '-msgstyle', 'std',
                *extra, '-lcf', str(lcf), '-map', str(out_map), '-o', str(out_elf), f'@{rsp}']
        self.run(self._wrap(self.compiler(mw_version, 'mwldeppc.exe'), args), cwd=self.decomp)

    def nm(self, obj: Path) -> List[tuple]:
        """(name, kind, defined) for every symbol in an object, via binutils nm
        when available, otherwise our own ELF reader."""
        from .elf import Elf, STT_FUNC, STT_OBJECT
        out = []
        for s in Elf(obj).symbols():
            if not s.name or s.type not in (STT_FUNC, STT_OBJECT, 0):
                continue
            if s.type == 0 and s.defined:
                continue
            kind = 'function' if s.type == STT_FUNC else 'object' if s.type == STT_OBJECT else 'undef'
            out.append((s.name, kind, s.defined, s.bind))
        return out


# ---- MWLD map parsing --------------------------------------------------------

MAP_LINE = re.compile(r'^\s+([0-9a-f]{8})\s+([0-9a-f]{6})\s+([0-9a-f]{8})\s+(\d+)\s+(\S+)\s+(\S+)\s*$')
MAP_UNUSED = re.compile(r'^\s+UNUSED\s+([0-9a-f]{6})\s+\.{8}\s+(\S+)\s+(\S+)\s*$')


@dataclass
class MapSymbol:
    name: str
    section: str
    addr: int
    size: int
    obj: str


def parse_map(path: Path) -> Dict[str, MapSymbol]:
    """Parse a Metrowerks link map into {name: MapSymbol}. Section-level rows
    (the first row of each object within a section) are skipped; unused
    symbols are reported with addr=-1."""
    out: Dict[str, MapSymbol] = {}
    section = ''
    for line in Path(path).read_text(encoding='utf-8', errors='replace').splitlines():
        m = re.match(r'^(\S+) section layout', line)
        if m:
            section = m.group(1)
            continue
        if section.startswith('Memory map') or section.startswith('Linker generated'):
            continue
        m = MAP_LINE.match(line)
        if m:
            size = int(m.group(2), 16)
            addr = int(m.group(3), 16)
            name, obj = m.group(5), m.group(6)
            if name == section:            # per-object section row
                continue
            key = name
            if key in out and out[key].obj != obj:
                key = f'{name}@{obj}'
            out[key] = MapSymbol(name, section, addr, size, obj)
            continue
        m = MAP_UNUSED.match(line)
        if m:
            name, obj = m.group(2), m.group(3)
            out.setdefault(name, MapSymbol(name, section, -1, int(m.group(1), 16), obj))
    return out
