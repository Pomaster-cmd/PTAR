from pathlib import Path
import re,sys

def read(p): return p.read_text(encoding='utf-8-sig')
def write(p,s): p.parent.mkdir(parents=True,exist_ok=True);p.write_text(s,encoding='utf-8-sig',newline='')

def transform(src,out):
    out.mkdir(parents=True,exist_ok=True)
    # install
    p=src/'install.ps1';s=read(p)
    s=s.replace("schema=6;package='PTAR_RC52_RASTER_STATE_QUARANTINE'","schema=7;package='PTAR_RC53_GUI_RESOLUTION_SYNC'")
    s=s.replace("$m.windowstyle.key=$key;$ws=Get-RegValueState $key 'WindowStyle';$m.windowstyle.exists=$ws.exists;$m.windowstyle.original=$ws.original;$m.windowstyle.applied=$false","$m.windowstyle.key=$key;$ws=Get-RegValueState $key 'WindowStyle';$m.windowstyle.exists=$ws.exists;$m.windowstyle.original=$ws.original;$m.windowstyle.applied=$false;$gui=Get-RegValueState $key 'AffectGuiResolution';$m.gui=[ordered]@{key=$key;exists=$gui.exists;original=$gui.original;installed=1;applied=$false}")
    s=s.replace("$wsText=if($ws.exists){[string]$ws.original}else{'UNSET'};L ('WINDOWSTYLE_PRESERVED='+$wsText)","$wsText=if($ws.exists){[string]$ws.original}else{'UNSET'};L ('WINDOWSTYLE_PRESERVED='+$wsText);$guiText=if($gui.exists){[string]$gui.original}else{'UNSET'};L ('AFFECTGUIRESOLUTION_ORIGINAL='+$guiText)")
    old="Copy-Item -LiteralPath $pr -Destination $a -Force;Copy-Item -LiteralPath $pc -Destination $c -Force;Copy-Item -LiteralPath $pi -Destination $i -Force;Copy-Item -LiteralPath $pv -Destination $v -Force;Copy-Item -LiteralPath $pb -Destination $bd -Force;Copy-Item -LiteralPath $px -Destination $xd -Force\n  if((Sha $a)-ne $ExpectedRuntime -or (Sha $c)-ne $ExpectedRuntime -or (Sha $i)-ne $ExpectedIni -or (Sha $v)-ne $ExpectedVersion -or (Sha $bd)-ne $ExpectedBorderless -or (Sha $xd)-ne $ExpectedRaster){throw 'Post-install hash mismatch.'}"
    new="Copy-Item -LiteralPath $pr -Destination $a -Force;Copy-Item -LiteralPath $pc -Destination $c -Force;Copy-Item -LiteralPath $pi -Destination $i -Force;Copy-Item -LiteralPath $pv -Destination $v -Force;Copy-Item -LiteralPath $pb -Destination $bd -Force;Copy-Item -LiteralPath $px -Destination $xd -Force\n  if((Sha $a)-ne $ExpectedRuntime -or (Sha $c)-ne $ExpectedRuntime -or (Sha $i)-ne $ExpectedIni -or (Sha $v)-ne $ExpectedVersion -or (Sha $bd)-ne $ExpectedBorderless -or (Sha $xd)-ne $ExpectedRaster){throw 'Post-install hash mismatch.'}\n  if(-not(Test-Path -LiteralPath $key)){New-Item -Path $key -Force|Out-Null};Set-ItemProperty -LiteralPath $key -Name AffectGuiResolution -Type DWord -Value 1;$m.gui.applied=$true;$m|ConvertTo-Json -Depth 12|Set-Content -LiteralPath (Join-Path $state 'install_state.json') -Encoding UTF8;L 'AFFECTGUIRESOLUTION_APPLIED=1'"
    if old not in s: raise RuntimeError('install payload block drift')
    s=s.replace(old,new,1)
    rb="if($m.windowstyle.applied -and (Test-Path -LiteralPath $key)){if($m.windowstyle.exists){Set-ItemProperty -LiteralPath $key -Name WindowStyle -Type DWord -Value ([int]$m.windowstyle.original)}else{Remove-ItemProperty -LiteralPath $key -Name WindowStyle -ErrorAction SilentlyContinue}}"
    gui_rb=rb+"\n  if($m.gui.applied -and (Test-Path -LiteralPath $key)){if($m.gui.exists){Set-ItemProperty -LiteralPath $key -Name AffectGuiResolution -Type DWord -Value ([int]$m.gui.original)}else{Remove-ItemProperty -LiteralPath $key -Name AffectGuiResolution -ErrorAction SilentlyContinue}}"
    s=s.replace(rb,gui_rb,1)
    s=s.replace("L 'INSTALL_RC52=PASS'","L 'INSTALL_RC53=PASS'")
    s=s.replace("L 'MODE=RC52_PRESENTER_SYNC_WITH_RASTER_STATE_QUARANTINE'","L 'MODE=RC53_GUI_RESOLUTION_SYNC_WITH_RC52_RASTER_AND_RC51_PRESENTER'")
    write(out/p.name,s)

    # verify
    p=src/'verify.ps1';s=read(p)
    s=s.replace('VERIFY_RC52_RASTER_STATE_QUARANTINE=PASS','VERIFY_RC53_GUI_RESOLUTION_SYNC=PASS')
    marker="Write-Host '[PASS] PTAR RC52"
    # append an explicit registry gate before final PASS marker in a conservative way
    keyline="$guiKey='HKCU:\\Software\\NeoCore Games\\Warhammer Martyr\\Options';if(-not(Test-Path -LiteralPath $guiKey)){F 'Cle registre GUI absente' 81};$guiVal=(Get-ItemProperty -LiteralPath $guiKey -Name AffectGuiResolution -ErrorAction Stop).AffectGuiResolution;if([int]$guiVal -ne 1){F ('AffectGuiResolution attendu=1 observe='+$guiVal) 82};Write-Host '[PASS] AffectGuiResolution=1'\n"
    final='Write-Host "VERIFY_RC53_GUI_RESOLUTION_SYNC=PASS"'
    if final in s:s=s.replace(final,keyline+final,1)
    else:
        # transformed marker may be single quoted
        final2="Write-Host 'VERIFY_RC53_GUI_RESOLUTION_SYNC=PASS'"
        if final2 not in s: raise RuntimeError('verify final marker drift')
        s=s.replace(final2,keyline+final2,1)
    write(out/p.name,s)

    # rollback and uninstall: restore GUI only when owned by current install state.
    for name in ('rollback.ps1','uninstall_overlay.ps1'):
        p=src/name;s=read(p)
        # Generic insertion near existing windowstyle restore code if install_state exposes gui.
        needle="if($m.windowstyle.applied -and (Test-Path -LiteralPath $key)){if($m.windowstyle.exists){Set-ItemProperty -LiteralPath $key -Name WindowStyle -Type DWord -Value ([int]$m.windowstyle.original)}else{Remove-ItemProperty -LiteralPath $key -Name WindowStyle -ErrorAction SilentlyContinue}}"
        if needle in s:
            add=needle+"\nif(($m.PSObject.Properties.Name -contains 'gui') -and $m.gui -and $m.gui.applied -and (Test-Path -LiteralPath $key)){try{$cur=(Get-ItemProperty -LiteralPath $key -Name AffectGuiResolution -ErrorAction Stop).AffectGuiResolution;if([int]$cur -eq [int]$m.gui.installed){if($m.gui.exists){Set-ItemProperty -LiteralPath $key -Name AffectGuiResolution -Type DWord -Value ([int]$m.gui.original)}else{Remove-ItemProperty -LiteralPath $key -Name AffectGuiResolution -ErrorAction SilentlyContinue}}}catch{}}"
            s=s.replace(needle,add,1)
        s=s.replace('RC52','RC53')
        write(out/p.name,s)

    # collector
    p=src/'collect_rc41.ps1';s=read(p).replace('RC52','RC53')
    insert="\ntry{$rk='HKCU:\\Software\\NeoCore Games\\Warhammer Martyr\\Options';$rv=(Get-ItemProperty -LiteralPath $rk -Name AffectGuiResolution -ErrorAction Stop).AffectGuiResolution;Add-Content -LiteralPath (Join-Path $d 'RC53_ACTIVE_PAYLOAD_SHA256.txt') -Value ('AFFECT_GUI_RESOLUTION='+[int]$rv) -Encoding ASCII}catch{Add-Content -LiteralPath (Join-Path $d 'RC53_ACTIVE_PAYLOAD_SHA256.txt') -Value 'AFFECT_GUI_RESOLUTION=UNAVAILABLE' -Encoding ASCII}\n"
    # place before compression/result stage
    token='Compress-Archive'
    idx=s.find(token)
    if idx<0: raise RuntimeError('collector compress marker drift')
    line_start=s.rfind('\n',0,idx)+1
    s=s[:line_start]+insert+s[line_start:]
    write(out/p.name,s)

if __name__=='__main__':
    if len(sys.argv)!=3: raise SystemExit('usage: build_rc53_scripts.py RC52_SCRIPT_DIR OUTPUT_DIR')
    transform(Path(sys.argv[1]),Path(sys.argv[2]))
