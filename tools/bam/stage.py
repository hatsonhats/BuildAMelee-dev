"""Staged overlay loading.

The GameCube apploader refuses DOL sections above 0x81200000 (0x80700000 on
production consoles), but the overlay must run at the top of MEM1, the only
fixed place no retail or Slippi structure uses. So the DOL carries the
overlay image in a *staging* section low in the arena (at __ArenaLo, which
nothing touches before OSInit's ClearArena), preceded by a tiny stub. The
ClearArena entry branches to the stub, which copies the image to its run
address, flushes caches, clears its magic word (so a hot reset, which reruns
ClearArena after the staging area has been reused, does not copy garbage),
and tail-branches into the overlay's ClearArena replacement.
"""
from __future__ import annotations
import struct
from typing import Tuple

from . import ppc

MAGIC = 0x42414D31   # 'BAM1'
FRAME = 0x20


def build_stub(stage: int, run_base: int, image: bytes, target: int,
               memcpy: int, dcflush: int, icinval: int) -> Tuple[bytes, int]:
    """Returns (section bytes, entry address). Layout at `stage`:
    stub code | magic word | pad to 32 | image."""
    words = []
    pc = lambda: stage + len(words) * 4  # noqa: E731
    # The stub is a fixed 36 instructions; compute addresses up front.
    n_code = 36
    magic_addr = stage + n_code * 4
    src = (magic_addr + 4 + 31) & ~31
    length = (len(image) + 31) & ~31
    e = words.append
    e(ppc.stwu(1, -FRAME, 1))
    e(ppc.mflr(0))
    e(ppc.stw(0, FRAME + 4, 1))
    # if (*magic != MAGIC) skip
    for w in ppc.load_imm32(6, magic_addr): e(w)
    e(ppc.lwz(7, 0, 6))
    for w in ppc.load_imm32(8, MAGIC): e(w)
    e(0x7C074000)                     # cmpw cr0, r7, r8
    skip_at = len(words); e(0)        # bne skip (patched below)
    for w in ppc.load_imm32(3, run_base): e(w)
    for w in ppc.load_imm32(4, src): e(w)
    for w in ppc.load_imm32(5, length): e(w)
    e(ppc.bl(pc(), memcpy))
    for w in ppc.load_imm32(3, run_base): e(w)
    for w in ppc.load_imm32(4, length): e(w)
    e(ppc.bl(pc(), dcflush))
    for w in ppc.load_imm32(3, run_base): e(w)
    for w in ppc.load_imm32(4, length): e(w)
    e(ppc.bl(pc(), icinval))
    for w in ppc.load_imm32(6, magic_addr): e(w)
    e(ppc.li(0, 0))
    e(ppc.stw(0, 0, 6))
    skip = pc()
    words[skip_at] = ppc.bne(stage + skip_at * 4, skip)
    e(ppc.lwz(0, FRAME + 4, 1))
    e(ppc.mtlr(0))
    e(ppc.addi(1, 1, FRAME))
    e(ppc.b(pc(), target))
    if len(words) > n_code:
        raise AssertionError(f'stub grew to {len(words)} instructions')
    while len(words) < n_code:
        words.append(ppc.NOP)
    out = bytearray(b''.join(struct.pack('>I', w) for w in words))
    out += struct.pack('>I', MAGIC)
    out += b'\0' * (src - stage - len(out))
    out += image + b'\0' * (length - len(image))
    return bytes(out), stage
