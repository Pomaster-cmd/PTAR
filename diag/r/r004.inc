/* NISFG1B18D specialized module: EDGE-LITE CLARITY II 25/45 FG spatial reconstruction.
 * Custom scaler, NOT NVIDIA NIS. Direct quality iteration of B18C on the B18B1 temporal base.
 * Cost envelope is intentionally preserved: one pixel pass, exactly five source loads,
 * no UAV, no intermediate texture, no compute pass, no new Flush/wait/gate.
 *
 * B18D reuses the four bilinear-corner samples for the nearest texel instead of loading
 * that texel twice. The freed fifth fetch becomes a directional outer tap selected from
 * the local edge orientation. This adds genuinely new spatial information with the same
 * texture-read count. Sharpening is predominantly luminance-domain, with limited chroma
 * reinforcement, a local 2x2 RGB envelope clamp, and separate REAL/GENERATED confidence.
 * EDGE 25 targets a visible step above B18C EDGE 25; EDGE 45 remains the stronger profile. */
static ID3D11PixelShader* g_fgCustomEdgeLitePS=0;
static UINT g_fgCustomEdgeLiteGenerated=0u,g_fgCustomEdgeLiteReal=0u,g_fgCustomEdgeLiteFallbacks=0u,g_fgCustomEdgeLiteCompileFailures=0u;
static UINT g_fgCustomEdgeLiteEligibleGenerated=0u,g_fgCustomEdgeLiteEligibleReal=0u;
static int g_fgCustomEdgeLiteAvailable=1,g_fgCustomEdgeLiteActiveLogged=0,g_fgCustomEdgeLiteFallbackLogged=0;
static const char g_fgCustomEdgeLiteShaderPS[] =
"cbuffer Params:register(b0){float2 SrcSize;float2 DstSize;float2 DstOrigin;float Sharp;float Overlay;float Fps;float Pad;float2 Reserved;}\n"
"Texture2D<float4> Src:register(t0);\n"
"float Y(float3 c){return dot(c,float3(.299,.587,.114));}\n"
"float4 main(float4 pos:SV_Position):SV_Target{float2 p=(pos.xy-DstOrigin)*SrcSize/DstSize-0.5;float2 q=floor(p);float2 f=p-q;int2 a=int2(q);int2 mx=int2(SrcSize)-1;"
"int2 p00=clamp(a,int2(0,0),mx);int2 p10=clamp(a+int2(1,0),int2(0,0),mx);int2 p01=clamp(a+int2(0,1),int2(0,0),mx);int2 p11=clamp(a+int2(1,1),int2(0,0),mx);"
"float4 c00=Src.Load(int3(p00,0)),c10=Src.Load(int3(p10,0)),c01=Src.Load(int3(p01,0)),c11=Src.Load(int3(p11,0));float4 x0=lerp(c00,c10,f.x),x1=lerp(c01,c11,f.x);float4 b=lerp(x0,x1,f.y);"
"float4 n=(f.x<.5)?((f.y<.5)?c00:c01):((f.y<.5)?c10:c11);int2 pn=clamp(int2(floor(p+0.5)),int2(0,0),mx);float y00=Y(c00.rgb),y10=Y(c10.rgb),y01=Y(c01.rgb),y11=Y(c11.rgb),yb=Y(b.rgb),yn=Y(n.rgb);"
"float sx=((y10+y11)-(y00+y01))*.5,sy=((y01+y11)-(y00+y10))*.5;float ax=abs(sx),ay=abs(sy);int ox=(f.x>=.5)?1:-1,oy=(f.y>=.5)?1:-1;int2 od=(ax>ay*1.30)?int2(ox,0):((ay>ax*1.30)?int2(0,oy):int2(ox,oy));float4 o=Src.Load(int3(clamp(pn+od,int2(0,0),mx),0));float yo=Y(o.rgb);"
"float ymin=min(min(y00,y10),min(y01,y11)),ymax=max(max(y00,y10),max(y01,y11));float range=ymax-ymin;float cross=abs((y00+y11)-(y10+y01))*.5;float edge=saturate((ax+ay)*2.05+range*1.10+cross*.24);float diag=saturate(1.0-abs(ax-ay)/(ax+ay+.0001));"
"float boundary=max(4.0*f.x*(1.0-f.x),4.0*f.y*(1.0-f.y));float phaseSafe=1.0-.15*saturate(boundary);float outerDelta=yn-yo;float micro=saturate((max(abs(yn-yb),abs(outerDelta)*.55)-.0014)*78.0);float detailSafe=.70+.30*micro;"
"float profile=saturate((Sharp-.25)*5.0);float effSharp=saturate(Sharp*(1.17+.05*profile));float genGain=lerp(1.0,.92-.02*profile,saturate(Reserved.x));float antiHalo=.85+.15*(1.0-saturate(range*1.12));float orient=1.0+.10*diag+.04*saturate(cross*6.0);"
"float k=effSharp*genGain*(.29+.71*edge)*phaseSafe*detailSafe*antiHalo*orient;float yavg=(y00+y10+y01+y11)*.25;float outerConf=saturate(abs(outerDelta)*9.0);float lumCorr=(yn-yb)*phaseSafe+(yb-yavg)*(1.0-phaseSafe)*(.44+.10*diag)+outerDelta*(.050+.035*profile)*edge*outerConf;"
"float lumN=yn-yb;float3 dn=n.rgb-b.rgb;float3 chroma=dn-float3(lumN,lumN,lumN);float3 corr=float3(lumCorr,lumCorr,lumCorr)+chroma*(.20+.06*profile);float3 lo=min(min(c00.rgb,c10.rgb),min(c01.rgb,c11.rgb));float3 hi=max(max(c00.rgb,c10.rgb),max(c01.rgb,c11.rgb));float3 rgb=clamp(b.rgb+corr*k,lo,hi);return float4(rgb,b.a);}\n";
