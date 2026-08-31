#!/usr/bin/env python3
from pathlib import Path
import hashlib,re,struct,subprocess,sys
ROOT=Path(__file__).resolve().parents[2]
RUNTIME='11fb4e484e79984641d72e2c793a2fa2b8eab871d50daf9cb03bfd77ed535dff'
RC10='d8faa86cfc4ddf7d8baefd75634c799c99248804a89340ab19ef3262e8e08ef8'
RC9='9d84bb1978a8463df2498455b180125f9aa6a9a2f6ee390cab1382cec44f1d59'
INI='34304f320f007aa417677c5c0239c85997ef71761af1dfa7c827b8d0f148ee98'
OBJ='176f6906bea2d81b6dab01ecf4d50f5bb48bff249ce4a7ee82d60bd93674b41d'
EXPECTED_FILE_COUNT=157
BUILD_ID='2026082922'

def sha(p):
 h=hashlib.sha256()
 with open(p,'rb') as f:
  for c in iter(lambda:f.read(1<<20),b''):h.update(c)
 return h.hexdigest()
def rel(p):return p.relative_to(ROOT).as_posix()
def parse_pe(p):
 b=p.read_bytes();e=struct.unpack_from('<I',b,0x3c)[0];assert b[e:e+4]==b'PE\0\0';machine,n=struct.unpack_from('<HH',b,e+4);opt=e+24;magic=struct.unpack_from('<H',b,opt)[0];subsys=struct.unpack_from('<H',b,opt+68)[0];maj=struct.unpack_from('<H',b,opt+48)[0];minr=struct.unpack_from('<H',b,opt+50)[0]
 return machine,n,magic,subsys,maj,minr

def main():
 checks=[]
 def ck(name,ok,detail=''):
  checks.append(name)
  if not ok:raise AssertionError(name+(' :: '+detail if detail else ''))
 files=[p for p in ROOT.rglob('*') if p.is_file() and '__pycache__' not in p.parts and p.suffix!='.pyc']
 ck('total file count',len(files)==EXPECTED_FILE_COUNT,f'{len(files)}/{EXPECTED_FILE_COUNT}')
 ck('no pycache',not any('__pycache__' in p.parts or p.suffix=='.pyc' for p in ROOT.rglob('*') if p.is_file()))
 ck('runtime hash',sha(ROOT/'win81_nis_dx11_x64.dll')==RUNTIME,sha(ROOT/'win81_nis_dx11_x64.dll'))
 ck('INI byte-identical baseline',sha(ROOT/'win81_nis.ini')==INI,sha(ROOT/'win81_nis.ini'))
 ck('RC9 safe base reference exact',sha(ROOT/'diag/r/r062.dll')==RC9,sha(ROOT/'diag/r/r062.dll'))
 ck('RC10 rejected reference exact',sha(ROOT/'diag/r/r063.dll')==RC10,sha(ROOT/'diag/r/r063.dll'))
 ck('SHARELEGACY2 object exact',sha(ROOT/'diag/source_hotfix/shared_transport_legacy2_fallback.obj')==OBJ,sha(ROOT/'diag/source_hotfix/shared_transport_legacy2_fallback.obj'))
 machine,n,magic,subsys,maj,minr=parse_pe(ROOT/'win81_nis_dx11_x64.dll')
 ck('PE AMD64',machine==0x8664,hex(machine));ck('PE32+',magic==0x20b,hex(magic));ck('PE section count unchanged',n==8,str(n));ck('Windows GUI subsystem',subsys==2,str(subsys));ck('subsystem version 6.0',maj==6 and minr==0,f'{maj}.{minr}')
 # Imports/exports via objdump when available.
 try:
  out=subprocess.check_output(['objdump','-p',str(ROOT/'win81_nis_dx11_x64.dll')],text=True,errors='replace')
  dlls=re.findall(r'DLL Name:\s*(\S+)',out)
  ck('imports only KERNEL32 USER32',dlls==['KERNEL32.dll','USER32.dll'],repr(dlls))
  exports=set(re.findall(r'\]\s+\+base\[\s*\d+\]\s+\S+\s+(D3D11\w+)',out))
  expected={'D3D11CoreCreateDevice','D3D11CoreCreateLayeredDevice','D3D11CoreGetLayeredDeviceSize','D3D11CoreRegisterLayers','D3D11CreateDevice','D3D11CreateDeviceAndSwapChain','D3D11On12CreateDevice'}
  ck('seven D3D11 exports unchanged',exports==expected,repr(sorted(exports)))
 except Exception as e:raise AssertionError('objdump PE audit :: '+repr(e))
 ini=(ROOT/'win81_nis.ini').read_text(encoding='utf-8',errors='replace')
 ck('FG startup OFF',re.search(r'(?mi)^FrameGeneration=0\s*$',ini)is not None)
 ck('FG target 60 retained',re.search(r'(?mi)^FrameGenerationTargetFPS=60\s*$',ini)is not None)
 ck('SharedFlush enabled',re.search(r'(?mi)^FrameGenerationSharedFlush=1\s*$',ini)is not None)
 ck('VideoRecordProfile=3 retained',re.search(r'(?mi)^VideoRecordProfile=3\s*$',ini)is not None)
 pid=(ROOT/'diag/PTAR_PACKAGE_ID.txt').read_text(encoding='utf-8',errors='replace')
 ck('package RC11 identity','RC11_INPUTMAP2_FGREAL30_GATE1_SHARELEGACY2' in pid)
 ck('package runtime hash',f'RUNTIME_DLL_SHA256={RUNTIME}' in pid)
 ck('binary base explicitly RC9',f'BASE_RUNTIME_DLL_SHA256={RC9}' in pid)
 ck('RC10 explicitly rejected',f'REJECTED_PREDECESSOR_RUNTIME_SHA256={RC10}' in pid and 'REJECTED_PREDECESSOR=RC10_SHARELEGACY1' in pid)
 ck('end-to-end scope marker','FG_SHARED_FALLBACK_SCOPE=GAME_WORKER_TRANSPORT_PLUS_DISPLAY_MAILBOX_CONSTRUCTION' in pid)
 ck('primary every activation policy','FG_SHARED_SATGAT_POLICY=KEYED_PRIMARY_FIRST_EVERY_ACTIVATION_NO_FALLBACK_ON_SUCCESS' in pid)
 ck('Auto-VSync deferred','FG_AUTO_VSYNC_IMPLEMENTED=NO' in pid)
 ver=(ROOT/'VERIFY_FULLSTACK1_INSTALL.bat').read_text(encoding='utf-8',errors='replace')
 ck('verifier expected runtime',f'set "EXPECTED_DLL={RUNTIME}"' in ver)
 ck('verifier end-to-end marker','FG_SHARED_TRANSPORT=KEYED_PRIMARY_LEGACY_SHARED_END_TO_END_FALLBACK' in ver)
 ck('verifier transport+mailbox scope','FG_SHARED_TRANSPORT_SCOPE=GAME_WORKER_TRANSPORT_PLUS_DISPLAY_MAILBOX' in ver)
 ck('verifier reset contract','FG_SHARED_TRANSPORT_RESET=MAILBOX_RETURN_PLUS_PRIMARY_PRECLEAR' in ver)
 ck('verifier every-activation keyed policy','FG_SHARED_TRANSPORT_SATGAT_POLICY=KEYED_PRIMARY_FIRST_EVERY_ACTIVATION_NO_FALLBACK_ON_SUCCESS' in ver)
 ck('verifier SharedFlush','FrameGenerationSharedFlush=1' in ver)
 core=(ROOT/'tools/installer/PTAR_CORE_INSTALL.bat').read_text(encoding='utf-8',errors='replace')
 ck('installer runtime marker',f'echo DLL_SHA256={RUNTIME}' in core)
 ck('installer end-to-end marker','echo FG_SHARED_TRANSPORT=KEYED_PRIMARY_LEGACY_SHARED_END_TO_END_FALLBACK' in core)
 ck('installer bounded trigger','echo FG_SHARED_TRANSPORT_FALLBACK=E_INVALIDARG_EMPTY_TRANSPORT_SHAREDFLUSH_THROUGH_MAILBOX' in core)
 ck('installer scope marker','echo FG_SHARED_TRANSPORT_SCOPE=GAME_WORKER_TRANSPORT_PLUS_DISPLAY_MAILBOX' in core)
 ck('installer reset marker','echo FG_SHARED_TRANSPORT_RESET=MAILBOX_RETURN_PLUS_PRIMARY_PRECLEAR' in core)
 ck('installer SatGat policy','echo FG_SHARED_TRANSPORT_SATGAT_POLICY=KEYED_PRIMARY_FIRST_EVERY_ACTIVATION_NO_FALLBACK_ON_SUCCESS' in core)
 ck('installer GATE1 marker','FG_REAL_SOURCE_GATE=RUNTIME_ENABLED_AND_SCHEDULER_RUNNING' in core)
 ck('installer Auto-VSync deferred','FG_AUTO_VSYNC=NOT_IMPLEMENTED_RC11_FUTURE_CONTRACT_FG_ONLY' in core)
 col=(ROOT/'diag/05-COLLECT_RESULTS.bat').read_text(encoding='utf-8',errors='replace')
 ck('collector RC11 identity','RC11_INPUTMAP2_FGREAL30_GATE1_SHARELEGACY2_COLLECT=PASS' in col)
 idx=(ROOT/'diag/r/INDEX.txt').read_text(encoding='utf-8',errors='replace')
 ck('RC9 reference indexed','diag/r/r062.dll' in idx and RC9 in idx)
 ck('RC10 rejected reference indexed','diag/r/r063.dll' in idx and RC10 in idx and 'REJECTED RC10' in idx)
 ck('RC10 historical implementation retained',(ROOT/'diag/source_hotfix/shared_transport_legacy_fallback.s').is_file() and (ROOT/'diag/SHARELEGACY1_STATIC_VALIDATION.txt').is_file())
 sv=(ROOT/'diag/SHARELEGACY2_STATIC_VALIDATION.txt').read_text(encoding='utf-8',errors='replace')
 ck('SHARELEGACY2 static validation recorded','SHARELEGACY2 STATIC VALIDATION: PASS 64/64' in sv)
 det=(ROOT/'diag/SHARELEGACY2_DETERMINISM.txt').read_text(encoding='utf-8',errors='replace')
 ck('object determinism recorded','Two fresh object builds: bit-identical.' in det and OBJ in det)
 proto=(ROOT/'diag/SHARELEGACY2_HARDWARE_PROTOCOL.txt').read_text(encoding='utf-8',errors='replace')
 ck('protocol single activation','One activation attempt is sufficient' in proto)
 ck('protocol manual VSync only','Auto-VSync is not implemented yet' in proto)
 # No new destructive behavior in installation/verification paths. The dedicated
 # uninstaller is intentionally destructive and must remain byte-identical to RC10.
 destructive=re.compile(r'(?im)(^|[^A-Za-z])(del|erase|rmdir|rd)\s|Remove-Item')
 for rr in ['01-INSTALL_FULLSTACK1.bat','tools/installer/PTAR_CORE_INSTALL.bat','VERIFY_FULLSTACK1_INSTALL.bat','diag/05-COLLECT_RESULTS.bat']:
  txt=(ROOT/rr).read_text(encoding='utf-8',errors='replace')
  ck('no destructive command '+rr,destructive.search(txt)is None)
 ck('top-level uninstaller unchanged',sha(ROOT/'05-DESINSTALLER_PTAR.bat')=='4138f33c3edea77d9ce0c78fb931aa79ed147b2f07efbee45ab2d3584b7e050f')
 ck('safe uninstall engine unchanged',sha(ROOT/'_PTAR_UNINSTALL/PTAR_SAFE_UNINSTALL.ps1')=='4145d093d824f05edd315fe091053a8dfe7ee30a250db923964c17b7a84f7527')
 # Manifest excludes itself and ownership only.
 mf=ROOT/'_PTAR_UNINSTALL/PTAR_MANIFEST_SHA256.txt';entries={}
 for line in mf.read_text(encoding='utf-8').splitlines():
  if not line.strip():continue
  h,p=line.split('  ',1);entries[p]=h
 expected=sorted(rel(p) for p in files if rel(p) not in {'_PTAR_UNINSTALL/PTAR_MANIFEST_SHA256.txt','_PTAR_UNINSTALL/PTAR_STATIC_OWNERSHIP.tsv'})
 ck('manifest entry count',len(entries)==len(expected),f'{len(entries)}/{len(expected)}');ck('manifest paths exact',sorted(entries)==expected)
 bad=[p for p,h in entries.items() if sha(ROOT/p)!=h];ck('manifest hashes exact',not bad,','.join(bad[:5]))
 # Ownership excludes itself + self-deleting top-level uninstaller, includes manifest.
 own=ROOT/'_PTAR_UNINSTALL/PTAR_STATIC_OWNERSHIP.tsv';ls=own.read_text(encoding='utf-8').splitlines();ck('ownership namespace',ls[0]=='PTAR_OWNER_NAMESPACE\t8101');ck('ownership build id',ls[1]==f'PTAR_BUILD_ID\t{BUILD_ID}',ls[1])
 rows=[]
 for line in ls[2:]:
  if not line.strip():continue
  i,p,h=line.split('|',2);rows.append((int(i),p,h))
 exo=sorted(rel(p) for p in files if rel(p) not in {'_PTAR_UNINSTALL/PTAR_STATIC_OWNERSHIP.tsv','05-DESINSTALLER_PTAR.bat'})
 ck('ownership entry count',len(rows)==len(exo),f'{len(rows)}/{len(exo)}');ck('ownership paths exact',sorted(p for _,p,_ in rows)==exo);ck('ownership IDs unique',len({i for i,_,_ in rows})==len(rows))
 bad=[p for _,p,h in rows if sha(ROOT/p)!=h];ck('ownership hashes exact',not bad,','.join(bad[:5]))
 print(f'RC11 PACKAGE STATIC VALIDATION: PASS {len(checks)}/{len(checks)}')
 for n in checks:print('[PASS]',n)
if __name__=='__main__':main()
