"""
Reference adapter mirroring the HLSL EDGE v04 STEP-WENO insertion point.
It deliberately does not reconstruct EDGE v03 direction/orientation.
"""
from ptar_step_weno_reference import sw1_vec4, sw2_vec4, sw3_vec4

MODE_BASELINE = 0
MODE_SW1 = 1
MODE_SW2 = 2
MODE_SW3 = 3

def edge_v04(mode, edge_v03_baseline, edge_confidence,
             fm1, f0, f1, f2, lm1, l0, l1, l2, phase, epsilon):
    if mode == MODE_BASELINE:
        return tuple(float(x) for x in edge_v03_baseline)
    if mode == MODE_SW1:
        return sw1_vec4(fm1, f0, f1, f2, lm1, l0, l1, l2, phase, epsilon)
    if mode == MODE_SW2:
        return sw2_vec4(fm1, f0, f1, f2, lm1, l0, l1, l2, phase, epsilon)
    if mode == MODE_SW3:
        return sw3_vec4(edge_v03_baseline, edge_confidence,
                        fm1, f0, f1, f2, lm1, l0, l1, l2, phase, epsilon)
    raise ValueError("mode must be 0..3")
