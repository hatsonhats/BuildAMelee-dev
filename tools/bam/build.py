"""Build pipeline: retail DOL + overlay section + hook patches -> main.dol (+ ISO)."""
from __future__ import annotations
import json
import re
import shutil
import struct
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Set

from . import ppc
from .compare import compare_function, FunctionDiff
from .dol import Dol
from .elf import Elf, STT_FUNC, STT_OBJECT
from .hooks import Hook, Trampolines, build_patches, Patch
from .project import Project, OverrideSpec
from .symbols import SymbolTable, parse_symbols, parse_splits, Symbol
from .toolchain import Toolchain, parse_map, ToolError
from .transform import (apply_anchor_edits, apply_line_edits, line_edits_for, externize, check_sha256,
                        LineEdit, AnchorEdit, TransformError, changed_by_text, strip_functions)

MUTABLE_SECTIONS = {'.bss', '.sbss', '.sdata', '.data'}
SMALL_SECTIONS = {'.sdata', '.sbss', '.sdata2', '.sbss2'}
STRING_LITERAL = re.compile(r'^@\d+$')


class BuildError(RuntimeError):
    pass


@dataclass
class UnitBuild:
    spec: OverrideSpec
    source: Path            # generated source
    obj: Path
    defined_functions: Set[str] = field(default_factory=set)
    defined_objects: Set[str] = field(default_factory=set)
    externized: List[str] = field(default_factory=list)
    keep: Optional[Set[str]] = None
    # Kept functions renamed to a unit-unique name (alias -> retail name):
    # retail has several functions of that name (decideFighter, doEnter).
    renames: Dict[str, str] = field(default_factory=dict)


@dataclass
class BuildResult:
    dol: Path
    map: Path
    report: Path
    overlay_size: int
    patches: List[Patch]
    diffs: List[FunctionDiff]



def source_hash(root: Path) -> str:
    """Six hex digits over everything that goes into a release DOL. Same
    source -> same id on any machine (line endings normalized), so a player's
    own build of a release tag matches the published patch online."""
    import hashlib
    h = hashlib.sha1()
    files = [root / 'project.toml']
    for d in ('src', 'overrides'):
        files += sorted(f for f in (root / d).rglob('*') if f.is_file())
    for f in files:
        rel = f.relative_to(root).as_posix()
        if rel.startswith('src/qa/'):
            continue
        h.update(rel.encode() + b'\0' + f.read_bytes().replace(b'\r\n', b'\n') + b'\0')
    return h.hexdigest()[:6]

class Builder:
    def __init__(self, project: Project, decomp: Optional[Path] = None, verbose: bool = False):
        self.p = project
        self.verbose = verbose
        self.decomp = Path(decomp) if decomp else project.decomp
        if not (self.decomp / 'config/GALE01/symbols.txt').is_file():
            raise BuildError(f'decomp checkout not found at {self.decomp}; run `bam.py fetch` or set paths.decomp')
        self.tc = Toolchain(self.decomp)
        self.syms = SymbolTable(parse_symbols(self.decomp / 'config/GALE01/symbols.txt'),
                                parse_splits(self.decomp / 'config/GALE01/splits.txt'))
        self.aliases: Dict[str, int] = {}
        self.unit_aliases: Dict[str, Dict[str, str]] = {}
        self.out = project.build_dir / 'overlay'
        self.out.mkdir(parents=True, exist_ok=True)

    def log(self, *a):
        print(*a, flush=True)

    # ---- inputs ------------------------------------------------------------

    def retail_dol(self) -> Dol:
        cands = [self.p.build_dir / 'retail/main.dol', self.decomp / 'orig/GALE01/sys/main.dol']
        for c in cands:
            if c.is_file():
                return Dol.load(c)
        iso = self.p.local.get('paths', {}).get('melee_iso')
        if iso:
            from .iso import read_dol
            data = read_dol(Path(iso))
            dst = self.p.build_dir / 'retail/main.dol'
            dst.parent.mkdir(parents=True, exist_ok=True)
            dst.write_bytes(data)
            return Dol.load(dst)
        raise BuildError('retail main.dol not found: set paths.melee_iso in config/local.toml')

    # ---- compile own sources ----------------------------------------------

    def own_cflags(self) -> List[str]:
        base = ['-nowraplines', '-cwd', 'source', '-Cpp_exceptions', 'off', '-proc', 'gekko', '-fp', 'hardware',
                '-align', 'powerpc', '-nosyspath', '-fp_contract', 'on', '-multibyte', '-enum', 'int', '-nodefaults',
                '-pragma', 'cats off', '-pragma', 'warn_notinlined off', '-RTTI', 'off', '-str', 'reuse',
                '-DBUILD_VERSION=0', '-DVERSION_GALE01', '-maxerrors', '1', '-msgstyle', 'std', '-warn', 'off',
                '-requireprotos', '-lang', 'c99', '-O4,p', '-DNDEBUG=1', '-inline', 'auto', '-sym', 'off',
                '-sdata', '0', '-sdata2', '0']
        base += self.tc.include_flags()
        # Generated includes (the bone tables) come before src/.
        base += ['-i', str(self.p.build_dir / 'generated'), '-i', str(self.p.root / 'src')]
        for k, v in self.p.defines.items():
            base.append(f'-D{k}={v}')
        self.build_id = f'{self.p.version}-{source_hash(self.p.root)}'
        base.append(f'-DBAM_VERSION="{self.p.version}"')
        base.append(f'-DBAM_BUILD_ID="{self.build_id}"')
        base.append(f'-DBAM_OVERLAY_BASE=0x{self.p.overlay_base:08X}')
        base.append(f'-DBAM_OVERLAY_RESERVE=0x{self.p.overlay_reserve:X}')
        base += self.p.source_cflags
        return base

    def assemble_runtime(self) -> List[Path]:
        """Retail Melee has no EABI _savegpr/_restgpr helpers; MWCC emits calls
        to them at -O4,p. Assemble the decomp's copy into the overlay."""
        src = self.decomp / 'src/Runtime/eabi_save_restore.s'
        obj = self.out / 'obj' / 'runtime' / 'eabi_save_restore.o'
        if obj.is_file() and obj.stat().st_mtime >= src.stat().st_mtime:
            return [obj]
        as_exe = (self.tc.binutils / 'powerpc-eabi-as') if self.tc.binutils else None
        if as_exe is None or not (as_exe.is_file() or as_exe.with_suffix('.exe').is_file()):
            raise BuildError('powerpc-eabi-as not found in the decomp binutils directory')
        if not as_exe.is_file():
            as_exe = as_exe.with_suffix('.exe')
        obj.parent.mkdir(parents=True, exist_ok=True)
        self.log('  AS   Runtime/eabi_save_restore.s')
        self.tc.run([str(as_exe), '-mgekko', '--strip-local-absolute', '-I', 'include', '-I', 'src',
                     '-I', 'build/GALE01/include', '--defsym', 'BUILD_VERSION=0', '-o', str(obj), str(src)],
                    cwd=self.decomp)
        return [obj]

    def bone_tables(self) -> Path:
        """build/generated/engine/bone_tables.{h,inc} from the player's ISO
        (bonetables.py). Rewritten only when its content changes, so the
        sources that include it rebuild only then."""
        from .bonetables import generate
        from .iso import disc_reader
        iso = self.p.local.get('paths', {}).get('melee_iso')
        if not iso:
            raise BuildError('the bone tables are read from your ISO: set paths.melee_iso in config/local.toml '
                             'or put the ISO in .iso/')
        dest = self.p.build_dir / 'generated' / 'engine' / 'bone_tables.inc'
        dest.parent.mkdir(parents=True, exist_ok=True)
        tmp = self.p.build_dir / 'generated' / 'tmp' / dest.name
        tmp.parent.mkdir(parents=True, exist_ok=True)
        generate(disc_reader(Path(iso)), tmp, self.log)
        for t, d in ((tmp, dest), (tmp.with_suffix('.h'), dest.with_suffix('.h'))):
            if d.is_file() and d.read_bytes() == t.read_bytes():
                t.unlink()
            else:
                t.replace(d)
                self.log(f'  GEN  engine/{d.name}')
        self._hdr_mtime = None
        return dest

    def compile_sources(self) -> List[Path]:
        self.bone_tables()
        objs = self.assemble_runtime()
        cflags = self.own_cflags()
        # Objects are reused by mtime, so a change of flags or defines (a
        # --debug build after a release one) must invalidate them all. The
        # build ID changes with every source edit and only arena.c uses it.
        sig = self._flags_stamp('own', [f for f in cflags if not f.startswith('-DBAM_BUILD_ID=')])
        for d in self.p.source_dirs:
            for src in sorted((self.p.root / d).rglob('*.c')):
                rel = src.relative_to(self.p.root)
                obj = self.out / 'obj' / rel.with_suffix('.o')
                # arena.c prints the build ID: always rebuild it so the ID is fresh.
                if src.name != 'arena.c' and obj.is_file() and obj.stat().st_mtime >= max(src.stat().st_mtime, self._headers_mtime(), sig):
                    objs.append(obj)
                    continue
                self.log(f'  CC   {rel}')
                self.tc.compile(src, obj, self.p.source_compiler, cflags, sjis=False)
                objs.append(obj)
        return objs

    def check_engine_globals(self, objs: List[Path]) -> None:
        """Refuse writable globals in src/engine that are not listed in
        project.toml [state.engine_globals] (they would not be rolled back)."""
        allowed = self.p.engine_globals
        found = []
        for o in objs:
            if 'engine' not in o.parts:
                continue
            elf = Elf(o)
            for sym in elf.symbols():
                if not (sym.defined and sym.type == STT_OBJECT and sym.name) or sym.shndx >= len(elf.sections):
                    continue
                if elf.sections[sym.shndx].name not in ('.bss', '.sbss', '.data', '.sdata'):
                    continue
                if sym.name.startswith('@') and not sym.name.startswith('@LOCAL@'):
                    continue  # compiler literals
                if sym.name not in allowed:
                    found.append(f'{o.stem}: {sym.name}')
        if found:
            raise BuildError('writable globals in src/engine are not rolled back by Slippi; put them in the '
                             'match-heap state (see project.toml [state]) or list them there with a reason: '
                             + ', '.join(found))

    _hdr_mtime: Optional[float] = None

    def _flags_stamp(self, name: str, flags: List[str]) -> float:
        """mtime of a stamp file rewritten whenever these flags change."""
        stamp = self.out / 'obj' / f'.flags-{name}'
        text = '\n'.join(flags)
        if not stamp.is_file() or stamp.read_text() != text:
            stamp.parent.mkdir(parents=True, exist_ok=True)
            stamp.write_text(text)
        return stamp.stat().st_mtime

    def _headers_mtime(self) -> float:
        if self._hdr_mtime is None:
            m = 0.0
            dirs = [self.p.root / d for d in self.p.source_dirs] + [self.p.build_dir / 'generated']
            for d in dirs:
                for pattern in ('*.h', '*.inc'):
                    for h in d.rglob(pattern):
                        m = max(m, h.stat().st_mtime)
            m = max(m, (self.p.root / 'project.toml').stat().st_mtime)
            self._hdr_mtime = m
        return self._hdr_mtime

    # ---- overrides ---------------------------------------------------------

    def _unit_source(self, unit: str) -> Path:
        flags = self.tc.unit_flags().get(unit)
        if flags is None:
            raise BuildError(f'unit {unit} not in decomp build.ninja')
        return self.decomp / flags.source

    def auto_externize(self, unit: str, keep_const: bool) -> List[str]:
        names = []
        for split in [s for s in self.syms.splits if s.unit == unit]:
            if split.section not in MUTABLE_SECTIONS:
                continue
            for s in self.syms.symbols:
                if s.kind != 'object' or not (split.start <= s.addr < split.end):
                    continue
                if STRING_LITERAL.match(s.name) or '$' in s.name:
                    continue   # compiler generated literals: duplicates are harmless
                if split.section == '.data' and keep_const:
                    # Writable-section tables are usually read-only in practice.
                    # Only share the ones that are plainly state.
                    continue
                names.append(s.name)
        return sorted(set(names))

    fix_dependencies: List[str]

    def note_fix_dependencies(self, unit: str, original: str, after_lines: str, fixes) -> None:
        """Record fixes whose anchor is not in the retail source as is: they
        match text a line edit or an earlier fix wrote, so they depend on it
        (and break if it changes). Listed in the build report."""
        if not hasattr(self, 'fix_dependencies'):
            self.fix_dependencies = []
        for i, e in enumerate(fixes):
            if e['anchor'] in original:
                continue
            first = e['anchor'].strip().split('\n')[0].strip()
            by = [f['id'] for f in fixes[:i] if first in f['replacement']]
            what = f'fix {by[-1]}' if by else ('the line edits' if e['anchor'] in after_lines else 'an earlier fix')
            line = f'{e.get("id")} ({unit}) depends on {what}'
            if line not in self.fix_dependencies:
                self.fix_dependencies.append(line)

    def generate_unit(self, spec: OverrideSpec) -> UnitBuild:
        unit = spec.unit
        src = self._unit_source(unit)
        original = src.read_text(encoding='utf-8')
        text = original
        label = unit
        if spec.line_edits:
            manifest = json.loads((self.p.root / spec.line_edits).read_text(encoding='utf-8'))
            entry = manifest['files'].get('src/' + unit) or manifest['files'].get(unit)
            if entry is None:
                raise BuildError(f'{spec.line_edits} has no entry for {unit}')
            check_sha256(text, entry['base_sha256'], label)
            text = apply_line_edits(text, line_edits_for(entry, text, label), label)
        if spec.anchor_edits:
            self.note_fix_dependencies(unit, original, text, spec.anchor_edits)
            text = apply_anchor_edits(text, [AnchorEdit(e['anchor'], e['replacement'], e.get('occurrences', 1), e.get('id', ''))
                                             for e in spec.anchor_edits], label)
        # Decomp units may define their globals in "x.static.h"; inline those
        # so the definitions can be externized like any other.
        def inline_static(m):
            path = src.parent / m.group(1)
            return path.read_text(encoding='utf-8') if path.is_file() else m.group(0)
        text = re.sub(r'#include "([^"]+\.static\.h)"', inline_static, text)
        original = re.sub(r'#include "([^"]+\.static\.h)"', inline_static, original)
        keep: Optional[Set[str]] = None
        renames: Dict[str, str] = {}
        if spec.strip_unchanged:
            changed, _outside = changed_by_text(original, text)
            keep = (changed - set(spec.skip_functions)) | set(spec.keep_functions)
            text = strip_functions(text, keep, label)
            # A kept function the unit declares `static` is often referenced
            # only from a retail table (ftAction's command handlers, motion
            # state callbacks). The recompiled unit binds those tables to the
            # retail copies, so nothing references the static and MWCC drops
            # it: no symbol, no override, and the edit silently never runs.
            # Give kept functions external linkage so they are always emitted.
            pinned = []
            for name in keep:
                if name not in self.syms.by_name:
                    continue   # helper with no retail symbol: stays file-local
                sym = self.syms.by_name[name]
                if sym.scope == 'local' and not self._unique_local(name):
                    # Another unit has a static of the same name: keep it
                    # static and pin it with a reference instead.
                    if re.search(r'\bstatic\b[^;{(]*\b' + re.escape(name) + r'\s*\(', text):
                        pinned.append(name)
                    continue
                text = re.sub(r'(^|\n)([ \t]*(?:/\*[^\n]*?\*/[ \t]*)?)static\s+((?!inline)[^;{(]*\b' + re.escape(name) + r'\s*\()',
                              r'\1\2\3', text)
            if pinned:
                tag = re.sub(r'\W', '_', unit)
                text += ('\n/* BuildAMelee: edited statics referenced only from retail tables. */\n'
                         'void* const bam_pin_' + tag + '[] = { ' +
                         ', '.join('(void*) ' + n for n in sorted(pinned)) + ' };\n')
            # A name several retail units use (static helpers like
            # decideFighter, doEnter; some are even global in one unit) would
            # clash between overlay units, or bind the patch to another
            # unit's copy. Give each kept one a unit-unique name; the retail
            # symbol is still looked up in this unit's own table.
            renames = {}
            tag = re.sub(r'\W', '_', unit)
            for name in sorted(keep):
                if name in self.syms.by_name and not self._unique_local(name):
                    alias = f'bam_{tag}_{name}'
                    text = re.sub(r'\b' + re.escape(name) + r'\b', alias, text)
                    renames[alias] = name

        if spec.externize is not None:
            ext = spec.externize
        elif spec.strip_unchanged:
            ext = self.shareable_objects(unit)
            for name, alias in self.unit_aliases.get(unit, {}).items():
                if re.search(r'\b' + re.escape(name) + r'\b', text):
                    text = re.sub(r'\b' + re.escape(name) + r'\b', alias, text)
                    ext.append(alias)
        else:
            ext = self.auto_externize(unit, spec.keep_const_copies)
        if ext:
            text = externize(text, ext, label, strict=not spec.strip_unchanged)
        if spec.prepend:
            text = spec.prepend.rstrip('\n') + '\n' + text
        gen = self.out / 'gen' / unit
        gen.parent.mkdir(parents=True, exist_ok=True)
        if not gen.is_file() or gen.read_text(encoding='utf-8') != text:
            gen.write_text(text, encoding='utf-8')
        obj = self.out / 'obj' / 'overrides' / (unit + '.o')
        ub = UnitBuild(spec, gen, obj, externized=ext)
        ub.keep = keep
        ub.renames = renames if spec.strip_unchanged else {}
        return ub

    def shareable_objects(self, unit: str) -> List[str]:
        """Every named data object of a retail unit that the overlay can bind
        to by name (global, or a local whose name is unique), so recompiled
        functions use retail's copy instead of carrying their own."""
        names = []
        for split in [s for s in self.syms.splits if s.unit == unit]:
            for s in self.syms.symbols:
                if s.kind != 'object' or not (split.start <= s.addr < split.end):
                    continue
                if STRING_LITERAL.match(s.name) or '$' in s.name or not re.match(r'^[A-Za-z_]\w*$', s.name):
                    continue
                if s.scope == 'local' and not self._unique_local(s.name):
                    # Same static name in several retail units: bind this
                    # unit's copy by address through a unique alias.
                    alias = f'bam_ret_{s.addr:08X}_{s.name}'
                    self.aliases[alias] = s.addr
                    self.unit_aliases.setdefault(unit, {})[s.name] = alias
                    continue
                names.append(s.name)
        return sorted(set(names))

    def compile_unit(self, ub: UnitBuild) -> None:
        flags = self.tc.unit_flags()[ub.spec.unit]
        cflags = [f for f in flags.cflags]
        # Make include paths absolute (we do not run from the decomp root).
        fixed = []
        it = iter(cflags)
        for f in it:
            if f in ('-i', '-ir'):
                fixed += [f, str(self.decomp / next(it))]
            else:
                fixed.append(f)
        if ub.spec.compiler and ub.spec.compiler.startswith('Wii/'):
            fixed = [f for f in fixed if f != '-lang=c']
        cflags = fixed + ['-sdata', '0', '-sdata2', '0', '-i', str((self.decomp / flags.source).parent), '-i', str(self.p.root / 'src')]
        for k, v in self.p.defines.items():
            cflags.append(f'-D{k}={v}')
        cflags += ub.spec.extra_cflags
        sig = self._flags_stamp('units', [f'-D{k}={v}' for k, v in sorted(self.p.defines.items())])
        if ub.obj.is_file() and ub.obj.stat().st_mtime >= max(ub.source.stat().st_mtime, self._headers_mtime(), sig):
            pass
        else:
            self.log(f'  CC*  {ub.spec.unit}  [{ub.spec.compiler or flags.mw_version}]' +
                     (f' keep {len(ub.keep)}' if ub.keep is not None else ''))
            self.tc.compile(ub.source, ub.obj, ub.spec.compiler or flags.mw_version, cflags, sjis=flags.sjis,
                            cwd=self.decomp)
        elf = Elf(ub.obj)
        for s in elf.sections:
            if s.name in SMALL_SECTIONS and s.size:
                raise BuildError(f'{ub.spec.unit}: object has non-empty {s.name}; small data cannot live in the overlay')
        ub.defined_functions.clear(); ub.defined_objects.clear()
        for sym in elf.symbols():
            if not sym.defined or not sym.name:
                continue
            if sym.type == STT_FUNC:
                ub.defined_functions.add(sym.name)
            elif sym.type == STT_OBJECT:
                ub.defined_objects.add(sym.name)

    # ---- link ----------------------------------------------------------------

    def write_lcf(self, path: Path, exclude: Set[str], force: List[str]) -> None:
        lines = [f'MEMORY {{ ovl : origin = 0x{self.p.overlay_base:08X} }}',
                 'SECTIONS {', '    GROUP: {']
        for sec in ('.init', '.text', '.ctors', '.dtors', '.rodata', '.data', '.bss', '.sdata', '.sbss', '.sdata2', '.sbss2'):
            lines.append(f'        {sec} ALIGN(0x20):{{}}')
        lines += ['    } > ovl', '    _bam_ovl_end = .;']
        seen: Set[str] = set()
        for s in self.syms.symbols:
            if s.name in exclude or s.name in seen:
                continue
            if not re.match(r'^[A-Za-z_][A-Za-z0-9_]*$', s.name):
                continue
            if s.scope == 'local' and not self._unique_local(s.name):
                continue
            seen.add(s.name)
            lines.append(f'    {s.name} = 0x{s.addr:08X};')
        for alias, addr in sorted(self.aliases.items()):
            if alias not in exclude:
                lines.append(f'    {alias} = 0x{addr:08X};')
        lines.append('}')
        if force:
            lines.append('FORCEACTIVE { ' + ' '.join(sorted(set(force))) + ' }')
        path.write_text('\n'.join(lines) + '\n', encoding='utf-8')

    _local_counts: Optional[Dict[str, int]] = None

    def _unique_local(self, name: str) -> bool:
        if self._local_counts is None:
            c: Dict[str, int] = {}
            for s in self.syms.symbols:
                c[s.name] = c.get(s.name, 0) + 1
            self._local_counts = c
        return self._local_counts.get(name, 0) == 1

    # ---- main --------------------------------------------------------------

    def build(self, iso_out: Optional[Path] = None, slippi_ini: Optional[Path] = None) -> BuildResult:
        p = self.p
        retail = self.retail_dol()
        self.log(f'[bam] retail DOL: {len(retail.sections)} sections; overlay @ 0x{p.overlay_base:08X} reserve 0x{p.overlay_reserve:X}')

        self.log('[bam] compiling sources')
        objs = self.compile_sources()
        self.check_engine_globals(objs)
        units: List[UnitBuild] = []
        for spec in p.overrides:
            ub = self.generate_unit(spec)
            self.compile_unit(ub)
            units.append(ub)
            objs.append(ub.obj)

        elf_path, map_path = self.out / 'overlay.elf', self.out / 'overlay.map'
        lcf = self.out / 'overlay.lcf'
        for attempt in range(8):
            # Symbols the overlay defines must not also be absolute.
            defined: Set[str] = set()
            for o in objs:
                for sym in Elf(o).symbols():
                    if sym.defined and sym.name and sym.type in (STT_FUNC, STT_OBJECT):
                        defined.add(sym.name)
            # Any retail *mutable* object the overlay redefines is a state split.
            for ub in units:
                for name in ub.defined_objects:
                    rs = self.syms.by_name.get(name)
                    if rs and rs.kind == 'object' and rs.section in ('.bss', '.sbss'):
                        raise BuildError(f'{ub.spec.unit}: redefines retail state {name} ({rs.section}); externize it')
                    if rs and rs.kind == 'object' and rs.section == '.sdata' and attempt == 0:
                        self.log(f'  note: {ub.spec.unit} keeps its own copy of initialised table {name}')
            force = list(p.exports) + [h.target for h in p.hooks if h.target]
            for ub in units:
                force += sorted(ub.defined_functions)
                # Edited statics are kept alive only by their unit's pin table.
                force += sorted(n for n in ub.defined_objects if n.startswith('bam_pin_'))
            self.write_lcf(lcf, defined, force)
            self.log('[bam] linking overlay')
            try:
                self.tc.link(objs, lcf, elf_path, map_path, p.link_compiler)
                break
            except ToolError as e:
                missing = sorted(set(re.findall(r"undefined: '([^']+)'", str(e))))
                fixed = False
                for ub in units:
                    if ub.keep is None:
                        continue
                    from .transform import function_spans
                    spans = function_spans(ub.source.read_text(encoding='utf-8'))
                    src_all = function_spans(self._unit_source(ub.spec.unit).read_text(encoding='utf-8'))
                    need = [m for m in missing if m in src_all and m not in ub.keep]
                    if need:
                        self.log(f'  keep {need} in {ub.spec.unit} (no retail symbol to bind to)')
                        ub.spec.keep_functions.extend(need)
                        nb = self.generate_unit(ub.spec)
                        ub.source, ub.keep, ub.renames = nb.source, nb.keep, nb.renames
                        self.compile_unit(ub)
                        fixed = True
                if not fixed:
                    raise
        else:
            raise BuildError('link did not converge')
        elf = Elf(elf_path)
        mapsyms = parse_map(map_path)
        image = elf.image(p.overlay_base)
        ovl_end = p.overlay_base + len(image)

        # Overlay symbol lookup from the ELF symbol table (authoritative).
        ovl: Dict[str, int] = {}
        for sym in elf.symbols():
            if sym.defined and sym.name and sym.type in (STT_FUNC, STT_OBJECT, 0):
                ovl.setdefault(sym.name, sym.value)

        # Compare recompiled retail functions against retail bytes.
        diffs: List[FunctionDiff] = []
        ovl_to_retail: Dict[int, int] = {}
        unit_retail: Dict[str, Dict[str, Symbol]] = {}
        for ub in units:
            table = {s.name: s for s in self.syms.unit_functions(ub.spec.unit)}
            unit_retail[ub.spec.unit] = table
            for name in ub.defined_functions:
                rname = ub.renames.get(name, name)
                if rname in table and name in ovl:
                    ovl_to_retail[ovl[name]] = table[rname].addr
        # Overlay address of `name` as defined by unit `ub`: statics can share
        # a name across units, so look it up per object in the link map.
        def ovl_of(ub, name):
            objname = Path(ub.obj).name
            for ms in mapsyms.values():
                if ms.name == name and ms.obj == objname and ms.addr >= 0:
                    return ms.addr
            return ovl.get(name)
        missing = []
        for ub in units:
            if ub.keep is None:
                continue
            table = unit_retail[ub.spec.unit]
            emitted = {ub.renames.get(n, n) for n in ub.defined_functions}
            for name in sorted(ub.keep):
                if name in table and name not in emitted and name not in ub.spec.skip_functions:
                    missing.append(f'{ub.spec.unit}:{name}')
        if missing:
            raise BuildError('edited functions were not emitted by the compiler (never patched): ' + ', '.join(missing[:20]))
        for ub in units:
            table = unit_retail[ub.spec.unit]
            for name in sorted(ub.defined_functions):
                oaddr_any = ovl_of(ub, name)
                if oaddr_any is None or oaddr_any < 0:
                    continue
                rname = ub.renames.get(name, name)
                rs = table.get(rname)
                if rs is None:
                    continue   # new helper introduced by an edit
                if ub.spec.functions is not None and rname not in ub.spec.functions:
                    continue
                if ub.keep is not None:
                    # Text-detected: kept functions are changed by definition.
                    if rname in ub.keep and rname not in ub.spec.skip_functions:
                        diffs.append(FunctionDiff(rname, rs.addr, ovl_of(ub, name), rs.size, True, 'edited'))
                    continue
                oaddr = oaddr_any
                osize = next((s.size for s in elf.symbols() if s.name == name and s.defined), 0)
                obytes = image[oaddr - p.overlay_base: oaddr - p.overlay_base + osize]
                rbytes = retail.read_bytes(rs.addr, rs.size)
                diffs.append(compare_function(name, rs.addr, rbytes, oaddr, obytes, ovl_to_retail.get))

        # Hooks: explicit + one override per changed function.
        hooks: List[Hook] = [Hook(h.id, h.kind, h.at, h.target, list(h.args), h.ret, h.value, h.note) for h in p.hooks]
        for d in diffs:
            if d.changed:
                hooks.append(Hook(f'override:{d.name}', 'override', f'0x{d.retail_addr:08X}', f'__ovl_0x{d.overlay_addr:08X}', note=d.reason))
        tramp_base = (ovl_end + 0x1F) & ~0x1F
        tramp = Trampolines(tramp_base)
        patches = build_patches(hooks, retail.read_u32,
                                lambda n: self.syms.by_name[n].addr if n in self.syms.by_name else None,
                                lambda n: int(n[6:], 16) if n.startswith('__ovl_0x') else ovl.get(n), tramp)
        full = image + b'\0' * (tramp_base - ovl_end) + tramp.to_bytes()
        if len(full) > p.overlay_reserve:
            raise BuildError(f'overlay is 0x{len(full):X} bytes, reserve is 0x{p.overlay_reserve:X}; raise overlay.reserve')
        free = p.overlay_reserve - len(full)
        if free < 0x4000:
            print(f'[bam] WARNING: only {free / 1024:.1f} KB of the {p.overlay_reserve // 1024} KB overlay left '
                  '(docs/STATUS.md: ways to reclaim space)')

        # Optional: refuse to patch where Slippi's own codes inject.
        ini = slippi_ini or (self.p.root / p.slippi_ini if p.slippi_ini else None)
        conflicts = []
        slip: Dict[int, List[str]] = {}
        if ini and Path(ini).is_file():
            from .slippi import injection_addresses
            slip.update(injection_addresses(Path(ini)))
        if p.slippi_gct and (self.p.root / p.slippi_gct).is_file():
            from .slippi import gct_injection_addresses
            for a, names in gct_injection_addresses(self.p.root / p.slippi_gct).items():
                slip.setdefault(a, []).extend(names)
        if slip:
            for pt in patches:
                if pt.addr in slip:
                    conflicts.append((pt, slip[pt.addr]))
            # An override swallows every injection inside the retail body.
            for d in diffs:
                if not d.changed:
                    continue
                for a in range(d.retail_addr + 4, d.retail_addr + d.size, 4):
                    if a in slip:
                        conflicts.append((Patch(a, 0, f'override:{d.name} (body)'), slip[a]))
            for h in p.hooks:
                if h.kind != 'override':
                    continue
                fn = self.syms.by_name.get(h.at.split('+')[0].strip())
                if fn:
                    for a in range(fn.addr + 4, fn.end, 4):
                        if a in slip:
                            conflicts.append((Patch(a, 0, f'{h.id} (body of {fn.name})'), slip[a]))
            if conflicts:
                msg = '\n'.join(f'  {pt.addr:#010x} {pt.hook} vs Slippi {codes}' for pt, codes in conflicts)
                raise BuildError('patches collide with Slippi gecko injections:\n' + msg)

        # Assemble DOL.
        out = Dol.load(self.p.build_dir / 'retail/main.dol') if (self.p.build_dir / 'retail/main.dol').is_file() else self.retail_dol()
        if p.overlay_stage is not None:
            # The apploader will not load sections this high; ship the image
            # low and let a stub copy it up from the stage_hook site.
            from .stage import build_stub
            entry = next((pt for pt in patches if pt.hook == p.stage_hook), None)
            if entry is None:
                raise BuildError(f'overlay.stage_hook {p.stage_hook!r} matches no hook')
            target = ppc.branch_target(entry.value, entry.addr)
            look = lambda n: self.syms.by_name[n].addr  # noqa: E731
            staged, stub_entry = build_stub(p.overlay_stage, p.overlay_base, full, target,
                                            look('memcpy'), look('DCFlushRange'), look('ICInvalidateRange'))
            if p.overlay_stage + len(staged) > 0x80700000:
                raise BuildError('staged overlay crosses 0x80700000 (production apploader limit)')
            entry.value = ppc.b(entry.addr, stub_entry)
            self.log(f'[bam] staged at 0x{p.overlay_stage:08X} (0x{len(staged):X} bytes), copied to '
                     f'0x{p.overlay_base:08X} from {p.stage_hook}')
        for pt in patches:
            out.write_u32(pt.addr, pt.value)
        if p.overlay_stage is not None:
            out.add_section(p.overlay_stage, staged, text=True)
        else:
            out.add_section(p.overlay_base, full, text=True)
        out_dir = p.build_dir / 'output'
        out_dir.mkdir(parents=True, exist_ok=True)
        dol_path = out_dir / 'main.dol'
        out.save(dol_path)
        shutil.copyfile(map_path, out_dir / 'overlay.map')

        report = dict(
            overlay=dict(base=f'0x{p.overlay_base:08X}', end=f'0x{p.overlay_base + len(full):08X}', size=len(full),
                         trampolines=f'0x{tramp_base:08X}', reserve=p.overlay_reserve),
            patches=[dict(addr=f'0x{pt.addr:08X}', value=f'0x{pt.value:08X}', original=f'0x{pt.original:08X}', hook=pt.hook) for pt in patches],
            overrides=[dict(name=d.name, retail=f'0x{d.retail_addr:08X}', overlay=f'0x{d.overlay_addr:08X}', changed=d.changed, reason=d.reason) for d in diffs],
            units=[dict(unit=u.spec.unit, functions=sorted(u.defined_functions), externized=u.externized) for u in units],
            fix_dependencies=getattr(self, 'fix_dependencies', []),
            dol=dict(path=str(dol_path), sections=out.describe().splitlines()))
        report_path = out_dir / 'build-report.json'
        report_path.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')

        changed = sum(1 for d in diffs if d.changed)
        self.log(f'[bam] overlay 0x{len(full):X} bytes, {(p.overlay_reserve - len(full)) / 1024:.1f} KB free ({len(image):#x} code/data + {tramp.size:#x} trampolines); '
                 f'{len(patches)} patches; {changed}/{len(diffs)} recompiled functions differ')
        deps = getattr(self, 'fix_dependencies', [])
        if deps:
            self.log(f'[bam] {len(deps)} fixes match text an earlier edit wrote (build-report.json fix_dependencies)')
        self.log(f'[bam] wrote {dol_path}  (BuildAMelee {self.build_id})')

        if iso_out:
            src_iso = p.local.get('paths', {}).get('melee_iso')
            if not src_iso:
                raise BuildError('ISO output requested but paths.melee_iso is not set')
            from .iso import assemble, verify
            verify(Path(src_iso), quick=True)
            from .parts import for_image
            files = for_image(Path(src_iso), self.p.root, p.overlay_base + p.overlay_reserve, self.log)
            off = assemble(Path(src_iso), dol_path, Path(iso_out), files)
            self.log(f'[bam] wrote {iso_out} (DOL at disc offset 0x{off:X}, {len(files)} trimmed donor models added)')
        return BuildResult(dol_path, out_dir / 'overlay.map', report_path, len(full), patches, diffs)
