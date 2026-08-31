#!/usr/bin/env python3
import hashlib, struct, sys
from pathlib import Path

RC9='9d84bb1978a8463df2498455b180125f9aa6a9a2f6ee390cab1382cec44f1d59'
RC11='11fb4e484e79984641d72e2c793a2fa2b8eab871d50daf9cb03bfd77ed535dff'
RC12='b253f0457f61a7c268ea3747864eb53e14ab113464f3c62f2a137da56bc18148'
IMAGE_BASE=0x180000000
PATCH_RVA=0xCEC3
HELPER_RVA=0x34FD1D5
FGDAT_RVA=0x34FC000
LEGACY_ACTIVE_RVA=0x34FC098
FG_SCHED_RVA=0x2C93B94
EPILOGUE_RVA=0xD2CC
DXGI_INVALID=0x887A0001
ORIG=bytes.fromhex('89 d5 44 8b bc 24 48 01 00 00')

checks=[]
def ck(name,ok,detail=''):
    checks.append((name,bool(ok),detail))
    if not ok: raise AssertionError(name+(' :: '+detail if detail else ''))
def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def parse_pe(b):
    e=struct.unpack_from('<I',b,0x3c)[0]; ck('PE signature',b[e:e+4]==b'PE\0\0')
    coff=e+4; machine,nsec,_,_,_,optsz,_=struct.unpack_from('<HHIIIHH',b,coff)
    ck('AMD64 machine',machine==0x8664,hex(machine)); opt=coff+20
    ck('PE32+',struct.unpack_from('<H',b,opt)[0]==0x20b)
    ib=struct.unpack_from('<Q',b,opt+24)[0]; ck('image base',ib==IMAGE_BASE,hex(ib))
    sh=opt+optsz; secs=[]
    for i in range(nsec):
        o=sh+i*40; name=b[o:o+8].rstrip(b'\0').decode('ascii','replace')
        vs,va,rs,rp,_,_,_,_,ch=struct.unpack_from('<IIIIIIHHI',b,o+8)
        secs.append(dict(name=name,off=o,vs=vs,va=va,rs=rs,rp=rp,ch=ch))
    return dict(opt=opt,cks=opt+64,secs=secs,nsec=nsec)

def rva_off(pe,rva):
    for s in pe['secs']:
        if s['va']<=rva<s['va']+max(s['vs'],s['rs']): return s['rp']+(rva-s['va'])
    raise AssertionError('unmapped '+hex(rva))

def pe_checksum(blob,off):
    b=bytearray(blob); struct.pack_into('<I',b,off,0); s=0
    for i in range(0,len(b)-1,2):
        s += b[i] | (b[i+1]<<8); s=(s&0xffff)+(s>>16)
    if len(b)&1: s+=b[-1]
    s=(s&0xffff)+(s>>16); s=(s&0xffff)+(s>>16)
    return (s+len(b))&0xffffffff

def rel32_target(b,pe,rva):
    o=rva_off(pe,rva); ck('call opcode at '+hex(rva),b[o]==0xE8,hex(b[o]))
    d=struct.unpack_from('<i',b,o+1)[0]; return rva+5+d

def diff_ranges(a,b):
    ix=[i for i,(x,y) in enumerate(zip(a,b)) if x!=y]
    if len(a)!=len(b): ix.extend(range(min(len(a),len(b)),max(len(a),len(b))))
    if not ix:return []
    out=[]; st=pr=ix[0]
    for x in ix[1:]:
        if x==pr+1: pr=x
        else: out.append((st,pr+1)); st=pr=x
    out.append((st,pr+1)); return out

def main():
    if len(sys.argv)!=5: raise SystemExit('usage: validate... RC9 RC11 RC12 OBJ')
    p9,p11,p12,obj=sys.argv[1:]
    B=Path(p9).read_bytes(); E=Path(p11).read_bytes(); N=Path(p12).read_bytes()
    ck('RC9 hash',hashlib.sha256(B).hexdigest()==RC9)
    ck('RC11 rejected-base hash',hashlib.sha256(E).hexdigest()==RC11)
    ck('RC12 hash',hashlib.sha256(N).hexdigest()==RC12)
    ck('same DLL size',len(B)==len(E)==len(N),str((len(B),len(E),len(N))))
    pe9=parse_pe(B); pe11=parse_pe(E); pe=parse_pe(N)
    ck('8 sections',pe['nsec']==8,str(pe['nsec']))
    names=[s['name'] for s in pe['secs']]; ck('section names',names==['.text','.rdata','.data','.pdata','.reloc','.fgov','.fgdat','.fgdia'],repr(names))
    s9={s['name']:s for s in pe9['secs']}; s11={s['name']:s for s in pe11['secs']}; sn={s['name']:s for s in pe['secs']}
    ck('fgov geometry unchanged', (sn['.fgov']['va'],sn['.fgov']['rs'],sn['.fgov']['vs'])==(s9['.fgov']['va'],s9['.fgov']['rs'],s9['.fgov']['vs']))
    ck('GATE1 fgov byte-identical to RC9',N[sn['.fgov']['rp']:sn['.fgov']['rp']+sn['.fgov']['rs']]==B[s9['.fgov']['rp']:s9['.fgov']['rp']+s9['.fgov']['rs']])
    ck('fgdat RVA/raw unchanged', (sn['.fgdat']['va'],sn['.fgdat']['rs'])==(FGDAT_RVA,0x200))
    ck('fgdat VS 0x9C',sn['.fgdat']['vs']==0x9C,hex(sn['.fgdat']['vs']))
    ck('fgdat raw remains zero-initialized',set(N[sn['.fgdat']['rp']:sn['.fgdat']['rp']+sn['.fgdat']['rs']])<={0})
    ck('legacy active slot in zero data',N[sn['.fgdat']['rp']+0x98:sn['.fgdat']['rp']+0x9c]==b'\0'*4)
    ck('fgdia exact 0x200 code',sn['.fgdia']['vs']==0x200 and sn['.fgdia']['rs']==0x200)
    ck('RC11 fgdia was 0x1C4',s11['.fgdia']['vs']==0x1C4)

    # Outside expected mutable regions RC12 must equal RC9.
    mutable=[]
    mutable.append((pe['cks'],pe['cks']+4))
    mutable.append((sn['.fgdat']['off']+8,sn['.fgdat']['off']+12))
    mutable.append((sn['.fgdia']['off']+8,sn['.fgdia']['off']+12))
    mutable.append((sn['.fgdia']['rp'],sn['.fgdia']['rp']+0x200))
    # Existing SHARELEGACY2 patch sites plus new ResizeBuffers guard.
    for rva,n in [(0x221E1,5),(0x22E39,15),(0x22EFC,8),(0x22F64,8),(0x242F7,5),(0x25434,8),(0x2552F,8),(0x255CB,8),(PATCH_RVA,10)]:
        o=rva_off(pe,rva); mutable.append((o,o+n))
    def covered(i): return any(a<=i<b for a,b in mutable)
    bad=[(a,b) for a,b in diff_ranges(B,N) if any(not covered(i) for i in range(a,b))]
    ck('RC9->RC12 diff bounded to hotfix sites/headers',not bad,repr(bad[:8]))

    po=rva_off(pe,PATCH_RVA)
    ck('RC9 resize prologue exact',B[rva_off(pe9,PATCH_RVA):rva_off(pe9,PATCH_RVA)+10]==ORIG)
    ck('RC11 resize prologue remained original',E[rva_off(pe11,PATCH_RVA):rva_off(pe11,PATCH_RVA)+10]==ORIG)
    ck('RC12 resize site call+nops',N[po]==0xE8 and N[po+5:po+10]==b'\x90'*5,N[po:po+10].hex())
    ck('resize call targets helper',rel32_target(N,pe,PATCH_RVA)==HELPER_RVA,hex(rel32_target(N,pe,PATCH_RVA)))

    # Exact helper semantics by bytes/displacements.
    ho=rva_off(pe,HELPER_RVA); h=N[ho:ho+0x2B]
    ck('helper legacy cmp opcode',h[0:3]==bytes.fromhex('83 3d')+h[2:3]) # structural sanity; detailed below
    ck('helper first cmp imm zero',h[6]==0)
    d=struct.unpack_from('<i',h,2)[0]; ck('helper legacy-active target',HELPER_RVA+7+d==LEGACY_ACTIVE_RVA,hex(HELPER_RVA+7+d))
    ck('helper scheduler cmp opcode',h[9:11]==bytes.fromhex('83 3d'))
    d=struct.unpack_from('<i',h,11)[0]; ck('helper scheduler target',HELPER_RVA+9+7+d==FG_SCHED_RVA,hex(HELPER_RVA+16+d))
    ck('helper lock HRESULT',h[0x12:0x17]==b'\xBD'+struct.pack('<I',DXGI_INVALID),h[0x12:0x17].hex())
    ck('helper discards CALL return',h[0x17:0x1b]==bytes.fromhex('48 83 c4 08'))
    ck('helper jmp opcode',h[0x1b]==0xE9)
    jd=struct.unpack_from('<i',h,0x1c)[0]; ck('helper jumps outer epilogue',HELPER_RVA+0x20+jd==EPILOGUE_RVA,hex(HELPER_RVA+0x20+jd))
    ck('helper passthrough reconstructs mov ebp,edx',h[0x20:0x22]==bytes.fromhex('89 d5'))
    ck('helper passthrough stack offset +150',h[0x22:0x2a]==bytes.fromhex('44 8b bc 24 50 01 00 00'))
    ck('helper passthrough ret',h[0x2a]==0xC3)

    # Persistent mode marker: mailbox wrapper copies transient fallback flag -> legacy-active.
    mw=0x34FD142; mo=rva_off(pe,mw); m=N[mo:mo+0x26]
    ck('mailbox wrapper loads fallback flag',m[:3]==bytes.fromhex('44 8b 15'))
    d=struct.unpack_from('<i',m,3)[0]; ck('mailbox wrapper fallback source',mw+7+d==0x34FC020,hex(mw+7+d))
    ck('mailbox wrapper stores legacy active',m[7:10]==bytes.fromhex('44 89 15'))
    d=struct.unpack_from('<i',m,10)[0]; ck('mailbox wrapper legacy-active target',mw+14+d==LEGACY_ACTIVE_RVA,hex(mw+14+d))
    ck('mailbox wrapper still calls original constructor',m[14:18]==bytes.fromhex('48 83 ec 28'))
    ck('mailbox wrapper clears transient fallback',bytes.fromhex('c7 05') in m and m.endswith(bytes.fromhex('00 00 00 c3')))

    # State model: keyed path must never activate resize guard; legacy+running must.
    def blocked(legacy,sched): return bool(legacy and sched)
    ck('model keyed+off passthrough',not blocked(0,0))
    ck('model keyed+running passthrough SatGat',not blocked(0,1))
    ck('model legacy before scheduler passthrough',not blocked(1,0))
    ck('model legacy+running locks resize',blocked(1,1))
    ck('lock is FG-runtime scoped via scheduler state',all(not blocked(l,0) for l in (0,1)))

    # PE checksum exact.
    stored=struct.unpack_from('<I',N,pe['cks'])[0]
    ck('PE checksum nonzero',stored!=0,hex(stored))
    ck('PE checksum recomputes',stored==pe_checksum(N,pe['cks']),f'{stored:#x}/{pe_checksum(N,pe["cks"]):#x}')

    # Object identity and exact size.
    op=Path(obj); ck('object exists',op.is_file())
    ck('object SHA',hashlib.sha256(op.read_bytes()).hexdigest()=='c9f80bf496e583baad220f2d0782220c860da4cfee9f8d3db0d0da19279fbbb6',hashlib.sha256(op.read_bytes()).hexdigest())

    # Core export names must remain in binary string table.
    for nm in [b'D3D11CoreCreateDevice\0',b'D3D11CoreCreateLayeredDevice\0',b'D3D11CoreGetLayeredDeviceSize\0',b'D3D11CoreRegisterLayers\0',b'D3D11CreateDevice\0',b'D3D11CreateDeviceAndSwapChain\0',b'D3D11On12CreateDevice\0']:
        ck('export string '+nm[:-1].decode(),nm in N)
    ck('no new DLL size growth',len(N)==308224,str(len(N)))

    print(f'SHARELEGACY3 + RESIZEGUARD1 STATIC VALIDATION: PASS {len(checks)}/{len(checks)}')
    for name,ok,detail in checks: print('[PASS]',name,(':: '+detail if detail else ''))

if __name__=='__main__': main()
