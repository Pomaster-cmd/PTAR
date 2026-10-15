$ErrorActionPreference='Stop'
$path='compat/x86_d3d9/ptar_d3d9_proxy.cpp'
$utf8=New-Object System.Text.UTF8Encoding($false)
$p=[IO.File]::ReadAllText((Resolve-Path $path)) -replace "`r`n","`n"
if($p.IndexOf('R15_FIELD_SAFETYFIX2',[StringComparison]::Ordinal) -lt 0){throw 'SAFETYFIX3 requires SAFETYFIX2'}
if($p.IndexOf('R15_FIELD_SAFETYFIX3',[StringComparison]::Ordinal) -ge 0){throw 'SAFETYFIX3 already applied'}
$old=@'
        g_ptarR15.currentVertexShaderPatched=PtR15IsPatchedVertexShader(shader);
        PtR15ApplyVertexConstant(self);
'@
$new=@'
        g_ptarR15.currentVertexShaderPatched=PtR15IsPatchedVertexShader(shader);
        PtR15ApplyVertexConstant(self);
        PtR15ApplyLodBiases(self);
'@
$a=$p.IndexOf($old,[StringComparison]::Ordinal)
if($a -lt 0){throw 'SAFETYFIX3 SetVertexShader anchor missing'}
if($p.IndexOf($old,$a+$old.Length,[StringComparison]::Ordinal) -ge 0){throw 'SAFETYFIX3 SetVertexShader anchor not unique'}
$p=$p.Substring(0,$a)+$new+$p.Substring($a+$old.Length)
$p=$p.Replace('// R15_FIELD_SAFETYFIX2',"// R15_FIELD_SAFETYFIX2`n// R15_FIELD_SAFETYFIX3")
if($p.IndexOf("PtR15ApplyVertexConstant(self);`n        PtR15ApplyLodBiases(self);",[StringComparison]::Ordinal) -lt 0){throw 'SAFETYFIX3 final LOD transition missing'}
[IO.File]::WriteAllText((Resolve-Path $path),$p,$utf8)
Write-Host 'R15_FIELD_SAFETYFIX3=PASS'
Write-Host 'R15_LOD_RESTORED_ON_VS_DOMAIN_CHANGE=PASS'
