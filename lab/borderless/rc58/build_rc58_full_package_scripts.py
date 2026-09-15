from pathlib import Path
import hashlib, shutil

BASE=Path('lab/raster/rc55/full_package_scripts')
OUT=Path('lab/borderless/rc58/full_package_scripts')
CARRIER='c04cbb21f87dd9a851f74b8334cbc6ba4c67274b4dc49f521b2f8523822cd633'
EXPECTED={
 'install.ps1':'baa4293dd15bd7ba4f9b631b24ac452de72c80f622fbb27e38ee6ffc451811b4',
 'verify.ps1':'f85bdfd3f6f6e6d84e53a99887b0dd3e41d1e8f178fbe9f040f1f0c5aa3df844',
 'rollback.ps1':'c561e3c15288ff3c2d243a78ca313a608a43ee9978b118d8ef6015b21168b0dd',
 'uninstall_overlay.ps1':'cc5be05be8b9902ccb3c8e4ca120810d8d89f3ae48f45e3b7d6bf1210eb4e15b',
 'collect_rc41.ps1':'c44246d1c6a89d4e93115917f03cccbe39964c5b3982b96aa41f57804724b614',
 'rc55_gui_migration.ps1':'10a87355e56a84af2ad2905d31313b0c17e64aa6169b2fd1084201f3e52c2a1b',
}

def read(name):
    return (BASE/name).read_text(encoding='utf-8-sig')
def write(name,text):
    OUT.mkdir(parents=True,exist_ok=True)
    (OUT/name).write_text(text,encoding='utf-8',newline='\n')

def require_replace(s,old,new,label):
    if old not in s: raise RuntimeError('missing replacement '+label)
    return s.replace(old,new)

s=read('install.ps1')
s=require_replace(s,"$ExpectedBorderless='59e699536998054ae38cc5a30e03375c9cd206220667debe87070962609dd26c'",f"$ExpectedBorderless='{CARRIER}'",'install expected carrier')
s=require_replace(s," '59e699536998054ae38cc5a30e03375c9cd206220667debe87070962609dd26c'\n\n)",f" '59e699536998054ae38cc5a30e03375c9cd206220667debe87070962609dd26c',\n '{CARRIER}'\n\n)",'install known carrier')
s=require_replace(s,"F 'Payload RC55 hash mismatch.' 10","F 'Payload RC58 hash mismatch.' 10",'install payload message')
s=require_replace(s,"schema=8;package='PTAR_RC55_BOUND_PHYSICAL_IDEMPOTENCE'","schema=9;package='PTAR_RC58_WINDOWED_BORDERLESS_STABLE'",'install state')
s=s.replace("F 'Module migration GUI RC55 absent/modifie.'","F 'Module migration GUI RC58 absent/modifie.'")
s=s.replace("F ('Migration GUI RC55 echouee: ","F ('Migration GUI RC58 echouee: ")
s=require_replace(s,"L 'INSTALL_RC55=PASS';L ('GAME_ROOT='+$g);L ('RUNTIME_SHA256='+$ExpectedRuntime);L ('BORDERLESS_SHA256='+$ExpectedBorderless);L ('RC55_RASTER_SIDECAR_SHA256='+$ExpectedRaster);L 'BASE_GITHUB_MAIN=009b8326d0c6f2d7869077ef621c712cca060479';L 'BASE_FILES=107/107_PRESERVED';L 'MODE=RC55_RC51_PRESENTER_RC52_QUARANTINE_BOUND_PHYSICAL_IDEMPOTENCE';L 'CONFIG=CANONICAL_GITHUB_MAIN_EXACT';exit 0","L 'INSTALL_RC58=PASS';L ('GAME_ROOT='+$g);L ('RUNTIME_SHA256='+$ExpectedRuntime);L ('RC58_BORDERLESS_SHA256='+$ExpectedBorderless);L ('RC55_RASTER_SIDECAR_SHA256='+$ExpectedRaster);L 'BASE_GITHUB_MAIN=009b8326d0c6f2d7869077ef621c712cca060479';L 'BASE_FILES=107/107_PRESERVED';L 'MODE=RC58_WINDOWED_GAME_DIRECT_BORDERLESS_NATIVE_PRESENTER';L 'WINDOWED=EXACT_1280x720_GAME_CLIENT';L 'BORDERLESS=NATIVE_1920x1080_USR_PRESENTER';L 'SWITCH=P1U46_F10_SAFE_PRESENT_BOUNDARY';L 'CONFIG=CANONICAL_GITHUB_MAIN_EXACT';exit 0",'install success')
write('install.ps1',s)

s=read('verify.ps1')
s=require_replace(s,"$ExpectedBorderless='59e699536998054ae38cc5a30e03375c9cd206220667debe87070962609dd26c'",f"$ExpectedBorderless='{CARRIER}'",'verify carrier')
s=s.replace('(RC55 ne l impose pas)','(RC58 ne l impose pas)')
s=require_replace(s,'VERIFY_RC55_BOUND_PHYSICAL_IDEMPOTENCE=PASS','VERIFY_RC58_WINDOWED_BORDERLESS_STABLE=PASS','verify success')
write('verify.ps1',s)

s=read('rollback.ps1')
s=require_replace(s," '59e699536998054ae38cc5a30e03375c9cd206220667debe87070962609dd26c'\n\n);if($bh",f" '59e699536998054ae38cc5a30e03375c9cd206220667debe87070962609dd26c',\n '{CARRIER}'\n\n);if($bh",'rollback carrier')
s=s.replace('Module migration GUI RC55 absent/modifie.','Module migration GUI RC58 absent/modifie.')
s=s.replace("'[PASS] RC55 retire; payload GitHub main exact restaure; sidecars connus retires.'","'[PASS] RC58 retire; payload GitHub main exact restaure; sidecars connus retires.'")
write('rollback.ps1',s)

s=read('uninstall_overlay.ps1')
s=require_replace(s," '59e699536998054ae38cc5a30e03375c9cd206220667debe87070962609dd26c'\n\n);$rasterExpected",f" '59e699536998054ae38cc5a30e03375c9cd206220667debe87070962609dd26c',\n '{CARRIER}'\n\n);$rasterExpected",'uninstall carrier')
s=require_replace(s,"$dyn=Join-Path $ov 'PTAR_RC41_INSTALL_LAST.log';$boot=if($gameRoot){Join-Path $gameRoot 'ptar_rc41_bootstrap.log'}else{$null};if(Test-Path -LiteralPath $dyn -PathType Leaf){Remove-Item -LiteralPath $dyn -Force -ErrorAction SilentlyContinue};if($boot -and (Test-Path -LiteralPath $boot -PathType Leaf)){Remove-Item -LiteralPath $boot -Force -ErrorAction SilentlyContinue}","$dyn=Join-Path $ov 'PTAR_RC41_INSTALL_LAST.log';$boot=if($gameRoot){Join-Path $gameRoot 'ptar_rc41_bootstrap.log'}else{$null};$rc58log=if($gameRoot){Join-Path $gameRoot 'ptar_borderless_rc58.log'}else{$null};if(Test-Path -LiteralPath $dyn -PathType Leaf){Remove-Item -LiteralPath $dyn -Force -ErrorAction SilentlyContinue};if($boot -and (Test-Path -LiteralPath $boot -PathType Leaf)){Remove-Item -LiteralPath $boot -Force -ErrorAction SilentlyContinue};if($rc58log -and (Test-Path -LiteralPath $rc58log -PathType Leaf)){Remove-Item -LiteralPath $rc58log -Force -ErrorAction SilentlyContinue}",'uninstall dynamic log')
write('uninstall_overlay.ps1',s)

s=read('collect_rc41.ps1')
s=s.replace('PTAR_RC55_DIAG_','PTAR_RC58_DIAG_')
s=s.replace("'RC55_DIAGNOSTIC=1'","'RC58_DIAGNOSTIC=1'")
s=s.replace("'RC55_BORDERLESS_SHA256='","'RC58_BORDERLESS_SHA256='")
s=s.replace("'NOTE_BORDERLESS_LOG=ptar_borderless_rc38.log name retained by validated RC40 controller'","'NOTE_RC58_LOG=ptar_borderless_rc58.log; legacy rc38 log is also collected when present'")
s=require_replace(s,"@((Join-Path $g 'ptar_borderless_rc38.log'),'ptar_borderless_rc38.log'),@((Join-Path $g 'ptar_rc41.log')","@((Join-Path $g 'ptar_borderless_rc58.log'),'ptar_borderless_rc58.log'),@((Join-Path $g 'ptar_borderless_rc38.log'),'ptar_borderless_rc38.log'),@((Join-Path $g 'ptar_rc41.log')",'collector rc58 log')
s=s.replace("@($tmp,'RC55_ACTIVE_PAYLOAD_SHA256.txt')","@($tmp,'RC58_ACTIVE_PAYLOAD_SHA256.txt')")
s=require_replace(s,"foreach($spec in @(@((Join-Path $g 'ptar_borderless_rc38.log'),'RC55_BORDERLESS_LOG_ABSENT.txt'),@((Join-Path $g 'ptar_rc41.log'),'RC55_RASTER_LOG_ABSENT.txt'),@((Join-Path $g 'ptar_rc41_bootstrap.log'),'RC55_BOOTSTRAP_LOG_ABSENT.txt'))","foreach($spec in @(@((Join-Path $g 'ptar_borderless_rc58.log'),'RC58_BORDERLESS_LOG_ABSENT.txt'),@((Join-Path $g 'ptar_rc41.log'),'RC58_RASTER_LOG_ABSENT.txt'),@((Join-Path $g 'ptar_rc41_bootstrap.log'),'RC58_BOOTSTRAP_LOG_ABSENT.txt'))",'collector absence')
s=s.replace('PTAR_RC55_RESULTS_','PTAR_RC58_RESULTS_')
s=s.replace('RESULTAT_RC55=','RESULTAT_RC58=')
s=s.replace('journaux RC55 raster/bootstrap ajoutes','journaux RC58 borderless + RC55 raster/bootstrap ajoutes')
write('collect_rc41.ps1',s)

write('rc55_gui_migration.ps1',read('rc55_gui_migration.ps1'))

for name,expected in EXPECTED.items():
    p=OUT/name
    got=hashlib.sha256(p.read_bytes()).hexdigest()
    if got!=expected: raise RuntimeError(f'{name} hash drift {got} != {expected}')
    print(f'{name} {got}')

install=(OUT/'install.ps1').read_text()
assert CARRIER in install and 'INSTALL_RC58=PASS' in install and "package='PTAR_RC58_WINDOWED_BORDERLESS_STABLE'" in install
assert 'AffectGuiResolution -Type DWord -Value 1' not in install
assert CARRIER in (OUT/'rollback.ps1').read_text()
assert CARRIER in (OUT/'uninstall_overlay.ps1').read_text()
assert 'ptar_borderless_rc58.log' in (OUT/'collect_rc41.ps1').read_text()
print('RC58_FULL_PACKAGE_SCRIPT_BUILD=PASS')
