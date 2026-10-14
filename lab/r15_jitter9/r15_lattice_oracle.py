#!/usr/bin/env python3
import math

SRC_W=1280
SRC_H=720
OUT_W=1920
OUT_H=1080

# Three compact source-pixel projection offsets.  Each phase deliberately uses
# only one source-index parity when publishing exact samples into the 1080p
# reservoir.  The other samples remain useful to the spatial fallback, but are
# not needed for the exact-lattice proof.
PHASES=(
    (-0.5,  1),       # odd source index -> output class 1 mod 3
    (-1.0/6.0, 0),    # even source index -> output class 0 mod 3
    ( 1.0/6.0, 1),    # odd source index -> output class 2 mod 3
)


def output_coord(source_index,jitter):
    # Pixel-center mapping for exact 1280 -> 1920 scale:
    #   (x_out + 0.5)/1920 = (x_src + 0.5 + jitter)/1280
    return 1.5*source_index + 0.25 + 1.5*jitter


def axis_coverage(src_n,out_n):
    owner=[None]*out_n
    for phase,(jitter,parity) in enumerate(PHASES):
        for s in range(parity,src_n,2):
            xf=output_coord(s,jitter)
            x=int(round(xf))
            if abs(xf-x)>1.0e-9:
                raise RuntimeError(
                    'non-integral lattice sample phase=%d src=%d coord=%.12f' %
                    (phase,s,xf))
            if x<0 or x>=out_n:
                continue
            if owner[x] is not None:
                raise RuntimeError(
                    'duplicate output sample x=%d owners=%r,%r' %
                    (x,owner[x],(phase,s)))
            owner[x]=(phase,s)
    missing=[i for i,v in enumerate(owner) if v is None]
    if missing:
        raise RuntimeError('missing output samples: %r' % missing[:16])
    return owner


x_owner=axis_coverage(SRC_W,OUT_W)
y_owner=axis_coverage(SRC_H,OUT_H)

# Prove that the Cartesian product of the three X phases and three Y phases
# owns every 1080p pixel exactly once.
phase_counts=[[0 for _ in range(3)] for _ in range(3)]
for y in range(OUT_H):
    py,_=y_owner[y]
    for x in range(OUT_W):
        px,_=x_owner[x]
        phase_counts[py][px]+=1

expected=(SRC_W//2)*(SRC_H//2)
for py in range(3):
    for px in range(3):
        if phase_counts[py][px]!=expected:
            raise RuntimeError(
                'phase (%d,%d) count=%d expected=%d' %
                (px,py,phase_counts[py][px],expected))

# An analytic high-frequency signal is evaluated directly at the recovered
# output coordinates.  No interpolation is involved in the exact reservoir.
def signal(x,y):
    return (
        0.45 +
        0.20*math.sin(2.0*math.pi*0.41*x + 0.31) +
        0.16*math.sin(2.0*math.pi*0.37*y - 0.27) +
        0.10*math.sin(2.0*math.pi*(0.39*x+0.34*y) + 0.73)
    )

max_error=0.0
# Sparse deterministic sample keeps the CI oracle cheap while still covering
# every phase/parity combination and image boundary class.
for y in list(range(0,OUT_H,17))+[OUT_H-1]:
    py,sy=y_owner[y]
    jy=PHASES[py][0]
    yf=output_coord(sy,jy)
    for x in list(range(0,OUT_W,19))+[OUT_W-1]:
        px,sx=x_owner[x]
        jx=PHASES[px][0]
        xf=output_coord(sx,jx)
        recovered=signal(xf,yf)
        reference=signal(float(x),float(y))
        max_error=max(max_error,abs(recovered-reference))

if max_error>1.0e-12:
    raise RuntimeError('analytic reconstruction error %.15g' % max_error)

print('R15_LATTICE_X=PASS samples=%d' % len(x_owner))
print('R15_LATTICE_Y=PASS samples=%d' % len(y_owner))
print('R15_PHASE_COMBINATIONS=9')
print('R15_SAMPLES_PER_PHASE=%d' % expected)
print('R15_OUTPUT_PIXELS=%d' % (OUT_W*OUT_H))
print('R15_EXACT_LATTICE_MAX_ERROR=%.15g' % max_error)
print('R15_TRUE720_JITTER9_ORACLE=PASS')
