"""
PTAR STEP-WENO reference primitive
==================================
Official continuation helper for the unfinished STEP-WENO experiment.

This module DOES NOT implement or reconstruct EDGE v03 routing/orientation.
It consumes the four directional samples already selected by EDGE v03.

Phase:
    1 -> 1/3 between f0 and f1
    2 -> 2/3 between f0 and f1

SW1:
    p=1, no monotone clamp.

SW2:
    p=2, component-wise monotone clamp to the central samples f0/f1.

SW3:
    SW2 blended with the caller-supplied EDGE baseline value using the
    caller-supplied existing EDGE confidence. No new gate is introduced.

epsilon is deliberately caller-supplied: the recovered project state did not
contain a locked epsilon value, so this reference does not invent one.
"""

INV9 = 1.0 / 9.0
BETA_CURV = 13.0 / 12.0
C13_L = 5.0 / 9.0
C13_R = 4.0 / 9.0
C23_L = 4.0 / 9.0
C23_R = 5.0 / 9.0


def _phase_constants(phase):
    if phase == 1:
        return C13_L, C13_R
    if phase == 2:
        return C23_L, C23_R
    raise ValueError("phase must be 1 (1/3) or 2 (2/3)")


def candidates_scalar(fm1, f0, f1, f2, phase):
    if phase == 1:
        ql = (-fm1 + 8.0 * f0 + 2.0 * f1) * INV9
        qr = (5.0 * f0 + 5.0 * f1 - f2) * INV9
    elif phase == 2:
        ql = (-fm1 + 5.0 * f0 + 5.0 * f1) * INV9
        qr = (2.0 * f0 + 8.0 * f1 - f2) * INV9
    else:
        raise ValueError("phase must be 1 (1/3) or 2 (2/3)")
    return ql, qr


def smoothness(lm1, l0, l1, l2):
    d_l = l0 - lm1
    d_c = l1 - l0
    d_r = l2 - l1
    beta_l = d_c * d_c + BETA_CURV * (d_c - d_l) * (d_c - d_l)
    beta_r = d_c * d_c + BETA_CURV * (d_r - d_c) * (d_r - d_c)
    return beta_l, beta_r


def weight_left(beta_l, beta_r, phase, power, epsilon):
    if epsilon <= 0.0:
        raise ValueError("epsilon must be > 0")
    c_l, c_r = _phase_constants(phase)
    s_l = epsilon + beta_l
    s_r = epsilon + beta_r

    # Algebraically equivalent to alpha_s = C_s/(epsilon + beta_s)^power
    # but requires only one final reciprocal/division.
    if power == 1:
        a_l = c_l * s_r
        a_r = c_r * s_l
    elif power == 2:
        a_l = c_l * s_r * s_r
        a_r = c_r * s_l * s_l
    else:
        raise ValueError("power must be 1 or 2")
    return a_l / (a_l + a_r)


def reconstruct_scalar(fm1, f0, f1, f2,
                       lm1, l0, l1, l2,
                       phase, power, epsilon,
                       monotone_clamp=False):
    ql, qr = candidates_scalar(fm1, f0, f1, f2, phase)
    beta_l, beta_r = smoothness(lm1, l0, l1, l2)
    wl = weight_left(beta_l, beta_r, phase, power, epsilon)
    value = wl * ql + (1.0 - wl) * qr
    if monotone_clamp:
        lo = min(f0, f1)
        hi = max(f0, f1)
        value = min(max(value, lo), hi)
    return value


def _vec4(v):
    if len(v) != 4:
        raise ValueError("expected 4 components")
    return tuple(float(x) for x in v)


def _candidate_vec4(fm1, f0, f1, f2, phase):
    fm1, f0, f1, f2 = map(_vec4, (fm1, f0, f1, f2))
    ql = []
    qr = []
    for c in range(4):
        a, b = candidates_scalar(fm1[c], f0[c], f1[c], f2[c], phase)
        ql.append(a)
        qr.append(b)
    return tuple(ql), tuple(qr)


def reconstruct_vec4(fm1, f0, f1, f2,
                     lm1, l0, l1, l2,
                     phase, power, epsilon,
                     monotone_clamp=False):
    fm1, f0, f1, f2 = map(_vec4, (fm1, f0, f1, f2))
    ql, qr = _candidate_vec4(fm1, f0, f1, f2, phase)
    beta_l, beta_r = smoothness(lm1, l0, l1, l2)
    wl = weight_left(beta_l, beta_r, phase, power, epsilon)
    out = []
    for c in range(4):
        v = wl * ql[c] + (1.0 - wl) * qr[c]
        if monotone_clamp:
            lo = min(f0[c], f1[c])
            hi = max(f0[c], f1[c])
            v = min(max(v, lo), hi)
        out.append(v)
    return tuple(out)


def sw1_vec4(fm1, f0, f1, f2, lm1, l0, l1, l2, phase, epsilon):
    return reconstruct_vec4(
        fm1, f0, f1, f2, lm1, l0, l1, l2,
        phase=phase, power=1, epsilon=epsilon,
        monotone_clamp=False
    )


def sw2_vec4(fm1, f0, f1, f2, lm1, l0, l1, l2, phase, epsilon):
    return reconstruct_vec4(
        fm1, f0, f1, f2, lm1, l0, l1, l2,
        phase=phase, power=2, epsilon=epsilon,
        monotone_clamp=True
    )


def sw3_vec4(edge_baseline_value,
             edge_confidence,
             fm1, f0, f1, f2,
             lm1, l0, l1, l2,
             phase, epsilon):
    base = _vec4(edge_baseline_value)
    candidate = sw2_vec4(
        fm1, f0, f1, f2, lm1, l0, l1, l2, phase, epsilon
    )
    confidence = min(max(float(edge_confidence), 0.0), 1.0)
    return tuple(
        base[c] + confidence * (candidate[c] - base[c])
        for c in range(4)
    )
