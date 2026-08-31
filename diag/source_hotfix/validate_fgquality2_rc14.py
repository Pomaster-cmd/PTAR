#!/usr/bin/env python3
from pathlib import Path
import hashlib,re,struct,subprocess,sys
ROOT=Path(__file__).resolve().parents[2]
RC14='3808fcb1681f00342b7ce07e39aebc2b1cabbed8aeb2fd244788744136d1868f'
RC13='2c8b77c540988c5671f604abeaf244c660e2ede11f5b74fb716c7e9a184affd2'
RC12='b253f0457f61a7c268ea3747864eb53e14ab113464f3c62f2a137da56bc18148'
OBJ='eff2a35f4325f69eb5a1e22c1a37129106cb566aa6a0d42448bcbe8dcbaaab42'
BUILD_ID='2026082925'
PE_CHECKSUM=0x4EABD
EXPECTED_EXPORTS={'D3D11CoreCreateDevice','D3D11CoreCreateLayeredDevice','D3D11CoreGetLayeredDeviceSize','D3D11CoreRegisterLayers','D3D11CreateDevice','D3D11CreateDeviceAndSwapChain','D3D11On12CreateDevice'}

def sha(p):
 h=hashlib.sha256()
 with open(p,'rb') as f:
  for c in iter(lambda:f.read(1<<20),b''):h.update(c)
 return h.hexdigest()
def txt(p):return p.read_text(encoding='utf-8',errors='strict').replace('\r\n','\n').replace('\r','\n')
def rel(p):return p.relative_to(ROOT).as_posix()
def pe_info(b):
 e=struct.unpack_from('<I',b,0x3c)[0]; machine,n=struct.unpack_from('<HH',b,e+4); optsz=struct.unpack_from('<H',b,e+20)[0];opt=e+24;sh=opt+optsz;secs={}
 for i in range(n):
  o=sh+i*40;name=b[o:o+8].split(b'\0')[0].decode('ascii');vs,va,rs,rp=struct.unpack_from('<IIII',b,o+8);secs[name]=(o,vs,va,rs,rp)
 return {'e':e,'machine':machine,'n':n,'opt':opt,'magic':struct.unpack_from('<H',b,opt)[0],'checksum':struct.unpack_from('<I',b,opt+64)[0],'subsystem':struct.unpack_from('<H',b,opt+68)[0],'subver':struct.unpack_from('<HH',b,opt+48),'imagebase':struct.unpack_from('<Q',b,opt+24)[0],'sizeimg':struct.unpack_from('<I',b,opt+56)[0],'secs':secs}
def rvaoff(b,rva):
 pi=pe_info(b)
 for _,vs,va,rs,rp in pi['secs'].values():
  if va<=rva<va+max(vs,rs):return rp+rva-va
 raise ValueError(hex(rva))
def checksum(blob,off):
 b=bytearray(blob);struct.pack_into('<I',b,off,0);s=0
 for i in range(0,len(b)-1,2):s+=b[i]|(b[i+1]<<8);s=(s&0xffff)+(s>>16)
 if len(b)&1:s+=b[-1]
 s=(s&0xffff)+(s>>16);s=(s&0xffff)+(s>>16)
 return (s+len(b))&0xffffffff
def call_target(b,rva):
 o=rvaoff(b,rva); assert b[o]==0xe8;disp=struct.unpack_from('<i',b,o+1)[0];return rva+5+disp

def main():
 checks=[]
 def ck(n,x,d=''):
  if not x:raise AssertionError(n+(' :: '+d if d else ''))
  checks.append(n)
 cur=ROOT/'win81_nis_dx11_x64.dll';base=ROOT/'diag/r/r066.dll';rc12=ROOT/'diag/r/r065.dll'
 ck('runtime exists',cur.is_file());ck('RC13 reference r066 exists',base.is_file());ck('RC12 reference r065 exists',rc12.is_file())
 ck('runtime SHA exact',sha(cur)==RC14,sha(cur));ck('RC13 ref exact',sha(base)==RC13,sha(base));ck('RC12 ref exact',sha(rc12)==RC12,sha(rc12))
 ck('quality object exact',sha(ROOT/'diag/source_hotfix/fgquality_profiles.obj')==OBJ,sha(ROOT/'diag/source_hotfix/fgquality_profiles.obj'))
 C=cur.read_bytes();B=base.read_bytes();pc=pe_info(C);pb=pe_info(B)
 ck('PE AMD64',pc['machine']==0x8664);ck('PE32+',pc['magic']==0x20b);ck('8 sections retained',pc['n']==8,str(pc['n']));ck('GUI subsystem',pc['subsystem']==2);ck('subsystem 6.0',pc['subver']==(6,0),repr(pc['subver']));ck('image base unchanged',pc['imagebase']==pb['imagebase']==0x180000000)
 ck('PE checksum field exact',pc['checksum']==PE_CHECKSUM,hex(pc['checksum']));ck('PE checksum recomputes',checksum(C,pc['opt']+64)==PE_CHECKSUM,hex(checksum(C,pc['opt']+64)))
 # Section preservation and bounded append.
 for nm in ['.text','.rdata','.data','.pdata','.reloc','.fgov']:
  ck(nm+' geometry unchanged',pc['secs'][nm][1:]==pb['secs'][nm][1:],repr((pc['secs'][nm],pb['secs'][nm])))
 ck('.fgdat geometry expected',pc['secs']['.fgdat'][1:]==(0xF0,0x34FC000,0x200,0x4B000),repr(pc['secs']['.fgdat']))
 ck('.fgdia geometry expected',pc['secs']['.fgdia'][1:]==(0x547,0x34FD000,0x600,0x4B200),repr(pc['secs']['.fgdia']))
 ck('SizeOfImage unchanged',pc['sizeimg']==pb['sizeimg'],hex(pc['sizeimg']))
 ck('no overlay after expanded .fgdia',len(C)==0x4B200+0x600,str(len(C)))
 # Frozen regions.
 def secbytes(b,pi,nm,n):
  rp=pi['secs'][nm][4];return b[rp:rp+n]
 ck('GATE1 .fgov byte-identical to RC13',secbytes(C,pc,'.fgov',0x200)==secbytes(B,pb,'.fgov',0x200))
 ck('legacy .fgdat 0..9B byte-identical',secbytes(C,pc,'.fgdat',0x9C)==secbytes(B,pb,'.fgdat',0x9C))
 ck('SHARELEGACY3/RESIZEGUARD1 .fgdia first 0x200 byte-identical',secbytes(C,pc,'.fgdia',0x200)==secbytes(B,pb,'.fgdia',0x200))
 # Quality globals/key.
 drp=pc['secs']['.fgdat'][4]
 ck('new quality globals start zero',C[drp+0x9C:drp+0xB0]==b'\0'*0x14,C[drp+0x9C:drp+0xB0].hex())
 key=('FrameGenerationQuality\0').encode('utf-16le')
 ck('quality UTF16 key exact',C[drp+0xC0:drp+0xC0+len(key)]==key)
 ck('quality key occurs exactly once',C.count(key)==1,str(C.count(key)))
 # Hook targets.
 expected={0x2B46:0x34FD200,0x2C5D:0x34FD24D,0x220BD:0x34FD283,0x220CC:0x34FD2CC,0x12225:0x34FD2EF,0x12271:0x34FD3CF}
 for site,target in expected.items():ck('hook '+hex(site)+' target',call_target(C,site)==target,hex(call_target(C,site)))
 # NOP tails at bounded replacement sites.
 ck('parser hook tail NOP bounded',C[rvaoff(C,0x2B46)+5:rvaoff(C,0x2B46)+22]==b'\x90'*17)
 ck('guard hook tail NOP bounded',C[rvaoff(C,0x2C5D)+5:rvaoff(C,0x2C5D)+6]==b'\x90')
 ck('width hook tail NOP bounded',C[rvaoff(C,0x220BD)+5:rvaoff(C,0x220BD)+15]==b'\x90'*10)
 ck('height hook tail NOP bounded',C[rvaoff(C,0x220CC)+5:rvaoff(C,0x220CC)+15]==b'\x90'*10)
 ck('action0 hook tail NOP bounded',C[rvaoff(C,0x12225)+5:rvaoff(C,0x12225)+7]==b'\x90'*2)
 # Profile code/strings.
 for st in [b'P1FG7N QUALITY LEGACY - ME /3 - BLEND GUARD 65',b'P1FG7N QUALITY BALANCED - ME /3 - BLEND GUARD 50',b'P1FG7N QUALITY QUALITY - ME /2 - BLEND GUARD 35',b'P1FG7N QUALITY CONSERVATIVE - ME /2 - BLEND GUARD 25',b'P1FG7N QUALITY ME TIER PENDING - CTRL+F6 OFF/ON TO APPLY FULL PROFILE']:
  ck('runtime string '+st.decode(),C.count(st)==1,str(C.count(st)))
 # Keycode evidence exact CTRL/F8, Shift/Alt rejection in wrapper bytes.
 q=C[rvaoff(C,0x34FD2EF):rvaoff(C,0x34FD3CF)]
 for nm,pat in [('VK_CONTROL',bytes.fromhex('b9 11 00 00 00')),('VK_F8',bytes.fromhex('b9 77 00 00 00')),('VK_SHIFT',bytes.fromhex('b9 10 00 00 00')),('VK_ALT',bytes.fromhex('b9 12 00 00 00'))]:ck(nm+' polled',pat in q)
 ck('four GetAsyncKeyState indirect calls',q.count(bytes.fromhex('ff 15'))>=4,str(q.count(bytes.fromhex('ff 15'))))
 # ABI imports/exports.
 objdump='/usr/local/swift/usr/bin/llvm-objdump'
 if not Path(objdump).exists():objdump='llvm-objdump'
 out=subprocess.check_output([objdump,'-p',str(cur)],text=True,errors='replace')
 dlls=re.findall(r'DLL Name:\s*(\S+)',out);ck('imports only KERNEL32 USER32',dlls==['KERNEL32.dll','USER32.dll'],repr(dlls))
 ex=set(re.findall(r'\b(D3D11(?:Core|On12|Create)[A-Za-z0-9_]*)\s*$',out,re.M));ck('seven D3D11 exports unchanged',ex==EXPECTED_EXPORTS,repr(sorted(ex)))
 # INI/package policy.
 ini=txt(ROOT/'win81_nis.ini');baseini=txt(ROOT/'_PTAR_UNINSTALL/reference/win81_nis.CADENCEFIX1_BASELINE.ini');ck('root INI equals uninstall baseline',ini==baseini)
 for nm,pat in [('quality default 2',r'(?m)^FrameGenerationQuality=2$'),('FG starts OFF',r'(?m)^FrameGeneration=0$'),('profile2 guard identity',r'(?m)^FrameGenerationBlendGuard=35$'),('profile2 ME identity',r'(?m)^FrameGenerationMEScalePercent=50$'),('target 60',r'(?m)^FrameGenerationTargetFPS=60$'),('SharedFlush 1',r'(?m)^FrameGenerationSharedFlush=1$'),('recorder profile3',r'(?m)^VideoRecordProfile=3$'),('status F8 preserved',r'(?m)^Status=F8$')]:ck(nm,re.search(pat,ini) is not None)
 ck('INI documents CTRL+F8', 'CTRL+F8' in ini)
 pid=txt(ROOT/'diag/PTAR_PACKAGE_ID.txt')
 for nm,t in [('RC14 identity','RC14_INPUTMAP2_FGREAL30_GATE1_SHARELEGACY3_RESIZEGUARD1_FGQUALITY2'),('runtime hash metadata','RUNTIME_DLL_SHA256='+RC14),('base RC13 metadata','BASE_RUNTIME_DLL_SHA256='+RC13),('quality key metadata','FG_QUALITY_INI_KEY=FrameGenerationQuality'),('profile0 metadata','FG_QUALITY_0=LEGACY_DIV3_GUARD65_SATGAT_STYLE'),('profile2 metadata','FG_QUALITY_2=QUALITY_DIV2_GUARD35_RC13_BASELINE'),('profile3 metadata','FG_QUALITY_3=CONSERVATIVE_DIV2_GUARD25'),('CTRL+F8 metadata','FG_QUALITY_HOTKEY=CTRL+F8'),('F8 preserved metadata','FG_QUALITY_F8_POLICY=F8_REMAINS_STATUS'),('no auto rebuild metadata','FG_QUALITY_CROSS_TIER_ACTIVE_POLICY=NO_AUTOMATIC_REBUILD_CTRL+F6_OFF_ON_REQUIRED'),('GATE1 metadata','FG_REAL_SOURCE_GATE=RUNTIME_ENABLED_AND_SCHEDULER_RUNNING'),('AutoVSync deferred','FG_AUTO_VSYNC_IMPLEMENTED=NO'),('temporal PTAR deferred','FG_TEMPORAL_PTAR_IMPLEMENTED=NO')]:ck(nm,t in pid)
 core=txt(ROOT/'tools/installer/PTAR_CORE_INSTALL.bat')
 for nm,t in [('quality default var','set "KEEP_FG_QUALITY=2"'),('quality parse old INI','FrameGenerationQuality=[0-3]'),('quality 0 guard','if "!KEEP_FG_QUALITY!"=="0" set "KEEP_FG_BLEND_GUARD=65"'),('quality 0 ME','if "!KEEP_FG_QUALITY!"=="0" set "KEEP_FG_ME_SCALE=33"'),('quality 1 guard','if "!KEEP_FG_QUALITY!"=="1" set "KEEP_FG_BLEND_GUARD=50"'),('quality 2 guard','if "!KEEP_FG_QUALITY!"=="2" set "KEEP_FG_BLEND_GUARD=35"'),('quality 3 guard','if "!KEEP_FG_QUALITY!"=="3" set "KEEP_FG_BLEND_GUARD=25"'),('quality INI write','echo FrameGenerationQuality=!KEEP_FG_QUALITY!'),('quality marker','FG_QUALITY_ENGINE=FGQUALITY2_SELECTABLE_PROFILE'),('CTRL+F8 marker','FG_QUALITY_HOTKEY=CTRL_F8_EXACT_SHIFT_ALT_REJECTED'),('GATE1 retained','FG_REAL_SOURCE_GATE=RUNTIME_ENABLED_AND_SCHEDULER_RUNNING'),('RESIZEGUARD retained','FG_RESIZE_GUARD=RESIZEGUARD1_LEGACY_ACTIVE_AND_SCHEDULER_ONLY'),('AutoVSync deferred core','FG_AUTO_VSYNC=NOT_IMPLEMENTED_RC14_FUTURE_CONTRACT_FG_ONLY')]:ck(nm,t in core)
 ver=txt(ROOT/'VERIFY_FULLSTACK1_INSTALL.bat');ck('verifier RC14 hash','set "EXPECTED_DLL='+RC14+'"' in ver);ck('verifier quality dynamic','FrameGenerationQuality=[0-3]' in ver);ck('verifier profile0 coherence','Profil 0 exige Guard65 / ME33' in ver);ck('verifier profile2 coherence','Profil 2 exige Guard35 / ME50' in ver);ck('verifier F8 contract','CTRL+F8 reserve a la qualite FG ; F8 reste statut.' in ver)
 col=txt(ROOT/'diag/05-COLLECT_RESULTS.bat');ck('collector RC14 identity','RC14_INPUTMAP2_FGREAL30_GATE1_SHARELEGACY3_RESIZEGUARD1_FGQUALITY2_COLLECT=PASS' in col);ck('collector GTX960M','FIELD_MACHINE_EXPECTED=GTX_960M_WINDOWS_8_1_FOR_RC14_FGQUALITY2_TEST' in col)
 proto=txt(ROOT/'diag/FGQUALITY2_HARDWARE_PROTOCOL.txt');ck('protocol starts profile2','FrameGenerationQuality=2' in proto);ck('protocol tests profile0','profile 0 LEGACY' in proto);ck('protocol no auto rebuild','No automatic FG teardown/rebuild' in proto);ck('protocol recorder off','Do NOT test recorder' in proto)
 # Non-destructive installer/test scripts.
 destructive=re.compile(r'(?im)(^|[^A-Za-z])(del|erase|rmdir|rd)\s|Remove-Item')
 for rr in ['01-INSTALL_FULLSTACK1.bat','tools/installer/PTAR_CORE_INSTALL.bat','VERIFY_FULLSTACK1_INSTALL.bat','diag/05-COLLECT_RESULTS.bat']:
  ck('no destructive command '+rr,destructive.search(txt(ROOT/rr)) is None)
 # Manifest/ownership if already generated.
 mf=ROOT/'_PTAR_UNINSTALL/PTAR_MANIFEST_SHA256.txt';own=ROOT/'_PTAR_UNINSTALL/PTAR_STATIC_OWNERSHIP.tsv'
 dirs_probe=ROOT/'_PTAR_UNINSTALL/PTAR_STATIC_DIRS.tsv'
 integrity_current = dirs_probe.exists() and ('PTAR_BUILD_ID\t'+BUILD_ID) in txt(dirs_probe)
 if mf.exists() and own.exists() and integrity_current:
  files=[p for p in ROOT.rglob('*') if p.is_file() and '__pycache__' not in p.parts and p.suffix!='.pyc']
  entries={}
  for line in txt(mf).splitlines():
   if line.strip():h,p=line.split('  ',1);entries[p]=h
  exp=sorted(rel(p) for p in files if rel(p) not in {'_PTAR_UNINSTALL/PTAR_MANIFEST_SHA256.txt','_PTAR_UNINSTALL/PTAR_STATIC_OWNERSHIP.tsv'})
  ck('manifest paths exact',sorted(entries)==exp,f'{len(entries)}/{len(exp)}');ck('manifest hashes exact',not [p for p,h in entries.items() if sha(ROOT/p)!=h])
  ls=txt(own).splitlines();ck('ownership namespace',ls[0]=='PTAR_OWNER_NAMESPACE\t8101');ck('ownership build id',ls[1]=='PTAR_BUILD_ID\t'+BUILD_ID,ls[1])
  rows=[]
  for line in ls[2:]:
   if line.strip():ident,p,h=line.split('|',2);rows.append((int(ident),p,h))
  exo=sorted(rel(p) for p in files if rel(p) not in {'_PTAR_UNINSTALL/PTAR_STATIC_OWNERSHIP.tsv','05-DESINSTALLER_PTAR.bat'})
  ck('ownership paths exact',sorted(p for _,p,_ in rows)==exo,f'{len(rows)}/{len(exo)}');ck('ownership IDs unique',len({i for i,_,_ in rows})==len(rows));ck('ownership hashes exact',not [p for _,p,h in rows if sha(ROOT/p)!=h])
  dirs=txt(ROOT/'_PTAR_UNINSTALL/PTAR_STATIC_DIRS.tsv').splitlines();ck('static dirs build id',dirs[1]=='PTAR_BUILD_ID\t'+BUILD_ID,dirs[1])
 print(f'RC14 FGQUALITY2 STATIC/PACKAGE VALIDATION: PASS {len(checks)}/{len(checks)}')
 for n in checks:print('[PASS]',n)
if __name__=='__main__':main()
