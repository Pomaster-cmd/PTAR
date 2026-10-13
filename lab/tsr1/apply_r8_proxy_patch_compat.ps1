$ErrorActionPreference='Stop'

# The canonical patch is now lineage-tolerant: resource lifetime wiring is
# separated from the semantic TEMPORALPAIR60 HookPresent adapter. Keep this
# compatibility entry point so existing CI callers remain stable.
& .\lab\tsr1\apply_r8_proxy_patch.ps1
if($LASTEXITCODE){throw 'R8 canonical semantic proxy patch failed'}

Write-Host 'R8_PROXY_PATCH_COMPAT=PASS'
Write-Host 'R8_PROXY_PATCH_COMPAT_MODE=CANONICAL_SEMANTIC'
