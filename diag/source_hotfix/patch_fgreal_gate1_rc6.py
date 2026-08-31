#!/usr/bin/env python3
import hashlib, struct, sys

BASE_SHA='9d67705897a0faa7c7b25c09e8d85a7d6fcbb8ee043344dd601ecc5be5e0c263'
IMAGE_BASE=0x180000000
WRAPPER_RVA=0x34FB000
FGDAT_RVA=0x34FC000
FGDIA_RVA=0x34FD000
FG_SUBMIT_RVA=0x127C0
FG_TARGET_FPS_RVA=0x4B068
QPC_IAT_RVA=0x49F18
QPF_IAT_RVA=0x49F20
SLEEP_IAT_RVA=0x49F50
FG_ENABLED_RVA=0x2C3F6CC
FG_SCHEDULER_RUNNING_RVA=0x2C93B94
UNWIND_INFO=bytes.fromhex('01 04 01 00 04 82 00 00')


def sha(b): return hashlib.sha256(b).hexdigest()
def align(v,a): return (v+a-1)&~(a-1)
def checksum(blob,off):
    b=bytearray(blob); struct.pack_into('<I',b,off,0); s=0
    for i in range(0,len(b)-1,2):
        s += b[i] | (b[i+1]<<8); s=(s&0xffff)+(s>>16)
    if len(b)&1: s += b[-1]; s=(s&0xffff)+(s>>16)
    s=(s&0xffff)+(s>>16)
    return (s+len(b))&0xffffffff

def parse_pe(b):
    e=struct.unpack_from('<I',b,0x3c)[0]
    if b[e:e+4]!=b'PE\0\0': raise RuntimeError('PE signature')
    coff=e+4; machine,nsec,_,_,_,optsz,_=struct.unpack_from('<HHIIIHH',b,coff)
    if machine!=0x8664: raise RuntimeError('not x64')
    opt=coff+20
    if struct.unpack_from('<H',b,opt)[0]!=0x20b: raise RuntimeError('not PE32+')
    ib=struct.unpack_from('<Q',b,opt+24)[0]; sa=struct.unpack_from('<I',b,opt+32)[0]
    fa=struct.unpack_from('<I',b,opt+36)[0]; cks_off=opt+64; sh=opt+optsz
    secs=[]
    for i in range(nsec):
        o=sh+i*40; name=b[o:o+8].rstrip(b'\0').decode('ascii','replace')
        vs,va,rs,rp,_,_,_,_,ch=struct.unpack_from('<IIIIIIHHI',b,o+8)
        secs.append(dict(name=name,off=o,vs=vs,va=va,rs=rs,rp=rp,ch=ch))
    return dict(e=e,coff=coff,opt=opt,nsec=nsec,ib=ib,sa=sa,fa=fa,cks_off=cks_off,sh=sh,secs=secs)

def coff_text(objp):
    b=open(objp,'rb').read(); machine,nsec,_,symptr,nsym,optsz,_=struct.unpack_from('<HHIIIHH',b,0)
    if machine!=0x8664 or optsz!=0: raise RuntimeError('bad COFF')
    sec=None
    for i in range(nsec):
        o=20+i*40; name=b[o:o+8].rstrip(b'\0').decode('ascii','replace')
        _vs,_va,rawsz,rawptr,relptr,_,nrel,_,_=struct.unpack_from('<IIIIIIHHI',b,o+8)
        if name=='.text': sec=(rawsz,rawptr,relptr,nrel)
    if not sec: raise RuntimeError('no text')
    rawsz,rawptr,relptr,nrel=sec; code=bytearray(b[rawptr:rawptr+rawsz])
    st=symptr+nsym*18; slen=struct.unpack_from('<I',b,st)[0]; stab=b[st:st+slen]
    def sn(idx):
        o=symptr+idx*18; rn=b[o:o+8]; z,so=struct.unpack('<II',rn)
        if z==0:
            end=stab.find(b'\0',so); end=len(stab) if end<0 else end
            return stab[so:end].decode('ascii')
        return rn.rstrip(b'\0').decode('ascii')
    rel=[]
    for i in range(nrel):
        o=relptr+i*10; ro,sidx,typ=struct.unpack_from('<IIH',b,o)
        if typ!=4: raise RuntimeError(f'reloc type {typ:#x}')
        rel.append((ro,sn(sidx)))
    return code,rel

def rva_off(pe,rva):
    for s in pe['secs']:
        if s['va'] <= rva < s['va']+max(s['vs'],s['rs']):
            return s['rp']+(rva-s['va'])
    raise RuntimeError('RVA not mapped '+hex(rva))

def main():
    if len(sys.argv)!=4: raise SystemExit('usage: patch_fgreal_gate1_rc6.py RC6_DLL GATE_OBJ OUT_DLL')
    base,obj,outp=sys.argv[1:]
    B=bytearray(open(base,'rb').read())
    if sha(B)!=BASE_SHA: raise RuntimeError('refusing non-RC6 exact base: '+sha(B))
    pe=parse_pe(B)
    if pe['ib']!=IMAGE_BASE or pe['nsec']!=8: raise RuntimeError('unexpected RC6 PE')
    names=[s['name'] for s in pe['secs']]
    if names[-3:]!=['.fgov','.fgdat','.fgdia']: raise RuntimeError('unexpected hotfix sections '+repr(names[-3:]))
    fg=next(s for s in pe['secs'] if s['name']=='.fgov')
    fd=next(s for s in pe['secs'] if s['name']=='.fgdat')
    dia=next(s for s in pe['secs'] if s['name']=='.fgdia')
    if (fg['va'],fg['rs'],fd['va'],dia['va'])!=(WRAPPER_RVA,0x200,FGDAT_RVA,FGDIA_RVA): raise RuntimeError('hotfix geometry mismatch')
    code,rel=coff_text(obj)
    if len(code)!=0x1B4: raise RuntimeError('unexpected new wrapper size '+hex(len(code)))
    unwind_off=align(len(code),4)
    total=unwind_off+len(UNWIND_INFO)
    if total>fg['rs']: raise RuntimeError('wrapper exceeds .fgov raw')
    targets={
      'FG_ENABLED':IMAGE_BASE+FG_ENABLED_RVA,
      'FG_SCHEDULER_RUNNING':IMAGE_BASE+FG_SCHEDULER_RUNNING_RVA,
      'FG_TARGET_FPS':IMAGE_BASE+FG_TARGET_FPS_RVA,
      'QPC_IAT':IMAGE_BASE+QPC_IAT_RVA,
      'QPF_IAT':IMAGE_BASE+QPF_IAT_RVA,
      'SLEEP_IAT':IMAGE_BASE+SLEEP_IAT_RVA,
      'GOV_NEXT_TICK':IMAGE_BASE+FGDAT_RVA+0x00,
      'GOV_INTERVAL':IMAGE_BASE+FGDAT_RVA+0x08,
      'GOV_SLEEP1_TICKS':IMAGE_BASE+FGDAT_RVA+0x10,
      'GOV_LAST_TARGET':IMAGE_BASE+FGDAT_RVA+0x18,
      'GOV_ARMED':IMAGE_BASE+FGDAT_RVA+0x1C,
      'FG_SUBMIT':IMAGE_BASE+FG_SUBMIT_RVA,
    }
    va=IMAGE_BASE+WRAPPER_RVA; seen=set()
    for ro,name in rel:
        if name not in targets: raise RuntimeError('unknown reloc '+name)
        add=struct.unpack_from('<i',code,ro)[0]
        disp=targets[name]+add-(va+ro+4)
        if not -(1<<31)<=disp<(1<<31): raise RuntimeError('rel32 overflow '+name)
        struct.pack_into('<i',code,ro,disp); seen.add(name)
    if set(targets)!=seen: raise RuntimeError('missing relocs '+repr(set(targets)-seen))
    out=bytearray(B)
    # Replace only the existing .fgov raw section; keep .fgdat/.fgdia byte-identical.
    out[fg['rp']:fg['rp']+fg['rs']]=b'\0'*fg['rs']
    out[fg['rp']:fg['rp']+len(code)]=code
    out[fg['rp']+unwind_off:fg['rp']+unwind_off+8]=UNWIND_INFO
    struct.pack_into('<I',out,fg['off']+8,total)  # .fgov VirtualSize
    # Existing governor RUNTIME_FUNCTION is the final 12 bytes of .pdata.
    pdata=next(s for s in pe['secs'] if s['name']=='.pdata')
    if pdata['vs']!=0x1200 or pdata['rs']!=0x1200: raise RuntimeError('unexpected pdata')
    rf=pdata['rp']+0x11F4
    old=bytes(B[rf:rf+12])
    exp=struct.pack('<III',WRAPPER_RVA,WRAPPER_RVA+0x18A,WRAPPER_RVA+0x18C)
    if old!=exp: raise RuntimeError('old governor runtime function mismatch '+old.hex())
    out[rf:rf+12]=struct.pack('<III',WRAPPER_RVA,WRAPPER_RVA+len(code),WRAPPER_RVA+unwind_off)
    struct.pack_into('<I',out,pe['cks_off'],0); c=checksum(out,pe['cks_off']); struct.pack_into('<I',out,pe['cks_off'],c)
    open(outp,'wb').write(out)
    print('BASE_SHA256='+BASE_SHA)
    print('OUTPUT_SHA256='+sha(out))
    print(f'WRAPPER_RVA=0x{WRAPPER_RVA:X}')
    print(f'WRAPPER_SIZE=0x{len(code):X}')
    print(f'UNWIND_RVA=0x{WRAPPER_RVA+unwind_off:X}')
    print(f'FG_ENABLED_RVA=0x{FG_ENABLED_RVA:X}')
    print(f'FG_SCHEDULER_RUNNING_RVA=0x{FG_SCHEDULER_RUNNING_RVA:X}')
    print(f'PE_CHECKSUM=0x{c:X}')

if __name__=='__main__': main()
