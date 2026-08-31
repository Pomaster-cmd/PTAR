#ifndef PTAR_CLANG_HLSL_DIAGNOSTIC_SHIM
#define PTAR_CLANG_HLSL_DIAGNOSTIC_SHIM
// Diagnostic-only shim for the local Clang HLSL frontend. NOT shipped to runtime.
typedef unsigned int uint;
typedef uint uint2 __attribute__((ext_vector_type(2)));
typedef uint uint3 __attribute__((ext_vector_type(3)));
typedef uint uint4 __attribute__((ext_vector_type(4)));
typedef float float2 __attribute__((ext_vector_type(2)));
typedef float float3 __attribute__((ext_vector_type(3)));
typedef float float4 __attribute__((ext_vector_type(4)));

float min(float a,float b){return a<b?a:b;}
float max(float a,float b){return a>b?a:b;}
float4 min(float4 a,float4 b){return __builtin_elementwise_min(a,b);}
float4 max(float4 a,float4 b){return __builtin_elementwise_max(a,b);}
float clamp(float v,float lo,float hi){return min(max(v,lo),hi);}
float4 clamp(float4 v,float4 lo,float4 hi){return min(max(v,lo),hi);}
float saturate(float v){return clamp(v,0.0f,1.0f);}
float4 lerp(float4 a,float4 b,float t){return a+t*(b-a);}
float rcp(float v){return 1.0f/v;}
#endif
