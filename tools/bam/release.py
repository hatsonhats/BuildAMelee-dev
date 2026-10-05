"""Player release: build, patch against the clean ISO, package the zip.

  bam.py release 1.2

1. Sets [project] version in project.toml (when a version is given).
2. Builds the DOL and writes it into a copy of the clean ISO.
3. Makes patch.xdelta (clean ISO -> BuildAMelee ISO) and checks that
   applying it reproduces the ISO exactly.
4. Packages build/release/BuildAMelee-vX.Y.zip from packaging/: the
   drag-and-drop .bat, the Linux/Mac script, README.txt and xdelta3.exe.

xdelta3 is downloaded once into .cache/ (the official 3.1.0 Windows build);
on Linux and macOS the system xdelta3 does the encoding.
"""
from __future__ import annotations
import io
import os
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import zipfile
from pathlib import Path

from .iso import assemble, file_hash, verify
from .project import Project

XDELTA_URL = 'https://github.com/jmacd/xdelta-gpl/releases/download/v3.1.0/xdelta3-3.1.0-x86_64.exe.zip'
XDELTA_EXE_MD5 = '93110bd8eaa3be753e03db56765f49a2'


def set_version(root: Path, version: str) -> None:
    if not re.fullmatch(r'\d+\.\d+(\.\d+)?', version):
        sys.exit(f'version must look like 1.2 (or 1.2.1), not {version!r}')
    path = root / 'project.toml'
    text = path.read_text(encoding='utf-8')
    new, n = re.subn(r'(?m)^version = "[^"]*"', f'version = "{version}"', text, count=1)
    if not n:
        sys.exit('project.toml has no [project] version line')
    if new != text:
        path.write_text(new, encoding='utf-8')
        print(f'[release] project.toml version -> {version}')


def windows_xdelta(root: Path) -> Path:
    """The xdelta3.exe shipped in the zip (downloaded once, checked)."""
    exe = root / '.cache' / 'xdelta3.exe'
    if exe.is_file() and file_hash(exe, 'md5') == XDELTA_EXE_MD5:
        return exe
    print(f'[release] downloading xdelta 3.1.0 from {XDELTA_URL}')
    with urllib.request.urlopen(XDELTA_URL) as r:
        data = r.read()
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        name = next(n for n in z.namelist() if n.endswith('.exe'))
        exe.parent.mkdir(parents=True, exist_ok=True)
        exe.write_bytes(z.read(name))
    if file_hash(exe, 'md5') != XDELTA_EXE_MD5:
        exe.unlink()
        sys.exit('downloaded xdelta3.exe does not match the expected build')
    return exe


def local_xdelta(root: Path) -> str:
    if os.name == 'nt':
        return str(windows_xdelta(root))
    for name in ('xdelta3', 'xdelta'):
        if shutil.which(name):
            return name
    sys.exit('xdelta3 not found: install it (apt install xdelta3 / brew install xdelta)')


def run(cmd):
    print('[release] ' + ' '.join(str(c) for c in cmd))
    subprocess.run([str(c) for c in cmd], check=True)


def release(root: Path, version: str | None, decomp: str | None = None) -> Path:
    if version:
        set_version(root, version)
    p = Project.load(root)
    version = p.version
    clean = p.local.get('paths', {}).get('melee_iso')
    if not clean:
        sys.exit('set paths.melee_iso in config/local.toml (your clean NTSC 1.02 ISO)')
    clean = Path(clean)
    print(f'[release] checking {clean}')
    verify(clean, quick=False)  # the patch only applies to this exact image

    from .build import Builder
    b = Builder(p, decomp)
    res = b.build()
    out = root / 'build' / 'release'
    out.mkdir(parents=True, exist_ok=True)
    iso = out / 'BuildAMelee.iso'
    from .parts import for_image
    files = for_image(clean, root, p.overlay_base + p.overlay_reserve)
    assemble(clean, res.dol, iso, files)
    iso_md5 = file_hash(iso, 'md5')

    xd = local_xdelta(root)
    patch = out / 'patch.xdelta'
    run([xd, '-e', '-9', '-S', 'djw', '-f', '-s', clean, iso, patch])
    with tempfile.TemporaryDirectory(dir=out) as tmp:
        check = Path(tmp) / 'check.iso'
        run([xd, '-d', '-f', '-s', clean, patch, check])
        if file_hash(check, 'md5') != iso_md5:
            sys.exit('patch check failed: applying patch.xdelta did not reproduce the ISO')
    print(f'[release] patch.xdelta checked ({patch.stat().st_size / 1e6:.1f} MB)')

    pkg = root / 'packaging'
    name = f'BuildAMelee-v{version}'
    zpath = out / f'{name}.zip'
    readme = (pkg / 'README.txt').read_text(encoding='utf-8').format(
        version=version, underline='=' * len(f'BuildAMelee v{version}'), md5=iso_md5)

    def crlf(s: str) -> bytes:
        return s.replace('\r\n', '\n').replace('\n', '\r\n').encode('utf-8')

    import time
    now = time.localtime()[:6]

    def text(z, arc, data: bytes, mode=0o644):
        info = zipfile.ZipInfo(f'{name}/{arc}', date_time=now)
        info.external_attr = (0o100000 | mode) << 16
        info.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(info, data)

    bat = 'DRAG VANILLA MELEE HERE.bat'
    with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED) as z:
        text(z, 'README.txt', crlf(readme))
        text(z, bat, crlf((pkg / bat).read_text(encoding='utf-8')))
        text(z, 'patch_linux_mac.sh',
             (pkg / 'patch_linux_mac.sh').read_text(encoding='utf-8').replace('\r\n', '\n').encode(), 0o755)
        z.write(patch, f'{name}/patch.xdelta')
        z.write(windows_xdelta(root), f'{name}/xdelta3.exe')

    print(f'\n[release] BuildAMelee {b.build_id}')
    print(f'[release] patched ISO MD5 {iso_md5}')
    print(f'[release] wrote {zpath}')
    print(f'[release] test: unzip it, drag your clean ISO onto the .bat, play the new BuildAMelee.iso')
    print(f'[release] then commit, tag v{version}, and attach the zip to a GitHub release')
    return zpath
