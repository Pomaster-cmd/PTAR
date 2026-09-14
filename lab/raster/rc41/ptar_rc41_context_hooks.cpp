#include "ptar_rc41_context_hooks.h"
#include <cstring>

namespace ptar_rc41 {

static ContextHooks* volatile g_owner=nullptr;

static ContextHooks* owner_now() noexcept {
    return reinterpret_cast<ContextHooks*>(InterlockedCompareExchangePointer(
        reinterpret_cast<PVOID volatile*>(&g_owner),nullptr,nullptr));
}

static D3D11_VIEWPORT mapped_viewport(const Contract& c,const D3D11_VIEWPORT& in) noexcept {
    const ViewportF v{in.TopLeftX,in.TopLeftY,in.Width,in.Height,in.MinDepth,in.MaxDepth};
    const ViewportF o=map_viewport(c,v,true);
    D3D11_VIEWPORT r{};
    r.TopLeftX=o.x; r.TopLeftY=o.y; r.Width=o.w; r.Height=o.h;
    r.MinDepth=o.minDepth; r.MaxDepth=o.maxDepth;
    return r;
}

static D3D11_RECT mapped_scissor(const Contract& c,const D3D11_RECT& in) noexcept {
    const RectI r{in.left,in.top,in.right,in.bottom};
    const RectI o=map_scissor(c,r,true);
    D3D11_RECT q{o.l,o.t,o.r,o.b};
    return q;
}

ContextHooks::ContextHooks() noexcept = default;
ContextHooks::~ContextHooks(){ uninstall(); }

void ContextHooks::add_counter(volatile LONG64& counter,LONG64 delta) noexcept {
    if(delta==1) InterlockedIncrement64(&counter);
    else InterlockedExchangeAdd64(&counter,delta);
}

bool ContextHooks::configure(const Contract& contract,ID3D11Resource* primaryResource) noexcept {
    if(InterlockedCompareExchange(&installed_,0,0) || !contract.valid() || !primaryResource) return false;
    if(!classifier_.set_primary_resource(primaryResource)) return false;
    contract_=contract;
    set_primary_bound(false);
    return true;
}

bool ContextHooks::update_primary_resource(ID3D11Resource* primaryResource) noexcept {
    if(!primaryResource || !contract_.valid()) return false;
    if(!classifier_.set_primary_resource(primaryResource)) return false;
    add_counter(primaryRefreshes_);
    if(context_) refresh_primary_from_context(context_);
    else set_primary_bound(false);
    return true;
}

bool ContextHooks::install(ID3D11DeviceContext* context) noexcept {
    if(!context || !contract_.valid() || InterlockedCompareExchange(&installed_,0,0)) return false;
    if(InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(&g_owner),this,nullptr)!=nullptr) return false;

    context->AddRef();
    context_=context;
    void*** objectVtable=reinterpret_cast<void***>(context);
    originalVtable_=*objectVtable;
    if(!originalVtable_){
        context_->Release(); context_=nullptr;
        InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(&g_owner),nullptr,this);
        return false;
    }
    std::memcpy(shadowVtable_,originalVtable_,sizeof(shadowVtable_));

    origOM_=reinterpret_cast<FnOMSetRenderTargets>(originalVtable_[kSlotOMSetRenderTargets]);
    origOMUav_=reinterpret_cast<FnOMSetRenderTargetsAndUAV>(originalVtable_[kSlotOMSetRenderTargetsAndUAV]);
    origExecuteCommandList_=reinterpret_cast<FnExecuteCommandList>(originalVtable_[kSlotExecuteCommandList]);
    origViewports_=reinterpret_cast<FnRSSetViewports>(originalVtable_[kSlotRSSetViewports]);
    origScissors_=reinterpret_cast<FnRSSetScissorRects>(originalVtable_[kSlotRSSetScissorRects]);
    origClearState_=reinterpret_cast<FnClearState>(originalVtable_[kSlotClearState]);
    if(!origOM_||!origOMUav_||!origExecuteCommandList_||!origViewports_||!origScissors_||!origClearState_){
        context_->Release(); context_=nullptr; originalVtable_=nullptr;
        InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(&g_owner),nullptr,this);
        return false;
    }

    shadowVtable_[kSlotOMSetRenderTargets]=reinterpret_cast<void*>(&ContextHooks::hook_om);
    shadowVtable_[kSlotOMSetRenderTargetsAndUAV]=reinterpret_cast<void*>(&ContextHooks::hook_om_uav);
    shadowVtable_[kSlotExecuteCommandList]=reinterpret_cast<void*>(&ContextHooks::hook_execute_command_list);
    shadowVtable_[kSlotRSSetViewports]=reinterpret_cast<void*>(&ContextHooks::hook_viewports);
    shadowVtable_[kSlotRSSetScissorRects]=reinterpret_cast<void*>(&ContextHooks::hook_scissors);
    shadowVtable_[kSlotClearState]=reinterpret_cast<void*>(&ContextHooks::hook_clear_state);

    void* prior=InterlockedCompareExchangePointer(
        reinterpret_cast<PVOID volatile*>(context_),shadowVtable_,originalVtable_);
    if(prior!=originalVtable_){
        context_->Release(); context_=nullptr; originalVtable_=nullptr;
        InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(&g_owner),nullptr,this);
        return false;
    }
    InterlockedExchange(&installed_,1);
    refresh_primary_from_context(context_);
    return true;
}

void ContextHooks::uninstall() noexcept {
    if(!context_) return;
    InterlockedExchange(&installed_,0);
    InterlockedCompareExchangePointer(
        reinterpret_cast<PVOID volatile*>(context_),originalVtable_,shadowVtable_);
    InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(&g_owner),nullptr,this);
    set_primary_bound(false);
    ID3D11DeviceContext* old=context_;
    context_=nullptr; originalVtable_=nullptr;
    old->Release();
}

bool ContextHooks::installed() const noexcept { return InterlockedCompareExchange(const_cast<volatile LONG*>(&installed_),0,0)!=0; }
bool ContextHooks::primary_bound() const noexcept { return InterlockedCompareExchange(const_cast<volatile LONG*>(&primaryBound_),0,0)!=0; }

HookStats ContextHooks::stats() const noexcept {
    HookStats s{};
    s.omCalls=(uint64_t)InterlockedCompareExchange64(const_cast<volatile LONG64*>(&omCalls_),0,0);
    s.omUavCalls=(uint64_t)InterlockedCompareExchange64(const_cast<volatile LONG64*>(&omUavCalls_),0,0);
    s.viewportCalls=(uint64_t)InterlockedCompareExchange64(const_cast<volatile LONG64*>(&viewportCalls_),0,0);
    s.scissorCalls=(uint64_t)InterlockedCompareExchange64(const_cast<volatile LONG64*>(&scissorCalls_),0,0);
    s.viewportMapped=(uint64_t)InterlockedCompareExchange64(const_cast<volatile LONG64*>(&viewportMapped_),0,0);
    s.scissorMapped=(uint64_t)InterlockedCompareExchange64(const_cast<volatile LONG64*>(&scissorMapped_),0,0);
    s.clearStateCalls=(uint64_t)InterlockedCompareExchange64(const_cast<volatile LONG64*>(&clearStateCalls_),0,0);
    s.executeCommandListCalls=(uint64_t)InterlockedCompareExchange64(const_cast<volatile LONG64*>(&executeCommandListCalls_),0,0);
    s.primaryRefreshes=(uint64_t)InterlockedCompareExchange64(const_cast<volatile LONG64*>(&primaryRefreshes_),0,0);
    return s;
}

void ContextHooks::set_primary_bound(bool value) noexcept { InterlockedExchange(&primaryBound_,value?1:0); }

void ContextHooks::refresh_primary_from_context(ID3D11DeviceContext* ctx) noexcept {
    ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
    ID3D11DepthStencilView* dsv=nullptr;
    ctx->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,rtvs,&dsv);
    const bool bound=classifier_.any_primary(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,rtvs,dsv);
    for(auto*& rtv:rtvs){ if(rtv){ rtv->Release(); rtv=nullptr; } }
    if(dsv) dsv->Release();
    set_primary_bound(bound);
}

void STDMETHODCALLTYPE ContextHooks::hook_om(ID3D11DeviceContext* ctx,UINT count,ID3D11RenderTargetView* const* rtvs,ID3D11DepthStencilView* dsv){
    ContextHooks* self=owner_now();
    if(!self || !self->origOM_){ return; }
    self->origOM_(ctx,count,rtvs,dsv);
    self->add_counter(self->omCalls_);
    self->set_primary_bound(self->classifier_.any_primary(count,rtvs,dsv));
}

void STDMETHODCALLTYPE ContextHooks::hook_om_uav(ID3D11DeviceContext* ctx,UINT count,ID3D11RenderTargetView* const* rtvs,ID3D11DepthStencilView* dsv,UINT uavStart,UINT uavCount,ID3D11UnorderedAccessView* const* uavs,const UINT* initialCounts){
    ContextHooks* self=owner_now();
    if(!self || !self->origOMUav_){ return; }
    self->origOMUav_(ctx,count,rtvs,dsv,uavStart,uavCount,uavs,initialCounts);
    self->add_counter(self->omUavCalls_);
    if(count!=D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL)
        self->set_primary_bound(self->classifier_.any_primary(count,rtvs,dsv));
}

void STDMETHODCALLTYPE ContextHooks::hook_execute_command_list(ID3D11DeviceContext* ctx,ID3D11CommandList* list,BOOL restore){
    ContextHooks* self=owner_now();
    if(!self || !self->origExecuteCommandList_){ return; }
    self->origExecuteCommandList_(ctx,list,restore);
    self->add_counter(self->executeCommandListCalls_);
    self->refresh_primary_from_context(ctx);
}

void STDMETHODCALLTYPE ContextHooks::hook_viewports(ID3D11DeviceContext* ctx,UINT count,const D3D11_VIEWPORT* viewports){
    ContextHooks* self=owner_now();
    if(!self || !self->origViewports_){ return; }
    self->add_counter(self->viewportCalls_);
    if(!self->primary_bound() || !viewports || !count || count>D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE){
        self->origViewports_(ctx,count,viewports); return;
    }
    D3D11_VIEWPORT mapped[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
    for(UINT i=0;i<count;++i) mapped[i]=mapped_viewport(self->contract_,viewports[i]);
    self->add_counter(self->viewportMapped_);
    self->origViewports_(ctx,count,mapped);
}

void STDMETHODCALLTYPE ContextHooks::hook_scissors(ID3D11DeviceContext* ctx,UINT count,const D3D11_RECT* rects){
    ContextHooks* self=owner_now();
    if(!self || !self->origScissors_){ return; }
    self->add_counter(self->scissorCalls_);
    if(!self->primary_bound() || !rects || !count || count>D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE){
        self->origScissors_(ctx,count,rects); return;
    }
    D3D11_RECT mapped[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
    for(UINT i=0;i<count;++i) mapped[i]=mapped_scissor(self->contract_,rects[i]);
    self->add_counter(self->scissorMapped_);
    self->origScissors_(ctx,count,mapped);
}

void STDMETHODCALLTYPE ContextHooks::hook_clear_state(ID3D11DeviceContext* ctx){
    ContextHooks* self=owner_now();
    if(!self || !self->origClearState_){ return; }
    self->origClearState_(ctx);
    self->add_counter(self->clearStateCalls_);
    self->set_primary_bound(false);
}

} // namespace ptar_rc41
