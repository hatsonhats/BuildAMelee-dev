"""Decide which recompiled retail functions actually differ from retail.

A recompiled translation unit lands in the overlay at a new address, so every
relative branch inside it changes even when the source did not. Comparison
therefore normalises `b`/`bl` targets: an overlay target that is itself a
recompiled copy of retail symbol X is treated as X's retail address.
Everything else must match byte for byte.
"""
from __future__ import annotations
import struct
from dataclasses import dataclass
from typing import Callable, Dict, List, Optional

from . import ppc


@dataclass
class FunctionDiff:
    name: str
    retail_addr: int
    overlay_addr: int
    size: int
    changed: bool
    reason: str = ''


def _words(data: bytes) -> List[int]:
    return list(struct.unpack('>%dI' % (len(data) // 4), data[: len(data) // 4 * 4]))


def compare_function(name: str, retail_addr: int, retail_bytes: bytes,
                     overlay_addr: int, overlay_bytes: bytes,
                     overlay_to_retail: Callable[[int], Optional[int]]) -> FunctionDiff:
    if len(retail_bytes) != len(overlay_bytes):
        return FunctionDiff(name, retail_addr, overlay_addr, len(retail_bytes), True,
                            f'size {len(retail_bytes):#x} -> {len(overlay_bytes):#x}')
    rw, ow = _words(retail_bytes), _words(overlay_bytes)
    for i, (a, b) in enumerate(zip(rw, ow)):
        if a == b and not ppc.is_relative_branch(a):
            continue
        if ppc.is_relative_branch(a) and ppc.is_relative_branch(b) and (a & 3) == (b & 3):
            ta = ppc.branch_target(a, retail_addr + i * 4)
            tb = ppc.branch_target(b, overlay_addr + i * 4)
            mapped = overlay_to_retail(tb)
            if mapped is None:
                mapped = tb
            if ta == mapped:
                continue
            return FunctionDiff(name, retail_addr, overlay_addr, len(retail_bytes), True,
                                f'+{i*4:#x}: branch {ta:#010x} vs {tb:#010x}')
        if a != b:
            return FunctionDiff(name, retail_addr, overlay_addr, len(retail_bytes), True,
                                f'+{i*4:#x}: {a:#010x} vs {b:#010x}')
    return FunctionDiff(name, retail_addr, overlay_addr, len(retail_bytes), False)
