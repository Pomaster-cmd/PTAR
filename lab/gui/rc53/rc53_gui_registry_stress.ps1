$ErrorActionPreference='Stop'
$base='HKCU:\Software\PTAR_RC53_LAB\Warhammer Martyr\Options'
function ReadState([string]$k){$x=[ordered]@{exists=$false;value=$null};if(Test-Path $k){$r=Get-ItemProperty -LiteralPath $k;if($r.PSObject.Properties.Name -contains 'AffectGuiResolution'){$x.exists=$true;$x.value=[int]$r.AffectGuiResolution}};return $x}
function Apply([string]$k){$s=ReadState $k;if(-not(Test-Path $k)){New-Item -Path $k -Force|Out-Null};Set-ItemProperty -LiteralPath $k -Name AffectGuiResolution -Type DWord -Value 1;return $s}
function RestoreIfOwned([string]$k,$s){if(-not(Test-Path $k)){return};$r=Get-ItemProperty -LiteralPath $k;if(-not($r.PSObject.Properties.Name -contains 'AffectGuiResolution')){return};$cur=[int]$r.AffectGuiResolution;if($cur -ne 1){return};if($s.exists){Set-ItemProperty -LiteralPath $k -Name AffectGuiResolution -Type DWord -Value ([int]$s.value)}else{Remove-ItemProperty -LiteralPath $k -Name AffectGuiResolution -ErrorAction SilentlyContinue}}
Remove-Item -LiteralPath 'HKCU:\Software\PTAR_RC53_LAB' -Recurse -Force -ErrorAction SilentlyContinue
$cases=0
foreach($mode in @('unset','zero','one','other')){
  1..1000|ForEach-Object{
    Remove-Item -LiteralPath 'HKCU:\Software\PTAR_RC53_LAB' -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -Path $base -Force|Out-Null
    if($mode -eq 'zero'){Set-ItemProperty -LiteralPath $base -Name AffectGuiResolution -Type DWord -Value 0}
    elseif($mode -eq 'one'){Set-ItemProperty -LiteralPath $base -Name AffectGuiResolution -Type DWord -Value 1}
    elseif($mode -eq 'other'){Set-ItemProperty -LiteralPath $base -Name AffectGuiResolution -Type DWord -Value 7}
    $before=ReadState $base;$saved=Apply $base;$after=ReadState $base
    if(-not $after.exists -or $after.value -ne 1){throw "apply failed mode=$mode iter=$_"}
    RestoreIfOwned $base $saved;$rest=ReadState $base
    if($before.exists -ne $rest.exists){throw "existence restore failed mode=$mode iter=$_"}
    if($before.exists -and $before.value -ne $rest.value){throw "value restore failed mode=$mode iter=$_ before=$($before.value) after=$($rest.value)"}
    $cases++
  }
}
# ownership safety: user change after installation must survive uninstall/rollback.
1..1000|ForEach-Object{
  Remove-Item -LiteralPath 'HKCU:\Software\PTAR_RC53_LAB' -Recurse -Force -ErrorAction SilentlyContinue;New-Item -Path $base -Force|Out-Null;Set-ItemProperty -LiteralPath $base -Name AffectGuiResolution -Type DWord -Value 0
  $saved=Apply $base;Set-ItemProperty -LiteralPath $base -Name AffectGuiResolution -Type DWord -Value 9;RestoreIfOwned $base $saved
  $r=ReadState $base;if(-not $r.exists -or $r.value -ne 9){throw "ownership safety failed iter=$_"};$cases++
}
Remove-Item -LiteralPath 'HKCU:\Software\PTAR_RC53_LAB' -Recurse -Force -ErrorAction SilentlyContinue
"RC53_GUI_REGISTRY_STRESS=PASS cases=$cases apply=1 restore_original=PASS user_change_preserved=PASS"