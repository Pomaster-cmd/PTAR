from dataclasses import dataclass

@dataclass(frozen=True)
class Case:
    name: str
    sw: int
    sh: int
    ow: int
    oh: int
    use_moe: bool = True
    q4: bool = True
    spec5: bool = True
    hq900: bool = True
    expected: str = ""


def aspect_fit(sw, sh, ow, oh):
    if not sw or not sh or not ow or not oh:
        return (0, 0)
    # All qualification cases preserve aspect. Keep exact integer fit semantics.
    lhs = ow * sh
    rhs = oh * sw
    if lhs <= rhs:
        fw = ow
        fh = (ow * sh) // sw
    else:
        fh = oh
        fw = (oh * sw) // sh
    return fw, fh


def exact15(sw, sh, fw, fh):
    return sw * 3 == fw * 2 and sh * 3 == fh * 2


def hq900_eligible(sw, sh, fw, fh):
    if not sw or not sh or not fw or not fh:
        return False
    if sw * sh > 1_440_000:
        return False
    if fw > 1920 or fh > 1080 or fw * fh > 2_073_600:
        return False
    sx = fw / sw
    sy = fh / sh
    return 1.0 < sx <= 1.5 and 1.0 < sy <= 1.5


def route(c: Case):
    if c.sw == c.ow and c.sh == c.oh:
        return "NATIVE_1X1"
    fw, fh = aspect_fit(c.sw, c.sh, c.ow, c.oh)
    is15 = exact15(c.sw, c.sh, fw, fh)
    q720 = c.sw == 1280 and c.sh == 720 and fw == 1920 and fh == 1080
    hq = (not is15) and hq900_eligible(c.sw, c.sh, fw, fh)
    if not c.use_moe:
        return "BILINEAR"
    if q720 and c.spec5:
        return "SPEC5"
    if is15 and c.q4:
        return "QSTAGE4"
    if hq and c.hq900:
        return "HQ900"
    if is15:
        return "SF5_FAILOPEN"
    return "BILINEAR"


cases = [
    Case("q720_primary",1280,720,1920,1080,expected="SPEC5"),
    Case("exact15_other",960,540,1440,810,expected="QSTAGE4"),
    Case("hq900_1600x900",1600,900,1920,1080,expected="HQ900"),
    Case("hq900_1440x810",1440,810,1920,1080,expected="HQ900"),
    Case("profile1_bilinear",1280,720,1920,1080,use_moe=False,expected="BILINEAR"),
    Case("native",1920,1080,1920,1080,expected="NATIVE_1X1"),
    Case("over_1p5",1280,720,2560,1440,expected="BILINEAR"),
    Case("input_over_1p44m",1920,1080,2304,1296,expected="BILINEAR"),
    Case("q720_spec5_missing_uses_q4",1280,720,1920,1080,spec5=False,expected="QSTAGE4"),
    Case("exact15_q4_missing_uses_sf5",960,540,1440,810,q4=False,expected="SF5_FAILOPEN"),
    Case("hq900_missing_bilinear",1600,900,1920,1080,hq900=False,expected="BILINEAR"),
]

failures=[]
for c in cases:
    got=route(c)
    ok=got==c.expected
    print(f"FULLQUALITY_SELECTOR_{c.name.upper()}={'PASS' if ok else 'FAIL'} got={got} expected={c.expected}")
    if not ok:
        failures.append((c.name,got,c.expected))

if failures:
    print(f"FULLQUALITY_SELECTOR_ALL=FAIL count={len(failures)}")
    raise SystemExit(1)

print("FULLQUALITY_SELECTOR_ALL=PASS")
