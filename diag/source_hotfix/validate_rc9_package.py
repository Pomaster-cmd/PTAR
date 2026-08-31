#!/usr/bin/env python3
from pathlib import Path
import hashlib, re, sys
ROOT=Path(__file__).resolve().parents[2]
RUNTIME='9d84bb1978a8463df2498455b180125f9aa6a9a2f6ee390cab1382cec44f1d59'
RC8='f26bbf5919ddeeb39937e5258d6c56f3196936e7d9a755099d55404693326d98'
INI='34304f320f007aa417677c5c0239c85997ef71761af1dfa7c827b8d0f148ee98'

def sha(p):
 h=hashlib.sha256()
 with open(p,'rb') as f:
  for c in iter(lambda:f.read(1<<20),b''): h.update(c)
 return h.hexdigest()
def rel(p): return p.relative_to(ROOT).as_posix()
def main():
 checks=[]
 def ck(n,v,d=''):
  checks.append(n)
  if not v: raise AssertionError(n+(' :: '+d if d else ''))
 files=[p for p in ROOT.rglob('*') if p.is_file()]
 ck('runtime hash',sha(ROOT/'win81_nis_dx11_x64.dll')==RUNTIME,sha(ROOT/'win81_nis_dx11_x64.dll'))
 ck('ini unchanged',sha(ROOT/'win81_nis.ini')==INI,sha(ROOT/'win81_nis.ini'))
 ck('RC8 upgrade reference exact',sha(ROOT/'diag/r/r061.dll')==RC8,sha(ROOT/'diag/r/r061.dll'))
 pid=(ROOT/'diag/PTAR_PACKAGE_ID.txt').read_text(encoding='utf-8',errors='replace')
 ck('package id RC9','RC9_INPUTMAP2_FGREAL30_GATE1_SHAREPROBE1' in pid)
 ck('package id runtime',f'RUNTIME_DLL_SHA256={RUNTIME}' in pid)
 ck('package id probe marker','SHAREPROBE1_STATUS_BASE=0xE3000000' in pid)
 ver=(ROOT/'VERIFY_FULLSTACK1_INSTALL.bat').read_text(encoding='utf-8',errors='replace')
 ck('verifier expected runtime',f'set "EXPECTED_DLL={RUNTIME}"' in ver)
 ck('verifier SHAREPROBE1 marker','FG_SHARED_TRANSPORT_DIAG=E_INVALIDARG_COMPATIBILITY_PROBE_MASK' in ver)
 core=(ROOT/'tools/installer/PTAR_CORE_INSTALL.bat').read_text(encoding='utf-8',errors='replace')
 ck('installer runtime marker',f'echo DLL_SHA256={RUNTIME}' in core)
 ck('installer SHAREPROBE1 marker','echo FG_SHARED_TRANSPORT_DIAG=E_INVALIDARG_COMPATIBILITY_PROBE_MASK' in core)
 ck('installer GATE1 marker','FG_REAL_SOURCE_GATE=RUNTIME_ENABLED_AND_SCHEDULER_RUNNING' in core)
 col=(ROOT/'diag/05-COLLECT_RESULTS.bat').read_text(encoding='utf-8',errors='replace')
 ck('collector RC9 marker','RC9_INPUTMAP2_FGREAL30_GATE1_SHAREPROBE1_COLLECT=PASS' in col)
 # Manifest: excludes itself and ownership only.
 mf=ROOT/'_PTAR_UNINSTALL/PTAR_MANIFEST_SHA256.txt'; entries={}
 for line in mf.read_text(encoding='utf-8').splitlines():
  h,p=line.split('  ',1); entries[p]=h
 expected=sorted(rel(p) for p in files if rel(p) not in {'_PTAR_UNINSTALL/PTAR_MANIFEST_SHA256.txt','_PTAR_UNINSTALL/PTAR_STATIC_OWNERSHIP.tsv'})
 ck('manifest entry count',len(entries)==len(expected),f'{len(entries)}/{len(expected)}')
 ck('manifest paths exact',sorted(entries)==expected)
 bad=[p for p,h in entries.items() if sha(ROOT/p)!=h]
 ck('manifest hashes exact',not bad,','.join(bad[:5]))
 # Ownership: excludes itself + self-deleting uninstaller, includes manifest.
 own=ROOT/'_PTAR_UNINSTALL/PTAR_STATIC_OWNERSHIP.tsv'; ls=own.read_text(encoding='utf-8').splitlines(); ck('ownership namespace',ls[0]=='PTAR_OWNER_NAMESPACE\t8101'); ck('ownership build id',ls[1]=='PTAR_BUILD_ID\t2026082920')
 rows=[]
 for line in ls[2:]:
  i,p,h=line.split('|',2); rows.append((int(i),p,h))
 exo=sorted(rel(p) for p in files if rel(p) not in {'_PTAR_UNINSTALL/PTAR_STATIC_OWNERSHIP.tsv','05-DESINSTALLER_PTAR.bat'})
 ck('ownership entry count',len(rows)==len(exo),f'{len(rows)}/{len(exo)}'); ck('ownership paths exact',sorted(p for _,p,_ in rows)==exo); ck('ownership ids unique',len({i for i,_,_ in rows})==len(rows))
 bad=[p for _,p,h in rows if sha(ROOT/p)!=h]; ck('ownership hashes exact',not bad,','.join(bad[:5]))
 ck('total file count',len(files)==135,str(len(files)))
 print(f'RC9 PACKAGE STATIC VALIDATION: PASS {len(checks)}/{len(checks)}')
 for n in checks: print('[PASS]',n)
if __name__=='__main__': main()
