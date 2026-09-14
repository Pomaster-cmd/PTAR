from pathlib import Path
import hashlib, struct, sys

BASE_SHA='6f1686992bef971c9995df9f0cb91b93c6946988e12a837e5de956df37c9082b'
BASE_SIZE=336384
HOOK_RVA=0x44EE
LOADER_RVA=0x035006C0
RC41_OLD_LOADER_RVA=0x03500600
EXPECTED_OLD_HOOK=bytes.fromhex('e80dc14f03')

def sha256(b): return hashlib.sha256(b).hexdigest()

def pe_checksum(buf, chkoff):
    tmp=bytearray(buf)
    tmp[chkoff:chkoff+4]=b'\0'*4
    s=0
    for i in range(0,len(tmp)-1,2):
        s += tmp[i] | (tmp[i+1]<<8)
        s=(s&0xffff)+(s>>16)
    if len(tmp)&1: s += tmp[-1]
    s=(s&0xffff)+(s>>16)
    s += s>>16
    return (s&0xffff)+len(tmp)

def sections_of(buf):
    pe=struct.unpack_from('<I',buf,0x3c)[0]
    assert buf[pe:pe+4]==b'PE\0\0'
    fh=pe+4
    nsec=struct.unpack_from('<H',buf,fh+2)[0]
    optsz=struct.unpack_from('<H',buf,fh+16)[0]
    sh=fh+20+optsz
    sections={}
    for i in range(nsec):
        o=sh+i*40
        name=buf[o:o+8].split(b'\0',1)[0].decode('ascii')
        vs,va,rs,rp=struct.unpack_from('<IIII',buf,o+8)
        sections[name]=(o,vs,va,rs,rp)
    return pe,sections

def main():
    if len(sys.argv)!=4:
        raise SystemExit('usage: patch_rc41b_runtime.py RC41_BASE.dll rc41b_loader.bin OUT.dll')
    src=Path(sys.argv[1]); loaderp=Path(sys.argv[2]); out=Path(sys.argv[3])
    base=src.read_bytes(); loader=loaderp.read_bytes()
    assert len(base)==BASE_SIZE, len(base)
    assert sha256(base)==BASE_SHA, sha256(base)
    assert 0 < len(loader) <= 0x940, hex(len(loader))
    pe,secs=sections_of(base)
    assert '.text' in secs and '.rc38' in secs
    rc_o,rc_vs,rc_va,rc_rs,rc_rp=secs['.rc38']
    assert (rc_va,rc_rs,rc_rp)==(0x03500000,0x1000,0x4e200)
    assert rc_vs==0x6a2, hex(rc_vs)
    text_o,text_vs,text_va,text_rs,text_rp=secs['.text']
    def fileoff(rva):
        assert text_va <= rva < text_va+text_rs
        return text_rp+(rva-text_va)
    hook_off=fileoff(HOOK_RVA)
    assert base[hook_off:hook_off+5]==EXPECTED_OLD_HOOK, base[hook_off:hook_off+5].hex()
    old_loader_off=rc_rp+(RC41_OLD_LOADER_RVA-rc_va)
    assert any(x!=0xcc for x in base[old_loader_off:old_loader_off+0xa2])
    loader_off=rc_rp+(LOADER_RVA-rc_va)
    assert loader_off+len(loader) <= rc_rp+rc_rs
    assert set(base[loader_off:loader_off+len(loader)]) <= {0xcc}, 'insertion area is not pristine CC padding'
    new_vs=(LOADER_RVA-rc_va)+len(loader)
    assert new_vs < 0x1000
    cand=bytearray(base)
    cand[loader_off:loader_off+len(loader)]=loader
    struct.pack_into('<I',cand,rc_o+8,new_vs)
    disp=LOADER_RVA-(HOOK_RVA+5)
    cand[hook_off:hook_off+5]=b'\xE8'+struct.pack('<i',disp)
    chkoff=pe+4+20+64
    struct.pack_into('<I',cand,chkoff,0)
    struct.pack_into('<I',cand,chkoff,pe_checksum(cand,chkoff))
    out.write_bytes(cand)
    diff=[i for i,(a,b) in enumerate(zip(base,cand)) if a!=b]
    print('RC41B_RUNTIME_BUILD=PASS')
    print('BASE_RC41_SHA256='+sha256(base))
    print('CANDIDATE_SHA256='+sha256(cand))
    print('LOADER_SHA256='+sha256(loader))
    print(f'LOADER_SIZE={len(loader)}')
    print(f'LOADER_RVA=0x{LOADER_RVA:08X}')
    print(f'RC38_VIRTUAL_SIZE=0x{new_vs:X}')
    print(f'HOOK_RVA=0x{HOOK_RVA:08X}')
    print('HOOK_BYTES='+cand[hook_off:hook_off+5].hex())
    print('DIFF_BYTES='+str(len(diff)))
    print('PE_CHECKSUM=PASS')

if __name__=='__main__': main()
