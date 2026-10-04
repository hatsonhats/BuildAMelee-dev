"""Hook manifest -> DOL word patches + trampoline code.

Hook kinds
----------
override   `b overlay_fn` at the entry of a retail function. The overlay
           function must have the same signature. Used for whole-function
           replacements (recompiled retail TUs).
call       The retail instruction at `at` is a `bl retail_fn`; it is rewritten
           to `bl overlay_fn` (same signature). No trampoline, zero overhead.
inject     Gecko-C2 style. The instruction at `at` is replaced by a branch to
           a generated trampoline that saves every volatile register (r0,
           r3-r12 via stmw, LR, CR, CTR, XER, f0-f13), calls a C function with
           selected registers as arguments, optionally writes its return
           value back to a register, restores everything, executes the
           displaced instruction and branches back to `at + 4`.
word       Raw 32-bit write (`04` code equivalent).

Addresses may be written as `0x8006xxxx` or `Symbol+0x10`.
"""
from __future__ import annotations
import re
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Sequence

from . import ppc

FRAME = 0x120
OFF_R0 = 0x08
OFF_GPR = 0x0C          # r3..r31 via stmw (29 regs, 0x74 bytes) -> 0x0C..0x80
OFF_LR = 0x84
OFF_CR = 0x88
OFF_CTR = 0x8C
OFF_XER = 0x90
OFF_FPR = 0x98          # f0..f13, 8 bytes each -> 0x98..0x108

REG_RE = re.compile(r'^r(\d+)$')


@dataclass
class Hook:
    id: str
    kind: str
    at: str                      # symbolic or hex address
    target: str = ''             # overlay symbol (override/call/inject)
    args: List[str] = field(default_factory=list)
    ret: Optional[str] = None
    value: Optional[int] = None  # word kind
    note: str = ''


@dataclass
class Patch:
    addr: int
    value: int
    hook: str
    original: Optional[int] = None


def gpr_slot(reg: int) -> int:
    if reg == 0:
        return OFF_R0
    if 3 <= reg <= 31:
        return OFF_GPR + (reg - 3) * 4
    raise ValueError(f'r{reg} is not saved by the trampoline (r1/r2 are never touched)')


def resolve_address(expr: str, lookup) -> int:
    """`0x8006ABCD` or `Name` or `Name+0x10` / `Name-0x4`."""
    expr = expr.strip()
    if re.fullmatch(r'0x[0-9A-Fa-f]+', expr):
        return int(expr, 16)
    m = re.fullmatch(r'([A-Za-z_@$][\w@$.]*)\s*([+-]\s*0x[0-9A-Fa-f]+|[+-]\s*\d+)?', expr)
    if not m:
        raise ValueError(f'bad address expression: {expr!r}')
    base = lookup(m.group(1))
    if base is None:
        raise ValueError(f'unknown symbol in address expression: {m.group(1)}')
    off = 0
    if m.group(2):
        off = int(m.group(2).replace(' ', ''), 0)
    return base + off


class Trampolines:
    """Accumulates trampoline machine code placed at a fixed base address."""

    def __init__(self, base: int):
        self.base = base
        self.words: List[int] = []

    @property
    def cursor(self) -> int:
        return self.base + len(self.words) * 4

    @property
    def size(self) -> int:
        return len(self.words) * 4

    def emit(self, w: int) -> None:
        self.words.append(w & 0xFFFFFFFF)

    def to_bytes(self) -> bytes:
        import struct
        return b''.join(struct.pack('>I', w) for w in self.words)

    def _shared(self) -> None:
        """Emit the save and restore bodies every trampoline calls (once)."""
        if hasattr(self, 'save_at'):
            return
        e = self.emit
        # save: r3-r31, CR, CTR, XER, f0-f13 into the caller's frame. r0 and
        # LR are already saved by the trampoline; r0 is scratch here.
        self.save_at = self.cursor
        e(ppc.stmw(3, OFF_GPR, 1))
        e(ppc.mfcr(0)); e(ppc.stw(0, OFF_CR, 1))
        e(ppc.mfctr(0)); e(ppc.stw(0, OFF_CTR, 1))
        e(ppc.mfxer(0)); e(ppc.stw(0, OFF_XER, 1))
        for i in range(14):
            e(ppc.stfd(i, OFF_FPR + i * 8, 1))
        e(ppc.BLR)
        # restore: the same registers back (the trampoline restores LR, r0).
        self.restore_at = self.cursor
        for i in range(14):
            e(ppc.lfd(i, OFF_FPR + i * 8, 1))
        e(ppc.lwz(0, OFF_XER, 1)); e(ppc.mtxer(0))
        e(ppc.lwz(0, OFF_CTR, 1)); e(ppc.mtctr(0))
        e(ppc.lwz(0, OFF_CR, 1)); e(ppc.mtcr(0))
        e(ppc.lmw(3, OFF_GPR, 1))
        e(ppc.BLR)

    def inject(self, site: int, original: int, fn: int, args: Sequence[str], ret: Optional[str]) -> int:
        """Emit a trampoline; returns its entry address. Saving and restoring
        the registers is shared (`_shared`), so each hook costs ~20 words."""
        if not ppc.is_displaceable(original):
            raise ValueError(f'instruction at {site:#010x} ({original:#010x}) cannot be displaced; '
                             'move the hook one instruction up or down')
        self._shared()
        entry = self.cursor
        e = self.emit
        e(ppc.stwu(1, -FRAME, 1))
        e(ppc.stw(0, OFF_R0, 1))
        e(ppc.mflr(0)); e(ppc.stw(0, OFF_LR, 1))
        e(ppc.bl(self.cursor, self.save_at))
        # Arguments: r3.. <- selected saved registers.
        if len(args) > 8:
            raise ValueError('at most 8 register arguments')
        for i, a in enumerate(args):
            dst = 3 + i
            if a == 'regs':
                e(ppc.addi(dst, 1, OFF_R0))
            elif a == 'site':
                for w in ppc.load_imm32(dst, site):
                    e(w)
            elif a in ('r1', 'sp'):
                e(ppc.addi(dst, 1, FRAME))
            else:
                m = REG_RE.match(a)
                if not m:
                    raise ValueError(f'bad hook argument {a!r}')
                e(ppc.lwz(dst, gpr_slot(int(m.group(1))), 1))
        for w in ppc.load_imm32(12, fn):
            e(w)
        e(ppc.mtctr(12))
        e(ppc.BCTRL)
        if ret:
            m = REG_RE.match(ret)
            if not m:
                raise ValueError(f'bad ret register {ret!r}')
            e(ppc.stw(3, gpr_slot(int(m.group(1))), 1))
        e(ppc.bl(self.cursor, self.restore_at))
        e(ppc.lwz(0, OFF_LR, 1)); e(ppc.mtlr(0))
        e(ppc.lwz(0, OFF_R0, 1))
        e(ppc.addi(1, 1, FRAME))
        # Displaced original instruction.
        if ppc.is_relative_branch(original):
            e(ppc.rebase_relative_branch(original, site, self.cursor))
        else:
            e(original)
        e(ppc.b(self.cursor, site + 4))
        return entry


def build_patches(hooks: Sequence[Hook], read_word, retail_lookup, overlay_lookup,
                  tramp: Trampolines) -> List[Patch]:
    """Turn hooks into word patches. `read_word(addr)` reads the retail DOL,
    `retail_lookup(name)`/`overlay_lookup(name)` map symbols to addresses."""
    patches: List[Patch] = []
    seen: Dict[int, str] = {}

    def lookup(name: str):
        v = overlay_lookup(name)
        return v if v is not None else retail_lookup(name)

    for h in hooks:
        at = resolve_address(h.at, lookup)
        if at % 4:
            raise ValueError(f'{h.id}: unaligned address {at:#x}')
        if at in seen:
            raise ValueError(f'{h.id}: address {at:#010x} already patched by {seen[at]}')
        seen[at] = h.id
        original = read_word(at)
        if h.kind == 'word':
            if h.value is None:
                raise ValueError(f'{h.id}: word hook needs value')
            patches.append(Patch(at, h.value, h.id, original))
        elif h.kind == 'override':
            fn = overlay_lookup(h.target)
            if fn is None:
                raise ValueError(f'{h.id}: overlay symbol {h.target} not found')
            patches.append(Patch(at, ppc.b(at, fn), h.id, original))
        elif h.kind == 'call':
            fn = overlay_lookup(h.target)
            if fn is None:
                raise ValueError(f'{h.id}: overlay symbol {h.target} not found')
            if not ppc.is_relative_branch(original) or not (original & 1):
                raise ValueError(f'{h.id}: instruction at {at:#010x} is not a bl')
            patches.append(Patch(at, ppc.bl(at, fn), h.id, original))
        elif h.kind == 'inject':
            fn = overlay_lookup(h.target)
            if fn is None:
                raise ValueError(f'{h.id}: overlay symbol {h.target} not found')
            entry = tramp.inject(at, original, fn, h.args, h.ret)
            patches.append(Patch(at, ppc.b(at, entry), h.id, original))
        else:
            raise ValueError(f'{h.id}: unknown hook kind {h.kind}')
    return patches
