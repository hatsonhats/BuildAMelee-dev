#!/usr/bin/env python3
"""BuildAMelee command line.

  bam.py doctor                 check toolchain, decomp checkout, ISO
  bam.py fetch                  clone the pinned decomp and set it up (tools, compilers)
  bam.py build [--iso OUT]      build build/output/main.dol (and an ISO)
  bam.py release [X.Y]           set the version, build, make the player zip (build/release/)
  bam.py check-slippi           list Slippi injections inside functions we touch
  bam.py info                   print retail DOL layout and project settings
"""
from __future__ import annotations
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from bam.project import Project, ROOT  # noqa: E402


def cmd_info(args):
    p = Project.load(ROOT)
    from bam.build import Builder
    b = Builder(p, args.decomp)
    print(f'project {p.name}: overlay 0x{p.overlay_base:08X} reserve 0x{p.overlay_reserve:X}')
    print(f'decomp: {b.decomp}')
    print(b.retail_dol().describe())


def cmd_build(args):
    if args.qa:
        import subprocess
        script = ROOT / 'qa' / (args.qa + '.txt')
        subprocess.run([sys.executable, str(ROOT / 'tools/qa/script.py'), str(script),
                        str(ROOT / 'src/qa/qa_script.inc')], check=True)
    p = Project.load(ROOT, qa=bool(args.qa))
    if args.debug:
        p.defines['BAM_DEBUG'] = '1'
    from bam.build import Builder
    b = Builder(p, args.decomp, verbose=args.verbose)
    iso_out = Path(args.iso) if args.iso else None
    if iso_out is None and args.auto_iso:
        iso_out = p.build_dir / 'output' / 'BuildAMelee.iso'
    b.build(iso_out=iso_out)


def cmd_check_slippi(args):
    p = Project.load(ROOT)
    from bam.build import Builder
    from bam.slippi import functions_hooked
    b = Builder(p, args.decomp)
    ini = Path(args.ini) if args.ini else (ROOT / p.slippi_ini if p.slippi_ini else None)
    if not ini or not ini.is_file():
        sys.exit('no Slippi INI; pass --ini <GALE01r2.ini>')
    hooked = functions_hooked(ini, b.syms)
    touched = set()
    for spec in p.overrides:
        touched.update(s.name for s in b.syms.unit_functions(spec.unit))
    for h in p.hooks:
        name = h.at.split('+')[0].split('-')[0].strip()
        if name in b.syms.by_name:
            touched.add(name)
    print(f'Slippi default codes inject into {len(hooked)} retail functions')
    both = sorted(touched & set(hooked))
    print(f'{len(both)} of them are functions this project touches:')
    for fn in both:
        for addr, off, names in hooked[fn]:
            print(f'  {fn}+0x{off:X} ({addr:#010x}): {", ".join(sorted(set(names)))}')


def cmd_doctor(args):
    p = Project.load(ROOT)
    ok = True
    decomp = Path(args.decomp) if args.decomp else p.decomp
    print(f'decomp checkout: {decomp} ', end='')
    if (decomp / 'config/GALE01/symbols.txt').is_file():
        print('ok')
    else:
        print('MISSING (run: bam.py fetch)'); ok = False
    for rel in ('build/compilers/GC/1.3.2/mwcceppc.exe', 'build/compilers/GC/1.2.5n/mwcceppc.exe',
                'build/compilers/Wii/1.7/mwcceppc.exe', 'build.ninja', 'orig/GALE01/sys/main.dol'):
        print(f'  {rel}: ', 'ok' if (decomp / rel).is_file() else 'MISSING')
        ok &= (decomp / rel).is_file()
    iso = p.local.get('paths', {}).get('melee_iso')
    print(f'melee iso: {iso or "(not set: config/local.toml paths.melee_iso or .iso/*.iso)"}')
    if iso:
        from bam.iso import verify
        try:
            verify(Path(iso), quick=True); print('  dol sha1 ok (clean NTSC 1.02)')
        except Exception as e:  # noqa: BLE001
            print('  INVALID:', e); ok = False
    print('OK' if ok else 'problems found')


def cmd_iso(args):
    """Place a prebuilt DOL into a copy of the retail ISO (no compiler needed)."""
    p = Project.load(ROOT)
    from bam.iso import assemble, verify
    src = args.source or p.local.get('paths', {}).get('melee_iso')
    if not src:
        sys.exit('set paths.melee_iso in config/local.toml, drop the ISO in .iso/, or pass --source')
    dol = Path(args.dol) if args.dol else ROOT / 'dist/main.dol'
    if not args.dol and not dol.is_file():
        dol = p.build_dir / 'output/main.dol'  # a source checkout: the last build
    out = Path(args.out) if args.out else p.build_dir / 'output/BuildAMelee.iso'
    info = verify(Path(src), quick=args.quick)
    print(f'source ok: {info["image"]}')
    from bam.parts import for_image
    files = for_image(Path(src), ROOT, p.overlay_base + p.overlay_reserve)
    off = assemble(Path(src), dol, out, files)
    import re as _re
    m = _re.search(rb'\0(\d+\.\d+(?:\.\d+)?-[0-9a-f]{6})\0', dol.read_bytes())
    print(f'BuildAMelee build {m.group(1).decode() if m else "?"}  (dolphin.log should show the same "build")')
    print(f'wrote {out}\n  DOL {dol} placed at disc offset 0x{off:X}; {len(files)} trimmed donor models added')


def cmd_release(args):
    from bam.release import release
    release(ROOT, args.version, args.decomp)


def cmd_fetch(args):
    from bam.deps import fetch
    fetch(ROOT, Project.load(ROOT), jobs=args.jobs, build_baseline=not args.no_baseline)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--decomp', help='path to the decomp checkout (default .cache/melee or paths.decomp)')
    sub = ap.add_subparsers(dest='cmd', required=True)
    s = sub.add_parser('info'); s.set_defaults(fn=cmd_info)
    s = sub.add_parser('build'); s.set_defaults(fn=cmd_build)
    s.add_argument('--iso', help='also write a patched ISO to this path')
    s.add_argument('--auto-iso', action='store_true', help='write build/output/BuildAMelee.iso')
    s.add_argument('-v', '--verbose', action='store_true')
    s.add_argument('--debug', action='store_true', help='detailed [bam] log and internal checks (not for release)')
    s.add_argument('--qa', metavar='SCRIPT', help='QA build driven by qa/SCRIPT.txt (never ship)')
    s = sub.add_parser('check-slippi'); s.set_defaults(fn=cmd_check_slippi)
    s.add_argument('--ini')
    s = sub.add_parser('doctor'); s.set_defaults(fn=cmd_doctor)
    s = sub.add_parser('iso', help='write an ISO from a prebuilt DOL (dist/main.dol)'); s.set_defaults(fn=cmd_iso)
    s.add_argument('--dol'); s.add_argument('--out'); s.add_argument('--source')
    s.add_argument('--quick', action='store_true', help='skip the full-image MD5 (checks the DOL hash only)')
    s = sub.add_parser('release', help='set the version, build, and package the player zip')
    s.set_defaults(fn=cmd_release)
    s.add_argument('version', nargs='?', help='new version (X.Y, or X.Y.Z for a hotfix); default: keep project.toml\'s')
    s = sub.add_parser('fetch'); s.set_defaults(fn=cmd_fetch)
    s.add_argument('--jobs', type=int, default=4)
    s.add_argument('--no-baseline', action='store_true', help='skip building/verifying the retail DOL')
    args = ap.parse_args(argv)
    args.fn(args)


if __name__ == '__main__':
    main()
