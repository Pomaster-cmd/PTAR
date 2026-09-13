#!/usr/bin/env python3
import argparse, hashlib, json, os, re, struct
from pathlib import Path

BASE_COMMIT='009b8326d0c6f2d7869077ef621c712cca060479'
BASE_RUNTIME='864c0ca8f24f22f6a3cd4c21a0e213f431f5268e69b04e3860c52fe72600fc3c'
BASE_INI='bd39b7c703ddf7a8658bed4df620232d00c06972351ca404c2d8386187fd3f50'
BASE_VERSION='8c15a4ab74222f1efc7305315bf25dd06903f45bd568abdd3a04d152501e0c51'
RC33B_RUNTIME='a903321a9504bd644f21468ff21877be86e03bf3a7056c92e41e30934b70fe05'
PATCHES=[(8637,bytes.fromhex('c7054582040000000000')),(8647,bytes.fromhex('c7053782040000000000'))]
NOP10=b'\x90'*10


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def wtext(p,s):
    Path(p).parent.mkdir(parents=True,exist_ok=True)
    Path(p).write_bytes(s.replace('\n','\r\n').encode('ascii'))

def pe_checksum(buf):
    b=bytearray(buf)
    pe=struct.unpack_from('<I',b,0x3c)[0]
    chk=pe+4+20+64
    b[chk:chk+4]=b'\0\0\0\0'
    s=0
    i=0
    while i+1<len(b):
        s += b[i] | (b[i+1]<<8)
        s=(s & 0xffff)+(s>>16)
        i+=2
    if i<len(b):
        s += b[i]
        s=(s & 0xffff)+(s>>16)
    s=(s & 0xffff)+(s>>16)
    s=(s+len(b)) & 0xffffffff
    return s,chk

def unique(seq):
    out=[]
    for x in seq:
        if x not in out: out.append(x)
    return out

def ps_array(vals):
    return '@(\n'+'\n'.join(" '"+x+"'," for x in vals[:-1])+('\n' if len(vals)>1 else '')+" '"+vals[-1]+"'\n)"

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--base',required=True); args=ap.parse_args()
    root=Path(args.base).resolve(); ov=root/'ROOT_AUTHORITY_RC34'; pay=ov/'payload'; pay.mkdir(parents=True,exist_ok=True)
    tracked=sorted([p for p in root.rglob('*') if p.is_file()])
    base_hash={str(p.relative_to(root)).replace('\\','/'):sha(p) for p in tracked}
    assert base_hash['payload/d3d11.dll']==BASE_RUNTIME
    assert base_hash['payload/win81_nis_dx11_x64.dll']==BASE_RUNTIME
    assert base_hash['payload/win81_nis.ini']==BASE_INI
    assert base_hash['payload/win81_nis_version.txt']==BASE_VERSION

    # Runtime: derive candidate only from exact GitHub main runtime; base tree itself remains untouched.
    src=bytearray((root/'payload/d3d11.dll').read_bytes())
    assert src==(root/'payload/win81_nis_dx11_x64.dll').read_bytes()
    for off,old in PATCHES:
        assert bytes(src[off:off+10])==old, (off,bytes(src[off:off+10]).hex())
        src[off:off+10]=NOP10
    calc,chkoff=pe_checksum(src)
    struct.pack_into('<I',src,chkoff,calc)
    (pay/'d3d11.dll').write_bytes(src); (pay/'win81_nis_dx11_x64.dll').write_bytes(src)
    runtime=hashlib.sha256(src).hexdigest()

    # Candidate config: exact base bytes + only the two dormant authority switches enabled.
    ini=(root/'payload/win81_nis.ini').read_bytes()
    assert ini.count(b'GraphicsCartographer=0')==1 and ini.count(b'RootAuthority=0')==1
    ini2=ini.replace(b'GraphicsCartographer=0',b'GraphicsCartographer=1').replace(b'RootAuthority=0',b'RootAuthority=1')
    (pay/'win81_nis.ini').write_bytes(ini2); inih=hashlib.sha256(ini2).hexdigest()

    ver=(root/'payload/win81_nis_version.txt').read_bytes()
    assert (b'DLL_SHA256='+BASE_RUNTIME.encode()) in ver
    ver2=ver.replace(b'DLL_SHA256='+BASE_RUNTIME.encode(),b'DLL_SHA256='+runtime.encode(),1)
    ver2=re.sub(br'(?m)^WIN81_NIS_VERSION=([^\r\n]+)',lambda m:b'WIN81_NIS_VERSION='+m.group(1)+b'-ROOTAUTH1',ver2,count=1)
    nl=b'\r\n' if b'\r\n' in ver2 else b'\n'
    if not ver2.endswith((b'\n',b'\r')): ver2+=nl
    ver2 += nl.join([
        b'ROOT_AUTHORITY_RC34_BASE_GITHUB_MAIN='+BASE_COMMIT.encode(),
        b'ROOT_AUTHORITY_RC34_RUNTIME_BASE_SHA256='+BASE_RUNTIME.encode(),
        b'ROOT_AUTHORITY_RC34_PATCH_FILE_OFFSET_1=8637',
        b'ROOT_AUTHORITY_RC34_PATCH_FILE_OFFSET_2=8647',
        b'ROOT_AUTHORITY_RC34_PATCH_BYTES_OLD_1=c7054582040000000000',
        b'ROOT_AUTHORITY_RC34_PATCH_BYTES_OLD_2=c7053782040000000000',
        b'ROOT_AUTHORITY_RC34_PATCH_BYTES_NEW=90909090909090909090',
        b'ROOT_AUTHORITY_RC34_CONFIG=GraphicsCartographer=1,RootAuthority=1',b''
    ])
    (pay/'win81_nis_version.txt').write_bytes(ver2); verh=hashlib.sha256(ver2).hexdigest()

    # Known runtime ancestry from canonical installer, plus the bad RC33B so the user can safely overwrite it.
    canon_install=(root/'diag/install.ps1').read_text(encoding='utf-8-sig')
    known=unique(re.findall(r"'([0-9a-f]{64})'",canon_install)+[RC33B_RUNTIME,runtime])
    known_ps=ps_array(known)

    install=r'''$ErrorActionPreference='Stop'
$PackRoot=Split-Path -Parent $PSScriptRoot
$Payload=Join-Path $PSScriptRoot 'payload'
$StateRoot=Join-Path $PackRoot '_PTAR_UNINSTALL\state'
$Log=Join-Path $PSScriptRoot 'PTAR_ROOTAUTH_RC34_INSTALL_LAST.log'
$ExpectedRuntime='__RUNTIME__'
$ExpectedIni='__INI__'
$ExpectedVersion='__VERSION__'
$KnownPtar=__KNOWN__
function Sha([string]$p){if(Test-Path -LiteralPath $p -PathType Leaf){return (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}return $null}
function L([string]$s){$x='['+(Get-Date -Format 'HH:mm:ss')+'] '+$s;Write-Host $x;Add-Content -LiteralPath $Log -Value $x -Encoding UTF8}
function F([string]$s,[int]$c=90){L ('FAIL: '+$s);exit $c}
function Resolve-GameRoot{if(Test-Path -LiteralPath (Join-Path $PackRoot 'Warhammer.exe') -PathType Leaf){return $PackRoot};$p=Split-Path -Parent $PackRoot;if(Test-Path -LiteralPath (Join-Path $p 'Warhammer.exe') -PathType Leaf){return $p};return $null}
Set-Content -LiteralPath $Log -Value ('START '+(Get-Date).ToString('o')) -Encoding UTF8
$g=Resolve-GameRoot;if(-not $g){F 'Warhammer.exe introuvable : placer le pack dans le dossier du jeu ou dans un sous-dossier direct.' 2}
if(Get-Process Warhammer -ErrorAction SilentlyContinue){F 'Fermer Warhammer avant installation.' 3}
$pr=Join-Path $Payload 'd3d11.dll';$pc=Join-Path $Payload 'win81_nis_dx11_x64.dll';$pi=Join-Path $Payload 'win81_nis.ini';$pv=Join-Path $Payload 'win81_nis_version.txt'
if((Sha $pr)-ne $ExpectedRuntime -or (Sha $pc)-ne $ExpectedRuntime -or (Sha $pi)-ne $ExpectedIni -or (Sha $pv)-ne $ExpectedVersion){F 'Payload ROOTAUTH RC34 hash mismatch.' 10}
$a=Join-Path $g 'd3d11.dll';$c=Join-Path $g 'win81_nis_dx11_x64.dll';$i=Join-Path $g 'win81_nis.ini';$v=Join-Path $g 'win81_nis_version.txt'
$PreexistingPtar=$false
foreach($p in @($a,$c)){if(Test-Path -LiteralPath $p -PathType Leaf){$h=Sha $p;if($KnownPtar -notcontains $h){F ('DLL locale inconnue, installation refusee : '+$p+' '+$h) 20};$PreexistingPtar=$true}}
New-Item -ItemType Directory -Path $StateRoot -Force|Out-Null
$state=Join-Path $StateRoot ('INSTALL_'+(Get-Date -Format 'yyyyMMdd_HHmmss_fff'));New-Item -ItemType Directory -Path $state -Force|Out-Null
$m=[ordered]@{schema=3;package='GW16H_SAFEPOINT11_FUSEDDETAIL1_ROOTAUTH1_RC34';game_root=$g;pack_root=$PackRoot;installed=@{};original=@{};windowstyle=@{};known_ptar=$KnownPtar}
foreach($r in @(@('d3d11.dll',$a,$ExpectedRuntime),@('win81_nis_dx11_x64.dll',$c,$ExpectedRuntime),@('win81_nis.ini',$i,$ExpectedIni),@('win81_nis_version.txt',$v,$ExpectedVersion))){$n=$r[0];$p=$r[1];$installSha=$r[2];$e=Test-Path -LiteralPath $p -PathType Leaf;$x=[ordered]@{exists=$e;sha=$null;backup=$null;ptar=$false};if($e){$x.sha=Sha $p;if(($n -eq 'd3d11.dll') -or ($n -eq 'win81_nis_dx11_x64.dll')){$x.ptar=($KnownPtar -contains $x.sha)}else{$x.ptar=$PreexistingPtar};$bk=Join-Path $state ('original_'+$n);Copy-Item -LiteralPath $p -Destination $bk -Force;if((Sha $bk)-ne $x.sha){F ('Backup incoherent '+$n) 21};$x.backup=$bk};$m.original[$n]=$x;$m.installed[$n]=$installSha}
$key='HKCU:\Software\NeoCore Games\Warhammer Martyr\Options';$m.windowstyle.key=$key;$m.windowstyle.exists=$false;$m.windowstyle.original=$null;$m.windowstyle.applied=$false
if(Test-Path -LiteralPath $key){$r=Get-ItemProperty -LiteralPath $key;if($r.PSObject.Properties.Name -contains 'WindowStyle'){$m.windowstyle.exists=$true;$m.windowstyle.original=[int]$r.WindowStyle;if([int]$r.WindowStyle -ne 1){Set-ItemProperty -LiteralPath $key -Name WindowStyle -Value 1;$m.windowstyle.applied=$true}}}
$m|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $state 'install_state.json') -Encoding UTF8
Set-Content -LiteralPath (Join-Path $StateRoot 'LATEST_STATE.txt') -Value $state -Encoding UTF8
Set-Content -LiteralPath (Join-Path $PackRoot 'win81_nis_install_target.txt') -Value $g -Encoding ASCII
Copy-Item -LiteralPath $pr -Destination $a -Force;Copy-Item -LiteralPath $pc -Destination $c -Force;Copy-Item -LiteralPath $pi -Destination $i -Force;Copy-Item -LiteralPath $pv -Destination $v -Force
if((Sha $a)-ne $ExpectedRuntime -or (Sha $c)-ne $ExpectedRuntime -or (Sha $i)-ne $ExpectedIni -or (Sha $v)-ne $ExpectedVersion){F 'Post-install hash mismatch.' 30}
L 'INSTALL=PASS';L ('GAME_ROOT='+$g);L ('RUNTIME_SHA256='+$ExpectedRuntime);L 'BASE_GITHUB_MAIN=009b8326d0c6f2d7869077ef621c712cca060479';L 'MODE=SAFEPOINT11_FUSEDDETAIL1_PLUS_ROOTAUTH1_ONLY';exit 0
'''.replace('__RUNTIME__',runtime).replace('__INI__',inih).replace('__VERSION__',verh).replace('__KNOWN__',known_ps)
    wtext(ov/'install.ps1',install)

    verify=r'''$ErrorActionPreference='Stop'
$PackRoot=Split-Path -Parent $PSScriptRoot;$S=Join-Path $PackRoot '_PTAR_UNINSTALL\state'
function Sha([string]$p){if(Test-Path -LiteralPath $p -PathType Leaf){return (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}return $null}
$l=Join-Path $S 'LATEST_STATE.txt';if(-not(Test-Path -LiteralPath $l -PathType Leaf)){Write-Host '[FAIL] Etat installation absent';exit 2}
$s=(Get-Content -LiteralPath $l -TotalCount 1).Trim();$m=Get-Content -LiteralPath (Join-Path $s 'install_state.json') -Raw|ConvertFrom-Json;$g=[string]$m.game_root;$bad=0
foreach($r in @(@('d3d11.dll','__RUNTIME__'),@('win81_nis_dx11_x64.dll','__RUNTIME__'),@('win81_nis.ini','__INI__'),@('win81_nis_version.txt','__VERSION__'))){$h=Sha (Join-Path $g $r[0]);if($h -eq $r[1]){Write-Host ('[PASS] '+$r[0])}else{$bad=1;Write-Host ('[FAIL] '+$r[0]+' '+$h)}}
$t=Get-Content -LiteralPath (Join-Path $g 'win81_nis.ini')
foreach($k in @('TargetExe=Warhammer.exe','Enabled=1','UniversalSpatialPresenter=1','PresenterExclusive=0','GraphicsCartographer=1','RootAuthority=1','Overlay=1','FrameGeneration=0','FrameGenerationPresentSync=1','FrameGenerationTargetFPS=60')){if($t -contains $k){Write-Host ('[PASS] '+$k)}else{$bad=1;Write-Host ('[FAIL] '+$k)}}
foreach($r in @(@('03-ARM_VISIBLE_FRAME_VERIFIER.bat','7c4bbfe64b02f8ef971449a95c98ce0dee73c302ac0747f9692d464a7ca7583e'),@('04-COLLECT_RESULTS.bat','f4beae44bbe4b2cf99da3c419005ffc8bb46faa477d8f9b4ab1549704d1d759f'),@('diag\collect.ps1','9c4877dc4cf739adad1d61cfa9014ae111968f08302f0a83c56283deac6e6cad'),@('06-DESINSTALLER_PTAR_COMPLET.bat','9063624508709f5104cd00c4745a3e78e06a431e2ec505a1bfe35d14a074295d'),@('_PTAR_UNINSTALL\PTAR_SAFE_UNINSTALL.ps1','6a142afbefc20d2ecd4b3846b97c1434db3b78f19e719a44de7fb736a5e400e6'))){$h=Sha (Join-Path $PackRoot $r[0]);if($h -eq $r[1]){Write-Host ('[PASS] BASE INTACT '+$r[0])}else{$bad=1;Write-Host ('[FAIL] BASE MODIFIEE '+$r[0]+' '+$h)}}
if($bad){exit 9}else{Write-Host 'VERIFY_ROOTAUTH_RC34=PASS';exit 0}
'''.replace('__RUNTIME__',runtime).replace('__INI__',inih).replace('__VERSION__',verh)
    wtext(ov/'verify.ps1',verify)

    rollback=r'''$ErrorActionPreference='Stop'
$PackRoot=Split-Path -Parent $PSScriptRoot;$S=Join-Path $PackRoot '_PTAR_UNINSTALL\state'
function Sha([string]$p){return (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}
$l=Join-Path $S 'LATEST_STATE.txt';if(-not(Test-Path -LiteralPath $l -PathType Leaf)){Write-Host '[FAIL] Etat installation absent';exit 2}
$s=(Get-Content -LiteralPath $l -TotalCount 1).Trim();$sp=Join-Path $s 'install_state.json';$m=Get-Content -LiteralPath $sp -Raw|ConvertFrom-Json;$g=[string]$m.game_root
if(Get-Process Warhammer -ErrorAction SilentlyContinue){Write-Host '[FAIL] Fermer Warhammer.';exit 3}
$base=Join-Path $PackRoot 'payload';$E=@{'d3d11.dll'='864c0ca8f24f22f6a3cd4c21a0e213f431f5268e69b04e3860c52fe72600fc3c';'win81_nis_dx11_x64.dll'='864c0ca8f24f22f6a3cd4c21a0e213f431f5268e69b04e3860c52fe72600fc3c';'win81_nis.ini'='bd39b7c703ddf7a8658bed4df620232d00c06972351ca404c2d8386187fd3f50';'win81_nis_version.txt'='8c15a4ab74222f1efc7305315bf25dd06903f45bd568abdd3a04d152501e0c51'}
foreach($n in $E.Keys){$src=Join-Path $base $n;if((Sha $src)-ne $E[$n]){Write-Host ('[FAIL] Base GitHub alteree: '+$n);exit 10};Copy-Item -LiteralPath $src -Destination (Join-Path $g $n) -Force;if((Sha (Join-Path $g $n))-ne $E[$n]){Write-Host ('[FAIL] Rollback '+$n);exit 11};$m.installed.$n=$E[$n]}
$m.package='GW16H_SAFEPOINT11_FUSEDDETAIL1_ROLLBACK_GITHUB_MAIN';$m|ConvertTo-Json -Depth 10|Set-Content -LiteralPath $sp -Encoding UTF8
Write-Host '[PASS] ROOT AUTHORITY retire; runtime/config GitHub main exacts restaures.';exit 0
'''
    wtext(ov/'rollback.ps1',rollback)

    cleanup=r'''param([Parameter(Mandatory=$true)][string]$Root,[Parameter(Mandatory=$true)][string]$Engine)
$ErrorActionPreference='Stop';$Root=[IO.Path]::GetFullPath($Root);$ov=Join-Path $Root 'ROOT_AUTHORITY_RC34';$own=Join-Path $ov 'OVERLAY_OWNERSHIP.tsv'
& $Engine -Root $Root;$rc=$LASTEXITCODE;if($rc -ne 0){exit $rc}
function Sha([string]$p){return (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}
if(Test-Path -LiteralPath $own -PathType Leaf){foreach($row in Get-Content -LiteralPath $own){if($row -notmatch '^\d+\|'){continue};$a=$row.Split('|');$p=Join-Path $Root ($a[1]-replace '/','\');if(Test-Path -LiteralPath $p -PathType Leaf){if((Sha $p)-eq $a[2]){Remove-Item -LiteralPath $p -Force}else{Write-Host ('[KEEP] Overlay modifie: '+$a[1])}}};Remove-Item -LiteralPath $own -Force -ErrorAction SilentlyContinue}
if(Test-Path -LiteralPath $ov -PathType Container){try{Remove-Item -LiteralPath $ov -Force -ErrorAction Stop}catch{}}
Write-Host '[PASS] Base PTAR + overlay ROOTAUTH RC34 desinstalles par ownership/SHA.';exit 0
'''
    wtext(ov/'uninstall_overlay.ps1',cleanup)

    bats={
      '00-INSTALL_ROOT_AUTHORITY_RC34.bat':r'''@echo off
setlocal EnableExtensions
cd /d "%~dp0"
title PTAR RC34 ROOT AUTHORITY - INSTALL
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0ROOT_AUTHORITY_RC34\install.ps1"
set "RC=%ERRORLEVEL%"
echo.
pause
exit /b %RC%
''',
      '00-VERIFY_ROOT_AUTHORITY_RC34.bat':r'''@echo off
setlocal EnableExtensions
cd /d "%~dp0"
title PTAR RC34 ROOT AUTHORITY - VERIFY
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0ROOT_AUTHORITY_RC34\verify.ps1"
set "RC=%ERRORLEVEL%"
echo.
pause
exit /b %RC%
''',
      '00-ROLLBACK_ROOT_AUTHORITY_RC34.bat':r'''@echo off
setlocal EnableExtensions
cd /d "%~dp0"
title PTAR RC34 ROOT AUTHORITY - ROLLBACK GITHUB MAIN
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0ROOT_AUTHORITY_RC34\rollback.ps1"
set "RC=%ERRORLEVEL%"
echo.
pause
exit /b %RC%
''',
      '00-DESINSTALLER_ROOT_AUTHORITY_RC34.bat':r'''@echo off
setlocal EnableExtensions
cd /d "%~dp0"
title PTAR RC34 ROOT AUTHORITY - DESINSTALLATION COMPLETE SECURISEE
for %%I in ("%~dp0.") do set "ROOT=%%~fI"
set "TMP=%TEMP%\PTAR_RC34_UNINSTALL_%RANDOM%_%RANDOM%"
mkdir "%TMP%" >nul 2>&1
copy /y "%ROOT%\_PTAR_UNINSTALL\PTAR_SAFE_UNINSTALL.ps1" "%TMP%\PTAR_SAFE_UNINSTALL.ps1" >nul || exit /b 20
copy /y "%ROOT%\ROOT_AUTHORITY_RC34\uninstall_overlay.ps1" "%TMP%\uninstall_overlay.ps1" >nul || exit /b 21
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%TMP%\uninstall_overlay.ps1" -Root "%ROOT%" -Engine "%TMP%\PTAR_SAFE_UNINSTALL.ps1"
set "RC=%ERRORLEVEL%"
echo.
if "%RC%"=="0" (echo [PASS] Desinstallation PTAR + ROOTAUTH RC34 terminee.) else (echo [FAIL] Desinstallation interrompue - code %RC%.)
echo Appuyez sur une touche pour fermer.
pause >nul
if "%RC%"=="0" (start "" /b "%ComSpec%" /d /c "ping -n 3 127.0.0.1 >nul & del /f /q ^"%ROOT%\00-DESINSTALLER_ROOT_AUTHORITY_RC34.bat^" >nul 2>&1 & rd /s /q ^"%TMP%^" >nul 2>&1") else (rd /s /q "%TMP%" >nul 2>&1)
exit /b %RC%
'''
    }
    for n,s in bats.items(): wtext(root/n,s)

    readme=f'''PTAR RC34 ROOT AUTHORITY - ADDITIVE OVERLAY ON EXACT GITHUB MAIN\n\nBASE IMMUTABLE\nGitHub main commit: {BASE_COMMIT}\nBase runtime SHA-256: {BASE_RUNTIME}\nAll original GitHub tracked files are preserved byte-for-byte. No canonical collector, verifier, QSV helper, rollback or uninstaller file is replaced.\n\nROOT AUTHORITY CANDIDATE\nCandidate runtime SHA-256: {runtime}\nPE checksum: 0x{calc:08X}\nOnly runtime instructions changed versus GitHub main: file offsets 8637 and 8647, 10 bytes each, from the two startup zero-stores to 10 NOPs. PE checksum field is recomputed.\nCandidate config is exact GitHub main config except GraphicsCartographer=1 and RootAuthority=1.\n\nUSE\n1. Close Warhammer.\n2. Run 00-INSTALL_ROOT_AUTHORITY_RC34.bat.\n3. Optional: run 00-VERIFY_ROOT_AUTHORITY_RC34.bat.\n4. Launch the same scene.\n5. Existing GitHub tools remain available unchanged: 03-ARM_VISIBLE_FRAME_VERIFIER.bat, 04-COLLECT_RESULTS.bat, QSV tools, 05-ROLLBACK_TEST.bat and 06-DESINSTALLER_PTAR_COMPLET.bat.\n\nROLLBACK\n00-ROLLBACK_ROOT_AUTHORITY_RC34.bat restores the exact GitHub main payload.\n\nUNINSTALL\n00-DESINSTALLER_ROOT_AUTHORITY_RC34.bat uses the exact canonical PTAR_SAFE_UNINSTALL.ps1 engine, then removes only additive RC34 files whose SHA still matches the overlay ownership ledger.\n'''
    wtext(ov/'README.txt',readme)

    # Base integrity ledger (captured before creating overlay files).
    (ov/'BASE_TRACKED_SHA256.txt').write_text(''.join(f'{h}  {p}\n' for p,h in sorted(base_hash.items())),encoding='ascii')

    delta={
      'schema':1,'candidate':'PTAR_RC34_ROOT_AUTHORITY_ADDITIVE','base_commit':BASE_COMMIT,
      'base_runtime_sha256':BASE_RUNTIME,'candidate_runtime_sha256':runtime,'candidate_ini_sha256':inih,'candidate_version_sha256':verh,
      'base_files_modified':[],
      'additive_files_only':True,
      'runtime_derivation':{'source':'payload/d3d11.dll from exact base','patches':[{'file_offset':o,'old':old.hex(),'new':NOP10.hex()} for o,old in PATCHES],'pe_checksum_offset':chkoff,'pe_checksum':f'0x{calc:08X}'},
      'config_delta':['GraphicsCartographer=0 -> 1','RootAuthority=0 -> 1'],
      'canonical_tools_preserved':['03-ARM_VISIBLE_FRAME_VERIFIER.bat','04-COLLECT_RESULTS.bat','diag/collect.ps1','04-TEST_QSV_5S_MP4.bat','05-ROLLBACK_TEST.bat','06-DESINSTALLER_PTAR_COMPLET.bat','_PTAR_UNINSTALL/PTAR_SAFE_UNINSTALL.ps1']
    }
    (ov/'DELTA_MANIFEST.json').write_text(json.dumps(delta,indent=2,sort_keys=True)+'\n',encoding='ascii')

    # Validate candidate binary delta against exact base.
    base=(root/'payload/d3d11.dll').read_bytes(); cand=(pay/'d3d11.dll').read_bytes(); assert len(base)==len(cand)
    dif=[i for i,(a,b) in enumerate(zip(base,cand)) if a!=b]
    allowed=set(range(8637,8657))|set(range(chkoff,chkoff+4)); assert set(dif)<=allowed and all(i in dif for i in range(8637,8657))
    machine=struct.unpack_from('<H',cand,struct.unpack_from('<I',cand,0x3c)[0]+4)[0]; assert machine==0x8664
    opt=struct.unpack_from('<I',cand,0x3c)[0]+24; assert struct.unpack_from('<H',cand,opt)[0]==0x20b
    sub=struct.unpack_from('<HH',cand,opt+48); assert sub==(6,0)
    hdr=struct.unpack_from('<I',cand,chkoff)[0]; assert hdr==calc
    assert cand== (pay/'win81_nis_dx11_x64.dll').read_bytes()
    assert ini2.count(b'GraphicsCartographer=1')==1 and ini2.count(b'RootAuthority=1')==1

    # Prove all original base files stayed byte-identical.
    for rel,h in base_hash.items(): assert sha(root/rel)==h, 'BASE MODIFIED: '+rel

    validation=[
      'PTAR RC34 ROOT AUTHORITY ADDITIVE LAB VALIDATION','BASE_GITHUB_MAIN='+BASE_COMMIT,'BASE_RUNTIME_SHA256='+BASE_RUNTIME,
      'CANDIDATE_RUNTIME_SHA256='+runtime,'CANDIDATE_INI_SHA256='+inih,'CANDIDATE_VERSION_SHA256='+verh,
      'PE_CHECKSUM=0x%08X'%calc,'PATCH_OFFSETS=8637,8647','BASE_TRACKED_FILES_UNCHANGED=%d/%d PASS'%(len(base_hash),len(base_hash)),
      'CANONICAL_COLLECTION_PRESERVED=PASS','CANONICAL_UNINSTALL_ENGINE_PRESERVED=PASS','CANONICAL_QSV_AND_VISIBLE_VERIFIER_PRESERVED=PASS',
      'RUNTIME_DIFF_CONSTRAINED_TO_20_PATCH_BYTES_PLUS_PE_CHECKSUM=PASS','MIRROR_RUNTIME_IDENTICAL=PASS','PE_X64_PE32PLUS_SUBSYSTEM_6_0=PASS',
      'IMPORTANT=LAB VALIDATION DOES NOT SUBSTITUTE FOR WINDOWS_8_1_REAL_GAME_FIELD_TEST'
    ]
    wtext(ov/'LAB_VALIDATION.txt','\n'.join(validation)+'\n')

    # Overlay ownership: additive files only, excluding this registry and the running uninstall BAT (self-deleted by delayed cmd).
    candidates=[]
    for p in root.rglob('*'):
        if not p.is_file(): continue
        rel=str(p.relative_to(root)).replace('\\','/')
        if rel in base_hash or rel=='ROOT_AUTHORITY_RC34/OVERLAY_OWNERSHIP.tsv' or rel=='00-DESINSTALLER_ROOT_AUTHORITY_RC34.bat': continue
        candidates.append(rel)
    lines=['PTAR_ROOTAUTH_OVERLAY_OWNERSHIP_SCHEMA|1']
    for i,rel in enumerate(sorted(candidates),1): lines.append(f'{83400000+i}|{rel}|{sha(root/rel)}')
    (ov/'OVERLAY_OWNERSHIP.tsv').write_text('\r\n'.join(lines)+'\r\n',encoding='ascii')

    # Final base integrity recheck after ownership creation.
    for rel,h in base_hash.items(): assert sha(root/rel)==h, 'BASE MODIFIED FINAL: '+rel
    print(json.dumps({'runtime':runtime,'ini':inih,'version':verh,'checksum':f'0x{calc:08X}','base_files':len(base_hash),'overlay_owned':len(candidates)},sort_keys=True))

if __name__=='__main__': main()
