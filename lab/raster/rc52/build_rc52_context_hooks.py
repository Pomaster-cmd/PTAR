from pathlib import Path
import re, sys

src = Path('lab/raster/rc41/ptar_rc41_context_hooks.cpp')
out = Path(sys.argv[1] if len(sys.argv) > 1 else 'lab/raster/rc52/ptar_rc41_context_hooks_rc52.cpp')
text = src.read_text(encoding='utf-8')

capture = r'''void ContextHooks::capture_raster_state_from_context\(ID3D11DeviceContext\* ctx\) noexcept \{.*?\n\}\n\nvoid ContextHooks::reconcile_cached_raster_state'''
replacement = r'''void ContextHooks::capture_raster_state_from_context(ID3D11DeviceContext* ctx) noexcept {
    if(!ctx) return;
    const bool bound=primary_bound();

    D3D11_VIEWPORT vp[kRasterSlots]{};UINT vpCount=kRasterSlots;
    ctx->RSGetViewports(&vpCount,vp);
    if(bound){
        bool vpPhysical=true;
        for(UINT i=0;i<vpCount;++i) if(viewport_exceeds_physical(contract_,vp[i])){vpPhysical=false;break;}
        cache_viewports(vpCount,vp,vpPhysical);
    } else {
        bool definitelyLogical=false;
        for(UINT i=0;i<vpCount;++i) if(viewport_exceeds_physical(contract_,vp[i])){definitelyLogical=true;break;}
        if(vpCount && definitelyLogical) cache_viewports(vpCount,vp,false);
        else {
            AcquireSRWLockExclusive(&rasterStateLock_);
            requestedViewportCount_=0;haveViewportState_=false;
            ReleaseSRWLockExclusive(&rasterStateLock_);
        }
    }

    D3D11_RECT sc[kRasterSlots]{};UINT scCount=kRasterSlots;
    ctx->RSGetScissorRects(&scCount,sc);
    if(bound){
        bool scPhysical=true;
        for(UINT i=0;i<scCount;++i) if(scissor_exceeds_physical(contract_,sc[i])){scPhysical=false;break;}
        cache_scissors(scCount,sc,scPhysical);
    } else {
        bool definitelyLogical=false;
        for(UINT i=0;i<scCount;++i) if(scissor_exceeds_physical(contract_,sc[i])){definitelyLogical=true;break;}
        if(scCount && definitelyLogical) cache_scissors(scCount,sc,false);
        else {
            AcquireSRWLockExclusive(&rasterStateLock_);
            requestedScissorCount_=0;haveScissorState_=false;
            ReleaseSRWLockExclusive(&rasterStateLock_);
        }
    }
}

void ContextHooks::reconcile_cached_raster_state'''
text, n = re.subn(capture, replacement, text, flags=re.S)
if n != 1:
    raise SystemExit(f'capture replacement count={n}')

vp_hook = r'''void STDMETHODCALLTYPE ContextHooks::hook_viewports\(ID3D11DeviceContext\* ctx,UINT count,const D3D11_VIEWPORT\* viewports\)\{.*?\n\}\n\nvoid STDMETHODCALLTYPE ContextHooks::hook_scissors'''
vp_repl = r'''void STDMETHODCALLTYPE ContextHooks::hook_viewports(ID3D11DeviceContext* ctx,UINT count,const D3D11_VIEWPORT* viewports){
    ContextHooks* self=owner_now();
    if(!self || !self->origViewports_){ return; }
    self->add_counter(self->viewportCalls_);
    const bool bound=self->primary_bound();
    if(!bound){
        self->add_counter(self->viewportCallsUnbound_);
        if(count==0){
            self->cache_viewports(0,nullptr,false);
        } else if(viewports && count<=kRasterSlots){
            bool definitelyLogical=false;
            for(UINT i=0;i<count;++i) if(viewport_exceeds_physical(self->contract_,viewports[i])){definitelyLogical=true;break;}
            if(definitelyLogical) self->cache_viewports(count,viewports,false);
            else {
                AcquireSRWLockExclusive(&self->rasterStateLock_);
                self->requestedViewportCount_=0;self->haveViewportState_=false;
                ReleaseSRWLockExclusive(&self->rasterStateLock_);
            }
        }
        self->origViewports_(ctx,count,viewports);return;
    }
    if(viewports || count==0) self->cache_viewports(count,viewports,false);
    if(!viewports || !count || count>kRasterSlots){self->origViewports_(ctx,count,viewports);return;}
    D3D11_VIEWPORT mapped[kRasterSlots]{};
    for(UINT i=0;i<count;++i) mapped[i]=mapped_viewport(self->contract_,viewports[i]);
    self->add_counter(self->viewportMapped_);
    self->origViewports_(ctx,count,mapped);
}

void STDMETHODCALLTYPE ContextHooks::hook_scissors'''
text, n = re.subn(vp_hook, vp_repl, text, flags=re.S)
if n != 1:
    raise SystemExit(f'viewport replacement count={n}')

sc_hook = r'''void STDMETHODCALLTYPE ContextHooks::hook_scissors\(ID3D11DeviceContext\* ctx,UINT count,const D3D11_RECT\* rects\)\{.*?\n\}\n\nvoid STDMETHODCALLTYPE ContextHooks::hook_clear_state'''
sc_repl = r'''void STDMETHODCALLTYPE ContextHooks::hook_scissors(ID3D11DeviceContext* ctx,UINT count,const D3D11_RECT* rects){
    ContextHooks* self=owner_now();
    if(!self || !self->origScissors_){ return; }
    self->add_counter(self->scissorCalls_);
    const bool bound=self->primary_bound();
    if(!bound){
        self->add_counter(self->scissorCallsUnbound_);
        if(count==0){
            self->cache_scissors(0,nullptr,false);
        } else if(rects && count<=kRasterSlots){
            bool definitelyLogical=false;
            for(UINT i=0;i<count;++i) if(scissor_exceeds_physical(self->contract_,rects[i])){definitelyLogical=true;break;}
            if(definitelyLogical) self->cache_scissors(count,rects,false);
            else {
                AcquireSRWLockExclusive(&self->rasterStateLock_);
                self->requestedScissorCount_=0;self->haveScissorState_=false;
                ReleaseSRWLockExclusive(&self->rasterStateLock_);
            }
        }
        self->origScissors_(ctx,count,rects);return;
    }
    if(rects || count==0) self->cache_scissors(count,rects,false);
    if(!rects || !count || count>kRasterSlots){self->origScissors_(ctx,count,rects);return;}
    D3D11_RECT mapped[kRasterSlots]{};
    for(UINT i=0;i<count;++i) mapped[i]=mapped_scissor(self->contract_,rects[i]);
    self->add_counter(self->scissorMapped_);
    self->origScissors_(ctx,count,mapped);
}

void STDMETHODCALLTYPE ContextHooks::hook_clear_state'''
text, n = re.subn(sc_hook, sc_repl, text, flags=re.S)
if n != 1:
    raise SystemExit(f'scissor replacement count={n}')

sentinels = {
    'ambiguous viewport quarantine':'requestedViewportCount_=0;haveViewportState_=false',
    'ambiguous scissor quarantine':'requestedScissorCount_=0;haveScissorState_=false',
    'logical viewport preservation':'if(definitelyLogical) self->cache_viewports(count,viewports,false)',
    'logical scissor preservation':'if(definitelyLogical) self->cache_scissors(count,rects,false)',
}
for name, token in sentinels.items():
    if token not in text:
        raise SystemExit(f'missing sentinel: {name}')

out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(text, encoding='utf-8', newline='\n')
print(f'RC52_CONTEXT_HOOKS_GENERATED={out}')
print('RC52_POLICY=quarantine ambiguous unbound physical raster state; preserve definitely-logical >physical state')
