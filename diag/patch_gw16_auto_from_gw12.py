#!/usr/bin/env python3
"""Deterministic GW12 -> GW16G QUALITYSAFE1 runtime patch.

Design:
- preserve GW12 Flip3 + LOADSHEDSOFT1 topology;
- source-side AUTO controller wraps the already existing FG REAL governor and preserves its EAX return contract;
- FG activation starts HIGH immediately: target 60 -> up to 30 REAL/s, SyncInterval=1 -> up to 60 visible/s;
- the historical LOW target30/Sync2 branch remains embedded for provenance but is unreachable in NOLOCK30_1;
- workload estimate excludes governor wait by measuring next-entry minus previous
  governor-return QPC;
- NOLOCK30_1 disables the automatic HIGH->LOW clamp entirely to avoid abrupt 60<->30 cadence changes in mixed-load gameplay;
- legacy threshold/state code is retained byte-layout-compatible but cannot arm LOW from the active HIGH path;
- QUALITYSAFE1 keeps CTRL+F8 live changes inside the already-active ME tier while FG is ON, so no /2<->/3 live reprepare is requested;
- HOTKEYSAFE2 consumes modifier/base-key collisions at the poll source: CTRL+F6 cannot leak into plain F6 and CTRL+F8 cannot leak into plain F8 status/DWM diagnostics;
- startup FG-OFF and GW12 soft-OFF both reset AUTO and temporarily set target 120 => 60 REAL/s through the existing governor, so OFF never inherits a 15/30-FPS cap;
- normal display path uses the existing DWM wrapper frame but bypasses its passive
  sampling body, loads AUTO_MODE into nonvolatile r13d, then calls the original
  Present helper directly; no new per-visible-frame call layer is added;
- REAL-only fallback uses a tiny wrapper forcing r13d=0 => SyncInterval=1.
"""
import hashlib, json, re, struct, sys
from pathlib import Path

BASE_SHA='50cf02fee971e615f0dba26a7614e27b833486a993cf569fe5369a0fa5b41f59'
CALLSITE0=0x0000C798
CALLSITE1=0x0000D678
GOV_WRAPPER=0x034FB000
FG_TARGET_FPS=0x0004B068
SOFT_FLAG=0x034FF8F0
FG_ENABLED=0x02C3F6CC
QPC_IAT=0x00049F18
QPF_IAT=0x00049F20
LOG_FUNC=0x00001E50
PRESENT_HELPER=0x00026540
PRESENT_SELECTOR=0x000265CF
FALLBACK_CALL=0x00025A0B
DWM_FASTPATCH=0x034FE01E
DWM_EPILOGUE=0x034FE290
AUTO_WRAPPER=0x034FFC80
AUTO_FALLBACK=AUTO_WRAPPER+0x280
AUTO_MODE=0x034FFF20
AUTO_ARMED=0x034FFF24
AUTO_DOWN_COUNT=0x034FFF28
AUTO_UP_COUNT=0x034FFF2C
AUTO_BACKOFF=0x034FFF30
AUTO_SWITCH_DOWN=0x034FFF34
AUTO_SWITCH_UP=0x034FFF38
AUTO_SAMPLES=0x034FFF3C
AUTO_LAST_EXIT=0x034FFF40
AUTO_LAST_WORK=0x034FFF48
AUTO_DOWN_TICKS=0x034FFF50
AUTO_UP_TICKS=0x034FFF58
STR_LOW=0x034FFF60
STR_HIGH=0x034FFFB0
CAVE_END=0x03500000
GETASYNC_IAT=0x00049FB8
F6_ACTION_CALL=0x00012230
F6_ACTION=0x00016950
F8_STATUS_CALL=0x00012304
F8_STATUS_ACTION=0x00019DC0
DWM_CTRL_F8_SKIP=0x034FE543
HOTKEY_F6_SAFE=0x034FD980
HOTKEY_F8_SAFE=0x034FD9B0
QUALITY_NEXT_SAFE=0x034FD9E0
HOTKEY_POLL_SAFE=0x034FDA10
HOTKEY_CAVE_END=0x034FDBF0
QUALITY_HANDLER=0x034FDC00
QUALITY_NEXT_SITE=0x034FDC95
QUALITY_SELECTED=0x034FC09C
QUALITY_APPLIED_TIER=0x034FC0A4
QUALITY_RESTART_PENDING=0x034FC0A8
QUALITY_ACTIVE_FG=0x02C93B94
DWM_QUALITY_CALL=0x034FE44C
ACTION_POLL=0x000167D0
STR_LOW_B=b'GW16 AUTO60/30: HIGH->LOW, stable 30 mode armed (workload >34ms)\0'
STR_HIGH_B=b'GW16 AUTO60/30: LOW->HIGH, 60 mode armed (workload <33ms qualified)\0'

SYMS={
 'SOFT_FLAG':SOFT_FLAG,'FG_ENABLED':FG_ENABLED,'AUTO_ARMED':AUTO_ARMED,'AUTO_MODE':AUTO_MODE,
 'AUTO_DOWN_COUNT':AUTO_DOWN_COUNT,'AUTO_UP_COUNT':AUTO_UP_COUNT,
 'AUTO_BACKOFF':AUTO_BACKOFF,'AUTO_SWITCH_DOWN':AUTO_SWITCH_DOWN,
 'AUTO_SWITCH_UP':AUTO_SWITCH_UP,'AUTO_SAMPLES':AUTO_SAMPLES,
 'AUTO_LAST_EXIT':AUTO_LAST_EXIT,'AUTO_LAST_WORK':AUTO_LAST_WORK,
 'AUTO_DOWN_TICKS':AUTO_DOWN_TICKS,'AUTO_UP_TICKS':AUTO_UP_TICKS,
 'FG_TARGET_FPS':FG_TARGET_FPS,'QPC_IAT':QPC_IAT,'QPF_IAT':QPF_IAT,
 'GOV_WRAPPER':GOV_WRAPPER,'PRESENT_HELPER':PRESENT_HELPER,'LOG_FUNC':LOG_FUNC,
 'STR_LOW':STR_LOW,'STR_HIGH':STR_HIGH,
}

def sha(b): return hashlib.sha256(bytes(b)).hexdigest()
def parse(b):
    e=struct.unpack_from('<I',b,0x3c)[0];coff=e+4
    n=struct.unpack_from('<H',b,coff+2)[0];optsz=struct.unpack_from('<H',b,coff+16)[0]
    opt=coff+20;sh=opt+optsz;secs=[]
    for i in range(n):
        o=sh+i*40;name=b[o:o+8].rstrip(b'\0').decode('ascii')
        vs,va,rs,rp=struct.unpack_from('<IIII',b,o+8);secs.append((name,vs,va,rs,rp))
    return opt,secs
def roff(b,rva):
    for _n,vs,va,rs,rp in parse(b)[1]:
        if va<=rva<va+max(vs,rs):return rp+(rva-va)
    raise RuntimeError('RVA not mapped %#x'%rva)
def rel32(src,dst,n=5): return struct.pack('<i',dst-(src+n))
def call_rel(src,dst): return b'\xE8'+rel32(src,dst)
def jmp_rel(src,dst): return b'\xE9'+rel32(src,dst)
def rip_disp(src,ilen,target): return struct.pack('<i',target-(src+ilen))
def ctrl_guard_wrapper(rva,target):
    # Windows x64 ABI: 0x28 keeps 16-byte call alignment and provides shadow space.
    b=bytearray()
    b+=bytes.fromhex('4883ec28')                 # sub rsp,0x28
    b+=bytes.fromhex('b911000000')               # mov ecx,VK_CONTROL
    call_iat_rva=rva+len(b)
    b+=bytes.fromhex('ff15')+rip_disp(call_iat_rva,6,GETASYNC_IAT)
    b+=bytes.fromhex('6685c0')                   # test ax,ax
    b+=bytes.fromhex('7805')                     # js skip_action
    call_rva=rva+len(b)
    b+=call_rel(call_rva,target)
    b+=bytes.fromhex('4883c428c3')               # add rsp,0x28; ret
    if len(b)!=30: raise AssertionError(len(b))
    return bytes(b)

def quality_next_wrapper(rva):
    # FG ON: toggle only the low bit (0<->1 or 2<->3), preserving the active ME tier.
    # FG OFF: full 0->1->2->3 cycle remains available for the next activation.
    b=bytearray()
    b+=bytes.fromhex('8b05')+rip_disp(rva+len(b),6,QUALITY_SELECTED)  # mov eax,[selected]
    b+=bytes.fromhex('833d')+rip_disp(rva+len(b),7,QUALITY_ACTIVE_FG)+bytes.fromhex('00') # cmp dword [fg],0
    b+=bytes.fromhex('7404')                         # je full_cycle
    b+=bytes.fromhex('83f001c3')                     # xor eax,1 ; ret
    b+=bytes.fromhex('ffc083e003c3')                 # inc eax ; and eax,3 ; ret
    return bytes(b)

def hotkey_poll_wrapper(rva):
    # Preserve the original quality-poller side effects and base-F6 action consumption.
    # If CTRL is physically held in this same poll, suppress the base-F6 return and
    # proactively consume queued plain-F8 status so the chord cannot leak after release.
    b=bytearray()
    b+=bytes.fromhex('4883ec38')                     # sub rsp,0x38
    b+=call_rel(rva+len(b),QUALITY_HANDLER)          # original quality/base-F6 poller
    b+=bytes.fromhex('89442430')                     # mov [rsp+0x30],eax
    b+=bytes.fromhex('b911000000')                   # mov ecx,VK_CONTROL
    ci=rva+len(b); b+=bytes.fromhex('ff15')+rip_disp(ci,6,GETASYNC_IAT)
    b+=bytes.fromhex('6685c0')                       # test ax,ax
    b+=bytes.fromhex('7911')                         # jns no_ctrl
    b+=bytes.fromhex('b908000000')                   # mov ecx,8 (plain F8 status action)
    b+=call_rel(rva+len(b),ACTION_POLL)
    b+=bytes.fromhex('31c0')                         # xor eax,eax
    b+=bytes.fromhex('4883c438c3')                   # add rsp,0x38; ret
    b+=bytes.fromhex('8b442430')                     # no_ctrl: mov eax,[rsp+0x30]
    b+=bytes.fromhex('4883c438c3')                   # add rsp,0x38; ret
    return bytes(b)

def checksum(blob,off):
    b=bytearray(blob);struct.pack_into('<I',b,off,0);s=0
    for i in range(0,len(b)-1,2):
        s+=b[i]|(b[i+1]<<8);s=(s&0xffff)+(s>>16)
    if len(b)&1:s+=b[-1]
    s=(s&0xffff)+(s>>16);s=(s&0xffff)+(s>>16)
    return (s+len(b))&0xffffffff

def load_template(root):
    tpl=bytearray((root/'auto60_30_wrapper.bin').read_bytes())
    if len(tpl)!=0x295: raise RuntimeError('AUTO wrapper .text size lock failed: %d'%len(tpl))
    rel=[]
    rx=re.compile(r'^([0-9A-Fa-f]{16})\s+IMAGE_REL_AMD64_REL32\s+(\S+)\s*$')
    for line in (root/'auto60_30_wrapper.relocs.txt').read_text(encoding='utf-8',errors='replace').splitlines():
        m=rx.match(line.strip())
        if m: rel.append((int(m.group(1),16),m.group(2)))
    if len(rel)!=55: raise RuntimeError('AUTO relocation count lock failed: %d'%len(rel))
    for off,sym in rel:
        if sym not in SYMS: raise RuntimeError('unknown AUTO relocation symbol '+sym)
        add=struct.unpack_from('<i',tpl,off)[0]
        val=SYMS[sym]+add-(AUTO_WRAPPER+off+4)
        struct.pack_into('<i',tpl,off,val)
    return bytes(tpl),rel

def main():
    if len(sys.argv)!=3: raise SystemExit('usage: patch_gw16_auto_from_gw12.py GW12_DLL OUT_DLL')
    src,dst=map(Path,sys.argv[1:]);base=src.read_bytes();root=Path(__file__).resolve().parent
    if sha(base)!=BASE_SHA: raise RuntimeError('exact GW12 runtime required')
    if len(base)!=320000: raise RuntimeError('size lock failed')
    out=bytearray(base);opt,secs=parse(base)
    if len(secs)!=9 or secs[-1][0]!='.dwmlab': raise RuntimeError('PE topology lock failed')

    # Exact source-governor callsite locks.
    for site in (CALLSITE0,CALLSITE1):
        o=roff(base,site);raw=base[o:o+5]
        if raw[0]!=0xE8 or site+5+struct.unpack_from('<i',raw,1)[0]!=GOV_WRAPPER:
            raise RuntimeError('GW12 governor callsite lock failed at %#x'%site)

    # Exact normal display and fallback locks.
    if base[roff(base,DWM_FASTPATCH):roff(base,DWM_FASTPATCH)+17] != bytes.fromhex('31f685db741aff05e60f00008b05e00f00'):
        raise RuntimeError('GW12 DWM wrapper fast-patch context lock failed')
    if base[roff(base,PRESENT_SELECTOR):roff(base,PRESENT_SELECTOR)+5] != bytes.fromhex('ba01000000'):
        raise RuntimeError('GW12 Present SyncInterval=1 selector lock failed')
    raw=base[roff(base,FALLBACK_CALL):roff(base,FALLBACK_CALL)+5]
    if raw[0]!=0xE8 or FALLBACK_CALL+5+struct.unpack_from('<i',raw,1)[0]!=PRESENT_HELPER:
        raise RuntimeError('GW12 REAL fallback Present helper call lock failed')

    # The extension tail must still be pristine in exact GW12.
    co=roff(base,AUTO_WRAPPER); cave=base[co:co+(CAVE_END-AUTO_WRAPPER)]
    if cave!=b'\xCC'*(CAVE_END-AUTO_WRAPPER): raise RuntimeError('AUTO cave not pristine')

    code,relocs=load_template(root)
    if AUTO_WRAPPER+len(code)>AUTO_MODE: raise RuntimeError('AUTO code overlaps state')
    if STR_LOW+len(STR_LOW_B)>STR_HIGH or STR_HIGH+len(STR_HIGH_B)>CAVE_END:
        raise RuntimeError('AUTO strings overflow cave')

    # Source-side controller wraps the existing validated governor.
    for site in (CALLSITE0,CALLSITE1):
        out[roff(base,site):roff(base,site)+5]=call_rel(site,AUTO_WRAPPER)

    # Normal visible path: retain the existing DWM wrapper ABI/prologue but skip its
    # passive sampler; load AUTO_MODE into r13d, call the original Present helper,
    # then jump to the original epilogue. No extra per-visible-frame call layer.
    p=DWM_FASTPATCH
    fast=(b'\x44\x8B\x2D'+rip_disp(p,7,AUTO_MODE)+
          call_rel(p+7,PRESENT_HELPER)+
          jmp_rel(p+12,DWM_EPILOGUE))
    if len(fast)!=17: raise AssertionError(len(fast))
    out[roff(base,p):roff(base,p)+17]=fast

    # Original helper now reads r13d: 0 => Sync1 (60 branch), 1 => Sync2 (30 branch).
    out[roff(base,PRESENT_SELECTOR):roff(base,PRESENT_SELECTOR)+5]=bytes.fromhex('418d550190')

    # REAL-only fallback bypasses the DWM wrapper, so force r13d=0 through a tiny
    # nonvolatile-preserving wrapper; this retains Sync1 for skipped/generated-less REALs.
    out[roff(base,FALLBACK_CALL):roff(base,FALLBACK_CALL)+5]=call_rel(FALLBACK_CALL,AUTO_FALLBACK)

    # Install controller code/state/strings into the pristine RWX tail.
    out[roff(base,AUTO_WRAPPER):roff(base,AUTO_WRAPPER)+len(code)]=code
    out[roff(base,AUTO_MODE):roff(base,STR_LOW)]=b'\0'*(STR_LOW-AUTO_MODE)
    out[roff(base,STR_LOW):roff(base,STR_LOW)+len(STR_LOW_B)]=STR_LOW_B
    out[roff(base,STR_HIGH):roff(base,STR_HIGH)+len(STR_HIGH_B)]=STR_HIGH_B

    # HOTKEYCHORD1: the inherited polling path treats base keys independently from
    # modifiers. On hardware CTRL+F6 also fired plain F6, and CTRL+F8 fired the
    # quality menu plus F8 status and passive DWM diagnostics. The F8 status path
    # then touched the pending ME-tier transition and contributed to a failed live
    # presenter reprepare. Guard the base-key actions when CTRL is physically down.
    def _call_target(buf,site):
        raw=buf[roff(buf,site):roff(buf,site)+5]
        if raw[0]!=0xE8: return None
        return site+5+struct.unpack_from('<i',raw,1)[0]
    if _call_target(base,F6_ACTION_CALL)!=F6_ACTION:
        raise RuntimeError('plain F6 action call lock failed')
    if _call_target(base,F8_STATUS_CALL)!=F8_STATUS_ACTION:
        raise RuntimeError('plain F8 status action call lock failed')
    if base[roff(base,DWM_CTRL_F8_SKIP):roff(base,DWM_CTRL_F8_SKIP)+2] != bytes.fromhex('7426'):
        raise RuntimeError('CTRL+F8 DWM diagnostic branch lock failed')
    if base[roff(base,HOTKEY_F6_SAFE):roff(base,HOTKEY_CAVE_END)] != b'\xCC'*(HOTKEY_CAVE_END-HOTKEY_F6_SAFE):
        raise RuntimeError('HOTKEYCHORD1 cave not pristine')

    f6safe=ctrl_guard_wrapper(HOTKEY_F6_SAFE,F6_ACTION)
    f8safe=ctrl_guard_wrapper(HOTKEY_F8_SAFE,F8_STATUS_ACTION)
    out[roff(base,HOTKEY_F6_SAFE):roff(base,HOTKEY_F6_SAFE)+len(f6safe)]=f6safe
    out[roff(base,HOTKEY_F8_SAFE):roff(base,HOTKEY_F8_SAFE)+len(f8safe)]=f8safe
    out[roff(base,F6_ACTION_CALL):roff(base,F6_ACTION_CALL)+5]=call_rel(F6_ACTION_CALL,HOTKEY_F6_SAFE)
    out[roff(base,F8_STATUS_CALL):roff(base,F8_STATUS_CALL)+5]=call_rel(F8_STATUS_CALL,HOTKEY_F8_SAFE)
    # Disable only CTRL+F8 passive DWM snapshot; CTRL+F4/F5 remain available.
    out[roff(base,DWM_CTRL_F8_SKIP):roff(base,DWM_CTRL_F8_SKIP)+2]=bytes.fromhex('eb26')

    # QUALITYSAFE1: the active CTRL+F8 handler may change guard/profile live, but while
    # FG is ON it must remain within the already-created ME tier. This prevents the
    # /2 -> /3 (or /3 -> /2) transition that previously led to a presenter teardown
    # and failed live recreation. When FG is OFF, the full four-profile cycle remains.
    qctx=base[roff(base,QUALITY_NEXT_SITE):roff(base,QUALITY_NEXT_SITE)+11]
    if qctx!=bytes.fromhex('8b0501e4ffffffc083e003'):
        raise RuntimeError('quality next-profile context lock failed')
    qsafe=quality_next_wrapper(QUALITY_NEXT_SAFE)
    if QUALITY_NEXT_SAFE+len(qsafe)>HOTKEY_POLL_SAFE:
        raise RuntimeError('QUALITYSAFE1 helper overlap')
    out[roff(base,QUALITY_NEXT_SAFE):roff(base,QUALITY_NEXT_SAFE)+len(qsafe)]=qsafe
    out[roff(base,QUALITY_NEXT_SITE):roff(base,QUALITY_NEXT_SITE)+11]=call_rel(QUALITY_NEXT_SITE,QUALITY_NEXT_SAFE)+b'\x90'*6

    # HOTKEYSAFE2: move chord suppression to the actual poll boundary. The original
    # late action-call guards are retained as defense in depth, but the wrapper now
    # consumes base F6/F8 actions while CTRL is physically down so they cannot leak
    # after the modifier has been released.
    raw=base[roff(base,DWM_QUALITY_CALL):roff(base,DWM_QUALITY_CALL)+5]
    if raw[0]!=0xE8 or DWM_QUALITY_CALL+5+struct.unpack_from('<i',raw,1)[0]!=QUALITY_HANDLER:
        raise RuntimeError('DWM quality-poller call lock failed')
    hp=hotkey_poll_wrapper(HOTKEY_POLL_SAFE)
    if HOTKEY_POLL_SAFE+len(hp)>HOTKEY_CAVE_END:
        raise RuntimeError('HOTKEYSAFE2 helper overflow')
    out[roff(base,HOTKEY_POLL_SAFE):roff(base,HOTKEY_POLL_SAFE)+len(hp)]=hp
    out[roff(base,DWM_QUALITY_CALL):roff(base,DWM_QUALITY_CALL)+5]=call_rel(DWM_QUALITY_CALL,HOTKEY_POLL_SAFE)

    csoff=opt+64;struct.pack_into('<I',out,csoff,0);struct.pack_into('<I',out,csoff,checksum(out,csoff))
    dst.write_bytes(out)
    print('BASE_SHA256='+BASE_SHA)
    print('OUT_SHA256='+sha(out))
    print('AUTO_WRAPPER_RVA='+hex(AUTO_WRAPPER))
    print('AUTO_FALLBACK_RVA='+hex(AUTO_FALLBACK))
    print('AUTO_MODE_RVA='+hex(AUTO_MODE))
    print('AUTO_START=HIGH_TARGET60_SYNC1')
    print('AUTO_HIGH=TARGET60_SYNC1')
    print('AUTO_LOW=EMBEDDED_BUT_UNREACHABLE_TARGET30_SYNC2')
    print('AUTO_DOWNSHIFT=DISABLED_NOLOCK30_1')
    print('AUTO_UPSHIFT=NOT_APPLICABLE_STARTS_HIGH')
    print('AUTO_FG_OFF_GATE=FG_ENABLED0_OR_SOFT_OFF_TARGET120_REAL60_SYNC1')
    print('DWM_PASSIVE_SAMPLER=BYPASSED_EXTERNAL_VBLANK3_AUTHORITATIVE')
    print('HOTKEYSAFE2=POLL_SOURCE_CTRL_F6_NO_PLAIN_F6_CTRL_F8_NO_STATUS_OR_DWM_DIAG')
    print('QUALITYSAFE1=FG_ON_SAME_ME_TIER_ONLY_FG_OFF_FULL_4_PROFILE_CYCLE')
    print('RELOCS='+str(len(relocs)))
if __name__=='__main__':main()
