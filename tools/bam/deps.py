"""Fetch and prepare the pinned Melee decomp (compilers, dtk, wibo, baseline build)."""
from __future__ import annotations
import platform
import shutil
import subprocess
import sys
import tomllib
from pathlib import Path


def run(cmd, cwd=None):
    print('+', ' '.join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], cwd=str(cwd) if cwd else None, check=True)


def fetch(root: Path, project, jobs: int = 4, build_baseline: bool = True) -> Path:
    manifest = tomllib.loads((root / 'deps/manifest.toml').read_text(encoding='utf-8'))
    dep = next(d for d in manifest['dependency'] if d['name'] == 'melee')
    dest = project.decomp
    if not (dest / '.git').exists():
        dest.parent.mkdir(parents=True, exist_ok=True)
        run(['git', 'clone', '--no-checkout', dep['url'], dest])
    run(['git', 'fetch', '--depth', '1', 'origin', dep['rev']], dest)
    run(['git', 'checkout', '--detach', dep['rev']], dest)
    iso = project.local.get('paths', {}).get('melee_iso')
    if not iso:
        raise SystemExit('set paths.melee_iso in config/local.toml (or put the ISO in .iso/) before fetching')
    from .iso import read_dol, verify
    verify(Path(iso), quick=True)
    orig = dest / 'orig/GALE01/sys/main.dol'
    orig.parent.mkdir(parents=True, exist_ok=True)
    orig.write_bytes(read_dol(Path(iso)))
    tools = manifest['tools']
    tdir = dest / 'build/tools'
    tdir.mkdir(parents=True, exist_ok=True)
    exe = '.exe' if platform.system() == 'Windows' else ''
    dl = [sys.executable, 'tools/download_tool.py']
    if platform.system() != 'Windows' and not (tdir / 'wibo').is_file():
        run(dl + ['wibo', tdir / 'wibo', '--tag', tools['wibo']], dest)
    for name, out in (('dtk', f'dtk{exe}'), ('objdiff-cli', f'objdiff-cli{exe}')):
        if not (tdir / out).is_file():
            run(dl + [name, tdir / out, '--tag', tools[name]], dest)
    if not (tdir / 'sjiswrap.exe').is_file():
        run(dl + ['sjiswrap', tdir / 'sjiswrap.exe', '--tag', tools['sjiswrap']], dest)
    if not (dest / 'build/compilers').is_dir():
        run(dl + ['compilers', dest / 'build/compilers', '--tag', tools['compilers']], dest)
    if not (dest / 'build/binutils').is_dir():
        run(dl + ['binutils', dest / 'build/binutils', '--tag', tools['binutils']], dest)
    cfg = [sys.executable, 'configure.py', '--map', '--no-progress',
           '--compilers', dest / 'build/compilers', '--binutils', dest / 'build/binutils',
           '--dtk', tdir / f'dtk{exe}', '--objdiff', tdir / f'objdiff-cli{exe}', '--sjiswrap', tdir / 'sjiswrap.exe']
    if platform.system() != 'Windows':
        cfg += ['--wrapper', tdir / 'wibo']
    ninja = shutil.which('ninja') or project.local.get('paths', {}).get('ninja')
    if ninja:
        cfg += ['--ninja', ninja]
    run(cfg, dest)
    if build_baseline:
        run([ninja or 'ninja', '-j', str(jobs), 'build/GALE01/main.dol'], dest)
        from .iso import file_hash, DOL_SHA1
        got = file_hash(dest / 'build/GALE01/main.dol', 'sha1')
        if got != DOL_SHA1:
            raise SystemExit(f'decomp baseline does not reproduce the retail DOL ({got})')
        print('baseline ok: decomp reproduces the retail DOL byte for byte')
    return dest
