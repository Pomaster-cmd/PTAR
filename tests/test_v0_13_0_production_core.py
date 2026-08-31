#!/usr/bin/env python3
from pathlib import Path
import hashlib,re,json

ROOT=Path(__file__).resolve().parents[1]
C=ROOT/"runtime_production/ptar_moe_v01_core"
cpp=(C/"ptar_moe_v01_runtime.cpp").read_text(encoding="utf-8")
hdr=(C/"ptar_moe_v01_runtime.h").read_text(encoding="utf-8")
emb=(C/"ptar_moe_v01_embedded_dxbc.h").read_text(encoding="utf-8")
base=json.loads((C/"EXPECTED_INTEGRATION_BASE.json").read_text())

checks=[]
def ck(n,c):
    checks.append((n,bool(c)))
    if not c: raise AssertionError(n)

ck("runtime create VS","CreateVertexShader" in cpp)
ck("runtime create PS","CreatePixelShader" in cpp)
ck("no D3DCompile","D3DCompile" not in cpp+hdr)
ck("no D3DDisassemble","D3DDisassemble" not in cpp+hdr)
ck("no UAV","RWTexture" not in cpp+hdr and "UnorderedAccess" not in cpp+hdr)
ck("no Flush","->Flush(" not in cpp)
ck("no Present","->Present(" not in cpp)
ck("no wait","WaitForSingleObject" not in cpp)
ck("one Draw","context->Draw(3,0)" in cpp)
ck("exact x1.5 integer gate","dstW*2ull" in cpp and "srcW*3ull" in cpp)
ck("non x1.5 S_FALSE","return S_FALSE;" in cpp)
ck("linear clamp sampler","D3D11_FILTER_MIN_MAG_MIP_LINEAR" in cpp and "D3D11_TEXTURE_ADDRESS_CLAMP" in cpp)
ck("dynamic constants","D3D11_USAGE_DYNAMIC" in cpp and "D3D11_MAP_WRITE_DISCARD" in cpp)
ck("SRV unbound after draw","PSSetShaderResources(0,1,&nullSrv)" in cpp)
ck("embedded VS DXBC", "g_ptarMoeVsDxbc" in emb and "0x44, 0x58, 0x42, 0x43" in emb)
ck("embedded MoE DXBC","g_ptarMoePsDxbc" in emb)
ck("embedded K185 control DXBC","g_ptarK185ControlPsDxbc" in emb)
ck("expected base zip hash",base["expected_base_zip_sha256"]=="a8185065db6ec60d3e7be06fe108386ba1bb7955a35e46b64cfc463f6065ee3e")
ck("expected base dll hash",base["expected_base_dll_sha256"]=="1e5e415bb09b33f8d4631f9d78f8f9734cd4cae4316cc6901d032cdcebb54420")
ck("package count 444",base["expected_package_files"]==444)

print(f"{sum(v for _,v in checks)}/{len(checks)} PASS")
for n,v in checks:
    print(("PASS" if v else "FAIL")+" - "+n)
