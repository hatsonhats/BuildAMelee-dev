#!/usr/bin/env python3
"""Split the clean ISO into <400 MB parts so it can be copied through the
desktop bridge (per-file limit). Parts go to build/iso_parts/. Never modifies
the source. Reassemble with: cat part.000 part.001 ... > GALE01.iso"""
import hashlib
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from bam.project import Project, ROOT  # noqa: E402

PART = 360 * 1024 * 1024


def main():
    p = Project.load(ROOT)
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(p.local['paths']['melee_iso'])
    out = ROOT / 'build/iso_parts'
    out.mkdir(parents=True, exist_ok=True)
    h = hashlib.md5()
    with src.open('rb') as f:
        i = 0
        while True:
            chunk = f.read(PART)
            if not chunk:
                break
            h.update(chunk)
            (out / f'GALE01.part{i:03d}').write_bytes(chunk)
            print(f'wrote part {i} ({len(chunk)} bytes)', flush=True)
            i += 1
    (out / 'GALE01.md5').write_text(h.hexdigest() + '\n')
    print('md5', h.hexdigest(), '->', out)


if __name__ == '__main__':
    main()
