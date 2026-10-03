"""Minimal PowerPC (Gekko) instruction encoders and decoders.

Only what the hook generator needs: enough to build register-preserving
trampolines, rewrite relative branches, and recognise instructions that
cannot be displaced from a hook site.
"""
from __future__ import annotations

MASK32 = 0xFFFFFFFF


def _s16(v: int) -> int:
    if not -0x8000 <= v <= 0xFFFF:
        raise ValueError(f'immediate out of range: {v:#x}')
    return v & 0xFFFF


def _d(op: int, rt: int, ra: int, d: int) -> int:
    return ((op << 26) | (rt << 21) | (ra << 16) | _s16(d)) & MASK32


def b(src: int, dst: int, link: bool = False) -> int:
    """Relative branch from src to dst (`b`/`bl`)."""
    off = dst - src
    if off % 4 or not -0x2000000 <= off < 0x2000000:
        raise ValueError(f'branch out of range: {src:#x} -> {dst:#x}')
    return ((18 << 26) | (off & 0x03FFFFFC) | (1 if link else 0)) & MASK32


def bl(src: int, dst: int) -> int:
    return b(src, dst, True)


def lis(rt: int, imm: int) -> int:        # addis rt, 0, imm
    return _d(15, rt, 0, imm)


def ori(ra: int, rs: int, imm: int) -> int:
    return ((24 << 26) | (rs << 21) | (ra << 16) | (imm & 0xFFFF)) & MASK32


def addi(rt: int, ra: int, imm: int) -> int:
    return _d(14, rt, ra, imm)


def lwz(rt: int, d: int, ra: int) -> int:
    return _d(32, rt, ra, d)


def stw(rs: int, d: int, ra: int) -> int:
    return _d(36, rs, ra, d)


def stwu(rs: int, d: int, ra: int) -> int:
    return _d(37, rs, ra, d)


def lmw(rt: int, d: int, ra: int) -> int:
    return _d(46, rt, ra, d)


def stmw(rs: int, d: int, ra: int) -> int:
    return _d(47, rs, ra, d)


def lfd(frt: int, d: int, ra: int) -> int:
    return _d(50, frt, ra, d)


def stfd(frs: int, d: int, ra: int) -> int:
    return _d(54, frs, ra, d)


def _spr(rt: int, spr: int, xo: int) -> int:
    enc = ((spr & 0x1F) << 5) | (spr >> 5)
    return ((31 << 26) | (rt << 21) | (enc << 11) | (xo << 1)) & MASK32


def mfspr(rt: int, spr: int) -> int:
    return _spr(rt, spr, 339)


def mtspr(spr: int, rs: int) -> int:
    return _spr(rs, spr, 467)


def mflr(rt: int) -> int:
    return mfspr(rt, 8)


def mtlr(rs: int) -> int:
    return mtspr(8, rs)


def mfctr(rt: int) -> int:
    return mfspr(rt, 9)


def mtctr(rs: int) -> int:
    return mtspr(9, rs)


def mfxer(rt: int) -> int:
    return mfspr(rt, 1)


def mtxer(rs: int) -> int:
    return mtspr(1, rs)


def mfcr(rt: int) -> int:
    return ((31 << 26) | (rt << 21) | (19 << 1)) & MASK32


def mtcr(rs: int) -> int:                 # mtcrf 0xFF, rs
    return ((31 << 26) | (rs << 21) | (0xFF << 12) | (144 << 1)) & MASK32


BCTRL = (19 << 26) | (20 << 21) | (528 << 1) | 1
BCTR = (19 << 26) | (20 << 21) | (528 << 1)
BLR = 0x4E800020
NOP = 0x60000000


def load_imm32(rt: int, value: int) -> list[int]:
    value &= MASK32
    hi = (value >> 16) & 0xFFFF
    lo = value & 0xFFFF
    return [lis(rt, hi), ori(rt, rt, lo)]


# ---- decoding helpers -------------------------------------------------------

def opcode(word: int) -> int:
    return (word >> 26) & 0x3F


def is_relative_branch(word: int) -> bool:
    """`b`/`bl` (opcode 18) with AA=0."""
    return opcode(word) == 18 and not (word & 2)


def is_conditional_branch(word: int) -> bool:
    return opcode(word) == 16


def is_indirect_branch(word: int) -> bool:
    if opcode(word) != 19:
        return False
    xo = (word >> 1) & 0x3FF
    return xo in (16, 528)   # bclr, bcctr


def branch_target(word: int, addr: int) -> int:
    if not is_relative_branch(word):
        raise ValueError('not a relative branch')
    li = word & 0x03FFFFFC
    if li & 0x02000000:
        li -= 0x04000000
    return (addr + li) & MASK32


def rebase_relative_branch(word: int, old_addr: int, new_addr: int) -> int:
    """Re-encode a displaced `b`/`bl` so it still reaches its original target."""
    target = branch_target(word, old_addr)
    return b(new_addr, target, bool(word & 1))


def is_displaceable(word: int) -> bool:
    """True if the instruction can be moved into a trampoline unchanged
    (or, for b/bl, rebased). Conditional/indirect branches and anything
    referring to the PC are refused."""
    if is_conditional_branch(word) or is_indirect_branch(word):
        return False
    if opcode(word) == 18 and (word & 2):     # absolute branch, fine but odd
        return True
    return True


def cmpwi(ra: int, imm: int, crf: int = 0) -> int:
    return ((11 << 26) | (crf << 23) | (ra << 16) | _s16(imm)) & MASK32


def bc(src: int, dst: int, bo: int, bi: int) -> int:
    off = dst - src
    if off % 4 or not -0x8000 <= off < 0x8000:
        raise ValueError('conditional branch out of range')
    return ((16 << 26) | (bo << 21) | (bi << 16) | (off & 0xFFFC)) & MASK32


def bne(src: int, dst: int) -> int:     # bne cr0
    return bc(src, dst, 4, 2)


def li(rt: int, imm: int) -> int:
    return addi(rt, 0, imm)
