sampler2D gPrev : register(s0);
sampler2D gCurr : register(s1);

// c0 = outputWidth, outputHeight, invOutputWidth, invOutputHeight
float4 gOutput : register(c0);

float Luma(float3 c)
{
    return dot(c,float3(0.2126,0.7152,0.0722));
}

float CandidateError(float2 uv,float2 pixels,float3 cur)
{
    float2 d=pixels*gOutput.zw;
    float p0=Luma(tex2D(gPrev,uv+d).rgb);
    float px=Luma(tex2D(gPrev,uv+d+float2(2.0,0.0)*gOutput.zw).rgb);
    float py=Luma(tex2D(gPrev,uv+d+float2(0.0,2.0)*gOutput.zw).rgb);
    return abs(p0-cur.x)+0.5*abs(px-cur.y)+0.5*abs(py-cur.z);
}

void TryCandidate(
    float2 uv,
    float2 pixels,
    float3 cur,
    inout float bestError,
    inout float2 bestMotion)
{
    float e=CandidateError(uv,pixels,cur);
    if(e<bestError)
    {
        bestError=e;
        bestMotion=pixels;
    }
}

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float3 cur=float3(
        Luma(tex2D(gCurr,uv).rgb),
        Luma(tex2D(gCurr,uv+float2(2.0,0.0)*gOutput.zw).rgb),
        Luma(tex2D(gCurr,uv+float2(0.0,2.0)*gOutput.zw).rgb));

    float bestError=1000.0;
    float2 bestMotion=0.0;

    TryCandidate(uv,float2(-16,-16),cur,bestError,bestMotion);
    TryCandidate(uv,float2( -8,-16),cur,bestError,bestMotion);
    TryCandidate(uv,float2(  0,-16),cur,bestError,bestMotion);
    TryCandidate(uv,float2(  8,-16),cur,bestError,bestMotion);
    TryCandidate(uv,float2( 16,-16),cur,bestError,bestMotion);

    TryCandidate(uv,float2(-16, -8),cur,bestError,bestMotion);
    TryCandidate(uv,float2( -8, -8),cur,bestError,bestMotion);
    TryCandidate(uv,float2(  0, -8),cur,bestError,bestMotion);
    TryCandidate(uv,float2(  8, -8),cur,bestError,bestMotion);
    TryCandidate(uv,float2( 16, -8),cur,bestError,bestMotion);

    TryCandidate(uv,float2(-16,  0),cur,bestError,bestMotion);
    TryCandidate(uv,float2( -8,  0),cur,bestError,bestMotion);
    TryCandidate(uv,float2(  0,  0),cur,bestError,bestMotion);
    TryCandidate(uv,float2(  8,  0),cur,bestError,bestMotion);
    TryCandidate(uv,float2( 16,  0),cur,bestError,bestMotion);

    TryCandidate(uv,float2(-16,  8),cur,bestError,bestMotion);
    TryCandidate(uv,float2( -8,  8),cur,bestError,bestMotion);
    TryCandidate(uv,float2(  0,  8),cur,bestError,bestMotion);
    TryCandidate(uv,float2(  8,  8),cur,bestError,bestMotion);
    TryCandidate(uv,float2( 16,  8),cur,bestError,bestMotion);

    TryCandidate(uv,float2(-16, 16),cur,bestError,bestMotion);
    TryCandidate(uv,float2( -8, 16),cur,bestError,bestMotion);
    TryCandidate(uv,float2(  0, 16),cur,bestError,bestMotion);
    TryCandidate(uv,float2(  8, 16),cur,bestError,bestMotion);
    TryCandidate(uv,float2( 16, 16),cur,bestError,bestMotion);

    float2 encoded=saturate(bestMotion/64.0+0.5);
    float confidence=saturate(1.0-bestError*2.5);
    return float4(encoded,confidence,1.0);
}
