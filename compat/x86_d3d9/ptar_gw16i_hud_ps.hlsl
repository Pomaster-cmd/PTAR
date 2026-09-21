// PTAR D3D9 HUD port of the embedded GW16I HUD from the supplied
// PTAR_GW16I_SLATEABS1_SATGAT1_UNIVERSAL1 production runtime.
//
// D3D11 source contract:
//   cbuffer HudParams { float4 A,B,C,D,E; }
//   Title: WIN81 USR V0.41
//   FPS / RES. / FILTER
//   transient USR/HUD state line + key
//   optional REAL/GENERATED cadence marker
//
// The layout, 3x5 glyph masks, spacing and colors are preserved. Only shader
// model syntax/bit operations are adapted for ps_3_0.

float4 A : register(c0);
float4 B : register(c1);
float4 C : register(c2);
float4 D : register(c3);
float4 E : register(c4);

float GB(float c)
{
    if(c==32)return 0;
    if(c==43)return 4680;
    if(c==45)return 448;
    if(c==46)return 8192;
    if(c==58)return 1040;
    if(c==88)return 23213;
    if(c==48)return 31599;
    if(c==49)return 29850;
    if(c==50)return 29671;
    if(c==51)return 31207;
    if(c==52)return 18925;
    if(c==53)return 31183;
    if(c==54)return 31695;
    if(c==55)return 9383;
    if(c==56)return 31727;
    if(c==57)return 31215;
    if(c==65)return 23530;
    if(c==66)return 15083;
    if(c==67)return 25166;
    if(c==68)return 15211;
    if(c==69)return 29391;
    if(c==70)return 4815;
    if(c==71)return 27470;
    if(c==72)return 23533;
    if(c==73)return 29847;
    if(c==75)return 23277;
    if(c==76)return 29257;
    if(c==77)return 23549;
    if(c==78)return 24573;
    if(c==79)return 31599;
    if(c==80)return 4843;
    if(c==82)return 23275;
    if(c==83)return 14798;
    if(c==84)return 9367;
    if(c==85)return 31597;
    if(c==86)return 11117;
    if(c==87)return 24557;
    if(c==89)return 9389;
    return 0;
}

float Bit(float mask,float bit)
{
    return fmod(floor(mask/exp2(bit)),2.0);
}

float Ch(float2 p,float2 o,float c)
{
    float2 q=floor((p-o)/2.0);
    if(q.x<0||q.y<0||q.x>=3||q.y>=5)return 0;
    float bit=q.y*3.0+q.x;
    return Bit(GB(c),bit)>=0.5?1.0:0.0;
}

float Cx(float2 p,float2 o,float i,float c)
{
    return Ch(p,o+float2(i*8.0,0),c);
}

float Dig3(float2 p,float2 o,float n)
{
    n=floor(n);
    float m=0;
    if(n>=100)m=max(m,Ch(p,o,48+fmod(floor(n/100),10)));
    if(n>=10)m=max(m,Ch(p,o+float2(8,0),48+fmod(floor(n/10),10)));
    m=max(m,Ch(p,o+float2(16,0),48+fmod(n,10)));
    return m;
}

float Dig4(float2 p,float2 o,float n)
{
    n=floor(n);
    float m=0;
    if(n>=1000)m=max(m,Ch(p,o,48+fmod(floor(n/1000),10)));
    if(n>=100)m=max(m,Ch(p,o+float2(8,0),48+fmod(floor(n/100),10)));
    if(n>=10)m=max(m,Ch(p,o+float2(16,0),48+fmod(floor(n/10),10)));
    m=max(m,Ch(p,o+float2(24,0),48+fmod(n,10)));
    return m;
}

float Word(
    float2 p,float2 o,
    float a,float b,float c,float d,float e,
    float f,float g,float h,float i,float j)
{
    float m=0;
    m=max(m,Cx(p,o,0,a));m=max(m,Cx(p,o,1,b));
    m=max(m,Cx(p,o,2,c));m=max(m,Cx(p,o,3,d));
    m=max(m,Cx(p,o,4,e));m=max(m,Cx(p,o,5,f));
    m=max(m,Cx(p,o,6,g));m=max(m,Cx(p,o,7,h));
    m=max(m,Cx(p,o,8,i));m=max(m,Cx(p,o,9,j));
    return m;
}

float Title(float2 p,float2 o)
{
    float m=0;
    m=max(m,Word(p,o,87,73,78,56,49,32,85,83,82,32));
    m=max(m,Cx(p,o,10,86));m=max(m,Cx(p,o,11,48));
    m=max(m,Cx(p,o,12,46));m=max(m,Cx(p,o,13,52));
    m=max(m,Cx(p,o,14,49));
    return m;
}

float Fps(float2 p,float2 o)
{
    float m=Word(p,o,70,80,83,32,32,32,32,32,32,32);
    return max(m,Dig3(p,o+float2(40,0),min(A.z,999.0)));
}

float Resolution(float2 p,float2 o)
{
    float m=Word(p,o,82,69,83,46,32,32,32,32,32,32);
    m=max(m,Dig4(p,o+float2(40,0),B.x));
    m=max(m,Ch(p,o+float2(74,0),88));
    m=max(m,Dig4(p,o+float2(84,0),B.y));
    return m;
}

float Filter(float2 p,float2 o)
{
    float m=Word(p,o,70,73,76,84,69,82,32,32,32,32);
    float q=floor(B.z+.5);
    float2 n=o+float2(64,0);
    if(q==0)
        m=max(m,Word(p,n,80,79,73,78,84,32,32,32,32,32));
    else if(q==1)
        m=max(m,Word(p,n,66,73,76,73,78,69,65,82,32,32));
    else if(q==2)
        m=max(m,Word(p,n,80,84,65,82,32,88,49,53,32,32));
    else if(q==3)
        m=max(m,Word(p,n,80,84,65,82,32,32,32,32,32,32));
    else if(q==5)
        m=max(m,Word(p,n,67,79,80,89,32,49,88,49,32,32));
    else if(q==6||q==7)
    {
        m=max(m,Word(p,n,69,68,71,69,32,32,32,32,32,32));
        m=max(m,Ch(p,n+float2(40,0),q==6?50:52));
        m=max(m,Ch(p,n+float2(48,0),53));
    }
    else
        m=max(m,Word(p,n,83,65,70,69,32,32,32,32,32,32));
    return m;
}

float Rc(float2 p,float2 a,float2 b)
{
    return (p.x>=a.x&&p.y>=a.y&&p.x<b.x&&p.y<b.y)?1.0:0.0;
}

float IdBit(float id,float bit)
{
    return fmod(floor(id/exp2(bit)),2.0);
}

float GrayBit(float id,float bit)
{
    return abs(IdBit(id,bit)-IdBit(id,bit+1.0));
}

float Mark(float2 p,float2 o)
{
    float id=fmod(floor(D.x),4096.0);
    float typ=fmod(floor(D.y),2.0);
    float parity=typ;
    float m=Cx(p,o,0,77);
    m=max(m,Dig4(p,o+float2(16,0),fmod(id,10000.0)));
    m=max(m,Cx(p,o,7,typ!=0?71:82));

    [unroll]
    for(int i=0;i<12;i++)
    {
        float bit=GrayBit(id,(float)i);
        parity=fmod(parity+bit,2.0);
        if(bit!=0)
            m=max(m,Rc(
                p,
                o+float2(72.0+i*5.0,0),
                o+float2(76.0+i*5.0,5.0)));
    }

    if(typ!=0)m=max(m,Rc(p,o+float2(132,0),o+float2(136,5)));
    if(parity!=0)m=max(m,Rc(p,o+float2(137,0),o+float2(141,5)));
    m=max(m,Rc(p,o+float2(142,0),o+float2(146,5)));
    return m;
}

float State(float2 p,float2 o,float s)
{
    float m=0;
    if(s<=2)
    {
        m=max(m,Cx(p,o,0,85));m=max(m,Cx(p,o,1,83));m=max(m,Cx(p,o,2,82));
        if(s==1)
        {
            m=max(m,Cx(p,o,4,79));m=max(m,Cx(p,o,5,70));m=max(m,Cx(p,o,6,70));
        }
        else
        {
            m=max(m,Cx(p,o,4,79));m=max(m,Cx(p,o,5,78));
        }
    }
    else
    {
        m=max(m,Cx(p,o,0,72));m=max(m,Cx(p,o,1,85));m=max(m,Cx(p,o,2,68));
        if(s==4)
        {
            m=max(m,Cx(p,o,4,79));m=max(m,Cx(p,o,5,70));m=max(m,Cx(p,o,6,70));
        }
        else
        {
            m=max(m,Cx(p,o,4,79));m=max(m,Cx(p,o,5,78));
        }
    }
    return m;
}

float Key(float2 p,float2 o,float s)
{
    float m=0;
    float vk=floor(((s>=3)?C.z:C.x)+.5);
    float mods=floor(((s>=3)?C.w:C.y)+.5);
    float k=0;

    if(fmod(floor(mods/2),2)!=0){m=max(m,Cx(p,o,k,67));k++;m=max(m,Cx(p,o,k,43));k++;}
    if(fmod(floor(mods/4),2)!=0){m=max(m,Cx(p,o,k,65));k++;m=max(m,Cx(p,o,k,43));k++;}
    if(fmod(mods,2)!=0){m=max(m,Cx(p,o,k,83));k++;m=max(m,Cx(p,o,k,43));k++;}

    if(vk>=112&&vk<=135)
    {
        m=max(m,Cx(p,o,k,70));k++;
        float n=vk-111;
        if(n>=10){m=max(m,Cx(p,o,k,48+floor(n/10)));k++;}
        m=max(m,Cx(p,o,k,48+fmod(n,10)));k++;
    }
    else if((vk>=65&&vk<=90)||(vk>=48&&vk<=57))
    {
        m=max(m,Cx(p,o,k,vk));k++;
    }
    else
    {
        m=max(m,Cx(p,o,k,70));k++;
        m=max(m,Cx(p,o,k,49));k++;
        m=max(m,Cx(p,o,k,48));k++;
    }

    k++;
    bool en=(s==1||s==4);
    if(en)
    {
        m=max(m,Cx(p,o,k,69));k++;m=max(m,Cx(p,o,k,78));k++;
        m=max(m,Cx(p,o,k,65));k++;m=max(m,Cx(p,o,k,66));k++;
        m=max(m,Cx(p,o,k,76));k++;m=max(m,Cx(p,o,k,69));
    }
    else
    {
        m=max(m,Cx(p,o,k,68));k++;m=max(m,Cx(p,o,k,73));k++;
        m=max(m,Cx(p,o,k,83));k++;m=max(m,Cx(p,o,k,65));k++;
        m=max(m,Cx(p,o,k,66));k++;m=max(m,Cx(p,o,k,76));k++;
        m=max(m,Cx(p,o,k,69));
    }
    return m;
}

float4 main(float2 pos:VPOS) : COLOR0
{
    float2 p=pos/2.0;
    bool active=A.w>.5;
    bool marker=D.z>.5;
    float state=floor(B.w+.5);

    if(!active&&state==0&&!marker)discard;

    if(active)
    {
        if(p.x<8||p.x>=340||p.y<8||p.y>=88)discard;
        float m=0;
        m=max(m,Title(p,float2(16,16)));
        m=max(m,Fps(p,float2(16,32)));
        m=max(m,Resolution(p,float2(16,48)));
        m=max(m,Filter(p,float2(16,64)));
        if(marker)m=max(m,Mark(p,float2(16,80)));
        return float4(
            lerp(float3(.01,.012,.018),float3(.92,.96,1),m),1);
    }

    if(marker&&state==0)
    {
        if(p.x<16||p.x>=168||p.y<80||p.y>=86)discard;
        float m=Mark(p,float2(16,80));
        return float4(
            lerp(float3(.01,.012,.018),float3(.92,.96,1),m),1);
    }

    if(p.x<8||p.x>=320||p.y<8||p.y>=48)discard;
    float m=max(
        State(p,float2(16,16),state),
        Key(p,float2(16,32),state));
    return float4(
        lerp(float3(.01,.012,.018),float3(1,1,1),m),1);
}
