"""Project manifest (project.toml) and local configuration (config/local.toml)."""
from __future__ import annotations
import os
import tomllib
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, List, Optional

ROOT = Path(__file__).resolve().parents[2]


def _int(v: Any) -> int:
    return int(v, 0) if isinstance(v, str) else int(v)


@dataclass
class OverrideSpec:
    unit: str                               # e.g. melee/ft/fighter.c
    line_edits: Optional[str] = None        # path to a special_adapters-style json (shared)
    anchor_edits: List[Dict[str, Any]] = field(default_factory=list)
    externize: Optional[List[str]] = None   # None = automatic
    keep_const_copies: bool = True
    functions: Optional[List[str]] = None   # restrict overrides to these functions
    compiler: Optional[str] = None          # default: the unit's retail compiler
    extra_cflags: List[str] = field(default_factory=list)
    prepend: str = ''                       # text prepended to the generated unit
    strip_unchanged: bool = False           # keep only functions whose text the edits change
    skip_functions: List[str] = field(default_factory=list)  # never override (keep retail)
    keep_functions: List[str] = field(default_factory=list)  # always keep in the overlay


def load_fixes(path: Path) -> List[Dict[str, Any]]:
    """Anchor edits from a .toml file or every .toml in a directory (in name
    order), each tagged with the file it came from. Ids must be unique."""
    files = sorted(path.glob('*.toml')) if path.is_dir() else [path]
    fixes, seen = [], {}
    for f in files:
        for fx in tomllib.loads(f.read_text(encoding='utf-8')).get('fix', []):
            for key in ('id', 'owner', 'file', 'anchor', 'replacement'):
                if key not in fx:
                    raise ValueError(f'{f.name}: a fix without `{key}` ({fx.get("id", "?")})')
            if fx['id'] in seen:
                raise ValueError(f'{f.name}: fix id {fx["id"]} also in {seen[fx["id"]]}')
            seen[fx['id']] = f.name
            fx['_source'] = f.name
            fixes.append(fx)
    return fixes


@dataclass
class HookSpec:
    id: str
    kind: str
    at: str
    target: str = ''
    args: List[str] = field(default_factory=list)
    ret: Optional[str] = None
    value: Optional[int] = None
    note: str = ''


@dataclass
class Project:
    root: Path
    name: str
    version: str
    overlay_base: int
    overlay_reserve: int
    overlay_stage: Optional[int]
    stage_hook: Optional[str]
    link_compiler: str
    source_dirs: List[str]
    source_compiler: str
    source_cflags: List[str]
    defines: Dict[str, str]
    overrides: List[OverrideSpec]
    hooks: List[HookSpec]
    exports: List[str]
    engine_globals: Dict[str, str]
    slippi_ini: Optional[str]
    slippi_gct: Optional[str]
    local: Dict[str, Any]

    @classmethod
    def load(cls, root: Path = ROOT, qa: bool = False) -> 'Project':
        root = Path(root)
        data = tomllib.loads((root / 'project.toml').read_text(encoding='utf-8'))
        if qa:
            extra = tomllib.loads((root / 'qa.toml').read_text(encoding='utf-8'))
            data.setdefault('sources', {}).setdefault('dirs', ['src'])
            data['sources']['dirs'] = data['sources']['dirs'] + extra.get('sources', {}).get('dirs', [])
            data.setdefault('defines', {}).update(extra.get('defines', {}))
            data['hook'] = data.get('hook', []) + extra.get('hook', [])
            data.setdefault('overlay', {}).update(extra.get('overlay', {}))
            # QA builds run in stock Dolphin, without Slippi's code list.
            if extra.get('slippi', {}).get('ignore'):
                data['slippi'] = {}
        ov = data.get('overlay', {})
        src = data.get('sources', {})
        overrides = []
        for o in data.get('override', []):
            overrides.append(OverrideSpec(
                unit=o['unit'], line_edits=o.get('line_edits'), anchor_edits=o.get('anchor_edits', []),
                externize=o.get('externize'), keep_const_copies=o.get('keep_const_copies', True),
                functions=o.get('functions'), compiler=o.get('compiler'),
                extra_cflags=o.get('extra_cflags', []), prepend=o.get('prepend', '')))
        # Override sets: one pinned edit collection applied to many units.
        for s in data.get('override_set', []):
            line_files = {}
            if s.get('line_edits'):
                import json
                manifest = json.loads((root / s['line_edits']).read_text(encoding='utf-8'))
                line_files = {k[len('src/'):] if k.startswith('src/') else k: True for k in manifest['files']}
            anchors_by_unit: Dict[str, List[Dict[str, Any]]] = {}
            if s.get('anchor_edits'):
                fixes = load_fixes(root / s['anchor_edits'])
                owners = tuple(s.get('owners', []))
                for fx in fixes:
                    if owners and not fx['owner'].startswith(owners):
                        continue
                    u = fx['file'][len('src/'):] if fx['file'].startswith('src/') else fx['file']
                    anchors_by_unit.setdefault(u, []).append(dict(anchor=fx['anchor'], replacement=fx['replacement'],
                                                                  occurrences=fx.get('occurrences', 1), id=fx['id'],
                                                                  source=fx['_source']))
            for u in sorted(set(line_files) | set(anchors_by_unit)):
                if u in s.get('exclude_units', []):
                    continue
                overrides.append(OverrideSpec(
                    unit=u, line_edits=s.get('line_edits') if u in line_files else None,
                    anchor_edits=anchors_by_unit.get(u, []), externize=None, keep_const_copies=False,
                    compiler=s.get('compiler'), extra_cflags=s.get('extra_cflags', []), prepend=s.get('prepend', ''),
                    strip_unchanged=s.get('strip_unchanged', False),
                    skip_functions=s.get('skip_functions', []), keep_functions=list(s.get('keep_functions', []))))
        hooks = [HookSpec(id=h['id'], kind=h['kind'], at=h['at'], target=h.get('target', ''),
                          args=h.get('args', []), ret=h.get('ret'),
                          value=_int(h['value']) if 'value' in h else None, note=h.get('note', ''))
                 for h in data.get('hook', [])]
        local_path = root / 'config/local.toml'
        local = tomllib.loads(local_path.read_text(encoding='utf-8')) if local_path.exists() else {}
        paths = local.setdefault('paths', {})
        for key, env in (('melee_iso', 'MELEE_ISO_PATH'), ('dolphin', 'DOLPHIN_PATH'), ('decomp', 'MELEE_DECOMP_PATH')):
            if os.environ.get(env):
                paths[key] = os.environ[env]
        if not paths.get('melee_iso'):
            images = sorted((root / '.iso').glob('*.iso')) + sorted((root / '.iso').glob('*.gcm'))
            if len(images) == 1:
                paths['melee_iso'] = str(images[0])
        return cls(root=root, name=data.get('project', {}).get('name', 'BuildAMelee'),
                   version=str(data.get('project', {}).get('version', '0.0.0')),
                   overlay_base=_int(ov.get('base', '0x80BD5C40')),
                   overlay_reserve=_int(ov.get('reserve', '0x80000')),
                   overlay_stage=_int(ov['stage']) if 'stage' in ov else None,
                   stage_hook=ov.get('stage_hook'),
                   link_compiler=ov.get('link_compiler', 'GC/1.3.2'),
                   source_dirs=src.get('dirs', ['src']), source_compiler=src.get('compiler', 'Wii/1.7'),
                   source_cflags=src.get('cflags', []), defines={k: str(v) for k, v in data.get('defines', {}).items()},
                   overrides=overrides, hooks=hooks, exports=data.get('exports', []),
                   engine_globals=data.get('state', {}).get('engine_globals', {}),
                   slippi_ini=data.get('slippi', {}).get('ini'), slippi_gct=data.get('slippi', {}).get('gct'), local=local)

    @property
    def decomp(self) -> Path:
        p = self.local.get('paths', {}).get('decomp')
        return Path(p) if p else self.root / '.cache/melee'

    @property
    def build_dir(self) -> Path:
        # QA builds compile with other defines and overlay addresses: their
        # objects must never be reused by a release build (mtime caching).
        return self.root / ('build-qa' if 'BAM_QA' in self.defines else 'build')
