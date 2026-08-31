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

ck("RAII guard class exists","class ComApartmentGuard" in cpp)
ck("guard initializes COM","CoInitializeEx(0,flags)" in cpp)
ck("guard destructor uninitializes COM","~ComApartmentGuard()" in cpp and "CoUninitialize();" in cpp)
ck("exactly one CoUninitialize remains",cpp.count("CoUninitialize();")==1,str(cpp.count("CoUninitialize();")))
ck("guard declared in wmain","ComApartmentGuard com(COINIT_MULTITHREADED);" in cpp)

wmain=cpp[cpp.find("int wmain("):]
guard_pos=wmain.find("ComApartmentGuard com")
wic_pos=wmain.find("ComPtr<IWICImagingFactory> wic")
gpu_pos=wmain.find("D3D11Harness gpu")
ck("guard declared before WIC",0 <= guard_pos < wic_pos)
ck("guard declared before D3D harness",0 <= guard_pos < gpu_pos)

ck("D3D destructor exists","~D3D11Harness()" in cpp)
ck("D3D destructor clears state","m_context->ClearState();" in cpp)
ck("cleanup marker exists","[CLEANUP] Validation complete" in cpp)

# Verify no error path manually shuts COM down before local smart pointers die.
ck("no manual CoUninitialize in wmain","CoUninitialize();" not in wmain)

# Portable RAII destruction-order model:
source=r"""
#include <vector>
struct Guard {
  std::vector<int>* log;
  explicit Guard(std::vector<int>* p):log(p){}
  ~Guard(){ log->push_back(3); }
};
struct Wic {
  std::vector<int>* log;
  explicit Wic(std::vector<int>* p):log(p){}
  ~Wic(){ log->push_back(2); }
};
struct Gpu {
  std::vector<int>* log;
  explicit Gpu(std::vector<int>* p):log(p){}
  ~Gpu(){ log->push_back(1); }
};
int main() {
  std::vector<int> log;
  {
    Guard com(&log);
    Wic wic(&log);
    Gpu gpu(&log);
  }
  return (log.size()==3 && log[0]==1 && log[1]==2 && log[2]==3) ? 0 : 1;
}
"""
with tempfile.TemporaryDirectory() as td:
    td=Path(td)
    src=td/"raii.cpp"; exe=td/"raii"
    src.write_text(source,encoding="utf-8")
    compiled=False; last=""
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
            ck("RAII destruction-order model runs",run.returncode==0,run.stderr)
            break
    ck("RAII destruction-order model compiles",compiled,last)

print(f"{sum(1 for _,p,_ in checks if p)}/{len(checks)} PASS")
for n,p,d in checks:
    print(("PASS" if p else "FAIL")+" - "+n+((" | "+d) if d else ""))
