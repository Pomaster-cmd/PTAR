#!/usr/bin/env python3
"""STRICTPAIR Gate D pass 4: compact destructive-path audit.

Read-only. Resolves F8 labels through all addressing forms used by injected
code: RIP-relative target, immediate RVA/VA, and pointer-cell indirection.
Then maps each label to its backing R8D counter and every executable writer.
"""
from __future__ import annotations
import hashlib, struct, sys
from pathlib import Path
from capstone import Cs, CS_ARCH_X86, CS_MODE_64, CS_AC_WRITE
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_OP_REG, X86_REG_RIP, X86_REG_R8D

IMAGE_BASE=0x180000000; EXECUTE=0x20000000
TARGET_LABELS=["SOURCE QUEUE FULL","GENERATED BUSY DROPS","LATE MIDPOINT DROPS","PRESENT QUEUE DROPS","MAILBOX GENERATED DROPS","NO-FREE-JOB DROPS","PUBLISHER GENERATED COALESCED","REAL_ONLY"]
REFERENCE_LABELS=["SOURCE SUBMITS","SOURCE FPS"]

def u16(b,o):return struct.unpack_from('<H',b,o)[0]
def u32(b,o):return struct.unpack_from('<I',b,o)[0]
def u64(b,o):return struct.unpack_from('<Q',b,o)[0]

class PE:
 def __init__(self,b):
  self.b=b;p=u32(b,0x3c);coff=p+4;n=u16(b,coff+2);optsz=u16(b,coff+16);opt=coff+20
  if b[:2]!=b'MZ' or b[p:p+4]!=b'PE\0\0' or u16(b,coff)!=0x8664 or u16(b,opt)!=0x20b:raise RuntimeError('bad PE')
  self.base=u64(b,opt+24);sh=opt+optsz;self.sections=[]
  for i in range(n):
   o=sh+40*i;name=b[o:o+8].rstrip(b'\0').decode('ascii','replace');vs,va,rs,rp=struct.unpack_from('<IIII',b,o+8);ch=u32(b,o+36)
   self.sections.append(dict(name=name,vs=vs,va=va,rs=rs,rp=rp,exec=bool(ch&EXECUTE)))
 def sec_rva(self,r):
  for s in self.sections:
   if s['va']<=r<s['va']+max(s['vs'],s['rs']):return s
 def section(self,n):
  for s in self.sections:
   if s['name']==n:return s
 def off_to_rva(self,o):
  for s in self.sections:
   if s['rp']<=o<s['rp']+s['rs']:return s['va']+o-s['rp']

def pdata(pe):
 s=pe.section('.pdata');out=[]
 if not s:return out
 raw=pe.b[s['rp']:s['rp']+s['rs']]
 for o in range(0,len(raw)-11,12):
  a,b,u=struct.unpack_from('<III',raw,o)
  if a and a<b:out.append((a,b,u))
 return sorted(out)

def func_of(rs,r):
 for a,b,u in rs:
  if a<=r<b:return(a,b)

def decode(pe):
 md=Cs(CS_ARCH_X86,CS_MODE_64);md.detail=True;md.skipdata=True;by={};allins=[]
 for s in pe.sections:
  if not s['exec'] or not s['rs']:continue
  seq=list(md.disasm(pe.b[s['rp']:s['rp']+s['rs']],pe.base+s['va']));by[s['name']]=seq;allins+=seq
 return by,sorted(allins,key=lambda x:x.address)

def ops(ins):
 try:return ins.operands
 except:return []

def rip_refs(ins):
 out=[]
 for op in ops(ins):
  if op.type==X86_OP_MEM and op.mem.base==X86_REG_RIP:out.append((ins.address+ins.size+op.mem.disp,bool(op.access&CS_AC_WRITE)))
 return out

def direct_call(ins):
 o=ops(ins)
 return o[0].imm if ins.mnemonic=='call' and len(o)==1 and o[0].type==X86_OP_IMM else None

def seq_index(seq,a):
 for i,x in enumerate(seq):
  if x.address==a:return i

def fmt(pe,i):
 r=i.address-pe.base;s=pe.sec_rva(r);return f"{s['name'] if s else '?'}:{r:#x} {i.mnemonic} {i.op_str}"

def str_rvas(pe,t):
 n=t.encode('ascii');out=[];p=0
 while True:
  p=pe.b.find(n,p)
  if p<0:break
  r=pe.off_to_rva(p)
  if r is not None:out.append(r)
  p+=1
 return sorted(set(out))

def ptr_cells(pe,target_rva):
 out=set();patterns=[struct.pack('<Q',pe.base+target_rva),struct.pack('<I',target_rva)]
 for s in pe.sections:
  raw=pe.b[s['rp']:s['rp']+s['rs']]
  for pat in patterns:
   p=0
   while True:
    p=raw.find(pat,p)
    if p<0:break
    r=s['va']+p
    if r!=target_rva:out.add(r)
    p+=len(pat)
 return out

def instruction_label_refs(pe,allins,label_rva):
 """Return (instruction, mode) for direct or pointer-cell references."""
 target_va=pe.base+label_rva;cells=ptr_cells(pe,label_rva);cell_vas={pe.base+x for x in cells};out=[]
 for ins in allins:
  hit=None
  for va,wr in rip_refs(ins):
   if va==target_va:hit='RIP_DIRECT';break
   if va in cell_vas:hit='RIP_PTRCELL';break
  if hit is None:
   for op in ops(ins):
    if op.type==X86_OP_IMM and op.imm in (label_rva,target_va):hit='IMM_DIRECT';break
    if op.type==X86_OP_IMM and op.imm in cells.union(cell_vas):hit='IMM_PTRCELL';break
  if hit:out.append((ins,hit))
 return out

def infer_counter(pe,by,label_ins):
 r=label_ins.address-pe.base;s=pe.sec_rva(r)
 if not s or s['name'] not in by:return None
 seq=by[s['name']];i=seq_index(seq,label_ins.address)
 if i is None:return None
 for j in range(i-1,max(-1,i-18),-1):
  q=seq[j];o=ops(q)
  if q.mnemonic=='mov' and len(o)>=2 and o[0].type==X86_OP_REG and o[0].reg==X86_REG_R8D:
   rr=rip_refs(q)
   if rr:return rr[0][0]-pe.base
 return None

def is_writer(ins,cva):
 for va,w in rip_refs(ins):
  if va==cva and (w or ins.mnemonic in ('inc','dec','xadd','cmpxchg','or','and','xor','add','sub')):return True
 return False

def local_window(pe,by,ins,b=10,a=6):
 s=pe.sec_rva(ins.address-pe.base)
 if not s or s['name'] not in by:return[]
 seq=by[s['name']];i=seq_index(seq,ins.address)
 return[] if i is None else seq[max(0,i-b):min(len(seq),i+a+1)]

def predicates(pe,by,ins):
 w=local_window(pe,by,ins,18,1);return[q for q in w if q.mnemonic in('cmp','test','bt') or(q.mnemonic.startswith('j') and q.mnemonic!='jmp')][-8:]

def callers(pe,allins,fn):
 if not fn:return[]
 t=pe.base+fn[0];return[x for x in allins if direct_call(x)==t]

def main():
 if len(sys.argv)!=2:raise SystemExit('usage: script DLL')
 p=Path(sys.argv[1]);blob=p.read_bytes();pe=PE(blob);rs=pdata(pe);by,allins=decode(pe)
 if pe.base!=IMAGE_BASE:raise RuntimeError('image base')
 print('STRICTPAIR_GATE_D_COMPACT=4');print('SHA256='+hashlib.sha256(blob).hexdigest());print('SIZE='+str(len(blob)));print('EXEC_SECTIONS='+','.join(f'{k}:{len(v)}' for k,v in by.items()));print()
 mapping={}
 print('=== LABEL_REFERENCE_SANITY ===')
 for label in TARGET_LABELS+REFERENCE_LABELS:
  uses=[];srvas=str_rvas(pe,label)
  for sr in srvas:
   for ins,mode in instruction_label_refs(pe,allins,sr):
    c=infer_counter(pe,by,ins)
    uses.append((sr,ins,mode,c))
  # only diagnostic uses with an identifiable nearby R8D counter are mapping candidates
  m={}
  for x in uses:
   if x[3] is not None:m.setdefault(x[3],x)
  mapping[label]=list(m.values())
  modes=','.join(sorted(set(x[2] for x in uses))) if uses else 'NONE'
  print(f'LABEL={label} STRINGS={len(srvas)} INSN_REFS={len(uses)} MODES={modes} COUNTERS={len(m)}')
  for sr,ins,mode,c in uses[:16]:print(f'  USE={fmt(pe,ins)} MODE={mode} COUNTER={(hex(c) if c is not None else "NONE")} STRING_RVA={sr:#x}')
 print()
 print('=== LABEL_TO_COUNTER ===')
 for label in TARGET_LABELS+REFERENCE_LABELS:
  if not mapping[label]:print(f'LABEL={label} COUNTER=UNRESOLVED')
  for sr,ins,mode,c in mapping[label]:print(f'LABEL={label} COUNTER={c:#x} VIA={mode} LABEL_USE={ins.address-pe.base:#x}')
 print()
 unresolved=[];multi=[];remote=[]
 print('=== DESTRUCTIVE_WRITERS ===')
 for label in TARGET_LABELS:
  ms=mapping[label]
  if not ms:unresolved.append(label);print(f'PATH={label} RESULT=UNRESOLVED');continue
  if len(ms)>1:multi.append(label)
  for _,_,_,c in ms:
   ws=[i for i in allins if is_writer(i,pe.base+c)];print(f'PATH={label} COUNTER={c:#x} WRITERS={len(ws)}')
   for n,w in enumerate(ws,1):
    r=w.address-pe.base;fn=func_of(rs,r);ps=predicates(pe,by,w);cs=callers(pe,allins,fn)
    print(f'  WRITER#{n} {fmt(pe,w)} FUNC='+(f'{fn[0]:#x}-{fn[1]:#x}' if fn else 'NO_PDATA'))
    print('    PREDICATES='+(' | '.join(fmt(pe,q) for q in ps) if ps else 'NONE_LOCAL'))
    print('    DIRECT_CALLERS='+(' | '.join(fmt(pe,q) for q in cs[:12]) if cs else 'NONE'))
    print('    CONTEXT_BEGIN')
    for q in local_window(pe,by,w):print('      '+fmt(pe,q))
    print('    CONTEXT_END')
    if not ps:remote.append((label,c,r))
   print()
 print('=== SOURCE_ADMISSION_REFERENCE ===')
 for label in REFERENCE_LABELS:
  for _,_,_,c in mapping[label]:
   ws=[i for i in allins if is_writer(i,pe.base+c)];print(f'REF={label} COUNTER={c:#x} WRITERS={len(ws)}')
   for w in ws:
    r=w.address-pe.base;fn=func_of(rs,r);print('  '+fmt(pe,w)+' FUNC='+(f'{fn[0]:#x}-{fn[1]:#x}' if fn else 'NO_PDATA'))
 print();print('=== DECISION_INPUT ===');print('UNRESOLVED_LABELS='+(';'.join(unresolved) if unresolved else 'NONE'));print('MULTI_COUNTER_LABELS='+(';'.join(multi) if multi else 'NONE'));print('NO_LOCAL_PREDICATE_WRITERS='+(';'.join(f'{a}@{b:#x}/writer={c:#x}' for a,b,c in remote) if remote else 'NONE'));print('AUDIT_COMPLETE=1')
if __name__=='__main__':main()
