"""Decide which functions in a recompiled retail unit differ from retail,
working on the unlinked object so relocation fields can be masked.

A word covered by a relocation is compared by *meaning* instead of bits:
  REL24 (bl/b)        -> target symbol name must equal the retail callee's
  ADDR16_HA/LO/ADDR32 -> data references are treated as equal (the overlay
                         may reference its own copy of a literal pool)
Everything else must match bit for bit, and sizes must match.
"""
from __future__ import annotations
import struct
from typing import Callable, Dict, List, Optional, Set, Tuple

from . import ppc
from .elf import Elf, relocations, function_bytes, R_PPC_REL24, R_PPC_EMB_SDA21


def changed_functions(obj: Elf, retail_read: Callable[[int, int], bytes],
                      retail_fns: Dict[str, Tuple[int, int]],
                      retail_name_at: Callable[[int], Optional[str]]) -> Dict[str, str]:
    """Returns {function name: reason} for functions that differ.
    Functions not present in retail are reported as 'new'."""
    fns = function_bytes(obj)
    relocs_by_sec: Dict[int, Dict[int, object]] = {}
    out: Dict[str, str] = {}
    for name, (sec, off, size) in fns.items():
        if name not in retail_fns:
            out[name] = 'new'
            continue
        raddr, rsize = retail_fns[name]
        if rsize != size:
            out[name] = f'size {rsize:#x} -> {size:#x}'
            continue
        if sec.index not in relocs_by_sec:
            relocs_by_sec[sec.index] = {r.offset: r for r in relocations(obj, sec)}
        rel = relocs_by_sec[sec.index]
        rbytes = retail_read(raddr, size)
        obytes = sec.data[off:off + size]
        for i in range(0, size, 4):
            ow = struct.unpack('>I', obytes[i:i + 4])[0]
            rw = struct.unpack('>I', rbytes[i:i + 4])[0]
            r = rel.get(off + i) or rel.get(off + i + 2)
            if r is None:
                if ow != rw:
                    out[name] = f'+{i:#x}: {rw:#010x} vs {ow:#010x}'
                    break
                continue
            if r.type == R_PPC_REL24:
                if (ow & 0xFC000003) != (rw & 0xFC000003) or not ppc.is_relative_branch(rw):
                    out[name] = f'+{i:#x}: branch kind'
                    break
                callee = retail_name_at(ppc.branch_target(rw, raddr + i))
                if callee != r.sym.name:
                    out[name] = f'+{i:#x}: calls {r.sym.name} (retail {callee})'
                    break
                continue
            if r.type == R_PPC_EMB_SDA21:
                # opcode + destination register; base reg and offset are relocated
                if (ow & 0xFFE00000) != (rw & 0xFFE00000):
                    out[name] = f'+{i:#x}: {rw:#010x} vs {ow:#010x} (sda)'
                    break
                continue
            # Data relocation: compare the non-relocated half of the instruction.
            if (ow & 0xFFFF0000) != (rw & 0xFFFF0000) and r.offset == off + i + 2:
                out[name] = f'+{i:#x}: {rw:#010x} vs {ow:#010x} (reloc)'
                break
    return out
