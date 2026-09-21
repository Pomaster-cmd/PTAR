// PTAR D3D9 port of the embedded GW16I production feedback HUD shader.
// Layout/glyph masks/messages are preserved; only SM5 integer operations and
// cbuffer syntax are adapted for ps_3_0.

float4 A : register(c0);
float4 B : register(c1);

float GB(float c)
{
    if(c==32)return 0;if(c==45)return 448;
    if(c==48)return 31599;if(c==49)return 29850;if(c==50)return 29671;
    if(c==51)return 31207;if(c==52)return 18925;if(c==53)return 31183;
    if(c==54)return 31695;if(c==55)return 9383;if(c==56)return 31727;
    if(c==57)return 31215;if(c==65)return 23530;if(c==66)return 15083;
    if(c==67)return 25166;if(c==68)return 15211;if(c==69)return 29391;
    if(c==70)return 4815;if(c==71)return 27470;if(c==72)return 23533;
    if(c==73)return 29847;if(c==75)return 23277;if(c==76)return 29257;
    if(c==78)return 24573;if(c==79)return 31599;if(c==80)return 4843;
    if(c==81)return 20335;if(c==82)return 23275;if(c==83)return 14798;
    if(c==84)return 9367;if(c==85)return 31597;if(c==86)return 11117;
    if(c==87)return 24557;if(c==89)return 9389;
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
    return Bit(GB(c),q.y*3.0+q.x)>=.5?1.0:0.0;
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

float Line1(float2 p,float t,float r,float a)
{
    float2 o=float2(8,8);
    float m=0;
    m=max(m,Cx(p,o,0,70));
    if(t==5)m=max(m,Cx(p,o,1,56));
    else if(t==6)m=max(m,Cx(p,o,1,57));
    else if(t==7)m=max(m,Cx(p,o,1,54));
    else m=max(m,Cx(p,o,1,55));

    if(t==1)
    {
        m=max(m,Cx(p,o,3,87));m=max(m,Cx(p,o,4,65));
        m=max(m,Cx(p,o,5,73));m=max(m,Cx(p,o,6,84));
        m=max(m,Dig3(p,float2(72,8),r));
    }
    else if(t==2)
    {
        m=max(m,Cx(p,o,3,82));m=max(m,Cx(p,o,4,85));
        m=max(m,Cx(p,o,5,78));
        m=max(m,Dig3(p,float2(64,8),r));
    }
    else if(t==3)
    {
        m=max(m,Cx(p,o,3,68));m=max(m,Cx(p,o,4,79));
        m=max(m,Cx(p,o,5,78));m=max(m,Cx(p,o,6,69));
        m=max(m,Dig3(p,float2(72,8),a));
    }
    else if(t==4)
    {
        m=max(m,Cx(p,o,3,66));m=max(m,Cx(p,o,4,85));
        m=max(m,Cx(p,o,5,83));m=max(m,Cx(p,o,6,89));
    }
    else if(t==5)
    {
        m=max(m,Cx(p,o,3,79));m=max(m,Cx(p,o,4,75));
    }
    else if(t==6)
    {
        m=max(m,Cx(p,o,3,67));m=max(m,Cx(p,o,4,65));
        m=max(m,Cx(p,o,5,80));
        m=max(m,Dig3(p,float2(64,8),a));
        m=max(m,Cx(p,o,11,83));m=max(m,Cx(p,o,12,65));
        m=max(m,Cx(p,o,13,86));m=max(m,Cx(p,o,14,69));
        m=max(m,Cx(p,o,15,68));
    }
    else if(t==7)
    {
        m=max(m,Cx(p,o,3,77));m=max(m,Cx(p,o,4,79));
        m=max(m,Cx(p,o,5,68));m=max(m,Cx(p,o,6,69));
        if(a==0)
        {
            m=max(m,Cx(p,o,8,80));m=max(m,Cx(p,o,9,79));
            m=max(m,Cx(p,o,10,73));m=max(m,Cx(p,o,11,78));
            m=max(m,Cx(p,o,12,84));
        }
        else if(a==1)
        {
            m=max(m,Cx(p,o,8,66));m=max(m,Cx(p,o,9,73));
            m=max(m,Cx(p,o,10,76));m=max(m,Cx(p,o,11,73));
            m=max(m,Cx(p,o,12,78));
        }
        else if(a==2)
        {
            m=max(m,Cx(p,o,8,80));m=max(m,Cx(p,o,9,84));
            m=max(m,Cx(p,o,10,65));m=max(m,Cx(p,o,11,82));
        }
        else
        {
            m=max(m,Cx(p,o,8,83));m=max(m,Cx(p,o,9,65));
            m=max(m,Cx(p,o,10,70));m=max(m,Cx(p,o,11,69));
        }
    }
    return m;
}

float Line2(float2 p,float t,float b,float c)
{
    if(t!=3)return 0;
    float2 o=float2(8,24);
    float m=0;
    m=max(m,Cx(p,o,0,76));m=max(m,Cx(p,o,1,79));m=max(m,Cx(p,o,2,87));
    m=max(m,Dig3(p,float2(40,24),b));
    m=max(m,Cx(p,o,9,80));m=max(m,Cx(p,o,10,57));m=max(m,Cx(p,o,11,57));
    m=max(m,Dig3(p,float2(112,24),c));
    return m;
}

float Nm(float2 p,float a)
{
    float2 o=float2(8,8);
    float m=0;
    if(a==0)
    {
        m=max(m,Cx(p,o,0,76));m=max(m,Cx(p,o,1,69));m=max(m,Cx(p,o,2,71));
        m=max(m,Cx(p,o,3,65));m=max(m,Cx(p,o,4,67));m=max(m,Cx(p,o,5,89));
    }
    else if(a==1)
    {
        m=max(m,Cx(p,o,0,66));m=max(m,Cx(p,o,1,65));m=max(m,Cx(p,o,2,76));
        m=max(m,Cx(p,o,3,65));m=max(m,Cx(p,o,4,78));m=max(m,Cx(p,o,5,67));
        m=max(m,Cx(p,o,6,69));m=max(m,Cx(p,o,7,68));
    }
    else if(a==2)
    {
        m=max(m,Cx(p,o,0,81));m=max(m,Cx(p,o,1,85));m=max(m,Cx(p,o,2,65));
        m=max(m,Cx(p,o,3,76));m=max(m,Cx(p,o,4,73));m=max(m,Cx(p,o,5,84));
        m=max(m,Cx(p,o,6,89));
    }
    else
    {
        m=max(m,Cx(p,o,0,67));m=max(m,Cx(p,o,1,79));m=max(m,Cx(p,o,2,78));
        m=max(m,Cx(p,o,3,83));m=max(m,Cx(p,o,4,69));m=max(m,Cx(p,o,5,82));
        m=max(m,Cx(p,o,6,86));m=max(m,Cx(p,o,7,65));m=max(m,Cx(p,o,8,84));
        m=max(m,Cx(p,o,9,73));m=max(m,Cx(p,o,10,86));m=max(m,Cx(p,o,11,69));
    }
    return m;
}

float DLine1(float2 p,float t,float a)
{
    if(t<8)return 0;
    if(t==8)return Nm(p,a);

    float2 o=float2(8,8);
    float m=0;

    if(t>=14)
    {
        m=max(m,Cx(p,o,0,82));m=max(m,Cx(p,o,1,69));m=max(m,Cx(p,o,2,67));
        if(t==14)
        {
            m=max(m,Cx(p,o,4,87));m=max(m,Cx(p,o,5,65));
            m=max(m,Cx(p,o,6,73));m=max(m,Cx(p,o,7,84));
        }
        else if(t==15)
        {
            m=max(m,Cx(p,o,4,79));m=max(m,Cx(p,o,5,78));
        }
        else if(t==16)
        {
            m=max(m,Cx(p,o,4,83));m=max(m,Cx(p,o,5,65));
            m=max(m,Cx(p,o,6,86));m=max(m,Cx(p,o,7,69));
        }
        else if(t==17)
        {
            m=max(m,Cx(p,o,4,69));m=max(m,Cx(p,o,5,78));
            m=max(m,Cx(p,o,6,68));
        }
        else
        {
            m=max(m,Cx(p,o,4,69));m=max(m,Cx(p,o,5,82));
            m=max(m,Cx(p,o,6,82));
        }
        return m;
    }

    if(t==12||t==13)
    {
        m=max(m,Cx(p,o,0,78));m=max(m,Cx(p,o,1,86));
        if(t==12)
        {
            m=max(m,Cx(p,o,3,79));m=max(m,Cx(p,o,4,78));
        }
        else
        {
            m=max(m,Cx(p,o,3,79));m=max(m,Cx(p,o,4,70));m=max(m,Cx(p,o,5,70));
        }
        return m;
    }

    if(t==10)
    {
        m=max(m,Cx(p,o,0,86));m=max(m,Cx(p,o,1,73));m=max(m,Cx(p,o,2,83));
        m=max(m,Cx(p,o,3,73));m=max(m,Cx(p,o,4,66));
        m=max(m,Cx(p,o,6,84));m=max(m,Cx(p,o,7,69));
        m=max(m,Cx(p,o,8,83));m=max(m,Cx(p,o,9,84));
        return m;
    }

    m=max(m,Cx(p,o,0,68));m=max(m,Cx(p,o,1,87));m=max(m,Cx(p,o,2,77));
    m=max(m,Cx(p,o,3,32));m=max(m,Cx(p,o,5,48+a));
    if(t==9)
    {
        m=max(m,Cx(p,o,7,76));m=max(m,Cx(p,o,8,65));
        m=max(m,Cx(p,o,9,66));m=max(m,Cx(p,o,10,32));
    }
    else
    {
        m=max(m,Cx(p,o,7,83));m=max(m,Cx(p,o,8,84));
        m=max(m,Cx(p,o,9,79));m=max(m,Cx(p,o,10,80));
    }
    return m;
}

float DLine2(float2 p,float t,float b,float c)
{
    if(t<9||t>=14)return 0;
    float2 o=float2(8,24);
    float m=0;

    if(t==12||t==13)
    {
        m=max(m,Cx(p,o,0,70));m=max(m,Cx(p,o,1,56));
        m=max(m,Cx(p,o,3,83));m=max(m,Cx(p,o,4,84));
        m=max(m,Cx(p,o,5,65));m=max(m,Cx(p,o,6,84));
        m=max(m,Cx(p,o,7,85));m=max(m,Cx(p,o,8,83));
        return m;
    }

    if(t==9)
    {
        m=max(m,Cx(p,o,0,67));m=max(m,Cx(p,o,1,84));
        m=max(m,Cx(p,o,2,82));m=max(m,Cx(p,o,3,76));
        m=max(m,Cx(p,o,5,70));m=max(m,Cx(p,o,6,54));
        m=max(m,Cx(p,o,8,78));m=max(m,Cx(p,o,9,69));
        m=max(m,Cx(p,o,10,88));m=max(m,Cx(p,o,11,84));
    }
    else if(t==10)
    {
        m=max(m,Cx(p,o,0,70));m=max(m,Cx(p,o,1,56));
        m=max(m,Cx(p,o,3,82));m=max(m,Cx(p,o,4,69));
        m=max(m,Cx(p,o,5,83));m=max(m,Cx(p,o,6,85));
        m=max(m,Cx(p,o,7,76));m=max(m,Cx(p,o,8,84));
        m=max(m,Cx(p,o,9,83));
    }
    else
    {
        m=max(m,Cx(p,o,0,67));m=max(m,Cx(p,o,1,84));
        m=max(m,Cx(p,o,2,82));m=max(m,Cx(p,o,3,76));
        m=max(m,Cx(p,o,5,70));m=max(m,Cx(p,o,6,54));
        m=max(m,Cx(p,o,8,82));m=max(m,Cx(p,o,9,69));
        m=max(m,Cx(p,o,10,84));m=max(m,Cx(p,o,11,82));
        m=max(m,Cx(p,o,12,89));
    }
    return m;
}

float4 main(float2 pos:VPOS) : COLOR0
{
    float2 p=(pos-A.xy)/2.0;
    float t=floor(A.z+.5);
    if(t==0||p.x<0||p.y<0||p.x>=220||p.y>=48)discard;

    float m=t>=8?
        max(DLine1(p,t,floor(B.x+.5)),DLine2(p,t,floor(B.y+.5),floor(B.z+.5))):
        max(Line1(p,t,floor(A.w+.5),floor(B.x+.5)),Line2(p,t,floor(B.y+.5),floor(B.z+.5)));

    return float4(
        lerp(float3(.01,.012,.018),float3(1,1,1),m),1);
}
