"""Reading HSD archives (the game's .dat files) from a disc image.

One reader for every tool that looks inside the player's own files: the
bone tables generator (bonetables.py) and the trimmed donor models
(parts.py). Nothing read here is ever committed; it comes from the clean
ISO at build time.
"""
from __future__ import annotations
import struct
from typing import Dict, Optional


class Archive:
    """An HSD archive: a data block, the offsets of its pointer fields
    (relocations) and named roots.

    A pointer field is relocated; an unrelocated field is null even if it
    holds 0 (the data block's first byte is a valid pointer target, so a
    zero value alone does not mean null). `ptr` returns None for null.
    """

    def __init__(self, raw: bytes):
        if len(raw) < 0x20:
            raise ValueError('truncated HSD header')
        size, data_size, nreloc, nroot, nref = struct.unpack_from('>5I', raw, 0)
        if size != len(raw):
            raise ValueError(f'HSD length mismatch ({size} in the header, {len(raw)} read)')
        self.data = raw[0x20:0x20 + data_size]
        at = 0x20 + data_size
        self.relocs = list(struct.unpack_from(f'>{nreloc}I', raw, at))
        self.reloc_set = set(self.relocs)
        at += nreloc * 4
        names_at = at + (nroot + nref) * 8
        self.roots: Dict[str, int] = {}
        for i in range(nroot):
            off, name = struct.unpack_from('>II', raw, at + i * 8)
            s = names_at + name
            self.roots[raw[s:raw.index(b'\0', s)].decode('ascii')] = off

    # Kept for older callers.
    @property
    def relocations(self):
        return self.reloc_set

    def unpack(self, fmt: str, offset: int):
        if offset < 0 or offset + struct.calcsize('>' + fmt) > len(self.data):
            raise ValueError('HSD structure outside data')
        return struct.unpack_from('>' + fmt, self.data, offset)

    def u32(self, offset: int) -> int:
        return struct.unpack_from('>I', self.data, offset)[0]

    def u16(self, offset: int) -> int:
        return struct.unpack_from('>H', self.data, offset)[0]

    def ptr(self, offset: int) -> Optional[int]:
        """The pointer at `offset`, or None when the field is null."""
        return self.u32(offset) if offset in self.reloc_set else None

    def root(self, suffix: str) -> int:
        """Offset of the first root whose name ends with `suffix` (not an animation root)."""
        return next(o for n, o in self.roots.items() if n.endswith(suffix) and 'anim' not in n.lower())


def joint_walk(arc: Archive, root: int):
    """Joints in the game's order (depth first: child, then next), each as
    (offset, parent walk index or -1)."""
    out = []

    def walk(j: Optional[int], parent: int) -> None:
        while j is not None:
            me = len(out)
            out.append((j, parent))
            walk(arc.ptr(j + 8), me)
            j = arc.ptr(j + 12)
    walk(root, -1)
    return out
