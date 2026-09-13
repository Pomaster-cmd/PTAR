from __future__ import annotations
import struct
from pe_patch import PE64

CALL_SITE_RVA=0x0000C2FE
ORIGINAL_LOG_RVA=0x000033B0
IAT_GETMODULEHANDLEEXW=0x00049EE8
IAT_GETPROCADDRESS=0x00049F00
IAT_LOADLIBRARYW=0x00049F10
SECTION_NAME='.rc38'
SIDECAR_NAME='ptar_borderless.dll'
AUTO_EXPORT='PTAR_BorderlessAutoStart'

class Emitter:
    def __init__(self, base_rva:int):
        self.base=base_rva; self.b=bytearray(); self.labels={}; self.fixups=[]
    def emit(self,x:bytes): self.b.extend(x)
    def label(self,n): self.labels[n]=len(self.b)
    def rel32(self,prefix:bytes,target:int|str):
        pos=len(self.b);self.b.extend(prefix+b'\0\0\0\0');self.fixups.append((pos+len(prefix),pos+len(prefix)+4,target))
    def call_rel(self,target):self.rel32(b'\xE8',target)
    def call_iat(self,target):
        pos=len(self.b);self.b.extend(b'\xFF\x15\0\0\0\0');n=self.base+pos+6;struct.pack_into('<i',self.b,pos+2,target-n)
    def lea_rip(self,op:bytes,target:int|str):self.rel32(op,target)
    def jcc(self,cc:int,label:str):
        pos=len(self.b);self.b.extend(bytes([0x0F,cc])+b'\0\0\0\0');self.fixups.append((pos+2,pos+6,label))
    def finish(self):
        for off,nxt,target in self.fixups:
            t=self.base+self.labels[target] if isinstance(target,str) else target
            struct.pack_into('<i',self.b,off,t-(self.base+nxt))
        return bytes(self.b)

def build_section(section_rva:int):
    e=Emitter(section_rva)
    e.label('entry');e.emit(b'\x48\x83\xEC\x48');e.call_rel(ORIGINAL_LOG_RVA)
    e.emit(b'\x48\xC7\x44\x24\x30\0\0\0\0');e.emit(b'\x48\xC7\x44\x24\x38\0\0\0\0')
    e.emit(b'\xB9\x02\0\0\0');e.lea_rip(b'\x48\x8D\x15','sidecar_w');e.emit(b'\x4C\x8D\x44\x24\x30');e.call_iat(IAT_GETMODULEHANDLEEXW)
    e.emit(b'\x85\xC0');e.jcc(0x85,'have_sidecar')
    e.lea_rip(b'\x48\x8D\x0D','sidecar_w');e.call_iat(IAT_LOADLIBRARYW);e.emit(b'\x48\x85\xC0');e.jcc(0x84,'done');e.emit(b'\x48\x89\x44\x24\x30')
    e.label('have_sidecar');e.emit(b'\xB9\x06\0\0\0');e.lea_rip(b'\x48\x8D\x15','entry');e.emit(b'\x4C\x8D\x44\x24\x38');e.call_iat(IAT_GETMODULEHANDLEEXW)
    e.emit(b'\x85\xC0');e.jcc(0x84,'done');e.emit(b'\x48\x8B\x4C\x24\x30');e.lea_rip(b'\x48\x8D\x15','export_a');e.call_iat(IAT_GETPROCADDRESS)
    e.emit(b'\x48\x85\xC0');e.jcc(0x84,'done');e.emit(b'\x48\x8B\x4C\x24\x38');e.emit(b'\xFF\xD0')
    e.label('done');e.emit(b'\x48\x83\xC4\x48\xC3')
    while len(e.b)%8:e.emit(b'\xCC')
    e.label('sidecar_w');e.emit((SIDECAR_NAME+'\0').encode('utf-16le'));e.label('export_a');e.emit((AUTO_EXPORT+'\0').encode('ascii'))
    payload=e.finish();return payload,{k:section_rva+v for k,v in e.labels.items()}

def patch_runtime(blob:bytes):
    pe=PE64(blob)
    if len(blob)!=320000:raise ValueError(f'unexpected canonical runtime size {len(blob)}')
    pe.write_rva(CALL_SITE_RVA,b'',expected=bytes.fromhex('e8ad70ffff'))
    o=pe.rva_to_offset(0x37FB)
    if bytes(pe.data[o:o+11])!=bytes.fromhex('498b493048890dbac2c302'):raise ValueError('game HWND capture anchor mismatch')
    last=max(pe.sections,key=lambda s:s.virtual_address)
    section_rva=PE64.align(last.virtual_address+max(last.virtual_size,last.raw_size),pe.section_alignment)
    payload,meta=build_section(section_rva);sec=pe.add_section(SECTION_NAME,payload,0x60000020)
    if sec.virtual_address!=section_rva:raise ValueError('section RVA drift')
    pe.write_rva(CALL_SITE_RVA,b'\xE8'+struct.pack('<i',section_rva-(CALL_SITE_RVA+5)),expected=bytes.fromhex('e8ad70ffff'))
    checksum=pe.recompute_checksum()
    return bytes(pe.data),{'section_rva':section_rva,'section_raw':sec.raw_offset,'section_virtual_size':len(payload),'section_raw_size':sec.raw_size,'bootstrap_rva':meta['entry'],'call_site_rva':CALL_SITE_RVA,'checksum':checksum,'size':len(pe.data),'labels':meta}
