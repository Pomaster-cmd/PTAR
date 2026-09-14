from pathlib import Path
import sys

if len(sys.argv)!=2:
    raise SystemExit('usage: build_rc54_prod_probe.py OUT_CPP')

src=Path('lab/raster/rc41/ptar_rc41_prod.cpp').read_text(encoding='utf-8')
helper=r'''
static void rc54_log_environment(IDXGISwapChain* sc) noexcept {
    if(!sc) return;
    DXGI_SWAP_CHAIN_DESC d{};HRESULT hr=sc->GetDesc(&d);
    RECT cr{};BOOL haveClient=(SUCCEEDED(hr)&&d.OutputWindow)?GetClientRect(d.OutputWindow,&cr):FALSE;
    HDC dc=GetDC(nullptr);int dpiX=dc?GetDeviceCaps(dc,LOGPIXELSX):0;int dpiY=dc?GetDeviceCaps(dc,LOGPIXELSY):0;if(dc)ReleaseDC(nullptr,dc);
    const int smW=GetSystemMetrics(SM_CXSCREEN),smH=GetSystemMetrics(SM_CYSCREEN);
    const BOOL aware=IsProcessDPIAware();
    char line[768]{};
    wsprintfA(line,"RC54_ENV dpi_aware=%u dpi=%dx%d system=%dx%d swap_raw=%ux%u client=%ldx%ld hwnd=0x%p",
        aware?1u:0u,dpiX,dpiY,smW,smH,SUCCEEDED(hr)?d.BufferDesc.Width:0u,SUCCEEDED(hr)?d.BufferDesc.Height:0u,
        haveClient?(cr.right-cr.left):0L,haveClient?(cr.bottom-cr.top):0L,SUCCEEDED(hr)?d.OutputWindow:nullptr);
    log_line(line);
}
'''
anchor='''static void log_dims(const char* tag,UINT a,UINT b,UINT c,UINT d) noexcept {
    char line[512]{};wsprintfA(line,"%s %u %u %u %u",tag,a,b,c,d);log_line(line);
}
'''
if src.count(anchor)!=1: raise SystemExit('log_dims anchor drift')
src=src.replace(anchor,anchor+helper,1)
call='''    log_dims("ACTIVE_RC41_LOGICAL_NATIVE_SUBRASTER logical/physical",logicalW,logicalH,physicalW,physicalH);
    return 0;'''
repl='''    log_dims("ACTIVE_RC41_LOGICAL_NATIVE_SUBRASTER logical/physical",logicalW,logicalH,physicalW,physicalH);
    rc54_log_environment(sc);
    log_line("RC54_GUI_DOMAIN_PROBE_ACTIVE behavior=RC52-identical trace_only=1");
    return 0;'''
if src.count(call)!=1: raise SystemExit('attach active anchor drift')
src=src.replace(call,repl,1)
out=Path(sys.argv[1]);out.parent.mkdir(parents=True,exist_ok=True);out.write_text(src,encoding='utf-8',newline='\n')
print(f'RC54_PROD={out}')
