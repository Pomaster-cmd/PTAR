from __future__ import annotations
import struct
from dataclasses import dataclass

@dataclass(frozen=True)
class Section:
    name: str
    virtual_size: int
    virtual_address: int
    raw_size: int
    raw_offset: int
    characteristics: int
    header_offset: int

class PE64:
    def __init__(self, blob: bytes | bytearray):
        self.data = bytearray(blob)
        if self.data[:2] != b'MZ':
            raise ValueError('not MZ')
        self.pe = struct.unpack_from('<I', self.data, 0x3C)[0]
        if self.data[self.pe:self.pe+4] != b'PE\0\0':
            raise ValueError('not PE')
        self.file_header = self.pe + 4
        self.machine, self.n_sections = struct.unpack_from('<HH', self.data, self.file_header)
        self.opt_size = struct.unpack_from('<H', self.data, self.file_header + 16)[0]
        self.optional = self.file_header + 20
        self.magic = struct.unpack_from('<H', self.data, self.optional)[0]
        if self.machine != 0x8664 or self.magic != 0x20B:
            raise ValueError('expected PE32+ AMD64')
        self.section_alignment = struct.unpack_from('<I', self.data, self.optional + 32)[0]
        self.file_alignment = struct.unpack_from('<I', self.data, self.optional + 36)[0]
        self.size_of_image_off = self.optional + 56
        self.size_of_headers = struct.unpack_from('<I', self.data, self.optional + 60)[0]
        self.checksum_off = self.optional + 64
        self.section_table = self.optional + self.opt_size
        self.sections = self._sections()

    @staticmethod
    def align(v: int, a: int) -> int:
        return (v + a - 1) // a * a

    def _sections(self):
        out=[]
        for i in range(self.n_sections):
            o=self.section_table+i*40
            name=self.data[o:o+8].split(b'\0',1)[0].decode('ascii')
            vs,va,rs,ro=struct.unpack_from('<IIII',self.data,o+8)
            ch=struct.unpack_from('<I',self.data,o+36)[0]
            out.append(Section(name,vs,va,rs,ro,ch,o))
        return out

    def rva_to_offset(self, rva: int) -> int:
        for s in self.sections:
            if s.virtual_address <= rva < s.virtual_address + max(s.virtual_size, s.raw_size):
                return s.raw_offset + (rva - s.virtual_address)
        if rva < self.size_of_headers:
            return rva
        raise ValueError(f'RVA not mapped: 0x{rva:X}')

    def add_section(self, name: str, payload: bytes, characteristics: int = 0x60000020) -> Section:
        if len(name.encode('ascii')) > 8:
            raise ValueError('section name too long')
        if any(s.name == name for s in self.sections):
            raise ValueError('section already exists')
        new_hdr=self.section_table+self.n_sections*40
        if new_hdr+40 > self.size_of_headers:
            raise ValueError('no room for another section header')
        last=max(self.sections,key=lambda s:s.virtual_address)
        va=self.align(last.virtual_address + max(last.virtual_size,last.raw_size), self.section_alignment)
        raw=self.align(max(s.raw_offset+s.raw_size for s in self.sections), self.file_alignment)
        raw_size=self.align(len(payload), self.file_alignment)
        if len(self.data) < raw:
            self.data.extend(b'\0'*(raw-len(self.data)))
        self.data.extend(payload)
        if len(payload) < raw_size:
            self.data.extend(b'\0'*(raw_size-len(payload)))
        hdr=bytearray(40)
        hdr[:8]=name.encode('ascii').ljust(8,b'\0')
        struct.pack_into('<IIII',hdr,8,len(payload),va,raw_size,raw)
        struct.pack_into('<I',hdr,36,characteristics)
        self.data[new_hdr:new_hdr+40]=hdr
        self.n_sections+=1
        struct.pack_into('<H',self.data,self.file_header+2,self.n_sections)
        struct.pack_into('<I',self.data,self.size_of_image_off,self.align(va+len(payload),self.section_alignment))
        self.sections=self._sections()
        return next(s for s in self.sections if s.name==name)

    def write_rva(self, rva: int, payload: bytes, expected: bytes | None = None):
        o=self.rva_to_offset(rva)
        if expected is not None and bytes(self.data[o:o+len(expected)]) != expected:
            raise ValueError(f'anchor mismatch at RVA 0x{rva:X}: {self.data[o:o+len(expected)].hex()} != {expected.hex()}')
        self.data[o:o+len(payload)] = payload

    def recompute_checksum(self) -> int:
        struct.pack_into('<I',self.data,self.checksum_off,0)
        s=0
        i=0
        n=len(self.data)
        while i+1<n:
            s += self.data[i] | (self.data[i+1]<<8)
            s=(s&0xffff)+(s>>16)
            i+=2
        if i<n:
            s+=self.data[i]
        s=(s&0xffff)+(s>>16)
        s=(s&0xffff)+(s>>16)
        s=(s+n)&0xffffffff
        struct.pack_into('<I',self.data,self.checksum_off,s)
        return s
