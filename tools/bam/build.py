"""Build pipeline: retail DOL + overlay section + hook patches -> main.dol (+ ISO)."""
from __future__ import annotations
import json
import re
import shutil
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Set

from . import ppc
from .compare import compare_function, FunctionDiff
from .dol import Dol
from .elf import Elf, STT_FUNC, STT_OBJECT
from .hooks import Hook, Trampolines, build_patches, Patch
from .project import Project
from .symbols import SymbolTable, parse_symbols, parse_splits, Symbol
from .toolchain import Toolchain, parse_map, ToolError
from .units import BuildError, UnitBuild, UnitSteps  # noqa: F401 (BuildError is re-exported)



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

class Builder(UnitSteps):
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

    # ---- main --------------------------------------------------------------

    # ---- steps -------------------------------------------------------------

    def link(self, objs: List[Path], units: List[UnitBuild]):
        """Links the overlay; retail symbols the overlay does not define are
        absolute. A unit missing a function it needs gets it kept and is
        recompiled, then the link is tried again."""
        p = self.p
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
        return elf_path, map_path

    def function_diffs(self, retail: Dol, elf: Elf, image: bytes, map_path: Path, ovl: Dict[str, int],
                       units: List[UnitBuild]) -> List[FunctionDiff]:
        """Every recompiled retail function, and whether it differs from the
        retail one (then it is overridden)."""
        p = self.p
        mapsyms = parse_map(map_path)
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
        return diffs

    def write_dol(self, patches: List[Patch], full: bytes):
        """The retail DOL with the hook patches and the overlay section:
        its path and its section list."""
        p = self.p
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
        return dol_path, out.describe().splitlines()

    def write_iso(self, dol_path: Path, iso_out: Path) -> None:
        """A patched copy of the player's ISO with the trimmed donor models."""
        p = self.p
        src_iso = p.local.get('paths', {}).get('melee_iso')
        if not src_iso:
            raise BuildError('ISO output requested but paths.melee_iso is not set')
        from .iso import assemble, verify
        verify(Path(src_iso), quick=True)
        from .parts import for_image
        files = for_image(Path(src_iso), self.p.root, p.overlay_base + p.overlay_reserve, self.log)
        off = assemble(Path(src_iso), dol_path, Path(iso_out), files)
        self.log(f'[bam] wrote {iso_out} (DOL at disc offset 0x{off:X}, {len(files)} trimmed donor models added)')

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

        elf_path, map_path = self.link(objs, units)
        elf = Elf(elf_path)
        image = elf.image(p.overlay_base)
        ovl_end = p.overlay_base + len(image)
        # Overlay symbol lookup from the ELF symbol table (authoritative).
        ovl: Dict[str, int] = {}
        for sym in elf.symbols():
            if sym.defined and sym.name and sym.type in (STT_FUNC, STT_OBJECT, 0):
                ovl.setdefault(sym.name, sym.value)
        diffs = self.function_diffs(retail, elf, image, map_path, ovl, units)

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

        # Refuse to patch where Slippi's own codes inject.
        from .slippi import check_collisions
        ini = slippi_ini or (self.p.root / p.slippi_ini if p.slippi_ini else None)
        gct = self.p.root / p.slippi_gct if p.slippi_gct else None
        check_collisions(patches, diffs, p.hooks, self.syms, ini, gct)

        dol_path, sections = self.write_dol(patches, full)
        out_dir = p.build_dir / 'output'
        shutil.copyfile(map_path, out_dir / 'overlay.map')
        report = dict(
            overlay=dict(base=f'0x{p.overlay_base:08X}', end=f'0x{p.overlay_base + len(full):08X}', size=len(full),
                         trampolines=f'0x{tramp_base:08X}', reserve=p.overlay_reserve),
            patches=[dict(addr=f'0x{pt.addr:08X}', value=f'0x{pt.value:08X}', original=f'0x{pt.original:08X}', hook=pt.hook) for pt in patches],
            overrides=[dict(name=d.name, retail=f'0x{d.retail_addr:08X}', overlay=f'0x{d.overlay_addr:08X}', changed=d.changed, reason=d.reason) for d in diffs],
            units=[dict(unit=u.spec.unit, functions=sorted(u.defined_functions), externized=u.externized) for u in units],
            fix_dependencies=getattr(self, 'fix_dependencies', []),
            dol=dict(path=str(dol_path), sections=sections))
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
            self.write_iso(dol_path, iso_out)
        return BuildResult(dol_path, out_dir / 'overlay.map', report_path, len(full), patches, diffs)
