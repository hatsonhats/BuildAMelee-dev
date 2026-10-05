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
from typing import Dict, List, Optional, Tuple

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


# Where the apploader puts the filesystem table in memory: the arena top,
# (TOP_BASE - table size) rounded down to 32 bytes. With the retail table
# (0x7529 bytes) that is 0x81829800 in Slippi Dolphin; the overlay must end
# at or below it (project.toml [overlay]).
FST_TOP_BASE = 0x81829800 + 0x7529


def fst_address(size: int) -> int:
    """Lowest address the table can land at for a table of `size` bytes
    (TOP_BASE is known to within 32 bytes)."""
    return (FST_TOP_BASE - 31 - size) & ~31


def _free_extent(ranges, image_size: int, length: int, after: int = 0) -> int:
    cursor = after
    for start, end in sorted(ranges) + [(image_size, image_size)]:
        cursor = (cursor + 31) & ~31
        if start - cursor >= length:
            return cursor
        cursor = max(cursor, end)
    raise ValueError(f'No free extent on the disc for {length} bytes')


def add_files(table: bytes, ranges, image_size: int, files: Dict[str, bytes]) -> Tuple[bytes, List[Tuple[int, bytes]]]:
    """A filesystem table with `files` appended to the root directory, and
    where to write each file's data (free extents of the disc)."""
    count = struct.unpack_from('>I', table, 8)[0]
    entries = bytearray(table[:count * 12])
    names = bytearray(table[count * 12:])
    while names and names[-1] == 0 and len(names) > 1 and names[-2] == 0:
        names.pop()
    if names and names[-1] != 0:
        names.append(0)
    used = list(ranges)
    writes: List[Tuple[int, bytes]] = []
    for name, data in files.items():
        offset = _free_extent(used, image_size, len(data))
        used.append((offset, offset + len(data)))
        writes.append((offset, data))
        entries += struct.pack('>III', len(names), offset, len(data))
        names += name.encode('ascii') + b'\0'
    struct.pack_into('>I', entries, 8, count + len(files))
    return bytes(entries + names), writes


def assemble(source: Path, dol: Path, output: Path, files: Optional[Dict[str, bytes]] = None) -> int:
    """Copy the retail image, place `dol` in a free extent and add `files`
    (trimmed donor models, parts.py) to the root directory. Returns the
    disc offset of the new executable."""
    source, dol, output = map(Path, (source, dol, output))
    if output.resolve() in (source.resolve(), dol.resolve()):
        raise ValueError('Output must differ from all inputs')
    image_size = source.stat().st_size
    executable = dol.read_bytes()
    with source.open('rb') as stream:
        table, ranges = layout(stream)
        stream.seek(0x424)
        fst_offset, _size, fst_max = struct.unpack('>III', stream.read(12))
    destination = _free_extent(ranges, image_size, len(executable))
    new_table, writes = table, []
    if files:
        new_table, writes = add_files(table, ranges + [(destination, destination + len(executable))],
                                      image_size, files)
        # The table grows in place: the space after it up to the next file is free.
        room = min(start for start, _end in ranges if start >= fst_offset + fst_max) - fst_offset
        if len(new_table) > room:
            raise ValueError(f'Filesystem table of {len(new_table)} bytes does not fit its {room}-byte extent')
    output.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, output)
    with output.open('r+b') as stream:
        stream.seek(destination)
        stream.write(executable)
        stream.seek(0x420)
        stream.write(struct.pack('>I', destination))
        if files:
            for offset, data in writes:
                stream.seek(offset)
                stream.write(data)
            stream.seek(fst_offset)
            stream.write(new_table)
            stream.seek(0x428)
            stream.write(struct.pack('>II', len(new_table), len(new_table)))
    with output.open('rb') as stream:
        actual, _ = layout(stream)
    count = struct.unpack_from('>I', table, 8)[0]
    if actual[12:count * 12] != table[12:count * 12] or actual != new_table:
        raise ValueError('Filesystem changed unexpectedly during assembly')
    return destination


def disc_reader(image: Path):
    """read(name) -> bytes of a root-directory file of the image."""
    with Path(image).open('rb') as stream:
        table, _ranges = layout(stream)
    count = struct.unpack_from('>I', table, 8)[0]
    strings = count * 12
    index = {}
    for i in range(1, count):
        kind, start, length = struct.unpack_from('>III', table, i * 12)
        if kind >> 24:
            continue
        at = strings + (kind & 0xFFFFFF)
        index[table[at:table.index(b'\0', at)].decode('ascii', 'replace')] = (start, length)

    def read(name: str) -> bytes:
        start, length = index[name]
        with Path(image).open('rb') as stream:
            stream.seek(start)
            return stream.read(length)
    return read
