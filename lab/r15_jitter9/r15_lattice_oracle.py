#!/usr/bin/env python3
import math

SRC_W=1280
SRC_H=720
OUT_W=1920
OUT_H=1080

# Calibrated D3D9 integer-raster-center convention.
#
# At exact 3:2 scaling, source pixel centres map according to
#     x_out = 1.5*x_src + 1.5*jitter
# for the screen-space sampling displacement used by the runtime patcher.
# The three deliberate sample jitters therefore own output residue classes
# {1,0,2} respectively.  The odd/even source subset avoids duplicate samples.
#
# phase_class, sample_jitter, source_index_parity, output_residue
PHASES=(
    (0, -1.0/3.0, 1, 1),
    (1,  0.0,       0, 0),
    (2,  1.0/3.0, 1, 2),
)


def output_coord(source_index,jitter):
    return 1.5*source_index + 1.5*jitter


def axis_coverage(src_n,out_n):
    owner=[None]*out_n
    for phase_class,jitter,parity,residue in PHASES:
        for s in range(parity,src_n,2):
            xf=output_coord(s,jitter)
            x=int(round(xf))
            if abs(xf-x)>1.0e-9:
                raise RuntimeError(
                    'non-integral lattice sample phase=%d src=%d coord=%.12f' %
                    (phase_class,s,xf))
            if x<0 or x>=out_n:
                continue
            if (x % 3) != residue:
                raise RuntimeError(
                    'residue mismatch phase=%d src=%d out=%d residue=%d expected=%d' %
                    (phase_class,s,x,x%3,residue))
            if owner[x] is not None:
                raise RuntimeError(
                    'duplicate output sample x=%d owners=%r,%r' %
                    (x,owner[x],(phase_class,s)))
            owner[x]=(phase_class,s,residue)
    missing=[i for i,v in enumerate(owner) if v is None]
    if missing:
        raise RuntimeError('missing output samples: %r' % missing[:16])
    return owner


x_owner=axis_coverage(SRC_W,OUT_W)
y_owner=axis_coverage(SRC_H,OUT_H)

# Prove that the Cartesian product of the three calibrated phase classes owns
# every 1080p pixel exactly once, even though phase class != output residue for
# classes 0 and 1.
phase_counts=[[0 for _ in range(3)] for _ in range(3)]
for y in range(OUT_H):
    py,_,_=y_owner[y]
    for x in range(OUT_W):
        px,_,_=x_owner[x]
        phase_counts[py][px]+=1

expected=(SRC_W//2)*(SRC_H//2)
for py in range(3):
    for px in range(3):
        if phase_counts[py][px]!=expected:
            raise RuntimeError(
                'phase (%d,%d) count=%d expected=%d' %
                (px,py,phase_counts[py][px],expected))

# Analytic high-frequency signal: every recovered physical source sample must
# land at the exact target-output coordinate. No spatial interpolation is used.
def signal(x,y):
    return (
        0.45 +
        0.20*math.sin(2.0*math.pi*0.41*x + 0.31) +
        0.16*math.sin(2.0*math.pi*0.37*y - 0.27) +
        0.10*math.sin(2.0*math.pi*(0.39*x+0.34*y) + 0.73)
    )

phase_by_class={phase:(jitter,parity,residue) for phase,jitter,parity,residue in PHASES}
max_error=0.0
for y in list(range(0,OUT_H,17))+[OUT_H-1]:
    py,sy,ry=y_owner[y]
    jy=phase_by_class[py][0]
    yf=output_coord(sy,jy)
    if int(round(yf))%3 != ry:
        raise RuntimeError('Y ownership residue drift')
    for x in list(range(0,OUT_W,19))+[OUT_W-1]:
        px,sx,rx=x_owner[x]
        jx=phase_by_class[px][0]
        xf=output_coord(sx,jx)
        if int(round(xf))%3 != rx:
            raise RuntimeError('X ownership residue drift')
        recovered=signal(xf,yf)
        reference=signal(float(x),float(y))
        max_error=max(max_error,abs(recovered-reference))

if max_error>1.0e-12:
    raise RuntimeError('analytic reconstruction error %.15g' % max_error)

print('R15_LATTICE_CONVENTION=D3D9_INTEGER_CENTER')
print('R15_PHASE0_JITTER=-1/3_RESIDUE=1_PARITY=ODD')
print('R15_PHASE1_JITTER=0_RESIDUE=0_PARITY=EVEN')
print('R15_PHASE2_JITTER=+1/3_RESIDUE=2_PARITY=ODD')
print('R15_LATTICE_X=PASS samples=%d' % len(x_owner))
print('R15_LATTICE_Y=PASS samples=%d' % len(y_owner))
print('R15_PHASE_COMBINATIONS=9')
print('R15_SAMPLES_PER_PHASE=%d' % expected)
print('R15_OUTPUT_PIXELS=%d' % (OUT_W*OUT_H))
print('R15_EXACT_LATTICE_MAX_ERROR=%.15g' % max_error)
print('R15_TRUE720_JITTER9_ORACLE=PASS')
