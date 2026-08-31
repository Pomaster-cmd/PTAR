#!/usr/bin/env python3
from pathlib import Path
import hashlib,re,struct,subprocess
ROOT=Path(__file__).resolve().parents[2]
RUNTIME='b253f0457f61a7c268ea3747864eb53e14ab113464f3c62f2a137da56bc18148'
RC9='9d84bb1978a8463df2498455b180125f9aa6a9a2f6ee390cab1382cec44f1d59'
RC10='d8faa86cfc4ddf7d8baefd75634c799c99248804a89340ab19ef3262e8e08ef8'
RC11='11fb4e484e79984641d72e2c793a2fa2b8eab871d50daf9cb03bfd77ed535dff'
INI='34304f320f007aa417677c5c0239c85997ef71761af1dfa7c827b8d0f148ee98'
OBJ='c9f80bf496e583baad220f2d0782220c860da4cfee9f8d3db0d0da19279fbbb6'
BUILD_ID='2026082923'

def sha(p):
 h=hashlib.sha256()
 with open(p,'rb') as f:
  for c in iter(lambda:f.read(1<<20),b''): h.update(c)
 return h.hexdigest()
def rel(p):return p.relative_to(ROOT).as_posix()
def main():
 checks=[]
 def ck(n,x,d=''):
  if not x: raise AssertionError(n+(' :: '+d if d else ''))
  checks.append(n)
 files=[p for p in ROOT.rglob('*') if p.is_file() and '__pycache__' not in p.parts and p.suffix!='.pyc']
 ck('runtime hash',sha(ROOT/'win81_nis_dx11_x64.dll')==RUNTIME,sha(ROOT/'win81_nis_dx11_x64.dll'))
 ck('INI unchanged',sha(ROOT/'win81_nis.ini')==INI)
 ck('RC9 safe base exact',sha(ROOT/'diag/r/r062.dll')==RC9)
 ck('RC10 rejected ref exact',sha(ROOT/'diag/r/r063.dll')==RC10)
 ck('RC11 rejected GTX960M ref exact',sha(ROOT/'diag/r/r064.dll')==RC11)
 ck('final object exact',sha(ROOT/'diag/source_hotfix/shared_transport_legacy3_resizeguard.obj')==OBJ)
 # PE contract.
 out=subprocess.check_output(['objdump','-p',str(ROOT/'win81_nis_dx11_x64.dll')],text=True,errors='replace')
 dlls=re.findall(r'DLL Name:\s*(\S+)',out);ck('imports only KERNEL32 USER32',dlls==['KERNEL32.dll','USER32.dll'],repr(dlls))
 ex=set(re.findall(r'\]\s+\+base\[\s*\d+\]\s+\S+\s+(D3D11\w+)',out))
 expected={'D3D11CoreCreateDevice','D3D11CoreCreateLayeredDevice','D3D11CoreGetLayeredDeviceSize','D3D11CoreRegisterLayers','D3D11CreateDevice','D3D11CreateDeviceAndSwapChain','D3D11On12CreateDevice'}
 ck('seven D3D11 exports',ex==expected,repr(sorted(ex)))
 b=(ROOT/'win81_nis_dx11_x64.dll').read_bytes();e=struct.unpack_from('<I',b,0x3c)[0];machine,n=struct.unpack_from('<HH',b,e+4);opt=e+24
 ck('PE AMD64',machine==0x8664);ck('PE sections 8',n==8,str(n));ck('PE32+',struct.unpack_from('<H',b,opt)[0]==0x20b);ck('GUI subsystem',struct.unpack_from('<H',b,opt+68)[0]==2);ck('subsystem 6.0',struct.unpack_from('<HH',b,opt+48)==(6,0))
 ini=(ROOT/'win81_nis.ini').read_text(encoding='utf-8',errors='replace')
 ck('FG startup off',re.search(r'(?mi)^FrameGeneration=0\s*$',ini)is not None)
 ck('FG target 60',re.search(r'(?mi)^FrameGenerationTargetFPS=60\s*$',ini)is not None)
 ck('SharedFlush 1',re.search(r'(?mi)^FrameGenerationSharedFlush=1\s*$',ini)is not None)
 ck('VideoRecordProfile 3',re.search(r'(?mi)^VideoRecordProfile=3\s*$',ini)is not None)
 pid=(ROOT/'diag/PTAR_PACKAGE_ID.txt').read_text(encoding='utf-8',errors='replace')
 for n,t in [
  ('RC12 identity','RC12_INPUTMAP2_FGREAL30_GATE1_SHARELEGACY3_RESIZEGUARD1'),
  ('runtime marker','RUNTIME_DLL_SHA256='+RUNTIME),
  ('RC9 direct base','BASE_RUNTIME_DLL_SHA256='+RC9),
  ('RC11 rejected marker','REJECTED_PREDECESSOR_RUNTIME_SHA256='+RC11),
  ('GTX960M field marker','FIELD_MACHINE=WINDOWS_8_1_X64_GTX_960M'),
  ('legacy marker','FG_LEGACY_RUNTIME_MARKER=LEGACY_ACTIVE_COPIED_AT_MAILBOX_STAGE'),
  ('resize guard marker','FG_RESIZE_GUARD=RESIZEGUARD1_LEGACY_ACTIVE_AND_SCHEDULER_ONLY'),
  ('keyed passthrough','FG_RESIZE_GUARD_KEYED_POLICY=PASS_THROUGH_ORIGINAL_RESIZEBUFFERS'),
  ('off passthrough','FG_RESIZE_GUARD_OFF_POLICY=PASS_THROUGH_ORIGINAL_RESIZEBUFFERS'),
  ('AutoVSync deferred','FG_AUTO_VSYNC_IMPLEMENTED=NO')]:ck(n,t in pid)
 core=(ROOT/'tools/installer/PTAR_CORE_INSTALL.bat').read_text(encoding='utf-8',errors='replace')
 for n,t in [('core hash','echo DLL_SHA256='+RUNTIME),('core GTX','FG_SHARED_FIELD_MACHINE=WINDOWS_8_1_X64_GTX_960M'),('core guard','FG_RESIZE_GUARD=RESIZEGUARD1_LEGACY_ACTIVE_AND_SCHEDULER_ONLY'),('core keyed','FG_RESIZE_GUARD_KEYED_POLICY=ORIGINAL_RESIZEBUFFERS_PASS_THROUGH'),('core off','FG_RESIZE_GUARD_OFF_POLICY=ORIGINAL_RESIZEBUFFERS_PASS_THROUGH'),('core GATE1','FG_REAL_SOURCE_GATE=RUNTIME_ENABLED_AND_SCHEDULER_RUNNING'),('core AutoVSync deferred','FG_AUTO_VSYNC=NOT_IMPLEMENTED_RC12_FUTURE_CONTRACT_FG_ONLY')]:ck(n,t in core)
 ver=(ROOT/'VERIFY_FULLSTACK1_INSTALL.bat').read_text(encoding='utf-8',errors='replace')
 ck('verifier hash',f'set "EXPECTED_DLL={RUNTIME}"' in ver)
 ck('verifier guard','FG_RESIZE_GUARD=RESIZEGUARD1_LEGACY_ACTIVE_AND_SCHEDULER_ONLY' in ver)
 ck('verifier keyed','FG_RESIZE_GUARD_KEYED_POLICY=ORIGINAL_RESIZEBUFFERS_PASS_THROUGH' in ver)
 ck('verifier off','FG_RESIZE_GUARD_OFF_POLICY=ORIGINAL_RESIZEBUFFERS_PASS_THROUGH' in ver)
 col=(ROOT/'diag/05-COLLECT_RESULTS.bat').read_text(encoding='utf-8',errors='replace')
 ck('collector identity','RC12_INPUTMAP2_FGREAL30_GATE1_SHARELEGACY3_RESIZEGUARD1_COLLECT=PASS' in col)
 ck('collector GTX960M marker','FIELD_MACHINE_EXPECTED=GTX_960M_WINDOWS_8_1_FOR_RC12_TEST' in col)
 idx=(ROOT/'diag/r/INDEX.txt').read_text(encoding='utf-8',errors='replace')
 ck('RC11 indexed rejected','r064.dll' in idx and RC11 in idx and 'GTX 960M' in idx)
 fld=(ROOT/'diag/RC11_FIELD_FINDING_RESIZE_RACE_GTX960M.txt').read_text(encoding='utf-8',errors='replace')
 ck('field finding transport success','ME scheduler' in fld and 'GTX 960M' in fld and 'isolated display threads all started' in fld)
 sv=(ROOT/'diag/SHARELEGACY3_RESIZEGUARD1_STATIC_VALIDATION.txt').read_text(encoding='utf-8',errors='replace')
 ck('binary validator record','PASS 68/68' in sv)
 det=(ROOT/'diag/SHARELEGACY3_RESIZEGUARD1_DETERMINISM.txt').read_text(encoding='utf-8',errors='replace')
 ck('determinism record','Two fresh object builds were bit-identical.' in det and 'Two fresh RC12 patch runs' in det and OBJ in det)
 proto=(ROOT/'diag/SHARELEGACY3_RESIZEGUARD1_HARDWARE_PROTOCOL.txt').read_text(encoding='utf-8',errors='replace')
 ck('protocol GTX960M','GTX 960M' in proto);ck('protocol one attempt','Press CTRL+F6 once' in proto);ck('protocol recorder off','Do NOT test CTRL+F9 recorder' in proto)
 # Installer/verification must stay non-destructive.
 destructive=re.compile(r'(?im)(^|[^A-Za-z])(del|erase|rmdir|rd)\s|Remove-Item')
 for rr in ['01-INSTALL_FULLSTACK1.bat','tools/installer/PTAR_CORE_INSTALL.bat','VERIFY_FULLSTACK1_INSTALL.bat','diag/05-COLLECT_RESULTS.bat']:
  ck('no destructive command '+rr,destructive.search((ROOT/rr).read_text(encoding='utf-8',errors='replace'))is None)
 ck('uninstaller unchanged',sha(ROOT/'05-DESINSTALLER_PTAR.bat')=='4138f33c3edea77d9ce0c78fb931aa79ed147b2f07efbee45ab2d3584b7e050f')
 ck('safe uninstall unchanged',sha(ROOT/'_PTAR_UNINSTALL/PTAR_SAFE_UNINSTALL.ps1')=='4145d093d824f05edd315fe091053a8dfe7ee30a250db923964c17b7a84f7527')
 # Manifest integrity.
 mf=ROOT/'_PTAR_UNINSTALL/PTAR_MANIFEST_SHA256.txt';entries={}
 for line in mf.read_text(encoding='utf-8').splitlines():
  if line.strip(): h,p=line.split('  ',1);entries[p]=h
 expected_paths=sorted(rel(p) for p in files if rel(p) not in {'_PTAR_UNINSTALL/PTAR_MANIFEST_SHA256.txt','_PTAR_UNINSTALL/PTAR_STATIC_OWNERSHIP.tsv'})
 ck('manifest paths exact',sorted(entries)==expected_paths,f'{len(entries)}/{len(expected_paths)}')
 ck('manifest hashes exact',not [p for p,h in entries.items() if sha(ROOT/p)!=h])
 own=ROOT/'_PTAR_UNINSTALL/PTAR_STATIC_OWNERSHIP.tsv';ls=own.read_text(encoding='utf-8').splitlines();ck('ownership namespace',ls[0]=='PTAR_OWNER_NAMESPACE\t8101');ck('ownership build',ls[1]=='PTAR_BUILD_ID\t'+BUILD_ID,ls[1])
 rows=[]
 for line in ls[2:]:
  if line.strip(): i,p,h=line.split('|',2);rows.append((int(i),p,h))
 exo=sorted(rel(p) for p in files if rel(p) not in {'_PTAR_UNINSTALL/PTAR_STATIC_OWNERSHIP.tsv','05-DESINSTALLER_PTAR.bat'})
 ck('ownership paths exact',sorted(p for _,p,_ in rows)==exo,f'{len(rows)}/{len(exo)}')
 ck('ownership ids unique',len({i for i,_,_ in rows})==len(rows))
 ck('ownership hashes exact',not [p for _,p,h in rows if sha(ROOT/p)!=h])
 dirs=(ROOT/'_PTAR_UNINSTALL/PTAR_STATIC_DIRS.tsv').read_text(encoding='utf-8').splitlines();ck('static dirs build',dirs[1]=='PTAR_BUILD_ID\t'+BUILD_ID,dirs[1])
 print(f'RC12 PACKAGE STATIC VALIDATION: PASS {len(checks)}/{len(checks)}')
 for n in checks: print('[PASS]',n)
if __name__=='__main__':main()
