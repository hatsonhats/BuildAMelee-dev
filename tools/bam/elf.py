"""Tiny big-endian ELF32 reader (sections + symbols) for MWLD output and MWCC objects."""
from __future__ import annotations
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional

SHT_NOBITS = 8
SHT_SYMTAB = 2
SHF_ALLOC = 0x2

STB_LOCAL, STB_GLOBAL, STB_WEAK = 0, 1, 2
STT_NOTYPE, STT_OBJECT, STT_FUNC, STT_SECTION, STT_FILE = 0, 1, 2, 3, 4


@dataclass
class ElfSection:
    index: int
    name: str
    type: int
    flags: int
    addr: int
    offset: int
    size: int
    link: int
    info: int
    data: bytes

    @property
    def alloc(self) -> bool:
        return bool(self.flags & SHF_ALLOC)

    @property
    def nobits(self) -> bool:
        return self.type == SHT_NOBITS


@dataclass
class ElfSymbol:
    name: str
    value: int
    size: int
    bind: int
    type: int
    shndx: int

    @property
    def defined(self) -> bool:
        return self.shndx != 0


class Elf:
    def __init__(self, path: Path):
        raw = Path(path).read_bytes()
        if raw[:4] != b'\x7fELF' or raw[5] != 2:
            raise ValueError(f'{path}: not a big-endian ELF')
        self.raw = raw
        (self.e_type, self.e_machine, _v, self.entry, _phoff, shoff, _flags,
         _ehsize, _phentsize, _phnum, shentsize, shnum, shstrndx) = struct.unpack('>HHIIIIIHHHHHH', raw[16:52])
        secs: List[ElfSection] = []
        for i in range(shnum):
            sh = raw[shoff + i * shentsize: shoff + (i + 1) * shentsize]
            name, typ, flags, addr, off, size, link, info, _align, _entsize = struct.unpack('>10I', sh)
            data = b'' if typ == SHT_NOBITS else raw[off:off + size]
            secs.append(ElfSection(i, '', typ, flags, addr, off, size, link, info, data))
        strtab = secs[shstrndx].data
        for s in secs:
            s.name = self._cstr(strtab, struct.unpack('>I', raw[shoff + s.index * shentsize: shoff + s.index * shentsize + 4])[0])
        self.sections = secs
        self.by_name: Dict[str, ElfSection] = {s.name: s for s in secs if s.name}

    @staticmethod
    def _cstr(tab: bytes, off: int) -> str:
        end = tab.index(b'\0', off)
        return tab[off:end].decode('latin-1')

    def symbols(self) -> List[ElfSymbol]:
        out: List[ElfSymbol] = []
        for s in self.sections:
            if s.type != SHT_SYMTAB:
                continue
            strtab = self.sections[s.link].data
            for i in range(0, s.size, 16):
                name, value, size, info, _other, shndx = struct.unpack('>IIIBBH', s.data[i:i + 16])
                out.append(ElfSymbol(self._cstr(strtab, name), value, size, info >> 4, info & 0xF, shndx))
        return out

    def section(self, name: str) -> Optional[ElfSection]:
        return self.by_name.get(name)

    def image(self, base: int) -> bytes:
        """Flatten all allocated sections (including NOBITS as zeros) into a
        contiguous image starting at `base`. Gaps are zero filled."""
        allocs = [s for s in self.sections if s.alloc and s.size]
        if not allocs:
            return b''
        end = max(s.addr + s.size for s in allocs)
        buf = bytearray(end - base)
        for s in allocs:
            if s.addr < base:
                raise ValueError(f'section {s.name} below image base')
            if not s.nobits:
                buf[s.addr - base:s.addr - base + s.size] = s.data
        return bytes(buf)


SHT_RELA = 4
R_PPC_ADDR32, R_PPC_ADDR16_LO, R_PPC_ADDR16_HI, R_PPC_ADDR16_HA, R_PPC_REL24 = 1, 4, 5, 6, 10
R_PPC_EMB_SDA21 = 109


@dataclass
class Reloc:
    offset: int
    type: int
    sym: ElfSymbol
    addend: int


def relocations(elf: 'Elf', section: ElfSection):
    """RELA entries that apply to `section` (MWCC emits .rela.<name>)."""
    syms = elf.symbols()
    out = []
    for s in elf.sections:
        if s.type != SHT_RELA or s.info != section.index:
            continue
        for i in range(0, s.size, 12):
            off, info, add = struct.unpack('>IIi', s.data[i:i + 12])
            out.append(Reloc(off, info & 0xFF, syms[info >> 8], add))
    return out


def function_bytes(elf: 'Elf'):
    """{name: (section, offset, size)} for defined function symbols."""
    out = {}
    for s in elf.symbols():
        if s.type == STT_FUNC and s.defined and s.size and s.shndx < len(elf.sections):
            out[s.name] = (elf.sections[s.shndx], s.value, s.size)
    return out
