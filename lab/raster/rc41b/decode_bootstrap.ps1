param([Parameter(Mandatory=$true)][string]$Path)
$ErrorActionPreference='Stop'
if(-not (Test-Path -LiteralPath $Path -PathType Leaf)){ throw "Bootstrap state absent: $Path" }
$b=[IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $Path))
if($b.Length -ne 64){ throw "Bootstrap state size invalid: $($b.Length)" }
$magic=[Text.Encoding]::ASCII.GetString($b,0,8)
if($magic -ne 'RC41BST1'){ throw "Bootstrap magic invalid: $magic" }
function U32([int]$o){ [BitConverter]::ToUInt32($b,$o) }
function I32([int]$o){ [BitConverter]::ToInt32($b,$o) }
function U64([int]$o){ [BitConverter]::ToUInt64($b,$o) }
$stage=U32 12
$names=@{
  1='ENTER_PATHS_READY';2='LOADLIB_FAIL';3='LOADLIB_OK';4='GETPROC_FAIL';5='GETPROC_OK';
  6='AUTOSTART_RETRY';7='ACTIVE';8='EXHAUSTED'
}
$name=if($names.ContainsKey([int]$stage)){$names[[int]$stage]}else{'UNKNOWN'}
@(
  'RC41B_BOOTSTRAP_DECODE=PASS',
  "MAGIC=$magic",
  "VERSION=$(U32 8)",
  "STAGE=$stage $name",
  "ATTEMPT=$(U32 16)",
  "LAST_ERROR=$(U32 20)",
  "AUTOSTART_RC=$(I32 24)",
  "LOAD_FAIL_COUNT=$(U32 28)",
  ('MODULE=0x{0:X16}' -f (U64 32)),
  ('PROC=0x{0:X16}' -f (U64 40)),
  ('RUNTIME=0x{0:X16}' -f (U64 48)),
  "AUTOSTART_RETRY_COUNT=$(U32 56)",
  "FLAGS=$(U32 60)"
)
