#!/usr/bin/env python3
import importlib.util
import math
import random
from pathlib import Path

ROOT = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("sw", ROOT / "ptar_step_weno_reference.py")
sw = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sw)

SEED = 0x50544152
rng = random.Random(SEED)
EPSILONS = (1e-12, 1e-10, 1e-8, 1e-6, 1e-4)

checks = []
def record(name, ok, detail=""):
    checks.append((name, bool(ok), detail))
    if not ok:
        raise AssertionError(f"{name}: {detail}")

def lagrange4(vals, t):
    xs = (-1.0, 0.0, 1.0, 2.0)
    out = 0.0
    for i, xi in enumerate(xs):
        li = 1.0
        for j, xj in enumerate(xs):
            if i != j:
                li *= (t - xj) / (xi - xj)
        out += vals[i] * li
    return out

# 1) Ideal weights reproduce the exact four-point cubic interpolant.
max_cubic_err = 0.0
for _ in range(100000):
    v = [rng.uniform(-4.0, 4.0) for _ in range(4)]
    for phase, t, cl in ((1, 1.0/3.0, 5.0/9.0), (2, 2.0/3.0, 4.0/9.0)):
        ql, qr = sw.candidates_scalar(*v, phase)
        got = cl * ql + (1.0 - cl) * qr
        ref = lagrange4(v, t)
        max_cubic_err = max(max_cubic_err, abs(got-ref))
record("ideal phase weights reproduce cubic interpolation",
       max_cubic_err < 5e-15, f"max_abs_error={max_cubic_err:.3e}")

# 2) Smoothness formula equals exact integral of p'^2 + p''^2 over [0,1].
def quad_coeff(nodes, vals):
    # Return a,b,c for p(x)=a*x^2+b*x+c using three Lagrange basis polynomials.
    coeff = [0.0, 0.0, 0.0]  # c,b,a
    for i, xi in enumerate(nodes):
        others = [nodes[j] for j in range(3) if j != i]
        den = (xi-others[0])*(xi-others[1])
        # (x-u)(x-v) = x^2 -(u+v)x + uv
        u,v = others
        coeff[2] += vals[i] / den
        coeff[1] += vals[i] * (-(u+v)) / den
        coeff[0] += vals[i] * (u*v) / den
    return coeff[2], coeff[1], coeff[0]

def smooth_integral(nodes, vals):
    a,b,_ = quad_coeff(nodes, vals)
    # p'=2ax+b ; p''=2a
    # integral_0^1 (p')^2 dx + integral_0^1 (p'')^2 dx
    return (4.0*a*a/3.0 + 2.0*a*b + b*b) + 4.0*a*a

max_beta_err = 0.0
for _ in range(100000):
    v = [rng.uniform(-4.0, 4.0) for _ in range(4)]
    bl, br = sw.smoothness(*v)
    il = smooth_integral((-1.0,0.0,1.0), v[:3])
    ir = smooth_integral((0.0,1.0,2.0), v[1:])
    max_beta_err = max(max_beta_err, abs(bl-il), abs(br-ir))
record("closed-form smoothness equals exact polynomial integral",
       max_beta_err < 2e-13, f"max_abs_error={max_beta_err:.3e}")

# 3) Constant, affine and quadratic signals are reproduced.
max_poly_err = 0.0
for _ in range(50000):
    a = rng.uniform(-2,2)
    b = rng.uniform(-2,2)
    c = rng.uniform(-2,2)
    for degree in (0,1,2):
        def f(x):
            if degree == 0: return c
            if degree == 1: return b*x+c
            return a*x*x+b*x+c
        vals = [f(-1),f(0),f(1),f(2)]
        for phase,t in ((1,1/3),(2,2/3)):
            for power in (1,2):
                for eps in EPSILONS:
                    got=sw.reconstruct_scalar(*vals,*vals,phase,power,eps,False)
                    max_poly_err=max(max_poly_err,abs(got-f(t)))
record("SW1/SW2 core reproduces polynomials through degree 2",
       max_poly_err < 2e-12, f"max_abs_error={max_poly_err:.3e}")

# 4) Mirror symmetry 1/3 <-> 2/3.
max_sym_err = 0.0
for _ in range(100000):
    v=[rng.uniform(-8,8) for _ in range(4)]
    l=[rng.uniform(-8,8) for _ in range(4)]
    for power,clamp in ((1,False),(2,True)):
        for eps in EPSILONS:
            a=sw.reconstruct_scalar(*v,*l,1,power,eps,clamp)
            b=sw.reconstruct_scalar(*reversed(v),*reversed(l),2,power,eps,clamp)
            max_sym_err=max(max_sym_err,abs(a-b))
record("phase mirror symmetry",
       max_sym_err < 5e-13, f"max_abs_error={max_sym_err:.3e}")

# 5) Weight positivity, normalization and finite stress.
min_w=1.0; max_w=0.0
for _ in range(200000):
    l=[rng.uniform(-32,32) for _ in range(4)]
    bl,br=sw.smoothness(*l)
    for phase in (1,2):
        for power in (1,2):
            for eps in EPSILONS:
                w=sw.weight_left(bl,br,phase,power,eps)
                if not math.isfinite(w):
                    record("weights finite", False, f"w={w}")
                min_w=min(min_w,w); max_w=max(max_w,w)
record("weights remain in [0,1]",
       min_w >= 0.0 and max_w <= 1.0,
       f"min={min_w:.17g}, max={max_w:.17g}")

# 6) SW2 component-wise monotone clamp.
worst_clamp = 0.0
for _ in range(200000):
    v=[rng.uniform(-16,16) for _ in range(4)]
    l=[rng.uniform(-16,16) for _ in range(4)]
    for phase in (1,2):
        for eps in EPSILONS:
            got=sw.reconstruct_scalar(*v,*l,phase,2,eps,True)
            lo=min(v[1],v[2]); hi=max(v[1],v[2])
            violation=max(lo-got, got-hi, 0.0)
            worst_clamp=max(worst_clamp,violation)
record("SW2 monotone clamp never exits central interval",
       worst_clamp == 0.0, f"max_violation={worst_clamp:.3e}")

# 7) SW3 endpoints and convexity.
max_sw3_endpoint=0.0
max_sw3_convex_violation=0.0
for _ in range(50000):
    vecs=[[rng.uniform(-4,4) for _ in range(4)] for __ in range(4)]
    l=[rng.uniform(-4,4) for _ in range(4)]
    base=tuple(rng.uniform(-4,4) for _ in range(4))
    phase=rng.choice((1,2)); eps=rng.choice(EPSILONS)
    sw2=sw.sw2_vec4(*vecs,*l,phase,eps)
    z=sw.sw3_vec4(base,0.0,*vecs,*l,phase,eps)
    o=sw.sw3_vec4(base,1.0,*vecs,*l,phase,eps)
    max_sw3_endpoint=max(max_sw3_endpoint,
                         max(abs(z[i]-base[i]) for i in range(4)),
                         max(abs(o[i]-sw2[i]) for i in range(4)))
    conf=rng.random()
    m=sw.sw3_vec4(base,conf,*vecs,*l,phase,eps)
    for i in range(4):
        lo=min(base[i],sw2[i]); hi=max(base[i],sw2[i])
        max_sw3_convex_violation=max(max_sw3_convex_violation,
                                     max(lo-m[i],m[i]-hi,0.0))
record("SW3 confidence endpoints are exact",
       max_sw3_endpoint < 2e-15, f"max_abs_error={max_sw3_endpoint:.3e}")
record("SW3 remains a convex blend of EDGE baseline and SW2",
       max_sw3_convex_violation == 0.0,
       f"max_violation={max_sw3_convex_violation:.3e}")

# 8) HLSL helper structural contract.
hlsl_path = ROOT/"ptar_step_weno_lite.hlsli"
if not hlsl_path.is_file():
    hlsl_path = ROOT.parent/"hlsl"/"ptar_step_weno_lite.hlsli"
hlsl=hlsl_path.read_text(encoding="utf-8")
upper=hlsl.upper()
record("HLSL runtime helper has no NIS token", "NIS" not in upper)
record("HLSL helper performs no texture/resource access",
       all(tok not in upper for tok in ("TEXTURE2D","RWTEXTURE","SAMPLERSTATE","CREATETEXTURE")))
record("HLSL helper contains both fixed x1.5 phases",
       "PTARSTEPCANDIDATES13" in upper and "PTARSTEPCANDIDATES23" in upper)
record("HLSL helper contains SW1/SW2/SW3",
       all(tok in upper for tok in ("PTARSTEPSW1_13","PTARSTEPSW2_13","PTARSTEPSW3_13",
                                    "PTARSTEPSW1_23","PTARSTEPSW2_23","PTARSTEPSW3_23")))

passed=sum(1 for _,ok,_ in checks if ok)
report = [
    "PTAR STEP-WENO FORMAL / STRUCTURAL VALIDATION",
    "=============================================",
    f"seed=0x{SEED:08X}",
    f"checks={passed}/{len(checks)} PASS",
    "",
]
for name,ok,detail in checks:
    report.append(f"{'PASS' if ok else 'FAIL'} - {name}" + (f" | {detail}" if detail else ""))
report += [
    "",
    "Scope:",
    "- Validates the recovered STEP-WENO primitive and SW1/SW2/SW3 definitions.",
    "- Does NOT claim image-quality non-regression versus EDGE v03.",
    "- Does NOT replace the missing original PTAR corpus/harness.",
    "- epsilon remains caller-supplied because no locked project epsilon was recovered.",
]
(ROOT/"STEP_WENO_FORMAL_VALIDATION.txt").write_text("\n".join(report)+"\n", encoding="utf-8")
print("\n".join(report))
