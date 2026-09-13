from __future__ import annotations
import argparse,hashlib,json,struct
from pathlib import Path
from pe_patch import PE64
from rc38_bootstrap import CALL_SITE_RVA,ORIGINAL_LOG_RVA,SECTION_NAME,SIDECAR_NAME,AUTO_EXPORT

BASE_RUNTIME_SHA256='864c0ca8f24f22f6a3cd4c21a0e213f431f5268e69b04e3860c52fe72600fc3c'
BASE_SIZE=320000;EXPECTED_NEW_SIZE=320512;EXPECTED_SECTION_RVA=0x03500000;EXPECTED_SECTION_RAW=320000
GAME_ANCHOR_RVA=0x000037FB;GAME_ANCHOR=bytes.fromhex('498b493048890dbac2c302')

def sha_bytes(b):return hashlib.sha256(b).hexdigest()
def u32(b,o):return struct.unpack_from('<I',b,o)[0]
def checksum(blob,off):
    b=bytearray(blob);struct.pack_into('<I',b,off,0);s=0;i=0
    while i+1<len(b):s+=b[i]|(b[i+1]<<8);s=(s&0xffff)+(s>>16);i+=2
    if i<len(b):s+=b[i]
    s=(s&0xffff)+(s>>16);s=(s&0xffff)+(s>>16);return (s+len(b))&0xffffffff

def validate(base:bytes,candidate:bytes):
    if sha_bytes(base)!=BASE_RUNTIME_SHA256 or len(base)!=BASE_SIZE:raise AssertionError('base runtime identity mismatch')
    if len(candidate)!=EXPECTED_NEW_SIZE:raise AssertionError('candidate size mismatch')
    bp=PE64(base);cp=PE64(candidate)
    if bp.n_sections!=9 or cp.n_sections!=10:raise AssertionError('section-count contract mismatch')
    sec=next((s for s in cp.sections if s.name==SECTION_NAME),None)
    if not sec:raise AssertionError('.rc38 section missing')
    if (sec.virtual_address,sec.raw_offset,sec.raw_size)!=(EXPECTED_SECTION_RVA,EXPECTED_SECTION_RAW,512):raise AssertionError('RC38 section geometry mismatch')
    if (sec.characteristics&0x20000000)==0 or (sec.characteristics&0x80000000):raise AssertionError('RC38 section must be executable/non-writable')
    ao=cp.rva_to_offset(GAME_ANCHOR_RVA)
    if candidate[ao:ao+len(GAME_ANCHOR)]!=GAME_ANCHOR:raise AssertionError('game HWND anchor drift')
    co=cp.rva_to_offset(CALL_SITE_RVA)
    if candidate[co]!=0xE8:raise AssertionError('RC38 hook is not CALL rel32')
    target=CALL_SITE_RVA+5+struct.unpack_from('<i',candidate,co+1)[0]
    if target!=EXPECTED_SECTION_RVA:raise AssertionError('RC38 hook target drift')
    bo=bp.rva_to_offset(CALL_SITE_RVA)
    if base[bo]!=0xE8 or CALL_SITE_RVA+5+struct.unpack_from('<i',base,bo+1)[0]!=ORIGINAL_LOG_RVA:raise AssertionError('base logger target drift')
    payload=candidate[sec.raw_offset:sec.raw_offset+sec.virtual_size]
    if (SIDECAR_NAME+'\0').encode('utf-16le') not in payload or (AUTO_EXPORT+'\0').encode('ascii') not in payload:raise AssertionError('bootstrap strings missing')
    hdr=u32(candidate,cp.checksum_off);calc=checksum(candidate,cp.checksum_off)
    if hdr!=calc:raise AssertionError('PE checksum mismatch')
    allowed=set(range(bp.file_header+2,bp.file_header+4))|set(range(bp.size_of_image_off,bp.size_of_image_off+4))|set(range(bp.checksum_off,bp.checksum_off+4))|set(range(bo,bo+5))
    nh=bp.section_table+bp.n_sections*40
    if any(base[nh:nh+40]):raise AssertionError('new section-header slot not zero in base')
    allowed|=set(range(nh,nh+40))
    diff=[i for i,(a,b) in enumerate(zip(base,candidate[:len(base)])) if a!=b]
    extra=[i for i in diff if i not in allowed]
    if extra:raise AssertionError('unexpected base-region runtime diffs: '+','.join(hex(x) for x in extra[:20]))
    if not any(i in diff for i in range(nh,nh+40)):raise AssertionError('section header unchanged')
    if candidate[bo:bo+5]==base[bo:bo+5] or candidate[bo]!=0xE8:raise AssertionError('hook bytes not redirected')
    return {'base_sha256':sha_bytes(base),'candidate_sha256':sha_bytes(candidate),'base_size':len(base),'candidate_size':len(candidate),'base_sections':bp.n_sections,'candidate_sections':cp.n_sections,'rc38_section_rva':f'0x{sec.virtual_address:08X}','rc38_section_raw':sec.raw_offset,'rc38_section_virtual_size':sec.virtual_size,'rc38_section_raw_size':sec.raw_size,'rc38_section_characteristics':f'0x{sec.characteristics:08X}','hook_rva':f'0x{CALL_SITE_RVA:08X}','hook_target_rva':f'0x{target:08X}','base_logger_target_rva':f'0x{ORIGINAL_LOG_RVA:08X}','pe_checksum':f'0x{hdr:08X}','base_region_changed_bytes':len(diff),'unexpected_base_region_changed_bytes':0,'validation':'PASS'}

def main():
    p=argparse.ArgumentParser();p.add_argument('--base',required=True);p.add_argument('--candidate',required=True);p.add_argument('--json-out');a=p.parse_args();r=validate(Path(a.base).read_bytes(),Path(a.candidate).read_bytes());t=json.dumps(r,indent=2,sort_keys=True)+'\n';print(t,end='');Path(a.json_out).write_text(t,encoding='ascii') if a.json_out else None
if __name__=='__main__':main()
