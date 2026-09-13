#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cstdio>
#include <cstdint>
#include <random>
#include <vector>
#include <algorithm>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

static HWND g_game = nullptr;
static HWND g_presenter = nullptr;
static UINT g_gameW = 640, g_gameH = 360;
static UINT g_outW = 960, g_outH = 540;
static LONG g_gameSizeMsgs = 0;
static LONG g_lastLogicalX = -1, g_lastLogicalY = -1;
static const UINT WM_PTAR_LOGICAL_MOUSE = WM_APP + 0x41;

static void pump_messages()
{
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

static RECT aspect_fit(UINT sw, UINT sh, UINT ow, UINT oh)
{
    const double sx = double(ow) / double(sw);
    const double sy = double(oh) / double(sh);
    const double s = sx < sy ? sx : sy;
    const LONG w = LONG(double(sw) * s + 0.5);
    const LONG h = LONG(double(sh) * s + 0.5);
    const LONG x = (LONG(ow) - w) / 2;
    const LONG y = (LONG(oh) - h) / 2;
    RECT r = { x, y, x + w, y + h };
    return r;
}

static POINT map_output_to_game(LONG x, LONG y)
{
    RECT fit = aspect_fit(g_gameW, g_gameH, g_outW, g_outH);
    if (fit.right <= fit.left || fit.bottom <= fit.top) return {0, 0};
    x = (std::max)(fit.left, (std::min)(fit.right - 1, x));
    y = (std::max)(fit.top, (std::min)(fit.bottom - 1, y));
    const double u = double(x - fit.left) / double(fit.right - fit.left);
    const double v = double(y - fit.top) / double(fit.bottom - fit.top);
    LONG gx = LONG(u * double(g_gameW));
    LONG gy = LONG(v * double(g_gameH));
    gx = (std::max)(0L, (std::min)(LONG(g_gameW) - 1, gx));
    gy = (std::max)(0L, (std::min)(LONG(g_gameH) - 1, gy));
    return {gx, gy};
}

static LRESULT CALLBACK GameProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_SIZE:
        ++g_gameSizeMsgs;
        return 0;
    case WM_PTAR_LOGICAL_MOUSE:
        g_lastLogicalX = GET_X_LPARAM(l);
        g_lastLogicalY = GET_Y_LPARAM(l);
        return 0;
    case WM_DESTROY:
        return 0;
    default:
        return DefWindowProcW(h, m, w, l);
    }
}

static LRESULT CALLBACK PresenterProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    {
        POINT p = map_output_to_game(GET_X_LPARAM(l), GET_Y_LPARAM(l));
        SendMessageW(g_game, WM_PTAR_LOGICAL_MOUSE, w, MAKELPARAM(p.x, p.y));
        return 0;
    }
    default:
        return DefWindowProcW(h, m, w, l);
    }
}

static bool set_client_size(HWND h, UINT cw, UINT ch)
{
    RECT r = {0, 0, LONG(cw), LONG(ch)};
    const DWORD style = DWORD(GetWindowLongPtrW(h, GWL_STYLE));
    const DWORD ex = DWORD(GetWindowLongPtrW(h, GWL_EXSTYLE));
    if (!AdjustWindowRectEx(&r, style, FALSE, ex)) return false;
    return !!SetWindowPos(h, nullptr, 0, 0, r.right-r.left, r.bottom-r.top,
                          SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static bool get_client_size(HWND h, UINT &w, UINT &hh)
{
    RECT r{};
    if (!GetClientRect(h, &r)) return false;
    w = UINT(r.right-r.left);
    hh = UINT(r.bottom-r.top);
    return true;
}

static bool verify_swap_size(IDXGISwapChain *sc, UINT w, UINT h)
{
    ID3D11Texture2D *tex = nullptr;
    HRESULT hr = sc->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tex));
    if (FAILED(hr) || !tex) return false;
    D3D11_TEXTURE2D_DESC d{};
    tex->GetDesc(&d);
    tex->Release();
    return d.Width == w && d.Height == h;
}

static IDXGISwapChain* make_swapchain(IDXGIFactory *factory, ID3D11Device *dev, HWND hwnd, UINT w, UINT h)
{
    DXGI_SWAP_CHAIN_DESC d{};
    d.BufferDesc.Width = w;
    d.BufferDesc.Height = h;
    d.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT;
    d.BufferCount = 1;
    d.OutputWindow = hwnd;
    d.Windowed = TRUE;
    d.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    IDXGISwapChain *sc = nullptr;
    if (FAILED(factory->CreateSwapChain(dev, &d, &sc))) return nullptr;
    return sc;
}

static bool fail(const char *what, int code)
{
    std::printf("FAIL code=%d %s\n", code, what);
    return false;
}

int main()
{
    HINSTANCE hi = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.hInstance = hi; wc.lpfnWndProc = GameProc; wc.lpszClassName = L"PTARLabGame";
    if (!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) return 10;
    WNDCLASSW pc{};
    pc.hInstance = hi; pc.lpfnWndProc = PresenterProc; pc.lpszClassName = L"PTARLabPresenter";
    if (!RegisterClassW(&pc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) return 11;

    g_game = CreateWindowExW(0, wc.lpszClassName, L"PTAR synthetic game",
        WS_OVERLAPPEDWINDOW, 10,10,640,360, nullptr,nullptr,hi,nullptr);
    if (!g_game) return 12;
    if (!set_client_size(g_game, g_gameW, g_gameH)) return 13;
    ShowWindow(g_game, SW_SHOW);
    pump_messages();

    const DWORD pex = WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
    g_presenter = CreateWindowExW(pex, pc.lpszClassName, L"PTAR borderless presenter",
        WS_POPUP, 0,0,LONG(g_outW),LONG(g_outH), g_game,nullptr,hi,nullptr);
    if (!g_presenter) return 14;
    ShowWindow(g_presenter, SW_SHOWNOACTIVATE);
    pump_messages();

    DWORD style = DWORD(GetWindowLongPtrW(g_presenter, GWL_STYLE));
    DWORD ex = DWORD(GetWindowLongPtrW(g_presenter, GWL_EXSTYLE));
    if (!(style & WS_POPUP) || (style & (WS_CAPTION|WS_THICKFRAME))) return fail("presenter style is not clean WS_POPUP borderless",15),15;
    if (!(ex & WS_EX_NOACTIVATE) || !(ex & WS_EX_TOOLWINDOW)) return fail("presenter missing noactivate/toolwindow",16),16;
    if (ex & WS_EX_TRANSPARENT) return fail("presenter must NOT be WS_EX_TRANSPARENT",17),17;
    if (GetWindow(g_presenter, GW_OWNER) != g_game) return fail("presenter is not owned by game HWND",18),18;
    if (SendMessageW(g_presenter, WM_MOUSEACTIVATE, reinterpret_cast<WPARAM>(g_game), MAKELPARAM(HTCLIENT,WM_LBUTTONDOWN)) != MA_NOACTIVATE)
        return fail("presenter mouse activation policy",19),19;

    ID3D11Device *dev = nullptr;
    ID3D11DeviceContext *ctx = nullptr;
    D3D_FEATURE_LEVEL fl{};
    const D3D_FEATURE_LEVEL req[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, req, ARRAYSIZE(req),
                                   D3D11_SDK_VERSION, &dev, &fl, &ctx);
    if (FAILED(hr)) return fail("D3D11 WARP creation",20),20;

    IDXGIDevice *dxdev = nullptr;
    IDXGIAdapter *ad = nullptr;
    IDXGIFactory *factory = nullptr;
    if (FAILED(dev->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxdev))) ||
        FAILED(dxdev->GetAdapter(&ad)) ||
        FAILED(ad->GetParent(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&factory)))) return 21;

    IDXGISwapChain *gameSC = make_swapchain(factory, dev, g_game, g_gameW, g_gameH);
    IDXGISwapChain *outSC = make_swapchain(factory, dev, g_presenter, g_outW, g_outH);
    if (!gameSC || !outSC) return fail("swapchain creation",22),22;
    if (FAILED(factory->MakeWindowAssociation(g_game, DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER)))
        return fail("MakeWindowAssociation no-window-changes",23),23;

    BOOL fs = TRUE;
    if (FAILED(gameSC->GetFullscreenState(&fs,nullptr)) || fs) return fail("game swapchain must remain windowed",24),24;
    if (FAILED(outSC->GetFullscreenState(&fs,nullptr)) || fs) return fail("presenter swapchain must remain windowed/borderless",25),25;
    if (!verify_swap_size(gameSC,g_gameW,g_gameH) || !verify_swap_size(outSC,g_outW,g_outH)) return 26;

    std::mt19937 rng(0x50544152u);
    const std::vector<POINT> renders = {{640,360},{800,450},{960,540},{1024,576},{1280,720},{1024,768},{1280,800}};
    const std::vector<POINT> outputs = {{960,540},{1280,720},{1366,768},{1600,900},{1920,1080}};
    unsigned resizeFailures = 0;
    unsigned outputOnlyLeaks = 0;
    unsigned mouseFailures = 0;
    unsigned long long checks = 0;

    for (unsigned i=0;i<20000;++i) {
        if ((rng() & 1u)==0) {
            POINT p = renders[rng()%renders.size()];
            g_gameW=UINT(p.x); g_gameH=UINT(p.y);
            LONG before = g_gameSizeMsgs;
            if (!set_client_size(g_game,g_gameW,g_gameH)) return 30;
            pump_messages();
            hr = gameSC->ResizeBuffers(0,g_gameW,g_gameH,DXGI_FORMAT_UNKNOWN,0);
            if (FAILED(hr)) ++resizeFailures;
            UINT cw=0,ch=0;
            if (!get_client_size(g_game,cw,ch) || cw!=g_gameW || ch!=g_gameH) return 31;
            if (!verify_swap_size(gameSC,g_gameW,g_gameH)) return 32;
            if (g_gameSizeMsgs < before) return 33;
            checks += 7;
        } else {
            POINT p = outputs[rng()%outputs.size()];
            const LONG before = g_gameSizeMsgs;
            UINT gcw=0,gch=0; get_client_size(g_game,gcw,gch);
            g_outW=UINT(p.x); g_outH=UINT(p.y);
            if (!SetWindowPos(g_presenter,nullptr,0,0,g_outW,g_outH,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE)) return 34;
            pump_messages();
            hr = outSC->ResizeBuffers(0,g_outW,g_outH,DXGI_FORMAT_UNKNOWN,0);
            if (FAILED(hr)) ++resizeFailures;
            UINT gcw2=0,gch2=0; get_client_size(g_game,gcw2,gch2);
            if (gcw!=gcw2 || gch!=gch2 || g_gameSizeMsgs!=before) ++outputOnlyLeaks;
            if (!verify_swap_size(outSC,g_outW,g_outH)) return 35;
            checks += 7;
        }

        for (int k=0;k<16;++k) {
            const LONG px = LONG(rng()%g_outW);
            const LONG py = LONG(rng()%g_outH);
            POINT expected = map_output_to_game(px,py);
            g_lastLogicalX = g_lastLogicalY = -1;
            SendMessageW(g_presenter,WM_MOUSEMOVE,0,MAKELPARAM(px,py));
            if (g_lastLogicalX!=expected.x || g_lastLogicalY!=expected.y) ++mouseFailures;
            if (g_lastLogicalX<0 || g_lastLogicalY<0 || g_lastLogicalX>=LONG(g_gameW) || g_lastLogicalY>=LONG(g_gameH)) ++mouseFailures;
            checks += 4;
        }
    }

    UINT finalCW=0,finalCH=0; get_client_size(g_game,finalCW,finalCH);
    if (finalCW!=g_gameW || finalCH!=g_gameH) return 40;
    if (!verify_swap_size(gameSC,g_gameW,g_gameH)) return 41;
    if (resizeFailures || outputOnlyLeaks || mouseFailures) {
        std::printf("FAIL resizeFailures=%u outputOnlyLeaks=%u mouseFailures=%u\n",resizeFailures,outputOnlyLeaks,mouseFailures);
        return 42;
    }

    std::printf("PASS PTAR_BORDERLESS_TAKEOVER_LAB\n");
    std::printf("transitions=20000 pointer_probes=320000 checks=%llu\n",checks);
    std::printf("resize_failures=0 output_only_game_resize_leaks=0 mouse_mapping_failures=0\n");
    std::printf("invariant=game_client_equals_game_backbuffer; presenter_client_equals_native_output\n");
    std::printf("presenter=WS_POPUP + WS_EX_NOACTIVATE + WS_EX_TOOLWINDOW + owned_by_game + NOT_TRANSPARENT\n");
    std::printf("dxgi=both_swapchains_windowed; DXGI_MWA_NO_WINDOW_CHANGES|NO_ALT_ENTER on game HWND\n");

    outSC->Release(); gameSC->Release(); factory->Release(); ad->Release(); dxdev->Release();
    ctx->ClearState(); ctx->Release(); dev->Release();
    DestroyWindow(g_presenter); DestroyWindow(g_game);
    return 0;
}
