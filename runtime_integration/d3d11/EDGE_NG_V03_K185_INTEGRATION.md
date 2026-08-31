# EDGE-NG v03 K185 runtime integration

## Candidate

`EDGE-NG-v03-K185`

This is new PTAR-NG code. It is not historical EDGE v03.

## Pixel path

Static HLSL source contains:

- 1 × `GatherGreen`
- 4 × `SampleLevel`
- 1 source texture
- 1 linear-clamp sampler
- no UAV
- no intermediate texture mandated by the algorithm
- no NIS dependency

K185 replaces the previous Lagrange cubic arithmetic with four fixed weights.
It does not add texture accesses.

## Host constants

`b0`:

```cpp
float2 inputSize;
float2 outputSize;
```

The current shader uses `inputSize` for texel-center addressing.
`outputSize` is retained in the ABI for future host diagnostics/variants.

## Output range

The mathematical cubic can overshoot [0,1].
The permanent RGB8 benchmark clips/quantizes to [0,1].
Runtime behavior must be tied to the actual RTV format:
- UNORM targets naturally clamp on storage;
- floating-point targets do not.

Do not silently add `saturate()` to an HDR/FP path without making it a new runtime variant.

## GPU timing

`PTARD3D11GpuTimerRing.h` provides an 8-slot non-blocking timestamp ring.
It uses `D3D11_QUERY_TIMESTAMP_DISJOINT` and `D3D11_ASYNC_GETDATA_DONOTFLUSH`.
It contains no `Flush()` call.

Measure the scaler pass itself, not Present or the whole frame.
