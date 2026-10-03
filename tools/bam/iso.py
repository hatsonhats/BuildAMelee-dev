"""Read-only verification of the retail image and placement of a grown DOL.

The output disc keeps the GALE01 game ID, the retail banner/header, the
retail apploader, and the retail filesystem byte for byte. Only the DOL
pointer at 0x420 changes, to a free extent that holds the larger executable.
Slippi recognises the disc as plain NTSC 1.02 and applies its normal code list.
"""
from __future__ import annotations
import hashlib
import shutil
import struct
from pathlib import Path
from typing import List, Tuple

ISO_MD5 = '0e63d4223b01d9aba596259dc155a174'
DOL_SHA1 = '08e0bf20134dfcb260699671004527b2d6bb1a45'
GAME_ID = b'GALE01'


def file_hash(path: Path, algo: str = 'sha256') -> str:
    h = hashlib.new(algo)
    with Path(path).open('rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def read_dol(image: Path) -> bytes:
    with Path(image).open('rb') as stream:
        header = stream.read(0x440)
        if len(header) != 0x440 or header[:6] != GAME_ID or header[7] != 2:
            raise ValueError('Expected GALE01 NTSC-U revision 1.02 disc header')
        offset = struct.unpack_from('>I', header, 0x420)[0]
        stream.seek(offset)
        dol_header = stream.read(0x100)
        offsets = struct.unpack_from('>18I', dol_header, 0)
        sizes = struct.unpack_from('>18I', dol_header, 0x90)
        length = max([0x100] + [o + s for o, s in zip(offsets, sizes) if s])
        stream.seek(offset)
        return stream.read(length)


def verify(image: Path, quick: bool = False) -> dict:
    image = Path(image).resolve()
    if not image.is_file():
        raise ValueError(f'Image does not exist: {image}')
    dol = read_dol(image)
    sha1 = hashlib.sha1(dol).hexdigest()
    if sha1 != DOL_SHA1:
        raise ValueError(f'Unsupported DOL SHA1 {sha1}; expected a clean NTSC 1.02 image')
    info = dict(image=str(image), game_id='GALE01', revision=2, bytes=image.stat().st_size,
                dol_sha1=sha1, dol_bytes=len(dol))
    if not quick:
        md5 = file_hash(image, 'md5')
        if md5 != ISO_MD5:
            raise ValueError(f'Unsupported image MD5 {md5}; expected {ISO_MD5}')
        info['md5'] = md5
    return info


def layout(stream) -> Tuple[bytes, List[Tuple[int, int]]]:
    stream.seek(0x420)
    dol, fst, size, maximum = struct.unpack('>4I', stream.read(16))
    stream.seek(fst)
    table = stream.read(size)
    if len(table) != size or size < 12 or table[0] != 1:
        raise ValueError('Invalid disc filesystem table')
    count = struct.unpack_from('>I', table, 8)[0]
    ranges = [(0, 0x2440), (fst, fst + maximum)]
    stream.seek(0x2454)
    loader, trailer = struct.unpack('>II', stream.read(8))
    ranges.append((0x2440, 0x2460 + loader + trailer))
    stream.seek(dol)
    header = stream.read(256)
    offsets = struct.unpack_from('>18I', header)
    lengths = struct.unpack_from('>18I', header, 0x90)
    ranges.append((dol, dol + max(256, *(a + b for a, b in zip(offsets, lengths) if b))))
    for i in range(1, count):
        kind, start, length = struct.unpack_from('>III', table, i * 12)
        if not kind >> 24:
            ranges.append((start, start + length))
    return table, sorted(ranges)


def assemble(source: Path, dol: Path, output: Path) -> int:
    """Copy the retail image and place `dol` in a free extent. Returns the
    disc offset of the new executable."""
    source, dol, output = map(Path, (source, dol, output))
    if output.resolve() in (source.resolve(), dol.resolve()):
        raise ValueError('Output must differ from all inputs')
    image_size = source.stat().st_size
    executable = dol.read_bytes()
    with source.open('rb') as stream:
        table, ranges = layout(stream)
    cursor = 0
    destination = None
    for start, end in ranges + [(image_size, image_size)]:
        cursor = (cursor + 31) & ~31
        if start - cursor >= len(executable):
            destination = cursor
            break
        cursor = max(cursor, end)
    if destination is None:
        raise ValueError('No free extent on the disc for the rebuilt executable')
    output.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, output)
    with output.open('r+b') as stream:
        stream.seek(destination)
        stream.write(executable)
        stream.seek(0x420)
        stream.write(struct.pack('>I', destination))
    with output.open('rb') as stream:
        actual, _ = layout(stream)
    if actual != table:
        raise ValueError('Filesystem changed during executable assembly')
    return destination
