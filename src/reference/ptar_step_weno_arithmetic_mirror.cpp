#include <cmath>
#include <cstdint>
#include <cstdio>

static double beta_l(double fm1, double f0, double f1) {
    const double dL = f0 - fm1;
    const double dC = f1 - f0;
    const double eL = dC - dL;
    return dC*dC + (13.0/12.0)*eL*eL;
}
static double beta_r(double f0, double f1, double f2) {
    const double dC = f1 - f0;
    const double dR = f2 - f1;
    const double eR = dR - dC;
    return dC*dC + (13.0/12.0)*eR*eR;
}
static double wl_p1(double bL, double bR, double cL, double cR, double eps) {
    const double aL = cL * (eps + bR);
    const double aR = cR * (eps + bL);
    return aL / (aL + aR);
}
static double wl_p2(double bL, double bR, double cL, double cR, double eps) {
    const double sL = eps + bL, sR = eps + bR;
    const double aL = cL*sR*sR;
    const double aR = cR*sL*sL;
    return aL / (aL + aR);
}
int main() {
    const double e = 1e-8;
    const double bL = beta_l(0.0, 1.0, 2.0);
    const double bR = beta_r(1.0, 2.0, 3.0);
    const double w1 = wl_p1(bL,bR,5.0/9.0,4.0/9.0,e);
    const double w2 = wl_p2(bL,bR,5.0/9.0,4.0/9.0,e);
    if (!(std::isfinite(w1) && std::isfinite(w2))) return 1;
    if (std::fabs(w1 - 5.0/9.0) > 1e-12) return 2;
    if (std::fabs(w2 - 5.0/9.0) > 1e-12) return 3;
    std::puts("PTAR STEP-WENO C++ arithmetic mirror: PASS");
    return 0;
}
