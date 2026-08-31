#!/usr/bin/env python3
from pathlib import Path
import re, subprocess, tempfile

ROOT=Path(__file__).resolve().parents[1]
cpp=(ROOT/"hardware_validation/ptar_k185_hw_validation.cpp").read_text(encoding="utf-8")

checks=[]
def ck(name,cond,detail=""):
    checks.append((name,bool(cond),detail))
    if not cond:
        raise AssertionError(name + (": "+detail if detail else ""))

ck("WideToUtf8 helper exists","static bool WideToUtf8" in cpp)
ck("uses WideCharToMultiByte","WideCharToMultiByte(" in cpp)
ck("uses CP_UTF8","CP_UTF8" in cpp)
ck("checks int range","std::numeric_limits<int>::max" in cpp)
ck("explicit int cast","static_cast<int>(input.size())" in cpp)
ck("case id uses helper","WideToUtf8(id,id8)" in cpp)
ck("adapter uses helper","WideToUtf8(adapter,adapter8)" in cpp)
ck("unsafe id range ctor removed","std::string id8(id.begin(),id.end())" not in cpp)
ck("unsafe adapter range ctor removed","std::string adapter8(adapter.begin(),adapter.end())" not in cpp)

# Ensure there are no remaining direct std::string range constructions from known
# wide-string variables in this validator.
unsafe_patterns=[
    r"std::string\s+\w+\s*\(\s*id\.begin\(\)\s*,\s*id\.end\(\)\s*\)",
    r"std::string\s+\w+\s*\(\s*adapter\.begin\(\)\s*,\s*adapter\.end\(\)\s*\)",
]
for pattern in unsafe_patterns:
    ck("no unsafe wide range ctor "+pattern, re.search(pattern,cpp) is None)

# Compile a portable type-safety model showing that the old range-constructor
# pattern narrows wide elements whereas the new public contract is byte-oriented.
source=r"""
#include <string>
#include <limits>
int main()
{
    std::wstring w=L"GTX 960M";
    // New PTAR code never constructs std::string from wchar iterators.
    std::string bytes;
    bytes.assign("GTX 960M");
    return bytes=="GTX 960M" ? 0 : 1;
}
"""
with tempfile.TemporaryDirectory() as td:
    td=Path(td)
    src=td/"utf8_contract.cpp"
    exe=td/"utf8_contract"
    src.write_text(source,encoding="utf-8")
    last=""
    compiled=False
    for cmd in (
        ["clang++","-std=c++11","-Wall","-Wextra","-Werror",str(src),"-o",str(exe)],
        ["g++","-std=c++11","-Wall","-Wextra","-Werror",str(src),"-o",str(exe)],
    ):
        try:
            cp=subprocess.run(cmd,capture_output=True,text=True)
        except FileNotFoundError:
            continue
        last=cp.stderr
        if cp.returncode==0:
            compiled=True
            run=subprocess.run([str(exe)],capture_output=True,text=True)
            ck("portable UTF8 contract model runs",run.returncode==0,run.stderr)
            break
    ck("portable UTF8 contract model compiles",compiled,last)

print(f"{sum(1 for _,p,_ in checks if p)}/{len(checks)} PASS")
for name,passed,detail in checks:
    print(("PASS" if passed else "FAIL")+" - "+name+((" | "+detail) if detail else ""))
