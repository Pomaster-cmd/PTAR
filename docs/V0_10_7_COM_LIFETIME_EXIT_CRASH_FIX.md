# PTAR v0.10.7 — clean-exit COM lifetime correction

## Real Windows 8.1 result from v0.10.6

The v0.10.6 hardware run completed the substantive validation:

- native build PASS;
- D3D11 GTX 960M selection PASS;
- SM5 shader compilation PASS;
- DXBC audit PASS;
- GPU parity 42/42 PASS;
- timing completed without NVIDIA TDR;
- K185 median: 0.497840 ms;
- K185 P95: 0.500832 ms;
- K185 P99: 0.502197 ms;
- bilinear median: 0.503136 ms;
- median delta: -0.005296 ms.

After printing those final timing results, Windows displayed a generic
`ptar_k185_hw_validation.exe has stopped working` dialog and the process was
reported by the batch layer as exit code 255.

## Source-level lifecycle defect

`wmain` previously called `CoUninitialize()` explicitly while these automatic
objects were still alive:

- `ComPtr<IWICImagingFactory> wic`;
- `D3D11Harness gpu`, which owns several D3D11 COM interfaces.

Those objects were only destroyed after `wmain` began returning.

v0.10.7 replaces manual COM shutdown with a `ComApartmentGuard` declared before
all COM-owning objects. C++ reverse destruction order now guarantees:

1. D3D11 harness/resources are destroyed;
2. WIC factory is released;
3. only then does `CoUninitialize()` run.

The D3D11 harness destructor also calls `ClearState()` before its ComPtr members
release, removing bound-context references cleanly.

## Timing interpretation

The v0.10.6 timing result is retained as real hardware evidence. K185 and the
bilinear control are separated by only about 0.0053 ms at the median on this
run. That difference is too small to claim that K185 is intrinsically faster;
the meaningful conclusion is that their measured cost is effectively very
close on this GTX 960M campaign.

v0.10.7 is intended to validate clean process shutdown and produce exit code 0.
