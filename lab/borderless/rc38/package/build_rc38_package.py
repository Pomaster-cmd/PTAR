from __future__ import annotations
import argparse,hashlib,json,re,shutil
from pathlib import Path
from rc38_bootstrap import patch_runtime
from package_scripts import OVERLAY,bat_scripts,ps_scripts
from validate_rc38_runtime import validate,BASE_RUNTIME_SHA256

BASE_COMMIT='009b8326d0c6f2d7869077ef621c712cca060479'
BASE_INI_SHA256='bd39b7c703ddf7a8658bed4df620232d00c06972351ca404c2d8386187fd3f50'
BASE_VERSION_SHA256='8c15a4ab74222f1efc7305315bf25dd06903f45bd568abdd3a04d152501e0c51'
EXPECTED_BASE_FILES=107

def sha_file(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def write_ascii(p,s):p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(s.replace('\n','\r\n').encode('ascii'))
def tracked(root):return sorted(p for p in root.rglob('*') if p.is_file())
def known_hashes(root,candidate):
    vals=re.findall(r"'([0-9a-fA-F]{64})'",(root/'diag/install.ps1').read_text(encoding='utf-8-sig'));out=[]
    for x in vals+[BASE_RUNTIME_SHA256,candidate]:
        x=x.lower()
        if x not in out:out.append(x)
    return out

def version_bytes(base,candidate,sidecar,info):
    anchor=b'DLL_SHA256='+BASE_RUNTIME_SHA256.encode()
    if base.count(anchor)!=1:raise AssertionError('version DLL hash anchor mismatch')
    x=base.replace(anchor,b'DLL_SHA256='+candidate.encode(),1);nl=b'\r\n' if b'\r\n' in x else b'\n'
    if not x.endswith((b'\r',b'\n')):x+=nl
    lines=['RC38_BORDERLESS_AUTHORITY=STABLE_RUNTIME_COMPOSITION','RC38_BASE_MAIN_COMMIT='+BASE_COMMIT,'RC38_BASE_RUNTIME_SHA256='+BASE_RUNTIME_SHA256,'RC38_RUNTIME_SHA256='+candidate,'RC38_RUNTIME_BOOTSTRAP_SECTION=.rc38','RC38_RUNTIME_BOOTSTRAP_RVA=0x%08X'%info['bootstrap_rva'],'RC38_RUNTIME_HOOK_RVA=0x%08X'%info['call_site_rva'],'RC38_SIDECAR_SHA256='+sidecar,'RC38_SIDECAR_API=PTAR_BorderlessAutoStart','RC38_GAME_HWND_RVA=0x02C3FAC0','RC38_PRESENTER_HWND_RVA=0x02C7DFE0','RC38_RENDER_SIZE_RVAS=0x02C3FB78,0x02C3FB7C','RC38_OUTPUT_SIZE_RVAS=0x0004B040,0x0004B044','RC38_PRESENTER_TOPMOST=FORBIDDEN','RC38_INPUT_VIRTUALIZATION=ENABLED','RC38_FAILURE_POLICY=FAIL_OPEN']
    return x+nl.join(s.encode('ascii') for s in lines)+nl

def main():
    p=argparse.ArgumentParser();p.add_argument('--base',required=True);p.add_argument('--sidecar',required=True);a=p.parse_args();root=Path(a.base).resolve();side=Path(a.sidecar).resolve()
    base_files=tracked(root)
    if len(base_files)!=EXPECTED_BASE_FILES:raise AssertionError(f'exact GitHub main must contain 107 files, got {len(base_files)}')
    base_hash={str(q.relative_to(root)).replace('\\','/'):sha_file(q) for q in base_files}
    for rel,exp in [('payload/d3d11.dll',BASE_RUNTIME_SHA256),('payload/win81_nis_dx11_x64.dll',BASE_RUNTIME_SHA256),('payload/win81_nis.ini',BASE_INI_SHA256),('payload/win81_nis_version.txt',BASE_VERSION_SHA256)]:
        if base_hash.get(rel)!=exp:raise AssertionError('base hash mismatch '+rel)
    if (root/'payload/d3d11.dll').read_bytes()!=(root/'payload/win81_nis_dx11_x64.dll').read_bytes():raise AssertionError('base runtime mirrors differ')
    ov=root/OVERLAY;pay=ov/'payload';pay.mkdir(parents=True,exist_ok=False)
    base_runtime=(root/'payload/d3d11.dll').read_bytes();cand,info=patch_runtime(base_runtime);runtime_sha=hashlib.sha256(cand).hexdigest();validation=validate(base_runtime,cand)
    side_bytes=side.read_bytes();side_sha=hashlib.sha256(side_bytes).hexdigest()
    if len(side_bytes)<100000:raise AssertionError('production sidecar unexpectedly small')
    (pay/'d3d11.dll').write_bytes(cand);(pay/'win81_nis_dx11_x64.dll').write_bytes(cand);shutil.copy2(root/'payload/win81_nis.ini',pay/'win81_nis.ini')
    ver=version_bytes((root/'payload/win81_nis_version.txt').read_bytes(),runtime_sha,side_sha,info);(pay/'win81_nis_version.txt').write_bytes(ver);version_sha=hashlib.sha256(ver).hexdigest();(pay/'ptar_borderless.dll').write_bytes(side_bytes)
    known=','.join("'"+x+"'" for x in known_hashes(root,runtime_sha));vals={'RUNTIME':runtime_sha,'INI':BASE_INI_SHA256,'VERSION':version_sha,'SIDECAR':side_sha,'BASE_RUNTIME':BASE_RUNTIME_SHA256,'BASE_INI':BASE_INI_SHA256,'BASE_VERSION':BASE_VERSION_SHA256,'KNOWN':known}
    for n,s in ps_scripts(vals).items():(ov/n).write_bytes(s.encode('ascii'))
    for n,s in bat_scripts().items():(root/n).write_bytes(s.encode('ascii'))
    readme=f'''PTAR RC38 BORDERLESS AUTHORITY - CANDIDAT COMPLET WINDOWS 8.1\n\nBASE IMMUTABLE\nGitHub main commit: {BASE_COMMIT}\n107/107 fichiers de la base canonique sont conserves byte-identiques dans ce pack.\nRuntime canonique SHA-256: {BASE_RUNTIME_SHA256}\n\nRC38\nRuntime candidat SHA-256: {runtime_sha}\nSidecar ptar_borderless.dll SHA-256: {side_sha}\nLe runtime conserve la production PTAR/NVENC/FG canonique et ajoute un bootstrap borne au safe-point de creation du presenter.\nLe sidecar impose la geometrie logique/rendu 1280x720 sur la fenetre jeu, maintient le presenter 1920x1080 natif, et virtualise la souris/capture vers la geometrie logique. Le presenter RC38 ne doit pas etre TOPMOST. Tout mismatch de layout runtime provoque un fail-open.\n\nUTILISATION\n1. Fermer Warhammer.\n2. Extraire ce pack dans le dossier du jeu ou un sous-dossier direct.\n3. Executer 00-INSTALL_BORDERLESS_AUTHORITY_RC38.bat.\n4. Executer 00-VERIFY_BORDERLESS_AUTHORITY_RC38.bat.\n5. Lancer le jeu et reproduire la scene RC35/RC37.\n\nROLLBACK\n00-ROLLBACK_BORDERLESS_AUTHORITY_RC38.bat restaure exactement les quatre fichiers de production GitHub main et retire le sidecar RC38 s'il est inchange.\n\nDESINSTALLATION\n00-DESINSTALLER_BORDERLESS_AUTHORITY_RC38.bat utilise le moteur canonique par ownership/SHA puis retire l'overlay RC38 par son propre registre SHA. Aucun fichier inconnu/modifie n'est supprime.\n''';write_ascii(ov/'README.txt',readme)
    (ov/'BASE_TRACKED_SHA256.txt').write_text(''.join(f'{h}  {rel}\n' for rel,h in sorted(base_hash.items())),encoding='ascii');(ov/'RC38_RUNTIME_VALIDATION.json').write_text(json.dumps(validation,indent=2,sort_keys=True)+'\n',encoding='ascii')
    delta={'schema':1,'candidate':'PTAR_RC38_BORDERLESS_AUTHORITY','base_commit':BASE_COMMIT,'base_file_count':107,'base_runtime_sha256':BASE_RUNTIME_SHA256,'candidate_runtime_sha256':runtime_sha,'candidate_ini_sha256':BASE_INI_SHA256,'candidate_version_sha256':version_sha,'sidecar_sha256':side_sha,'base_files_modified':[],'additive_files_only':True,'runtime_bootstrap':info,'runtime_validation':validation,'canonical_tools_preserved':['01-INSTALL_GW16.bat','02-VERIFY_INSTALL.bat','03-ARM_VISIBLE_FRAME_VERIFIER.bat','04-COLLECT_RESULTS.bat','04-TEST_QSV_5S_MP4.bat','05-ROLLBACK_TEST.bat','06-DESINSTALLER_PTAR_COMPLET.bat','_PTAR_UNINSTALL/PTAR_SAFE_UNINSTALL.ps1']};(ov/'DELTA_MANIFEST.json').write_text(json.dumps(delta,indent=2,sort_keys=True)+'\n',encoding='ascii')
    for rel,h in base_hash.items():
        if sha_file(root/rel)!=h:raise AssertionError('BASE MODIFIED: '+rel)
    own=[]
    for q in tracked(root):
        rel=str(q.relative_to(root)).replace('\\','/')
        if rel in base_hash or rel==f'{OVERLAY}/OVERLAY_OWNERSHIP.tsv' or rel=='00-DESINSTALLER_BORDERLESS_AUTHORITY_RC38.bat':continue
        own.append(rel)
    lines=['PTAR_RC38_OVERLAY_OWNERSHIP_SCHEMA|1']+[f'{83800000+i}|{rel}|{sha_file(root/rel)}' for i,rel in enumerate(sorted(own),1)];(ov/'OVERLAY_OWNERSHIP.tsv').write_text('\r\n'.join(lines)+'\r\n',encoding='ascii')
    for rel,h in base_hash.items():
        if sha_file(root/rel)!=h:raise AssertionError('BASE MODIFIED FINAL: '+rel)
    summary={'base_commit':BASE_COMMIT,'base_files':len(base_hash),'base_identity':'PASS','candidate_runtime_sha256':runtime_sha,'candidate_runtime_size':len(cand),'candidate_ini_sha256':BASE_INI_SHA256,'candidate_version_sha256':version_sha,'sidecar_sha256':side_sha,'sidecar_size':len(side_bytes),'overlay_owned_files':len(own),'runtime_validation':'PASS'};(ov/'BUILD_SUMMARY.json').write_text(json.dumps(summary,indent=2,sort_keys=True)+'\n',encoding='ascii');print(json.dumps(summary,sort_keys=True))
if __name__=='__main__':main()
