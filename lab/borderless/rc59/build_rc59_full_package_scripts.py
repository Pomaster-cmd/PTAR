from pathlib import Path
import hashlib, subprocess, sys, shutil

RC58=Path('lab/borderless/rc58/full_package_scripts')
OUT=Path('lab/borderless/rc59/full_package_scripts')
OLD='c04cbb21f87dd9a851f74b8334cbc6ba4c67274b4dc49f521b2f8523822cd633'
NEW='541c958c95f00d5e37dd1e5c852c8a0855e2ce806cf7686bbcdaa63c6ae6f97d'
RC51='59e699536998054ae38cc5a30e03375c9cd206220667debe87070962609dd26c'
RC56='aeee0a4ee9eafb2a7fc46373024db63b9ac4a63932b2455bd47b2db09d56c616'
RC57='c14f92add5cdfc7f05030dd3c3fd9a86b83ce8dac81042c1ab8b6e0237f05684'
EXPECTED_RC58={
 'install.ps1':'eb8cc4b145b40ff821fa6010b8aeeebd144034e54ecf90cdc33ae4e65825cef2',
 'verify.ps1':'f85bdfd3f6f6e6d84e53a99887b0dd3e41d1e8f178fbe9f040f1f0c5aa3df844',
 'rollback.ps1':'15166b28c9c3986a9c1ee5aa87d02f98ff6f88ff43ad86bb1b7460003d3012e3',
 'uninstall_overlay.ps1':'1e9180ece393bad2332a72ae5bc65b585a735ea09b51c008d44c20c407999f91',
 'collect_rc41.ps1':'c44246d1c6a89d4e93115917f03cccbe39964c5b3982b96aa41f57804724b614',
 'rc55_gui_migration.ps1':'10a87355e56a84af2ad2905d31313b0c17e64aa6169b2fd1084201f3e52c2a1b',
}
EXPECTED_RC59={
 'install.ps1':'f68c92194f287f9e05adf6d5bec67da742023f8a85ce932b4ff83d3b59fbbd5d',
 'verify.ps1':'077198bf402ad88f0a4c4d05621e4640a3ab9f41d65e0971a953281d21094c9e',
 'rollback.ps1':'378c9819e338a010ba372f20413c0cfc499f9f4aa54676c644e72aee0e39ec52',
 'uninstall_overlay.ps1':'9586eeb83d7c06c27f834ebd4c83dfe385418c79ba13e7700f65caebd9ec8c3a',
 'collect_rc41.ps1':'332776836a6f1c4b271652574f02187ae18b96f3f379c91ac60e140cb23ca290',
 'rc55_gui_migration.ps1':'10a87355e56a84af2ad2905d31313b0c17e64aa6169b2fd1084201f3e52c2a1b',
}

def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def req(s,old,new,label):
    if old not in s: raise RuntimeError('missing '+label)
    return s.replace(old,new,1)
def write(p,s):
    Path(p).parent.mkdir(parents=True,exist_ok=True)
    Path(p).write_text(s,encoding='utf-8',newline='\n')

# Rebuild the certified RC58 scripts from their committed generator.
subprocess.run([sys.executable,'lab/borderless/rc58/build_rc58_full_package_scripts.py'],check=True)
# Apply the already-certified RC58 V2 legacy carrier admission patch.
for name in ('install.ps1','rollback.ps1','uninstall_overlay.ps1'):
    p=RC58/name;s=p.read_text(encoding='utf-8')
    old=f" '{RC51}',\n '{OLD}'"
    new=f" '{RC51}',\n '{RC56}',\n '{RC57}',\n '{OLD}'"
    s=req(s,old,new,'RC58 V2 legacy carriers '+name)
    write(p,s)
for name,want in EXPECTED_RC58.items():
    got=sha(RC58/name)
    if got!=want: raise RuntimeError(f'RC58 certified source drift {name} {got} != {want}')

if OUT.exists(): shutil.rmtree(OUT)
OUT.mkdir(parents=True)

s=(RC58/'install.ps1').read_text(encoding='utf-8')
s=req(s,f"$ExpectedBorderless='{OLD}'",f"$ExpectedBorderless='{NEW}'",'install expected')
s=req(s,f" '{OLD}'\n\n)",f" '{OLD}',\n '{NEW}'\n\n)",'install known add')
s=s.replace('RC58','RC59')
s=s.replace("schema=9;package='PTAR_RC59_WINDOWED_BORDERLESS_STABLE'","schema=10;package='PTAR_RC59_FIELD_POPUP_RECOVERY'")
s=s.replace('MODE=RC59_WINDOWED_GAME_DIRECT_BORDERLESS_NATIVE_PRESENTER','MODE=RC59_POPUP_RECOVERY_WINDOWED_GAME_DIRECT_BORDERLESS_NATIVE_PRESENTER')
write(OUT/'install.ps1',s)

s=(RC58/'verify.ps1').read_text(encoding='utf-8')
s=req(s,f"$ExpectedBorderless='{OLD}'",f"$ExpectedBorderless='{NEW}'",'verify expected')
s=s.replace('RC58','RC59').replace('VERIFY_RC59_WINDOWED_BORDERLESS_STABLE=PASS','VERIFY_RC59_FIELD_POPUP_RECOVERY=PASS')
write(OUT/'verify.ps1',s)

s=(RC58/'rollback.ps1').read_text(encoding='utf-8')
s=req(s,f" '{OLD}'\n\n);if($bh",f" '{OLD}',\n '{NEW}'\n\n);if($bh",'rollback known add')
s=s.replace('RC58','RC59')
write(OUT/'rollback.ps1',s)

s=(RC58/'uninstall_overlay.ps1').read_text(encoding='utf-8')
s=req(s,f" '{OLD}'\n\n);$rasterExpected",f" '{OLD}',\n '{NEW}'\n\n);$rasterExpected",'uninstall known add')
old_dyn="$dyn=Join-Path $ov 'PTAR_RC41_INSTALL_LAST.log';$boot=if($gameRoot){Join-Path $gameRoot 'ptar_rc41_bootstrap.log'}else{$null};$rc58log=if($gameRoot){Join-Path $gameRoot 'ptar_borderless_rc58.log'}else{$null};if(Test-Path -LiteralPath $dyn -PathType Leaf){Remove-Item -LiteralPath $dyn -Force -ErrorAction SilentlyContinue};if($boot -and (Test-Path -LiteralPath $boot -PathType Leaf)){Remove-Item -LiteralPath $boot -Force -ErrorAction SilentlyContinue};if($rc58log -and (Test-Path -LiteralPath $rc58log -PathType Leaf)){Remove-Item -LiteralPath $rc58log -Force -ErrorAction SilentlyContinue}"
new_dyn="$dyn=Join-Path $ov 'PTAR_RC41_INSTALL_LAST.log';$boot=if($gameRoot){Join-Path $gameRoot 'ptar_rc41_bootstrap.log'}else{$null};$rc58log=if($gameRoot){Join-Path $gameRoot 'ptar_borderless_rc58.log'}else{$null};$rc59log=if($gameRoot){Join-Path $gameRoot 'ptar_borderless_rc59.log'}else{$null};if(Test-Path -LiteralPath $dyn -PathType Leaf){Remove-Item -LiteralPath $dyn -Force -ErrorAction SilentlyContinue};if($boot -and (Test-Path -LiteralPath $boot -PathType Leaf)){Remove-Item -LiteralPath $boot -Force -ErrorAction SilentlyContinue};if($rc58log -and (Test-Path -LiteralPath $rc58log -PathType Leaf)){Remove-Item -LiteralPath $rc58log -Force -ErrorAction SilentlyContinue};if($rc59log -and (Test-Path -LiteralPath $rc59log -PathType Leaf)){Remove-Item -LiteralPath $rc59log -Force -ErrorAction SilentlyContinue}"
s=req(s,old_dyn,new_dyn,'uninstall log cleanup').replace('additifs RC55','additifs RC59/RC55')
write(OUT/'uninstall_overlay.ps1',s)

s=(RC58/'collect_rc41.ps1').read_text(encoding='utf-8').replace('RC58','RC59').replace('rc58','rc59')
write(OUT/'collect_rc41.ps1',s)
shutil.copy2(RC58/'rc55_gui_migration.ps1',OUT/'rc55_gui_migration.ps1')

for name,want in EXPECTED_RC59.items():
    got=sha(OUT/name)
    if got!=want: raise RuntimeError(f'RC59 exact script drift {name} {got} != {want}')
    print(name,got)
print('RC59_FULL_PACKAGE_SCRIPT_BUILD=PASS')
