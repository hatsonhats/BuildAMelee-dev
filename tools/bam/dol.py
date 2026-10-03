"""GameCube DOL executable reader/writer with section injection and word patching."""
from __future__ import annotations
import struct
from dataclasses import dataclass, field
from pathlib import Path
from typing import List, Optional

HEADER_SIZE = 0x100
TEXT_SLOTS = 7
DATA_SLOTS = 11
ALIGN = 0x20


@dataclass
class Section:
    index: int          # 0..6 text, 7..17 data
    address: int
    data: bytearray

    @property
    def is_text(self) -> bool:
        return self.index < TEXT_SLOTS

    @property
    def end(self) -> int:
        return self.address + len(self.data)

    def contains(self, addr: int, size: int = 4) -> bool:
        return self.address <= addr and addr + size <= self.end


@dataclass
class Dol:
    sections: List[Section] = field(default_factory=list)
    bss_address: int = 0
    bss_size: int = 0
    entry: int = 0

    @classmethod
    def load(cls, path: Path) -> 'Dol':
        raw = Path(path).read_bytes()
        offs = struct.unpack('>18I', raw[0:72])
        addrs = struct.unpack('>18I', raw[72:144])
        sizes = struct.unpack('>18I', raw[144:216])
        bss_addr, bss_size, entry = struct.unpack('>3I', raw[216:228])
        dol = cls(bss_address=bss_addr, bss_size=bss_size, entry=entry)
        for i in range(18):
            if sizes[i]:
                dol.sections.append(Section(i, addrs[i], bytearray(raw[offs[i]:offs[i] + sizes[i]])))
        return dol

    def section_for(self, addr: int, size: int = 4) -> Optional[Section]:
        for s in self.sections:
            if s.contains(addr, size):
                return s
        return None

    def read_u32(self, addr: int) -> int:
        s = self.section_for(addr)
        if s is None:
            raise ValueError(f'address {addr:#010x} not in any DOL section')
        o = addr - s.address
        return struct.unpack('>I', s.data[o:o + 4])[0]

    def read_bytes(self, addr: int, size: int) -> bytes:
        s = self.section_for(addr, size)
        if s is None:
            raise ValueError(f'range {addr:#010x}+{size:#x} not in any DOL section')
        o = addr - s.address
        return bytes(s.data[o:o + size])

    def write_u32(self, addr: int, value: int) -> int:
        """Patch one word; returns the previous value."""
        s = self.section_for(addr)
        if s is None:
            raise ValueError(f'address {addr:#010x} not in any DOL section')
        o = addr - s.address
        old = struct.unpack('>I', s.data[o:o + 4])[0]
        s.data[o:o + 4] = struct.pack('>I', value & 0xFFFFFFFF)
        return old

    def free_slot(self, text: bool) -> int:
        used = {s.index for s in self.sections}
        rng = range(0, TEXT_SLOTS) if text else range(TEXT_SLOTS, TEXT_SLOTS + DATA_SLOTS)
        for i in rng:
            if i not in used:
                return i
        raise ValueError('no free DOL section slot')

    def add_section(self, address: int, data: bytes, text: bool = True) -> Section:
        if address % ALIGN:
            raise ValueError('section address must be 32-byte aligned')
        for s in self.sections:
            if not (address + len(data) <= s.address or s.end <= address):
                raise ValueError(f'new section overlaps section {s.index}')
        if not (address + len(data) <= self.bss_address or self.bss_address + self.bss_size <= address):
            raise ValueError('new section overlaps BSS')
        padded = bytearray(data)
        while len(padded) % ALIGN:
            padded.append(0)
        sec = Section(self.free_slot(text), address, padded)
        self.sections.append(sec)
        self.sections.sort(key=lambda s: s.index)
        return sec

    def to_bytes(self) -> bytes:
        offs = [0] * 18
        addrs = [0] * 18
        sizes = [0] * 18
        body = bytearray()
        cursor = HEADER_SIZE
        for s in self.sections:
            while cursor % ALIGN:
                body.append(0)
                cursor += 1
            offs[s.index] = cursor
            addrs[s.index] = s.address
            sizes[s.index] = len(s.data)
            body += s.data
            cursor += len(s.data)
        header = struct.pack('>18I', *offs) + struct.pack('>18I', *addrs) + struct.pack('>18I', *sizes)
        header += struct.pack('>3I', self.bss_address, self.bss_size, self.entry)
        header = header.ljust(HEADER_SIZE, b'\0')
        return bytes(header + body)

    def save(self, path: Path) -> None:
        Path(path).write_bytes(self.to_bytes())

    def describe(self) -> str:
        lines = []
        for s in self.sections:
            kind = 'text' if s.is_text else 'data'
            lines.append(f'{kind}{s.index:<3} {s.address:#010x}-{s.end:#010x} ({len(s.data):#x})')
        lines.append(f'bss      {self.bss_address:#010x}-{self.bss_address + self.bss_size:#010x} ({self.bss_size:#x})')
        lines.append(f'entry    {self.entry:#010x}')
        return '\n'.join(lines)
