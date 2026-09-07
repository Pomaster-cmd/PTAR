#!/usr/bin/env python3
from pathlib import Path
import base64, hashlib, json, lzma, os, shutil, subprocess, tarfile, tempfile

ROOT = Path(__file__).resolve().parents[1]
STAGE = ROOT / ".ptar_final"
CHUNKS = ["c00","c01","c02","c03","c04_05","c06_07","c08_09","c10","c11_12","c13_14","c15_16","c17_18","c19_20"]
EXPECTED_B64_SHA256 = "529d3c31e9b9f053871038d9509d434cd97c134dc1946bf538bff0efb897c9ad"
EXPECTED_XZ_SHA256 = "dcab3e1f058d3520cc0dae8b29a33cf968ba6b7ff7fd81e077d7e3cbebefac57"
EXPECTED_TAR_SHA256 = "683fa992a6b671a3548f9939b690645e3a417c52bdcdd75081c3849c0b457041"
GW12_SHA256 = "50cf02fee971e615f0dba26a7614e27b833486a993cf569fe5369a0fa5b41f59"
FUSED_SHA256 = "613714f5ac70bc94867a3044dd067f4de18bb3ef65262c60f0febce2ca9d4c70"
RUNTIME_SHA256 = "864c0ca8f24f22f6a3cd4c21a0e213f431f5268e69b04e3860c52fe72600fc3c"
LICENSE_GIT_BLOB = "f288702d2fa16d3cdf0035b15a9fcbc552cd88e7"

def sha256(b): return hashlib.sha256(b).hexdigest()
def gitblob(b): return hashlib.sha1(b"blob "+str(len(b)).encode()+b"\0"+b).hexdigest()
def die(msg): raise SystemExit("SAFEPOINT11 FINALIZE ERROR: "+msg)

def safe_extract(tf, dst):
    base = dst.resolve()
    for m in tf.getmembers():
        target = (dst / m.name).resolve()
        if target != base and base not in target.parents:
            die("unsafe tar path: "+m.name)
    tf.extractall(dst)

def run(*args):
    print("+", *args, flush=True)
    subprocess.run(args, cwd=ROOT, check=True)

# Preserve repository-owned LICENSE and the exact GW12 source blob before cleanup.
license_bytes = (ROOT/"LICENSE").read_bytes()
if gitblob(license_bytes) != LICENSE_GIT_BLOB:
    die("LICENSE blob mismatch")
gw12_path = ROOT/"diag/base/GW12_BASE.dll"
if not gw12_path.is_file(): die("GW12_BASE.dll missing")
gw12 = gw12_path.read_bytes()
if sha256(gw12) != GW12_SHA256: die("GW12_BASE.dll SHA-256 mismatch")

expected = json.loads((STAGE/"expected.json").read_text(encoding="utf-8"))
if len(expected) != 106: die(f"expected manifest count {len(expected)} != 106")

b64 = b"".join((STAGE/n).read_bytes() for n in CHUNKS)
if sha256(b64) != EXPECTED_B64_SHA256: die("staged base64 SHA-256 mismatch")
xz = base64.b64decode(b64, validate=True)
if sha256(xz) != EXPECTED_XZ_SHA256: die("decoded xz SHA-256 mismatch")
tar_bytes = lzma.decompress(xz)
if sha256(tar_bytes) != EXPECTED_TAR_SHA256: die("tar SHA-256 mismatch")

with tempfile.TemporaryDirectory(prefix="ptar-s11-") as td:
    out = Path(td)/"out"
    out.mkdir()
    tar_path = Path(td)/"payload.tar"
    tar_path.write_bytes(tar_bytes)
    with tarfile.open(tar_path, "r:") as tf:
        safe_extract(tf, out)

    fused_delta_p = out/"diag/base/FUSEDDETAIL1_FROM_GW12.xor.xz"
    runtime_delta_p = out/"diag/RUNTIME_FROM_GW12.xor.xz"
    fd = lzma.decompress(fused_delta_p.read_bytes())
    rd = lzma.decompress(runtime_delta_p.read_bytes())
    if len(fd) != len(gw12) or len(rd) != len(gw12): die("delta length mismatch")
    fused = bytes(a^b for a,b in zip(gw12,fd))
    runtime = bytes(a^b for a,b in zip(gw12,rd))
    if sha256(fused) != FUSED_SHA256: die("FUSEDDETAIL1 reconstruction mismatch")
    if sha256(runtime) != RUNTIME_SHA256: die("runtime reconstruction mismatch")

    fused_delta_p.unlink()
    runtime_delta_p.unlink()
    (out/"diag/base/GW12_BASE.dll").write_bytes(gw12)
    (out/"diag/base/FUSEDDETAIL1_SAFEPOINT8_BASE.dll").write_bytes(fused)
    (out/"payload/d3d11.dll").write_bytes(runtime)
    (out/"payload/win81_nis_dx11_x64.dll").write_bytes(runtime)

    actual = {}
    for p in sorted(out.rglob("*")):
        if p.is_file(): actual[p.relative_to(out).as_posix()] = sha256(p.read_bytes())
    if set(actual) != set(expected):
        missing=sorted(set(expected)-set(actual)); extra=sorted(set(actual)-set(expected))
        die(f"path set mismatch missing={missing} extra={extra}")
    bad=[p for p in expected if actual[p] != expected[p]]
    if bad: die("SHA-256 mismatch: "+", ".join(bad))
    print("Validated exact target: 106/106 files", flush=True)

    # Replace worktree contents atomically-ish, preserving only .git until target copy is ready.
    for p in list(ROOT.iterdir()):
        if p.name == ".git": continue
        if p.is_dir() and not p.is_symlink(): shutil.rmtree(p)
        else: p.unlink()
    for p in out.iterdir():
        dst=ROOT/p.name
        if p.is_dir(): shutil.copytree(p,dst)
        else: shutil.copy2(p,dst)
    (ROOT/"LICENSE").write_bytes(license_bytes)

# Final worktree proof: 106 exact package files + preserved LICENSE, no staging/workflow.
files=[p for p in ROOT.rglob("*") if p.is_file() and ".git" not in p.parts]
if len(files) != 107: die(f"final repository file count {len(files)} != 107")
for rel,h in expected.items():
    p=ROOT/rel
    if not p.is_file() or sha256(p.read_bytes()) != h: die("post-copy mismatch: "+rel)
if (ROOT/".ptar_final").exists() or (ROOT/".github").exists(): die("staging/workflow survived cleanup")
if gitblob((ROOT/"payload/d3d11.dll").read_bytes()) != "91a9cbb8001757cad55220ea93153f44984430b9":
    die("runtime Git blob mismatch")

run("git","config","user.name","github-actions[bot]")
run("git","config","user.email","41898282+github-actions[bot]@users.noreply.github.com")
run("git","add","-A")
run("git","commit","-m","release: promote SAFEPOINT11 FUSEDDETAIL1 baseline")
run("git","push","origin","HEAD:s11-promote-final-20260907")
print("SAFEPOINT11 FINALIZATION COMPLETE", flush=True)
