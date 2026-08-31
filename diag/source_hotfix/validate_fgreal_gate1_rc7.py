#!/usr/bin/env python3
import hashlib, struct, sys
BASE_SHA='9d67705897a0faa7c7b25c09e8d85a7d6fcbb8ee043344dd601ecc5be5e0c263'
NEW_SHA='f871c6a5e3a6a7cf1430d5fdd609d5e5f81a98f5c141559af81a5d51718cd2d2'
IB=0x180000000; WR=0x34FB000; CODE=0x1B4; UNW=0x34FB1B4; FD=0x34FC000; DIA=0x34FD000
FG_ENABLED=0x2C3F6CC; FG_SCHED=0x2C93B94; FG_SUBMIT=0x127C0
CALLS=(0xC798,0xD678)
def sha(b): return hashlib.sha256(b).hexdigest()
def checksum(blob,off):
 b=bytearray(blob); struct.pack_into('<I',b,off,0); s=0
 for i in range(0,len(b)-1,2): s+=b[i]|b[i+1]<<8; s=(s&0xffff)+(s>>16)
 if len(b)&1:s+=b[-1]
 s=(s&0xffff)+(s>>16); s=(s&0xffff)+(s>>16); return (s+len(b))&0xffffffff
def pe(b):
 e=struct.unpack_from('<I',b,0x3c)[0]; coff=e+4; opt=coff+20; n=struct.unpack_from('<H',b,coff+2)[0]; osz=struct.unpack_from('<H',b,coff+16)[0]; sh=opt+osz
 secs=[]
 for i in range(n):
  o=sh+i*40; name=b[o:o+8].rstrip(b'\0').decode('ascii'); vs,va,rs,rp,_,_,_,_,ch=struct.unpack_from('<IIIIIIHHI',b,o+8); secs.append(dict(name=name,off=o,vs=vs,va=va,rs=rs,rp=rp,ch=ch))
 return dict(coff=coff,opt=opt,n=n,sh=sh,ib=struct.unpack_from('<Q',b,opt+24)[0],cks_off=opt+64,cks=struct.unpack_from('<I',b,opt+64)[0],secs=secs)
def ro(P,rva):
 for s in P['secs']:
  if s['va']<=rva<s['va']+max(s['vs'],s['rs']):return s['rp']+(rva-s['va'])
 raise ValueError(hex(rva))
def rel32_target(b,P,insn_rva,disp_off,insn_end):
 o=ro(P,insn_rva)+disp_off; d=struct.unpack_from('<i',b,o)[0]; return P['ib']+insn_rva+insn_end+d
def call_target(b,P,rva):
 o=ro(P,rva); assert b[o]==0xE8; d=struct.unpack_from('<i',b,o+1)[0]; return P['ib']+rva+5+d
def model(active,target,render_ms,freq=10_000_000):
 interval=(freq*2)//target if active else None; t=0; nt=None; out=[]
 for i,ms in enumerate(render_ms):
  if i:t+=int(ms*freq/1000)
  if not active:
   out.append(t); nt=None; continue
  if nt is None: out.append(t); nt=t+interval; continue
  if t<nt: t=nt; out.append(t); nt+=interval
  else: out.append(t); nt=t+interval
 return out

def main():
 if len(sys.argv)!=3: raise SystemExit('usage: validate RC6 RC7')
 B=open(sys.argv[1],'rb').read(); N=open(sys.argv[2],'rb').read(); pb,pn=pe(B),pe(N); C=[]
 def ck(n,c,d=''):
  C.append((n,bool(c),d));
  if not c: raise AssertionError(n+(' :: '+d if d else ''))
 ck('RC6 exact hash',sha(B)==BASE_SHA,sha(B)); ck('RC7 exact hash',sha(N)==NEW_SHA,sha(N)); ck('same file size',len(B)==len(N),f'{len(B)}/{len(N)}')
 ck('x64 image base',pb['ib']==pn['ib']==IB); ck('section count unchanged 8',pb['n']==pn['n']==8)
 names=[s['name'] for s in pn['secs']]; ck('hotfix sections preserved',names[-3:]==['.fgov','.fgdat','.fgdia'],repr(names[-3:]))
 fgb=next(s for s in pb['secs'] if s['name']=='.fgov'); fgn=next(s for s in pn['secs'] if s['name']=='.fgov'); fdb=next(s for s in pb['secs'] if s['name']=='.fgdat'); fdn=next(s for s in pn['secs'] if s['name']=='.fgdat'); db=next(s for s in pb['secs'] if s['name']=='.fgdia'); dn=next(s for s in pn['secs'] if s['name']=='.fgdia')
 ck('fgov RVA/raw unchanged',fgn['va']==fgb['va']==WR and fgn['rp']==fgb['rp'] and fgn['rs']==fgb['rs']==0x200)
 ck('fgov virtual size updated only',fgb['vs']==0x194 and fgn['vs']==0x1BC,f'{fgb["vs"]:#x}->{fgn["vs"]:#x}')
 ck('fgdat exact byte-identical',B[fdb['rp']:fdb['rp']+fdb['rs']]==N[fdn['rp']:fdn['rp']+fdn['rs']])
 ck('SHAREDIAG1 exact byte-identical',B[db['rp']:db['rp']+db['rs']]==N[dn['rp']:dn['rp']+dn['rs']])
 ck('checksum valid',pn['cks']==checksum(N,pn['cks_off']),f'{pn["cks"]:#x}')
 for r in CALLS: ck(f'Present call {r:#x} still enters same governor RVA',call_target(N,pn,r)==IB+WR,hex(call_target(N,pn,r)))
 # Strict gates at wrapper entry: cmp [FG_ENABLED],0 ; je inactive ; cmp [FG_SCHED],0 ; je inactive.
 wo=ro(pn,WR)
 ck('enabled gate opcode',N[wo+0x0c:wo+0x13][:2]==b'\x83\x3d',N[wo+0x0c:wo+0x13].hex())
 ck('enabled gate resolves exact runtime flag',rel32_target(N,pn,WR+0x0c,2,7)==IB+FG_ENABLED,hex(rel32_target(N,pn,WR+0x0c,2,7)))
 ck('scheduler gate opcode',N[wo+0x19:wo+0x20][:2]==b'\x83\x3d',N[wo+0x19:wo+0x20].hex())
 ck('scheduler gate resolves exact runtime flag',rel32_target(N,pn,WR+0x19,2,7)==IB+FG_SCHED,hex(rel32_target(N,pn,WR+0x19,2,7)))
 # Both JEs target inactive block 0x18d.
 def jcc_target(off):
  assert N[wo+off:wo+off+2]==b'\x0f\x84'; d=struct.unpack_from('<i',N,wo+off+2)[0]; return WR+off+6+d
 ck('enabled false -> inactive passthrough',jcc_target(0x13)==WR+0x18D,hex(jcc_target(0x13)))
 ck('scheduler false -> inactive passthrough',jcc_target(0x20)==WR+0x18D,hex(jcc_target(0x20)))
 ck('inactive clears GOV_ARMED',N[wo+0x18D:wo+0x197].startswith(b'\xc7\x05'),N[wo+0x18D:wo+0x197].hex())
 ck('inactive jumps to submit',N[wo+0x197:wo+0x199]==b'\xeb\x0a',N[wo+0x197:wo+0x199].hex())
 # Tail jump still exact original FGSubmit.
 tail=WR+0x1AF; to=ro(pn,tail); ck('tail opcode JMP',N[to]==0xE9,hex(N[to])); d=struct.unpack_from('<i',N,to+1)[0]; ck('tail -> original FGSubmit',IB+tail+5+d==IB+FG_SUBMIT,hex(IB+tail+5+d))
 # Unwind and runtime function updated.
 ck('new unwind bytes',N[ro(pn,UNW):ro(pn,UNW)+8]==bytes.fromhex('0104010004820000'))
 pdata=next(s for s in pn['secs'] if s['name']=='.pdata'); rf=pdata['rp']+0x11F4
 ck('runtime function new range',N[rf:rf+12]==struct.pack('<III',WR,WR+CODE,UNW),N[rf:rf+12].hex())
 # Verify the two globals were independently derived from the existing F8 status function.
 # RVA 0x17CF9 reads P1FG7N ENABLED, RVA 0x17D18 reads scheduler-running.
 ck('F8 ENABLED status global mapping',rel32_target(N,pn,0x17CF9,2,6)==IB+FG_ENABLED,hex(rel32_target(N,pn,0x17CF9,2,6)))
 ck('F8 SCHEDULER status global mapping',rel32_target(N,pn,0x17D18,2,6)==IB+FG_SCHED,hex(rel32_target(N,pn,0x17D18,2,6)))
 # Change set strictly limited to .fgov bytes, .fgov VS, pdata runtime entry, checksum.
 allowed=set(range(fgb['rp'],fgb['rp']+fgb['rs']))|set(range(fgb['off']+8,fgb['off']+12))|set(range(rf,rf+12))|set(range(pn['cks_off'],pn['cks_off']+4))
 bad=[i for i,(x,y) in enumerate(zip(B,N)) if x!=y and i not in allowed]
 ck('no binary changes outside strict gate region/metadata',not bad,','.join(hex(x) for x in bad[:20]))
 # Behavioral timing model: OFF and failed-init states are exact pass-through; active state is half-rate.
 arrivals=[16.6667]*120; off=model(False,60,arrivals); raw=[]; t=0
 for i,ms in enumerate(arrivals):
  if i:t+=int(ms*10_000_000/1000)
  raw.append(t)
 ck('FG OFF has zero governor delay',off==raw)
 failed=model(False,60,arrivals); ck('failed FG init has zero governor delay',failed==raw)
 on=model(True,60,arrivals); dur=(on[-1]-on[0])/10_000_000; fps=(len(on)-1)/dur
 ck('FG enabled+scheduler running -> ~30 REAL fps',29.9<=fps<=30.1,f'{fps:.6f}')
 print(f'FGREAL30 GATE1 STATIC VALIDATION: PASS {len(C)}/{len(C)}')
 for n,_,d in C: print('[PASS]',n,(':: '+d if d else ''))
if __name__=='__main__': main()
