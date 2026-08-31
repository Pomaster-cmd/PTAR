#!/usr/bin/env python3
import hashlib, os, struct, sys

EXPECTED_BASE_SHA256 = '0e558350d789e56908a3f4bb866f76ba77e252dd282fe1bfe0df41dc085e8548'
IMAGE_BASE_EXPECTED = 0x180000000
CALL_SITES_RVA = (0xC798, 0xD678)
FG_SUBMIT_RVA = 0x127C0
FG_TARGET_FPS_RVA = 0x4B068
QPC_IAT_RVA = 0x49F18
QPF_IAT_RVA = 0x49F20
SLEEP_IAT_RVA = 0x49F50

FGOV_NAME = b'.fgov\0\0\0'
FGDAT_NAME = b'.fgdat\0\0'
FGOV_CHARS = 0x60000020   # code | execute | read
FGDAT_CHARS = 0xC0000040  # initialized data | read | write
UNWIND_INFO = bytes.fromhex('01 04 01 00 04 82 00 00')  # sub rsp,0x48


def align(v, a):
    return (v + a - 1) & ~(a - 1)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def pe_checksum(blob, checksum_off):
    b = bytearray(blob)
    struct.pack_into('<I', b, checksum_off, 0)
    s = 0
    for i in range(0, len(b) - 1, 2):
        s += b[i] | (b[i+1] << 8)
        s = (s & 0xffff) + (s >> 16)
    if len(b) & 1:
        s += b[-1]
        s = (s & 0xffff) + (s >> 16)
    s = (s & 0xffff) + (s >> 16)
    return (s + len(b)) & 0xffffffff


def coff_text_and_relocs(obj):
    b = open(obj, 'rb').read()
    if len(b) < 20:
        raise RuntimeError('COFF object too short')
    machine, nsec, _ts, symptr, nsym, optsz, _ch = struct.unpack_from('<HHIIIHH', b, 0)
    if machine != 0x8664 or optsz != 0:
        raise RuntimeError('Expected x64 COFF object')
    shoff = 20
    sections = []
    for i in range(nsec):
        off = shoff + i*40
        name = b[off:off+8].rstrip(b'\0').decode('ascii', 'replace')
        vsz, va, rawsz, rawptr, relptr, _lnptr, nrel, _nln, chars = struct.unpack_from('<IIIIIIHHI', b, off+8)
        sections.append((name, rawsz, rawptr, relptr, nrel, chars))
    text = next((s for s in sections if s[0] == '.text'), None)
    if not text:
        raise RuntimeError('COFF .text missing')
    _name, rawsz, rawptr, relptr, nrel, _chars = text
    code = bytearray(b[rawptr:rawptr+rawsz])
    strtab_off = symptr + nsym*18
    strtab_len = struct.unpack_from('<I', b, strtab_off)[0]
    strtab = b[strtab_off:strtab_off+strtab_len]

    def sym_name(index):
        off = symptr + index*18
        rawname = b[off:off+8]
        z, so = struct.unpack('<II', rawname)
        if z == 0:
            end = strtab.find(b'\0', so)
            if end < 0: end = len(strtab)
            return strtab[so:end].decode('ascii')
        return rawname.rstrip(b'\0').decode('ascii')

    relocs = []
    for i in range(nrel):
        off = relptr + i*10
        rva, symidx, typ = struct.unpack_from('<IIH', b, off)
        if typ != 0x0004:  # IMAGE_REL_AMD64_REL32
            raise RuntimeError(f'Unexpected relocation type {typ:#x} at {rva:#x}')
        relocs.append((rva, sym_name(symidx)))
    return code, relocs


def parse_pe(b):
    e_lfanew = struct.unpack_from('<I', b, 0x3c)[0]
    if b[e_lfanew:e_lfanew+4] != b'PE\0\0':
        raise RuntimeError('PE signature missing')
    coff = e_lfanew + 4
    machine, nsec, _ts, _ps, _ns, optsz, _ch = struct.unpack_from('<HHIIIHH', b, coff)
    if machine != 0x8664:
        raise RuntimeError('Expected PE32+ x64')
    opt = coff + 20
    if struct.unpack_from('<H', b, opt)[0] != 0x20b:
        raise RuntimeError('Expected PE32+')
    image_base = struct.unpack_from('<Q', b, opt+24)[0]
    sec_align = struct.unpack_from('<I', b, opt+32)[0]
    file_align = struct.unpack_from('<I', b, opt+36)[0]
    size_image = struct.unpack_from('<I', b, opt+56)[0]
    size_headers = struct.unpack_from('<I', b, opt+60)[0]
    checksum_off = opt+64
    shoff = opt+optsz
    secs=[]
    for i in range(nsec):
        o=shoff+i*40
        name=b[o:o+8].rstrip(b'\0').decode('ascii','replace')
        vs, va, rs, rp, pr, pl, nr, nl, ch=struct.unpack_from('<IIIIIIHHI',b,o+8)
        secs.append({'i':i,'off':o,'name':name,'vs':vs,'va':va,'rs':rs,'rp':rp,'ch':ch})
    return locals()


def main():
    if len(sys.argv) != 4:
        raise SystemExit('usage: build_fgreal30_patch.py BASE_DLL WRAPPER_OBJ OUT_DLL')
    base_path, obj_path, out_path = sys.argv[1:]
    original = bytearray(open(base_path,'rb').read())
    if sha256(original) != EXPECTED_BASE_SHA256:
        raise RuntimeError('Refusing non-RC4 base DLL')
    pe = parse_pe(original)
    if pe['image_base'] != IMAGE_BASE_EXPECTED or pe['nsec'] != 5:
        raise RuntimeError('Unexpected PE base/section count')
    if any(s['name'] in ('.fgov','.fgdat') for s in pe['secs']):
        raise RuntimeError('Governor sections already present')

    code, relocs = coff_text_and_relocs(obj_path)
    if len(code) != 0x18a:
        raise RuntimeError(f'Unexpected wrapper size {len(code):#x}')

    last=max(pe['secs'], key=lambda s:s['va'])
    fgov_va=align(last['va'] + max(last['vs'], last['rs']), pe['sec_align'])
    fgov_raw=align(len(original), pe['file_align'])
    unwind_off=align(len(code),4)
    fgov_vs=unwind_off+len(UNWIND_INFO)
    fgov_rs=align(fgov_vs, pe['file_align'])
    fgdat_va=align(fgov_va+fgov_vs, pe['sec_align'])
    fgdat_raw=fgov_raw+fgov_rs
    fgdat_vs=0x20
    fgdat_rs=align(fgdat_vs, pe['file_align'])
    new_size=fgdat_raw+fgdat_rs

    # State layout in .fgdat.
    targets={
      'FG_TARGET_FPS': pe['image_base'] + FG_TARGET_FPS_RVA,
      'QPC_IAT': pe['image_base'] + QPC_IAT_RVA,
      'QPF_IAT': pe['image_base'] + QPF_IAT_RVA,
      'SLEEP_IAT': pe['image_base'] + SLEEP_IAT_RVA,
      'GOV_NEXT_TICK': pe['image_base'] + fgdat_va + 0x00,
      'GOV_INTERVAL': pe['image_base'] + fgdat_va + 0x08,
      'GOV_SLEEP1_TICKS': pe['image_base'] + fgdat_va + 0x10,
      'GOV_LAST_TARGET': pe['image_base'] + fgdat_va + 0x18,
      'GOV_ARMED': pe['image_base'] + fgdat_va + 0x1c,
      'FG_SUBMIT': pe['image_base'] + FG_SUBMIT_RVA,
    }
    wrapper_va=pe['image_base']+fgov_va

    # Apply COFF REL32 relocations, retaining assembler addends (needed for
    # RIP-relative operands followed by immediate bytes).
    seen=set()
    for roff, name in relocs:
        if name not in targets:
            raise RuntimeError(f'Unknown relocation symbol {name}')
        addend=struct.unpack_from('<i', code, roff)[0]
        disp=targets[name] + addend - (wrapper_va + roff + 4)
        if not -(1<<31) <= disp < (1<<31):
            raise RuntimeError(f'REL32 out of range for {name}')
        struct.pack_into('<i', code, roff, disp)
        seen.add(name)
    required=set(targets)
    if not required.issubset(seen):
        raise RuntimeError(f'Missing expected relocations: {sorted(required-seen)}')

    # Extend file and write new sections.
    out=bytearray(original)
    if len(out) < fgov_raw:
        out.extend(b'\0'*(fgov_raw-len(out)))
    out.extend(b'\0'*(new_size-len(out)))
    out[fgov_raw:fgov_raw+len(code)] = code
    out[fgov_raw+unwind_off:fgov_raw+unwind_off+len(UNWIND_INFO)] = UNWIND_INFO
    # .fgdat is intentionally zero-initialized in file.

    # Patch only active-FG calls to FGSubmitFrame.
    text = next(s for s in pe['secs'] if s['name']=='.text')
    for call_rva in CALL_SITES_RVA:
        off=text['rp'] + (call_rva-text['va'])
        old=bytes(out[off:off+5])
        if old[0] != 0xE8:
            raise RuntimeError(f'Call site opcode changed at {call_rva:#x}: {old.hex()}')
        old_target=pe['image_base'] + call_rva + 5 + struct.unpack_from('<i',old,1)[0]
        if old_target != pe['image_base'] + FG_SUBMIT_RVA:
            raise RuntimeError(f'Call site target changed at {call_rva:#x}: {old_target:#x}')
        disp=wrapper_va-(pe['image_base']+call_rva+5)
        out[off:off+5]=b'\xE8'+struct.pack('<i',disp)

    # Add section headers (header slack is pre-existing and sufficient).
    new_shoff=pe['shoff'] + pe['nsec']*40
    if new_shoff+80 > pe['size_headers']:
        raise RuntimeError('No PE header room for two sections')
    if any(out[new_shoff:new_shoff+80]):
        raise RuntimeError('Expected zero PE header slack is not zero')
    def write_sh(off,name,vs,va,rs,rp,ch):
        out[off:off+40]=name+struct.pack('<IIIIIIHHI',vs,va,rs,rp,0,0,0,0,ch)
    write_sh(new_shoff,FGOV_NAME,fgov_vs,fgov_va,fgov_rs,fgov_raw,FGOV_CHARS)
    write_sh(new_shoff+40,FGDAT_NAME,fgdat_vs,fgdat_va,fgdat_rs,fgdat_raw,FGDAT_CHARS)
    struct.pack_into('<H',out,pe['coff']+2,pe['nsec']+2)
    struct.pack_into('<I',out,pe['opt']+4,struct.unpack_from('<I',out,pe['opt']+4)[0]+fgov_rs)
    struct.pack_into('<I',out,pe['opt']+8,struct.unpack_from('<I',out,pe['opt']+8)[0]+fgdat_rs)
    struct.pack_into('<I',out,pe['opt']+56,align(fgdat_va+fgdat_vs,pe['sec_align']))

    # Add unwind entry using the exact 12-byte slack at the end of .pdata.
    pdata=next(s for s in pe['secs'] if s['name']=='.pdata')
    if pdata['vs'] != 0x11f4 or pdata['rs'] != 0x1200:
        raise RuntimeError('Unexpected .pdata geometry')
    rf_off=pdata['rp']+pdata['vs']
    if any(out[rf_off:rf_off+12]):
        raise RuntimeError('.pdata append slack is not zero')
    out[rf_off:rf_off+12]=struct.pack('<III',fgov_va,fgov_va+len(code),fgov_va+unwind_off)
    struct.pack_into('<I',out,pdata['off']+8,0x1200)  # VirtualSize
    # Exception directory is data-directory entry #3, size field +4.
    dd=pe['opt']+112
    exc_rva, exc_size=struct.unpack_from('<II',out,dd+3*8)
    if exc_rva != pdata['va'] or exc_size != 0x11f4:
        raise RuntimeError('Unexpected exception directory')
    struct.pack_into('<I',out,dd+3*8+4,0x1200)

    # Compute and store PE checksum last.
    struct.pack_into('<I',out,pe['checksum_off'],0)
    csum=pe_checksum(out,pe['checksum_off'])
    struct.pack_into('<I',out,pe['checksum_off'],csum)

    open(out_path,'wb').write(out)
    print('BASE_SHA256='+EXPECTED_BASE_SHA256)
    print('OUTPUT_SHA256='+sha256(out))
    print(f'WRAPPER_RVA=0x{fgov_va:X}')
    print(f'WRAPPER_SIZE=0x{len(code):X}')
    print(f'UNWIND_RVA=0x{fgov_va+unwind_off:X}')
    print(f'STATE_RVA=0x{fgdat_va:X}')
    print(f'PE_CHECKSUM=0x{csum:X}')
    print(f'OUTPUT_SIZE={len(out)}')

if __name__=='__main__': main()
