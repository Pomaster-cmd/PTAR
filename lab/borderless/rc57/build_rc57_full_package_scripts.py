from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[3]
SRC=ROOT/'lab/raster/rc55/full_package_scripts'
OUT=Path(sys.argv[1]) if len(sys.argv)>1 else ROOT/'lab/borderless/rc57/full_package_scripts'
RC56='aeee0a4ee9eafb2a7fc46373024db63b9ac4a63932b2455bd47b2db09d56c616'
RC57='c14f92add5cdfc7f05030dd3c3fd9a86b83ce8dac81042c1ab8b6e0237f05684'


def rd(name): return (SRC/name).read_text(encoding='utf-8-sig').replace('\r\n','\n').replace('\r','\n')
def wr(name,text):
    OUT.mkdir(parents=True,exist_ok=True)
    data='\ufeff'+text.replace('\r\n','\n').replace('\r','\n').replace('\n','\r\n')
    (OUT/name).write_bytes(data.encode('utf-8'))
def one(t,o,n,label):
    c=t.count(o)
    if c!=1: raise RuntimeError(f'{label}: expected 1 got {c}')
    return t.replace(o,n,1)

# install
x=rd('install.ps1')
x=one(x,"$ExpectedBorderless='59e699536998054ae38cc5a30e03375c9cd206220667debe87070962609dd26c'",f"$ExpectedBorderless='{RC57}'",'install expected')
x=one(x," '59e699536998054ae38cc5a30e03375c9cd206220667debe87070962609dd26c'\n\n)",f" '59e699536998054ae38cc5a30e03375c9cd206220667debe87070962609dd26c',\n '{RC56}',\n '{RC57}'\n\n)",'install known')
x=one(x,"F 'Payload RC55 hash mismatch.' 10","F 'Payload RC57 hash mismatch.' 10",'install payload')
x=one(x,"package='PTAR_RC55_BOUND_PHYSICAL_IDEMPOTENCE'","package='PTAR_RC57_BORDERLESS_STARTUP_HANDSHAKE'",'install package')
x=one(x,"F ('Migration GUI RC55 echouee: ","F ('Migration GUI RC57 echouee: ",'install migration')
x=one(x,"L 'INSTALL_RC55=PASS'","L 'INSTALL_RC57=PASS'",'install pass')
x=one(x,"L 'MODE=RC55_RC51_PRESENTER_RC52_QUARANTINE_BOUND_PHYSICAL_IDEMPOTENCE'","L 'MODE=RC57_STARTUP_HANDSHAKE_RC56_PRESENTER_ONLY_RC55_RASTER'",'install mode')
x=one(x,"L 'CONFIG=CANONICAL_GITHUB_MAIN_EXACT';exit 0","L 'RC57_INITIAL_BORDERLESS=DEFERRED_TO_WINDOWED_BOOT';L 'RC57_UNLOCK=EXPLICIT_WINDOWSTYLE_0_THEN_1';L 'CONFIG=CANONICAL_GITHUB_MAIN_EXACT';exit 0",'install policy')
wr('install.ps1',x)

# verify
x=rd('verify.ps1')
x=one(x,"$ExpectedBorderless='59e699536998054ae38cc5a30e03375c9cd206220667debe87070962609dd26c'",f"$ExpectedBorderless='{RC57}'",'verify expected')
x=one(x,"(RC55 ne l impose pas)","(RC57 ne l impose pas)",'verify gui')
x=one(x,"VERIFY_RC55_BOUND_PHYSICAL_IDEMPOTENCE=PASS","VERIFY_RC57_STARTUP_HANDSHAKE=PASS",'verify marker')
needle="$s=(Get-Content -LiteralPath $l -TotalCount 1).Trim();$m=Get-Content -LiteralPath (Join-Path $s 'install_state.json') -Raw|ConvertFrom-Json;$g=[string]$m.game_root;$bad=0"
x=one(x,needle,needle+"\nif(([string]$m.package) -ne 'PTAR_RC57_BORDERLESS_STARTUP_HANDSHAKE'){$bad=1;Write-Host ('[FAIL] Etat package inattendu: '+[string]$m.package)}else{Write-Host '[PASS] Etat package RC57'}",'verify state')
needle="if($bad){exit 9}else{Write-Host 'VERIFY_RC57_STARTUP_HANDSHAKE=PASS';exit 0}"
x=one(x,needle,"Write-Host '[INFO] RC57: si WindowStyle=1 au demarrage, Borderless est differe; selectionner explicitement Fenetre puis Borderless pour deverrouiller.'\n"+needle,'verify policy')
wr('verify.ps1',x)

# rollback
x=rd('rollback.ps1')
x=one(x," '59e699536998054ae38cc5a30e03375c9cd206220667debe87070962609dd26c'\n\n)",f" '59e699536998054ae38cc5a30e03375c9cd206220667debe87070962609dd26c',\n '{RC56}',\n '{RC57}'\n\n)",'rollback known')
x=one(x,"[PASS] RC55 retire;","[PASS] RC57 retire;",'rollback pass')
wr('rollback.ps1',x)

# uninstall
x=rd('uninstall_overlay.ps1')
x=one(x," '59e699536998054ae38cc5a30e03375c9cd206220667debe87070962609dd26c'\n\n);",f" '59e699536998054ae38cc5a30e03375c9cd206220667debe87070962609dd26c',\n '{RC56}',\n '{RC57}'\n\n);",'uninstall known')
x=one(x,"additifs RC55 nettoyes","additifs RC57 nettoyes",'uninstall pass')
wr('uninstall_overlay.ps1',x)

# collector
x=rd('collect_rc41.ps1')
x=x.replace('PTAR_RC55_DIAG_','PTAR_RC57_DIAG_')
x=x.replace("'RC55_DIAGNOSTIC=1'","'RC57_DIAGNOSTIC=1'")
x=x.replace("'RC55_BORDERLESS_SHA256='","'RC57_BORDERLESS_SHA256='")
x=x.replace("'RC55_ACTIVE_PAYLOAD_SHA256.txt'","'RC57_ACTIVE_PAYLOAD_SHA256.txt'")
x=x.replace("'RC55_BORDERLESS_LOG_ABSENT.txt'","'RC57_BORDERLESS_LOG_ABSENT.txt'")
x=x.replace("'RC55_RASTER_LOG_ABSENT.txt'","'RC57_RASTER_LOG_ABSENT.txt'")
x=x.replace("'RC55_BOOTSTRAP_LOG_ABSENT.txt'","'RC57_BOOTSTRAP_LOG_ABSENT.txt'")
x=x.replace("('PTAR_RC55_RESULTS_'+$stamp+'.zip')","('PTAR_RC57_RESULTS_'+$stamp+'.zip')")
x=x.replace('RESULTAT_RC55=','RESULTAT_RC57=')
x=x.replace('journaux RC55 raster/bootstrap','journaux RC57/RC55 raster/bootstrap')
x=one(x,"'EXPECTED_PHYSICAL=1280x720','NOTE_BORDERLESS_LOG=ptar_borderless_rc38.log name retained by validated RC40 controller')","'EXPECTED_PHYSICAL=1280x720','RC57_INITIAL_BORDERLESS=DEFERRED_TO_WINDOWED_BOOT','RC57_UNLOCK=EXPLICIT_WINDOWSTYLE_0_THEN_1','NOTE_BORDERLESS_LOG=ptar_borderless_rc38.log name retained by validated RC40 controller')",'collector policy')
wr('collect_rc41.ps1',x)

# unchanged GUI migration module, re-emitted with exact package line-ending policy
wr('rc55_gui_migration.ps1',rd('rc55_gui_migration.ps1'))
print('RC57_FULL_PACKAGE_SCRIPTS_GENERATED=PASS')
