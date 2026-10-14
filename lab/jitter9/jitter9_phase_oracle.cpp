#include <cstdio>
#include <cmath>

static double Signal(double x,double y,int c)
{
    const double a=0.31+0.07*c;
    const double b=0.47+0.05*c;
    return 0.50+
        0.19*std::sin((3.7+c)*x+a*y)+
        0.17*std::cos(b*x+(4.3+0.2*c)*y)+
        0.09*std::sin(7.1*x-5.9*y+0.3*c);
}

int main()
{
    // For exact 3:2 scaling, target pixels i mod 3 map to source-space
    // fractions {5/6,1/2,1/6}. Rendering the 720p scene with geometry shifts
    // {+1/6,+1/2,-1/6} source pixels makes an integer source pixel land on
    // each of those target locations respectively.
    const double delta[3]={1.0/6.0,1.0/2.0,-1.0/6.0};
    double maxErr=0.0;
    int tested=0;

    for(int y=3;y<105;++y)
    for(int x=3;x<189;++x)
    {
        const double px=((double)x+0.5)/1.5-0.5;
        const double py=((double)y+0.5)/1.5-0.5;
        const int phaseX=x%3;
        const int phaseY=y%3;
        const int nx=(int)std::floor(px+0.5);
        const int ny=(int)std::floor(py+0.5);

        // A source pixel n rendered after a +delta geometry shift samples the
        // unjittered scene at n-delta.
        const double sx=(double)nx-delta[phaseX];
        const double sy=(double)ny-delta[phaseY];

        for(int c=0;c<3;++c)
        {
            const double reference=Signal(px,py,c);
            const double reconstructed=Signal(sx,sy,c);
            const double e=std::fabs(reference-reconstructed);
            if(e>maxErr) maxErr=e;
        }
        ++tested;
    }

    std::printf("JITTER9_PHASE_PIXELS=%d\n",tested);
    std::printf("JITTER9_PHASE_MAX_ERROR=%.12g\n",maxErr);
    const bool pass=maxErr<1.0e-12;
    std::printf("JITTER9_EXACT_3X3_LATTICE=%s\n",pass?"PASS":"FAIL");
    return pass?0:1;
}
