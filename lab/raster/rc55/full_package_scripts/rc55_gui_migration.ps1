$script:Rc55DefaultGuiKey='HKCU:\Software\NeoCore Games\Warhammer Martyr\Options'

function Get-Rc55GuiValueState{
 param([string]$RegistryKey=$script:Rc55DefaultGuiKey)
 $r=[ordered]@{exists=$false;value=$null}
 if(Test-Path -LiteralPath $RegistryKey){
  try{$r.value=[int](Get-ItemProperty -LiteralPath $RegistryKey -Name AffectGuiResolution -ErrorAction Stop).AffectGuiResolution;$r.exists=$true}catch{}
 }
 return $r
}

function Find-Rc55PriorGuiOwnerState{
 param([string]$StateRoot)
 if(-not(Test-Path -LiteralPath $StateRoot -PathType Container)){return $null}
 $states=@()
 try{$states=Get-ChildItem -LiteralPath $StateRoot -ErrorAction Stop|Where-Object {$_.PSIsContainer}|Sort-Object LastWriteTime -Descending}catch{return $null}
 foreach($d in $states){
  $sp=Join-Path $d.FullName 'install_state.json'
  if(-not(Test-Path -LiteralPath $sp -PathType Leaf)){continue}
  try{$pm=Get-Content -LiteralPath $sp -Raw|ConvertFrom-Json}catch{continue}
  $pkg=[string]$pm.package
  if(($pkg -ne 'PTAR_RC53_GUI_RESOLUTION_SYNC') -and ($pkg -ne 'PTAR_RC54_GUI_DOMAIN_PROBE')){continue}
  if(-not(($pm.PSObject.Properties.Name -contains 'gui') -and $pm.gui -and [bool]$pm.gui.applied)){continue}
  $key=$script:Rc55DefaultGuiKey
  if(($pm.gui.PSObject.Properties.Name -contains 'key') -and ([string]$pm.gui.key)){$key=[string]$pm.gui.key}
  return [ordered]@{manifest=$sp;package=$pkg;gui=$pm.gui;registry_key=$key}
 }
 return $null
}

function Invoke-Rc55GuiMigration{
 param([string]$StateRoot)
 $result=[ordered]@{checked=$true;owned=$false;changed=$false;event='OWNER_NONE';before_exists=$false;before_value=$null;after_exists=$false;after_value=$null;owner_state=$null;owner_package=$null;registry_key=$script:Rc55DefaultGuiKey;error=$null}
 $owner=Find-Rc55PriorGuiOwnerState -StateRoot $StateRoot
 if(-not $owner){return $result}
 $result.owned=$true;$result.owner_state=[string]$owner.manifest;$result.owner_package=[string]$owner.package;$result.registry_key=[string]$owner.registry_key
 $before=Get-Rc55GuiValueState -RegistryKey $result.registry_key
 $result.before_exists=[bool]$before.exists;$result.before_value=$before.value
 if(-not $before.exists){$result.event='CURRENT_ABSENT';return $result}
 if(-not($owner.gui.PSObject.Properties.Name -contains 'installed')){$result.event='OWNER_STATE_INVALID';return $result}
 if([int]$before.value -ne [int]$owner.gui.installed){$result.event='KEEP_CURRENT_CHANGED';return $result}
 try{
  if([bool]$owner.gui.exists){
   if(-not(Test-Path -LiteralPath $result.registry_key)){New-Item -Path $result.registry_key -Force|Out-Null}
   Set-ItemProperty -LiteralPath $result.registry_key -Name AffectGuiResolution -Type DWord -Value ([int]$owner.gui.original) -ErrorAction Stop
  }else{
   Remove-ItemProperty -LiteralPath $result.registry_key -Name AffectGuiResolution -ErrorAction Stop
  }
 }catch{
  $afterErr=Get-Rc55GuiValueState -RegistryKey $result.registry_key
  $result.after_exists=[bool]$afterErr.exists;$result.after_value=$afterErr.value
  $result.changed=(([bool]$result.before_exists -ne [bool]$result.after_exists) -or ($result.after_exists -and ([int]$result.before_value -ne [int]$result.after_value)))
  $result.event='ERROR';$result.error=$_.Exception.Message
  return $result
 }
 $after=Get-Rc55GuiValueState -RegistryKey $result.registry_key
 $result.after_exists=[bool]$after.exists;$result.after_value=$after.value
 $wantedExists=[bool]$owner.gui.exists
 $wantedValue=$null;if($wantedExists){$wantedValue=[int]$owner.gui.original}
 $matches=(($wantedExists -eq [bool]$after.exists) -and ((-not $wantedExists) -or ([int]$after.value -eq $wantedValue)))
 $result.changed=(([bool]$result.before_exists -ne [bool]$result.after_exists) -or ($result.after_exists -and ([int]$result.before_value -ne [int]$result.after_value)))
 if($matches){$result.event='RESTORED'}else{$result.event='ERROR';$result.error='Post-migration registry verification failed.'}
 return $result
}

function Undo-Rc55GuiMigration{
 param($Migration)
 $r=[ordered]@{changed=$false;event='NO_CHANGE';error=$null}
 if(-not $Migration){return $r}
 if(-not [bool]$Migration.changed){return $r}
 $key=$script:Rc55DefaultGuiKey
 if(($Migration.PSObject.Properties.Name -contains 'registry_key') -and ([string]$Migration.registry_key)){$key=[string]$Migration.registry_key}
 $cur=Get-Rc55GuiValueState -RegistryKey $key
 $matchesAfter=(([bool]$Migration.after_exists -eq [bool]$cur.exists) -and ((-not [bool]$cur.exists) -or ([int]$cur.value -eq [int]$Migration.after_value)))
 if(-not $matchesAfter){$r.event='KEEP_CURRENT_CHANGED';return $r}
 try{
  if([bool]$Migration.before_exists){
   if(-not(Test-Path -LiteralPath $key)){New-Item -Path $key -Force|Out-Null}
   Set-ItemProperty -LiteralPath $key -Name AffectGuiResolution -Type DWord -Value ([int]$Migration.before_value) -ErrorAction Stop
  }else{
   if(Test-Path -LiteralPath $key){Remove-ItemProperty -LiteralPath $key -Name AffectGuiResolution -ErrorAction Stop}
  }
 }catch{$r.event='ERROR';$r.error=$_.Exception.Message;return $r}
 $final=Get-Rc55GuiValueState -RegistryKey $key
 $matchesBefore=(([bool]$Migration.before_exists -eq [bool]$final.exists) -and ((-not [bool]$final.exists) -or ([int]$final.value -eq [int]$Migration.before_value)))
 if($matchesBefore){$r.changed=$true;$r.event='PRE_RC55_RESTORED'}else{$r.event='ERROR';$r.error='Post-undo registry verification failed.'}
 return $r
}
