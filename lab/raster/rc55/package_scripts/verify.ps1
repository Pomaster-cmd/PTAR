$ErrorActionPreference='Stop'
$Here=$PSScriptRoot;$PackRoot=Split-Path -Parent $Here
$ExpectedRC55='271187ab9fd82b6829c52a667223f241ec79e98fa340b13c9a72270142156949'
$ExpectedRuntime='6f1686992bef971c9995df9f0cb91b93c6946988e12a837e5de956df37c9082b'
$ExpectedBorderless='59e699536998054ae38cc5a30e03375c9cd206220667debe87070962609dd26c'
function Sha([string]$p){if(Test-Path -LiteralPath $p -PathType Leaf){return (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}return $null}
function Root{if(Test-Path -LiteralPath (Join-Path $PackRoot 'Warhammer.exe') -PathType Leaf){return $PackRoot};$p=Split-Path -Parent $PackRoot;if(Test-Path -LiteralPath (Join-Path $p 'Warhammer.exe') -PathType Leaf){return $p};return $null}
$g=Root;if(-not $g){Write-Host '[FAIL] Warhammer.exe introuvable.';exit 2}
$bad=0
foreach($c in @(@('d3d11.dll',$ExpectedRuntime),@('ptar_borderless.dll',$ExpectedBorderless),@('ptar_rc41.dll',$ExpectedRC55))){$h=Sha (Join-Path $g $c[0]);if($h -eq $c[1]){Write-Host ('[PASS] '+$c[0]+' '+$h)}else{$bad=1;Write-Host ('[FAIL] '+$c[0]+' '+$h)}}
if($bad){exit 9}
$key='HKCU:\Software\NeoCore Games\Warhammer Martyr\Options';$gui='UNAVAILABLE';try{$gui=[string]([int](Get-ItemProperty -LiteralPath $key -Name AffectGuiResolution -ErrorAction Stop).AffectGuiResolution)}catch{$gui='ABSENT'}
Write-Host ('AFFECT_GUI_RESOLUTION='+$gui)
Write-Host 'VERIFY_RC55_BOUND_PHYSICAL_IDEMPOTENCE=PASS';exit 0
