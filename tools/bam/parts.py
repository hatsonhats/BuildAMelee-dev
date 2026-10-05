"""Trimmed donor models: only the meshes borrowed moves draw.

A borrowed move that shows part of its donor's body (Marth's sword,
Mewtwo's tail, Peach's dress, Mr. Game & Watch's props...) needs the
donor's model. The whole costume file is 130-780 KB; the parts drawn are a
few KB. For each such donor this writes `Pl<code>Bm.dat`: the costume's
full skeleton (every joint, so poses and envelope weights resolve as in
the original) and every DObj in its original order (the game and the mod
number them), but only the DObjs a move draws keep their materials,
textures and geometry. The rest are empty DObjs (no material, no
polygons), which the game never draws.

The archive keeps the costume's joint root name, so the mod loads it with
the same lookup it uses for the real costume (sword_visual.c).

Built from the player's own disc while the patched image is assembled;
nothing here ships pre-made.
"""
from __future__ import annotations
import re
import struct
from pathlib import Path
from typing import Dict, List, Set, Tuple

# FighterKind -> two-letter file code (Pl<code>Nr.dat).
CODES = {0: 'Mr', 1: 'Fx', 2: 'Ca', 3: 'Dk', 4: 'Kb', 5: 'Kp', 6: 'Lk', 7: 'Sk', 8: 'Ns', 9: 'Pe',
         10: 'Pp', 11: 'Nn', 12: 'Pk', 13: 'Ss', 14: 'Ys', 15: 'Pr', 16: 'Mt', 17: 'Lg', 18: 'Ms',
         19: 'Zd', 20: 'Cl', 21: 'Dr', 22: 'Fc', 23: 'Pc', 24: 'Gw', 25: 'Gn', 26: 'Fe'}

# Model-part groups drawn from the donor's model (sword_visual.c vis_donors):
# kind -> [(group, lowest variant drawn)].
VIS_DONORS = {
    24: [(5, 0), (6, 0), (7, 0), (8, 0)],  # Mr. Game & Watch's props
    9: [(4, 1), (3, 0)],                   # Peach's crown in hand; forward smash items
    4: [(0, 2)],                           # Kirby's stone
    14: [(0, 1)],                          # Yoshi's egg
    13: [(0, 2)],                          # Samus's Morph Ball
}

JOBJ_SPLINE, JOBJ_PTCL = 0x4000, 0x20
MESH_ROW = re.compile(r'^\s*\{\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\s*\},\s*$')


def trimmed_name(kind: int) -> str:
    return f'Pl{CODES[kind]}Bm.dat'


def mesh_rows(anim_rest: Path) -> Dict[int, Set[int]]:
    """kind -> DObj indices from rogue_mesh[] in src/engine/anim_rest.inc."""
    out: Dict[int, Set[int]] = {}
    text = Path(anim_rest).read_text(encoding='utf-8')
    start = text.index('static const RogueMesh rogue_mesh[')
    end = text.index('};', start)
    for line in text[start:end].splitlines()[1:]:
        m = MESH_ROW.match(line)
        if m and int(m.group(1)) != 255:
            out.setdefault(int(m.group(1)), set()).add(int(m.group(3)))
    return out


class Dat:
    def __init__(self, raw: bytes):
        size, data_size, nreloc, nroot, nref = struct.unpack_from('>5I', raw, 0)
        self.data = raw[0x20:0x20 + data_size]
        at = 0x20 + data_size
        self.relocs = list(struct.unpack_from(f'>{nreloc}I', raw, at))
        at += nreloc * 4
        names_at = at + (nroot + nref) * 8
        self.roots: Dict[str, int] = {}
        for i in range(nroot):
            off, name = struct.unpack_from('>II', raw, at + i * 8)
            s = names_at + name
            self.roots[raw[s:raw.index(b'\0', s)].decode()] = off
        self.reloc_set = set(self.relocs)

    def u32(self, o: int) -> int:
        return struct.unpack_from('>I', self.data, o)[0]

    def ptr(self, o: int) -> int:
        """Pointer field at o, or 0 when the field is not relocated (null)."""
        return self.u32(o) if o in self.reloc_set else 0


def _bpp_block(fmt: int) -> Tuple[int, int, int]:
    """GX texture format -> (block width, block height, bytes per block)."""
    return {0: (8, 8, 32), 1: (8, 4, 32), 2: (8, 4, 32), 3: (4, 4, 32), 4: (4, 4, 32), 5: (4, 4, 32),
            6: (4, 4, 64), 8: (8, 8, 32), 9: (8, 4, 32), 10: (4, 4, 32), 14: (8, 8, 32)}.get(fmt, (4, 4, 64))


def _image_bytes(w: int, h: int, fmt: int, levels: int) -> int:
    bw, bh, bb = _bpp_block(fmt)
    total = 0
    for _ in range(max(1, levels)):
        total += ((w + bw - 1) // bw) * ((h + bh - 1) // bh) * bb
        w, h = max(1, w // 2), max(1, h // 2)
    return total


DIRECT_SIZE = {0: 1, 1: 1, 2: 2, 3: 2, 4: 4}  # GXCompType u8, s8, u16, s16, f32
COLOR_SIZE = {0: 2, 1: 3, 2: 4, 3: 2, 4: 3, 5: 4}


def _vertex_extents(dat: Dat, pobj: int) -> Dict[int, int]:
    """Vertex buffer offset -> bytes the PObj's display list reads from it."""
    verts, n_display, display = dat.ptr(pobj + 8), struct.unpack_from('>H', dat.data, pobj + 0xE)[0], dat.ptr(pobj + 0x10)
    attrs = []
    a = verts
    while a and a + 0x18 <= len(dat.data):
        attr, kind, cnt, ctype = struct.unpack_from('>4I', dat.data, a)
        if attr == 0xFF:
            break
        stride = struct.unpack_from('>H', dat.data, a + 0x12)[0]
        attrs.append((attr, kind, cnt, ctype, stride, dat.ptr(a + 0x14)))
        a += 0x18
    if not display or not attrs:
        return {}
    biggest = [-1] * len(attrs)
    pos, end = display, min(len(dat.data), display + n_display * 32)
    while pos + 3 <= end:
        op = dat.data[pos]
        if (op & 0xF8) == 0:  # NOP / end
            break
        count = struct.unpack_from('>H', dat.data, pos + 1)[0]
        pos += 3
        for _ in range(count):
            for k, (attr, kind, cnt, ctype, _stride, _buf) in enumerate(attrs):
                if kind == 2:
                    biggest[k] = max(biggest[k], dat.data[pos]); pos += 1
                elif kind == 3:
                    biggest[k] = max(biggest[k], struct.unpack_from('>H', dat.data, pos)[0]); pos += 2
                elif kind == 1:
                    if attr <= 8:
                        pos += 1
                    elif attr in (0xB, 0xC):
                        pos += COLOR_SIZE.get(ctype, 4)
                    else:
                        comps = {0x9: 2 + cnt, 0xA: 3}.get(attr, 1 + cnt)
                        pos += comps * DIRECT_SIZE.get(ctype, 4)
    out: Dict[int, int] = {}
    for k, (_attr, kind, _cnt, _ctype, stride, buf) in enumerate(attrs):
        if buf and kind in (2, 3) and biggest[k] >= 0:
            out[buf] = max(out.get(buf, 0), (biggest[k] + 1) * stride)
    return out


def trim(raw: bytes, joint_root: str, wanted: Set[int]) -> Tuple[bytes, int, int]:
    """A copy of the costume archive keeping every joint and DObj, with only
    the `wanted` DObjs (indices as the mod numbers them) still pointing at
    their material and polygons. -> (archive, DObjs kept, DObjs total)."""
    dat = Dat(raw)
    if joint_root not in dat.roots:
        raise ValueError(f'no root {joint_root}')
    root = dat.roots[joint_root]

    # Joints and DObjs in the order sword_visual.c numbers them.
    joints: List[int] = []

    def walk(j: int) -> None:
        while j:
            joints.append(j)
            child = dat.ptr(j + 8)
            if child:
                walk(child)
            j = dat.ptr(j + 12)
    walk(root)
    cut: Set[int] = set()        # pointer fields written as null
    clear_flags: Set[int] = set()  # joints whose spline/particle flags are cleared
    dobjs: List[int] = []
    for j in joints:
        if dat.u32(j + 4) & (JOBJ_SPLINE | JOBJ_PTCL):
            cut.add(j + 0x10)
            clear_flags.add(j)
            continue
        d = dat.ptr(j + 0x10)
        while d:
            dobjs.append(d)
            d = dat.ptr(d + 4)
    kept = 0
    for i, d in enumerate(dobjs):
        if i in wanted:
            kept += 1
        else:
            cut.add(d + 8)
            cut.add(d + 0xC)

    # Object extents: from each pointer target to the next one; blobs the
    # walk can size exactly (display lists, vertex buffers, images, tables)
    # may run longer than that.
    starts = sorted(set(dat.u32(r) for r in dat.relocs) | set(dat.roots.values()) | {len(dat.data)})
    import bisect

    def extent(t: int) -> int:
        k = bisect.bisect_right(starts, t)
        return (starts[k] if k < len(starts) else len(dat.data)) - t
    blob: Dict[int, int] = {}
    for i in wanted:
        if i >= len(dobjs):
            continue
        d = dobjs[i]
        m = dat.ptr(d + 8)
        t = dat.ptr(m + 8) if m else 0
        while t:
            img = dat.ptr(t + 0x4C)
            if img:
                w, h, fmt, mip = struct.unpack_from('>HHII', dat.data, img + 4)
                maxlod = struct.unpack_from('>f', dat.data, img + 0x14)[0]
                data = dat.ptr(img)
                if data:
                    levels = int(maxlod) + 1 if mip else 1
                    blob[data] = max(blob.get(data, 0), _image_bytes(w, h, fmt, levels))
            tl = dat.ptr(t + 0x50)
            if tl and dat.ptr(tl):
                n = struct.unpack_from('>H', dat.data, tl + 0xC)[0]
                blob[dat.ptr(tl)] = max(blob.get(dat.ptr(tl), 0), n * 2)
            t = dat.ptr(t + 4)
        p = dat.ptr(d + 0xC)
        while p:
            n_display = struct.unpack_from('>H', dat.data, p + 0xE)[0]
            dl = dat.ptr(p + 0x10)
            if dl:
                blob[dl] = max(blob.get(dl, 0), n_display * 32)
            for buf, size in _vertex_extents(dat, p).items():
                blob[buf] = max(blob.get(buf, 0), size)
            p = dat.ptr(p + 4)

    # Closure: every object reachable from the joint root through pointers
    # that are not cut. Only pointers inside an object's own extent count.
    primary: Dict[int, int] = {}
    todo = [root]
    relocs_sorted = sorted(dat.relocs)
    while todo:
        t = todo.pop()
        if t in primary:
            continue
        primary[t] = extent(t)
        lo = bisect.bisect_left(relocs_sorted, t)
        hi = bisect.bisect_left(relocs_sorted, t + primary[t])
        for r in relocs_sorted[lo:hi]:
            if r in cut:
                continue
            target = dat.u32(r)
            if target not in primary:
                todo.append(target)

    # Byte spans to copy (objects plus blob tails), merged; each keeps its
    # offset modulo 32 so textures and display lists stay aligned.
    ranges = sorted((t, t + max(size, blob.get(t, 0))) for t, size in primary.items())
    spans: List[List[int]] = []
    for a, b in ranges:
        b = min(b, len(dat.data))
        if spans and a <= spans[-1][1]:
            spans[-1][1] = max(spans[-1][1], b)
        else:
            spans.append([a, b])
    out = bytearray()
    remap: List[Tuple[int, int, int]] = []  # (old start, old end, new start)
    for a, b in spans:
        base = len(out)
        base += (a - base) % 32
        out += b'\0' * (base - len(out))
        out += dat.data[a:b]
        remap.append((a, b, base))
    starts_old = [s[0] for s in remap]

    def new(o: int) -> int:
        k = bisect.bisect_right(starts_old, o) - 1
        a, b, base = remap[k]
        if not (a <= o < b):
            raise ValueError(f'offset {o:#x} not copied')
        return base + o - a

    relocs: List[int] = []
    inside = sorted(primary.items())
    for r in relocs_sorted:
        # Pointer fields inside copied objects (not inside a blob tail).
        k = bisect.bisect_right(inside, (r, 1 << 62)) - 1
        if k < 0 or not (inside[k][0] <= r < inside[k][0] + inside[k][1]):
            continue
        at = new(r)
        if r in cut:
            out[at:at + 4] = b'\0\0\0\0'
            continue
        out[at:at + 4] = struct.pack('>I', new(dat.u32(r)))
        relocs.append(at)
    for j in clear_flags:
        at = new(j + 4)
        flags = struct.unpack_from('>I', out, at)[0] & ~(JOBJ_SPLINE | JOBJ_PTCL)
        out[at:at + 4] = struct.pack('>I', flags)
    while len(out) % 32:
        out.append(0)

    names = joint_root.encode() + b'\0'
    body = bytes(out)
    reloc_bytes = b''.join(struct.pack('>I', r) for r in sorted(relocs))
    root_table = struct.pack('>II', new(root), 0)
    tail = reloc_bytes + root_table + names
    tail += b'\0' * ((-(0x20 + len(body) + len(tail))) % 32)
    # HSD_ArchiveParse requires the header's size to equal the file's length.
    total = 0x20 + len(body) + len(tail)
    header = struct.pack('>5I', total, len(body), len(relocs), 1, 0) + b'\0' * 12
    archive = header + body + tail
    assert len(archive) == total
    return archive, kept, len(dobjs)


def _vis_dobjs(raw: bytes, groups: List[Tuple[int, int]]) -> Set[int]:
    """DObjs of the listed model-part group variants."""
    dat = Dat(raw)
    data = next(o for n, o in dat.roots.items() if n.startswith('ftData'))
    x8 = dat.ptr(data + 8)
    count, table = dat.u32(x8), dat.ptr(x8 + 4)
    out: Set[int] = set()
    for which in (1,):  # the full-detail set (sword_visual.c vis_lookup: vis_table[0][1])
        lookup = dat.ptr(table + 4 * which)
        if not lookup:
            continue
        for group, first in groups:
            if group >= count:
                continue
            variants, entries = dat.u32(lookup + 8 * group), dat.ptr(lookup + 8 * group + 4)
            for v in range(first, variants):
                n, at = dat.u32(entries + 8 * v), dat.ptr(entries + 8 * v + 4)
                out |= set(dat.data[at:at + n])
    return out


def build_all(read_file, anim_rest: Path, log=print) -> Dict[str, bytes]:
    """{file name: trimmed archive} for every donor the mod draws parts of.
    `read_file(name)` returns a disc file's bytes."""
    wanted = mesh_rows(anim_rest)
    for kind, groups in VIS_DONORS.items():
        wanted.setdefault(kind, set()).update(_vis_dobjs(read_file(f'Pl{CODES[kind]}.dat'), groups))
    out: Dict[str, bytes] = {}
    for kind in sorted(wanted):
        code = CODES[kind]
        raw = read_file(f'Pl{code}Nr.dat')
        dat = Dat(raw)
        root = next(n for n in dat.roots if n.endswith('_joint') and 'matanim' not in n and 'anim' not in n.lower())
        archive, kept, total = trim(raw, root, wanted[kind])
        out[trimmed_name(kind)] = archive
        log(f'[bam]   parts {trimmed_name(kind)}: {len(archive) // 1024} KB of {len(raw) // 1024} KB '
            f'({kept} of {total} meshes)')
    return out


def for_image(image: Path, root: Path, overlay_end: int, log=print) -> Dict[str, bytes]:
    """Trimmed models from a clean image, after checking that the larger
    filesystem table still leaves the overlay below the arena top."""
    from .iso import disc_reader, fst_address, layout
    files = build_all(disc_reader(image), Path(root) / 'src' / 'engine' / 'anim_rest.inc', log)
    with Path(image).open('rb') as stream:
        table, _ranges = layout(stream)
    grown = len(table) + sum(12 + len(n) + 1 for n in files)
    if overlay_end > fst_address(grown):
        raise ValueError(f'overlay ends at {overlay_end:#x}, above the arena top {fst_address(grown):#x} '
                         'with the trimmed models; lower overlay.reserve')
    return files
