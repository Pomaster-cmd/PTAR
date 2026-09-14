from pathlib import Path
import sys

def read(p): return p.read_text(encoding='utf-8-sig')
def write(p,s): p.parent.mkdir(parents=True,exist_ok=True);p.write_text(s,encoding='utf-8-sig',newline='')
def require_once(s,old,label):
    if s.count(old)!=1: raise RuntimeError(f'{label} count={s.count(old)}')

def transform(src,out):
    out.mkdir(parents=True,exist_ok=True)

    # install: keep all RC52 binaries/policies exact; add only transactional GUI-resolution state.
    p=src/'install.ps1';s=read(p)
    old="schema=6;package='PTAR_RC52_RASTER_STATE_QUARANTINE';game_root=$g;pack_root=$PackRoot;installed=@{};original=@{};windowstyle=@{};migration="
    new="schema=7;package='PTAR_RC53_GUI_RESOLUTION_SYNC';game_root=$g;pack_root=$PackRoot;installed=@{};original=@{};windowstyle=@{};gui=@{};migration="
    require_once(s,old,'install state schema');s=s.replace(old,new,1)

    old="$key='HKCU:\\Software\\NeoCore Games\\Warhammer Martyr\\Options';$m.windowstyle.key=$key;$ws=Get-RegValueState $key 'WindowStyle';$m.windowstyle.exists=$ws.exists;$m.windowstyle.original=$ws.original;$m.windowstyle.applied=$false"
    new=old+";$gui=Get-RegValueState $key 'AffectGuiResolution';$m.gui=[ordered]@{key=$key;exists=$gui.exists;original=$gui.original;installed=1;applied=$false}"
    require_once(s,old,'registry snapshot');s=s.replace(old,new,1)

    old="$wsText=if($ws.exists){[string]$ws.original}else{'UNSET'};L ('WINDOWSTYLE_PRESERVED='+$wsText)"
    new=old+";$guiText=if($gui.exists){[string]$gui.original}else{'UNSET'};L ('AFFECTGUIRESOLUTION_ORIGINAL='+$guiText)"
    require_once(s,old,'registry snapshot log');s=s.replace(old,new,1)

    post=" if((Sha $a)-ne $ExpectedRuntime -or (Sha $c)-ne $ExpectedRuntime -or (Sha $i)-ne $ExpectedIni -or (Sha $v)-ne $ExpectedVersion -or (Sha $bd)-ne $ExpectedBorderless -or (Sha $xd)-ne $ExpectedRaster){throw 'Post-install hash mismatch.'}"
    require_once(s,post,'post-install hash gate')
    apply=post+"\n if(-not(Test-Path -LiteralPath $key)){New-Item -Path $key -Force|Out-Null};Set-ItemProperty -LiteralPath $key -Name AffectGuiResolution -Type DWord -Value 1;$m.gui.applied=$true;$m|ConvertTo-Json -Depth 12|Set-Content -LiteralPath (Join-Path $state 'install_state.json') -Encoding UTF8;L 'AFFECTGUIRESOLUTION_APPLIED=1'"
    s=s.replace(post,apply,1)

    rb=" if($m.windowstyle.applied -and (Test-Path -LiteralPath $key)){if($m.windowstyle.exists){Set-ItemProperty -LiteralPath $key -Name WindowStyle -Type DWord -Value ([int]$m.windowstyle.original)}else{Remove-ItemProperty -LiteralPath $key -Name WindowStyle -ErrorAction SilentlyContinue}}"
    require_once(s,rb,'install rollback windowstyle')
    gui_rb=rb+"\n if($m.gui.applied -and (Test-Path -LiteralPath $key)){try{$cur=(Get-ItemProperty -LiteralPath $key -Name AffectGuiResolution -ErrorAction Stop).AffectGuiResolution;if([int]$cur -eq [int]$m.gui.installed){if($m.gui.exists){Set-ItemProperty -LiteralPath $key -Name AffectGuiResolution -Type DWord -Value ([int]$m.gui.original)}else{Remove-ItemProperty -LiteralPath $key -Name AffectGuiResolution -ErrorAction SilentlyContinue}}}catch{}}"
    s=s.replace(rb,gui_rb,1)
    s=s.replace("F 'Payload RC52 hash mismatch.'","F 'Payload RC53 hash mismatch.'",1)
    s=s.replace("L 'INSTALL_RC52=PASS'","L 'INSTALL_RC53=PASS'",1)
    s=s.replace("L ('RC52_RASTER_SIDECAR_SHA256='+$ExpectedRaster)","L ('RC53_RASTER_SIDECAR_SHA256='+$ExpectedRaster)",1)
    s=s.replace("L 'MODE=RC52_PRESENTER_SYNC_WITH_RASTER_STATE_QUARANTINE'","L 'MODE=RC53_GUI_RESOLUTION_SYNC_WITH_RC52_RASTER_AND_RC51_PRESENTER'",1)
    write(out/p.name,s)

    # verify: binary checks stay exact and GUI value becomes an additional hard gate.
    p=src/'verify.ps1';s=read(p)
    tail="if($bad){exit 9}else{Write-Host 'VERIFY_RC52_RASTER_STATE_QUARANTINE=PASS';exit 0}"
    require_once(s,tail,'verify final gate')
    replacement="$guiKey='HKCU:\\Software\\NeoCore Games\\Warhammer Martyr\\Options';if(Test-Path -LiteralPath $guiKey){try{$guiVal=(Get-ItemProperty -LiteralPath $guiKey -Name AffectGuiResolution -ErrorAction Stop).AffectGuiResolution;if([int]$guiVal -eq 1){Write-Host '[PASS] AffectGuiResolution=1'}else{$bad=1;Write-Host ('[FAIL] AffectGuiResolution attendu=1 observe='+$guiVal)}}catch{$bad=1;Write-Host '[FAIL] AffectGuiResolution absent'}}else{$bad=1;Write-Host '[FAIL] Cle registre GUI absente'}\nif($bad){exit 9}else{Write-Host 'VERIFY_RC53_GUI_RESOLUTION_SYNC=PASS';exit 0}"
    s=s.replace(tail,replacement,1);write(out/p.name,s)

    # rollback: restore original GUI state only while the value is still RC53-owned (1).
    p=src/'rollback.ps1';s=read(p)
    rbline="if($m.windowstyle -and $m.windowstyle.applied){$key=[string]$m.windowstyle.key;if(Test-Path -LiteralPath $key){if([bool]$m.windowstyle.exists){Set-ItemProperty -LiteralPath $key -Name WindowStyle -Type DWord -Value ([int]$m.windowstyle.original)}else{Remove-ItemProperty -LiteralPath $key -Name WindowStyle -ErrorAction SilentlyContinue}};$m.windowstyle.applied=$false}"
    require_once(s,rbline,'rollback windowstyle')
    guiline="if(($m.PSObject.Properties.Name -contains 'gui') -and $m.gui -and $m.gui.applied){$key=[string]$m.gui.key;if(Test-Path -LiteralPath $key){try{$cur=(Get-ItemProperty -LiteralPath $key -Name AffectGuiResolution -ErrorAction Stop).AffectGuiResolution;if([int]$cur -eq [int]$m.gui.installed){if([bool]$m.gui.exists){Set-ItemProperty -LiteralPath $key -Name AffectGuiResolution -Type DWord -Value ([int]$m.gui.original)}else{Remove-ItemProperty -LiteralPath $key -Name AffectGuiResolution -ErrorAction SilentlyContinue};$m.gui.applied=$false}else{Write-Host ('[KEEP] AffectGuiResolution modifie par utilisateur: '+$cur)}}catch{Write-Host '[KEEP] AffectGuiResolution absent/modifie'}}}"
    s=s.replace(rbline,rbline+'\n'+guiline,1)
    s=s.replace("[PASS] RC52 retire;","[PASS] RC53 retire;",1);write(out/p.name,s)

    # full uninstall wrapper: generic engine runs first, then RC53 restores its GUI-owned value.
    p=src/'uninstall_overlay.ps1';s=read(p)
    engine="& $Engine -Root $Root;$rc=$LASTEXITCODE;if($rc -ne 0){exit $rc}"
    require_once(s,engine,'uninstall engine gate')
    guirestore="if($m -and ($m.PSObject.Properties.Name -contains 'gui') -and $m.gui -and $m.gui.applied){$key=[string]$m.gui.key;if(Test-Path -LiteralPath $key){try{$cur=(Get-ItemProperty -LiteralPath $key -Name AffectGuiResolution -ErrorAction Stop).AffectGuiResolution;if([int]$cur -eq [int]$m.gui.installed){if([bool]$m.gui.exists){Set-ItemProperty -LiteralPath $key -Name AffectGuiResolution -Type DWord -Value ([int]$m.gui.original);Write-Host ('[OK] AffectGuiResolution restaure='+[int]$m.gui.original)}else{Remove-ItemProperty -LiteralPath $key -Name AffectGuiResolution -ErrorAction SilentlyContinue;Write-Host '[OK] AffectGuiResolution retire'}}else{Write-Host ('[KEEP] AffectGuiResolution modifie par utilisateur: '+$cur)}}catch{Write-Host '[KEEP] AffectGuiResolution absent/modifie'}}}"
    s=s.replace(engine,engine+'\n'+guirestore,1)
    s=s.replace('sidecars/additifs RC52 nettoyes','sidecars/additifs RC53 nettoyes',1);write(out/p.name,s)

    # collector: retain the exact diagnostics and add the effective engine GUI policy.
    p=src/'collect_rc41.ps1';s=read(p)
    for a,b in [('PTAR_RC52_DIAG_','PTAR_RC53_DIAG_'),('RC52_DIAGNOSTIC=1','RC53_DIAGNOSTIC=1'),('RC52_BORDERLESS_SHA256=','RC53_BORDERLESS_SHA256='),('RC52_RASTER_SIDECAR_SHA256=','RC53_RASTER_SIDECAR_SHA256='),('RC52_ACTIVE_PAYLOAD_SHA256.txt','RC53_ACTIVE_PAYLOAD_SHA256.txt'),('RC52_BORDERLESS_LOG_ABSENT.txt','RC53_BORDERLESS_LOG_ABSENT.txt'),('RC52_RASTER_LOG_ABSENT.txt','RC53_RASTER_LOG_ABSENT.txt'),('RC52_BOOTSTRAP_LOG_ABSENT.txt','RC53_BOOTSTRAP_LOG_ABSENT.txt'),('PTAR_RC52_RESULTS_','PTAR_RC53_RESULTS_'),('RESULTAT_RC52=','RESULTAT_RC53='),('journaux RC52 raster/bootstrap','journaux RC53 raster/bootstrap')]:s=s.replace(a,b)
    temp="$tmp=Join-Path $env:TEMP ('PTAR_RC53_DIAG_'+[guid]::NewGuid().ToString('N')+'.txt')"
    require_once(s,temp,'collector temporary diagnostics')
    pre=temp+"\n$guiValue='UNAVAILABLE';try{$guiKey='HKCU:\\Software\\NeoCore Games\\Warhammer Martyr\\Options';$guiValue=[string]([int](Get-ItemProperty -LiteralPath $guiKey -Name AffectGuiResolution -ErrorAction Stop).AffectGuiResolution)}catch{}"
    s=s.replace(temp,pre,1)
    arr="@('RC53_DIAGNOSTIC=1',";require_once(s,arr,'collector payload line')
    s=s.replace(arr,"@('RC53_DIAGNOSTIC=1',('AFFECT_GUI_RESOLUTION='+$guiValue),",1);write(out/p.name,s)

if __name__=='__main__':
    if len(sys.argv)!=3: raise SystemExit('usage: build_rc53_scripts.py RC52_SCRIPT_DIR OUTPUT_DIR')
    transform(Path(sys.argv[1]),Path(sys.argv[2]))
