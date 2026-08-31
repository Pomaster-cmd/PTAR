#!/usr/bin/env python3
import hashlib, struct, sys

BASE_SHA='0e558350d789e56908a3f4bb866f76ba77e252dd282fe1bfe0df41dc085e8548'
NEW_SHA='309f82ea51cb549a594dfe584b3d9d9039b3b25265af604e6d063cabadff0a00'
IMAGE_BASE=0x180000000
CALLS=(0xC798,0xD678)
FG_SUBMIT=0x127C0
WRAPPER=0x34FB000
WRAPPER_SIZE=0x18A
UNWIND=0x34FB18C
STATE=0x34FC000

def sha(b): return hashlib.sha256(b).hexdigest()
def rng(a,n): return set(range(a,a+n))
def pe(b):
    e=struct.unpack_from('<I',b,0x3c)[0]; coff=e+4; opt=coff+20
    n=struct.unpack_from('<H',b,coff+2)[0]; osz=struct.unpack_from('<H',b,coff+16)[0]; sh=opt+osz
    secs=[]
    for i in range(n):
        o=sh+i*40; name=b[o:o+8].rstrip(b'\0').decode('ascii')
        vs,va,rs,rp,_,_,_,_,ch=struct.unpack_from('<IIIIIIHHI',b,o+8)
        secs.append(dict(name=name,off=o,vs=vs,va=va,rs=rs,rp=rp,ch=ch))
    return dict(e=e,coff=coff,opt=opt,sh=sh,n=n,secs=secs,
                ib=struct.unpack_from('<Q',b,opt+24)[0],
                sa=struct.unpack_from('<I',b,opt+32)[0],fa=struct.unpack_from('<I',b,opt+36)[0],
                soi=struct.unpack_from('<I',b,opt+56)[0],soh=struct.unpack_from('<I',b,opt+60)[0],
                cks_off=opt+64, cks=struct.unpack_from('<I',b,opt+64)[0])
def checksum(blob,off):
    b=bytearray(blob); struct.pack_into('<I',b,off,0); s=0
    for i in range(0,len(b)-1,2):
        s += b[i] | (b[i+1]<<8); s=(s&0xffff)+(s>>16)
    if len(b)&1: s += b[-1]; s=(s&0xffff)+(s>>16)
    s=(s&0xffff)+(s>>16)
    return (s+len(b))&0xffffffff
def rva_off(P,rva):
    for s in P['secs']:
        span=max(s['vs'],s['rs'])
        if s['va'] <= rva < s['va']+span:
            return s['rp']+(rva-s['va'])
    raise ValueError(hex(rva))
def call_target(b,P,rva):
    o=rva_off(P,rva); assert b[o]==0xE8
    d=struct.unpack_from('<i',b,o+1)[0]
    return P['ib']+rva+5+d

def model(target_fps, render_intervals_ms, freq=10_000_000):
    # Pure timing model of the assembly policy. Calls occur after each render interval;
    # the wrapper either submits immediately if late or waits to next_tick.
    interval=(freq*2)//target_fps
    t=0; next_tick=None; out=[]
    for i,ms in enumerate(render_intervals_ms):
        if i: t += int(ms*freq/1000)
        if next_tick is None:
            out.append(t); next_tick=t+interval; continue
        if t < next_tick:
            t=next_tick
            out.append(t); next_tick += interval
            if t >= next_tick:
                next_tick=t+interval
        else:
            out.append(t); next_tick=t+interval
    return [x*1000/freq for x in out]

def main():
    if len(sys.argv)!=3: raise SystemExit('usage: validate BASE NEW')
    B=open(sys.argv[1],'rb').read(); N=open(sys.argv[2],'rb').read()
    checks=[]
    def ck(name,cond,detail=''):
        checks.append((name,bool(cond),detail));
        if not cond: raise AssertionError(name+(' :: '+detail if detail else ''))
    ck('base hash',sha(B)==BASE_SHA,sha(B)); ck('new hash',sha(N)==NEW_SHA,sha(N))
    pb,pn=pe(B),pe(N)
    ck('base five sections',pb['n']==5,str(pb['n'])); ck('new seven sections',pn['n']==7,str(pn['n']))
    ck('image base unchanged',pb['ib']==pn['ib']==IMAGE_BASE)
    ck('section alignment unchanged',pb['sa']==pn['sa']==0x1000)
    ck('file alignment unchanged',pb['fa']==pn['fa']==0x200)
    names=[s['name'] for s in pn['secs']]
    ck('new section names',names[-2:]==['.fgov','.fgdat'],repr(names[-2:]))
    fg=pn['secs'][-2]; fd=pn['secs'][-1]
    ck('fgov geometry',fg['va']==WRAPPER and fg['vs']==0x194 and fg['rs']==0x200 and fg['ch']==0x60000020,repr(fg))
    ck('fgdat geometry',fd['va']==STATE and fd['vs']==0x20 and fd['rs']==0x200 and fd['ch']==0xC0000040,repr(fd))
    ck('size of image',pn['soi']==0x34FD000,hex(pn['soi']))
    ck('checksum valid',pn['cks']==checksum(N,pn['cks_off']),f"stored={pn['cks']:#x} calc={checksum(N,pn['cks_off']):#x}")
    ck('output size',len(N)==0x4B200,hex(len(N)))
    for r in CALLS:
        ck(f'old call {r:#x} -> FGSubmit',call_target(B,pb,r)==IMAGE_BASE+FG_SUBMIT,hex(call_target(B,pb,r)))
        ck(f'new call {r:#x} -> governor',call_target(N,pn,r)==IMAGE_BASE+WRAPPER,hex(call_target(N,pn,r)))
    # Tail jump target.
    tail_rva=WRAPPER+0x185; to=rva_off(pn,tail_rva)
    ck('governor tail opcode',N[to]==0xE9,hex(N[to]))
    disp=struct.unpack_from('<i',N,to+1)[0]
    ck('governor tail -> original FGSubmit',IMAGE_BASE+tail_rva+5+disp==IMAGE_BASE+FG_SUBMIT,hex(IMAGE_BASE+tail_rva+5+disp))
    # Unwind metadata.
    uo=rva_off(pn,UNWIND)
    ck('unwind info',N[uo:uo+8]==bytes.fromhex('0104010004820000'),N[uo:uo+8].hex())
    pdata=next(s for s in pn['secs'] if s['name']=='.pdata')
    ck('pdata expanded exact',pdata['vs']==0x1200 and pdata['rs']==0x1200,repr(pdata))
    last=N[pdata['rp']+0x11f4:pdata['rp']+0x1200]
    ck('runtime function entry',last==struct.pack('<III',WRAPPER,WRAPPER+WRAPPER_SIZE,UNWIND),last.hex())
    dd=pn['opt']+112; er,es=struct.unpack_from('<II',N,dd+3*8)
    ck('exception directory expanded',er==0x34F8000 and es==0x1200,f'{er:#x}/{es:#x}')
    # Zero initialized governor state in file.
    ck('governor state zero initialized',N[fd['rp']:fd['rp']+0x20]==b'\0'*0x20)

    # Original-file byte delta is strictly bounded.
    allowed=set()
    allowed |= rng(pb['coff']+2,2)                        # NumberOfSections
    allowed |= rng(pb['opt']+4,4)                        # SizeOfCode
    allowed |= rng(pb['opt']+8,4)                        # SizeOfInitializedData
    allowed |= rng(pb['opt']+56,4)                       # SizeOfImage
    allowed |= rng(pb['opt']+64,4)                       # CheckSum
    allowed |= rng(pb['opt']+112+3*8+4,4)                # exception directory size
    pdata_b=next(s for s in pb['secs'] if s['name']=='.pdata')
    allowed |= rng(pdata_b['off']+8,4)                   # .pdata VirtualSize
    allowed |= rng(pb['sh']+pb['n']*40,80)               # two new section headers in header slack
    text_b=next(s for s in pb['secs'] if s['name']=='.text')
    for r in CALLS: allowed |= rng(text_b['rp']+(r-text_b['va']),5)
    allowed |= rng(pdata_b['rp']+0x11f4,12)              # appended RUNTIME_FUNCTION
    bad=[i for i,(x,y) in enumerate(zip(B,N[:len(B)])) if x!=y and i not in allowed]
    ck('no unexpected modifications in RC4 image',not bad,','.join(hex(x) for x in bad[:20]))

    # Timing-model checks.
    out=model(60,[16.6667]*120)
    duration=(out[-1]-out[0])/1000
    fps=(len(out)-1)/duration
    ck('model 60Hz source -> about 30 REAL fps',29.9 <= fps <= 30.1,f'{fps:.6f}')
    out120=model(120,[8.3333]*240); duration=(out120[-1]-out120[0])/1000; fps120=(len(out120)-1)/duration
    ck('model target120 -> about60 REAL fps',59.8 <= fps120 <= 60.2,f'{fps120:.6f}')
    slow=model(60,[50.0]*20); gaps=[slow[i]-slow[i-1] for i in range(1,len(slow))]
    ck('slow source never gets extra 33ms wait',min(gaps)>=49.9 and max(gaps)<=50.1,f'{min(gaps):.3f}/{max(gaps):.3f}')
    first=model(60,[16.6667]*2)
    ck('first FG REAL submits immediately',abs(first[0])<1e-9,str(first[0]))

    print(f'FGREAL30 STATIC VALIDATION: PASS {len(checks)}/{len(checks)}')
    for n,_,d in checks: print('[PASS]',n,(':: '+d if d else ''))

if __name__=='__main__': main()
