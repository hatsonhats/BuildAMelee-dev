"""Recompiled retail units: a decomp source file with our line edits and
fixes applied, cut down to the functions they change, its mutable data bound
to retail's copies, and compiled with the decomp's own flags."""
from __future__ import annotations
import json
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Set

from .elf import Elf, STT_FUNC, STT_OBJECT
from .project import OverrideSpec
from .transform import (apply_anchor_edits, apply_line_edits, line_edits_for, externize, check_sha256,
                        AnchorEdit, changed_by_text, strip_functions)

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


class UnitSteps:
    """The Builder's unit steps (build.py): expects self.p, self.tc,
    self.syms, self.decomp, self.out, self.aliases, self.unit_aliases,
    self.log, self._flags_stamp and self._headers_mtime."""

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

    _local_counts: Optional[Dict[str, int]] = None

    def _unique_local(self, name: str) -> bool:
        if self._local_counts is None:
            c: Dict[str, int] = {}
            for s in self.syms.symbols:
                c[s.name] = c.get(s.name, 0) + 1
            self._local_counts = c
        return self._local_counts.get(name, 0) == 1
