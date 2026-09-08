# PTAR-NG MoE v02 runtime integration — stage 1

This branch is intentionally derived from `SOURCE` and keeps the validated MoE v01 autonomous runtime frozen.

Selected experimental shader provenance:

- source branch: `lab/moe-ng-v02-native-detail`
- selected head: `ea291eac7484e1f368271bdbbbab18c27fd0b33f`
- selected tree: `069736e3fcdbadb874106640696d8cf871220696`
- architecture: LAB07 always-on MC native detail
- LAB09 is diagnostic evidence only; it does not change the selected shader math.

Stage 1 goals:

1. copy the selected v02 HLSL into an isolated runtime-integration branch;
2. prove that frozen v01 runtime/HLSL/CSO hashes are unchanged;
3. compile v02 as `ps_5_0` DXBC on a Windows CI runner;
4. persist CSO SHA-256 and disassembly as CI artifacts;
5. make no claim of GPU parity, visual quality, or GTX 960M timing yet.

No files in `main` or `SOURCE` are modified by this laboratory branch.
