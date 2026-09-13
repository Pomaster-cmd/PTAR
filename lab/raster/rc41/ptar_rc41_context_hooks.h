#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include "ptar_rc41_contract.h"
#include "ptar_rc41_target_classifier.h"

namespace ptar_rc41 {

struct HookStats {
    uint64_t omCalls=0;
    uint64_t omUavCalls=0;
    uint64_t viewportCalls=0;
    uint64_t scissorCalls=0;
    uint64_t viewportMapped=0;
    uint64_t scissorMapped=0;
    uint64_t clearStateCalls=0;
    uint64_t executeCommandListCalls=0;
};

class ContextHooks {
public:
    ContextHooks() noexcept;
    ~ContextHooks();
    ContextHooks(const ContextHooks&) = delete;
    ContextHooks& operator=(const ContextHooks&) = delete;

    bool configure(const Contract& contract, ID3D11Resource* primaryResource) noexcept;
    bool install(ID3D11DeviceContext* context) noexcept;
    void uninstall() noexcept;
    bool installed() const noexcept;
    bool primary_bound() const noexcept;
    HookStats stats() const noexcept;

private:
    static constexpr size_t kVtableSlots=115;
    static constexpr size_t kSlotOMSetRenderTargets=33;
    static constexpr size_t kSlotOMSetRenderTargetsAndUAV=34;
    static constexpr size_t kSlotExecuteCommandList=58;
    static constexpr size_t kSlotRSSetViewports=44;
    static constexpr size_t kSlotRSSetScissorRects=45;
    static constexpr size_t kSlotClearState=110;

    using FnOMSetRenderTargets=void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,ID3D11RenderTargetView* const*,ID3D11DepthStencilView*);
    using FnOMSetRenderTargetsAndUAV=void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,ID3D11RenderTargetView* const*,ID3D11DepthStencilView*,UINT,UINT,ID3D11UnorderedAccessView* const*,const UINT*);
    using FnExecuteCommandList=void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11CommandList*,BOOL);
    using FnRSSetViewports=void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,const D3D11_VIEWPORT*);
    using FnRSSetScissorRects=void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,const D3D11_RECT*);
    using FnClearState=void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*);

    static void STDMETHODCALLTYPE hook_om(ID3D11DeviceContext*,UINT,ID3D11RenderTargetView* const*,ID3D11DepthStencilView*);
    static void STDMETHODCALLTYPE hook_om_uav(ID3D11DeviceContext*,UINT,ID3D11RenderTargetView* const*,ID3D11DepthStencilView*,UINT,UINT,ID3D11UnorderedAccessView* const*,const UINT*);
    static void STDMETHODCALLTYPE hook_execute_command_list(ID3D11DeviceContext*,ID3D11CommandList*,BOOL);
    static void STDMETHODCALLTYPE hook_viewports(ID3D11DeviceContext*,UINT,const D3D11_VIEWPORT*);
    static void STDMETHODCALLTYPE hook_scissors(ID3D11DeviceContext*,UINT,const D3D11_RECT*);
    static void STDMETHODCALLTYPE hook_clear_state(ID3D11DeviceContext*);

    void refresh_primary_from_context(ID3D11DeviceContext*) noexcept;
    void set_primary_bound(bool value) noexcept;
    void add_counter(volatile LONG64& counter, LONG64 delta=1) noexcept;

    Contract contract_{};
    TargetClassifier classifier_{};
    ID3D11DeviceContext* context_=nullptr;
    void** originalVtable_=nullptr;
    void* shadowVtable_[kVtableSlots]{};
    FnOMSetRenderTargets origOM_=nullptr;
    FnOMSetRenderTargetsAndUAV origOMUav_=nullptr;
    FnExecuteCommandList origExecuteCommandList_=nullptr;
    FnRSSetViewports origViewports_=nullptr;
    FnRSSetScissorRects origScissors_=nullptr;
    FnClearState origClearState_=nullptr;
    volatile LONG installed_=0;
    volatile LONG primaryBound_=0;
    volatile LONG64 omCalls_=0,omUavCalls_=0,viewportCalls_=0,scissorCalls_=0;
    volatile LONG64 viewportMapped_=0,scissorMapped_=0,clearStateCalls_=0,executeCommandListCalls_=0;
};

} // namespace ptar_rc41
