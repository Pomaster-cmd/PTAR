from pathlib import Path
import sys

src = Path(sys.argv[1] if len(sys.argv) > 1 else 'lab/raster/rc52/generated/ptar_rc41_context_hooks_rc52.cpp')
out = Path(sys.argv[2] if len(sys.argv) > 2 else 'lab/raster/rc55/generated/ptar_rc41_context_hooks_rc55.cpp')
text = src.read_text(encoding='utf-8')

helper_anchor = '''static bool scissor_exceeds_physical(const Contract& c,const D3D11_RECT& r) noexcept {
    return r.right>(LONG)c.physical.w || r.bottom>(LONG)c.physical.h;
}
'''
helper = helper_anchor + r'''

static bool viewport_is_exact_full_physical(const Contract& c,const D3D11_VIEWPORT& v) noexcept {
    if(!c.valid()) return false;
    const float eps=0.75f;
    return std::fabs(v.TopLeftX)<=eps && std::fabs(v.TopLeftY)<=eps &&
           std::fabs(v.Width-float(c.physical.w))<=eps &&
           std::fabs(v.Height-float(c.physical.h))<=eps;
}

static bool scissor_is_exact_full_physical(const Contract& c,const D3D11_RECT& r) noexcept {
    return c.valid() && r.left==0 && r.top==0 &&
           r.right==(LONG)c.physical.w && r.bottom==(LONG)c.physical.h;
}
'''
if text.count(helper_anchor) != 1:
    raise SystemExit('RC55 helper anchor drift')
text = text.replace(helper_anchor, helper, 1)

vp_old = r'''    if(viewports || count==0) self->cache_viewports(count,viewports,false);
    if(!viewports || !count || count>kRasterSlots){self->origViewports_(ctx,count,viewports);return;}
    D3D11_VIEWPORT mapped[kRasterSlots]{};
    for(UINT i=0;i<count;++i) mapped[i]=mapped_viewport(self->contract_,viewports[i]);
    self->add_counter(self->viewportMapped_);
    self->origViewports_(ctx,count,mapped);'''
vp_new = r'''    if(viewports && count==1 && viewport_is_exact_full_physical(self->contract_,viewports[0])){
        // Field RC54 proved the game can submit an already-physical 1280x720 viewport while
        // the primary 1280x720 target is bound. Canonicalize it into logical cache state,
        // but do NOT multiply it by physical/logical again on the immediate API call.
        self->cache_viewports(count,viewports,true);
        self->origViewports_(ctx,count,viewports);
        return;
    }
    if(viewports || count==0) self->cache_viewports(count,viewports,false);
    if(!viewports || !count || count>kRasterSlots){self->origViewports_(ctx,count,viewports);return;}
    D3D11_VIEWPORT mapped[kRasterSlots]{};
    for(UINT i=0;i<count;++i) mapped[i]=mapped_viewport(self->contract_,viewports[i]);
    self->add_counter(self->viewportMapped_);
    self->origViewports_(ctx,count,mapped);'''
if text.count(vp_old) != 1:
    raise SystemExit('RC55 bound viewport anchor drift')
text = text.replace(vp_old, vp_new, 1)

sc_old = r'''    if(rects || count==0) self->cache_scissors(count,rects,false);
    if(!rects || !count || count>kRasterSlots){self->origScissors_(ctx,count,rects);return;}
    D3D11_RECT mapped[kRasterSlots]{};
    for(UINT i=0;i<count;++i) mapped[i]=mapped_scissor(self->contract_,rects[i]);
    self->add_counter(self->scissorMapped_);
    self->origScissors_(ctx,count,mapped);'''
sc_new = r'''    if(rects && count==1 && scissor_is_exact_full_physical(self->contract_,rects[0])){
        // Same idempotence rule as the viewport: preserve an already-physical full-output
        // scissor immediately while retaining a logical canonical value for later replay.
        self->cache_scissors(count,rects,true);
        self->origScissors_(ctx,count,rects);
        return;
    }
    if(rects || count==0) self->cache_scissors(count,rects,false);
    if(!rects || !count || count>kRasterSlots){self->origScissors_(ctx,count,rects);return;}
    D3D11_RECT mapped[kRasterSlots]{};
    for(UINT i=0;i<count;++i) mapped[i]=mapped_scissor(self->contract_,rects[i]);
    self->add_counter(self->scissorMapped_);
    self->origScissors_(ctx,count,mapped);'''
if text.count(sc_old) != 1:
    raise SystemExit('RC55 bound scissor anchor drift')
text = text.replace(sc_old, sc_new, 1)

for name, token in {
    'physical viewport idempotence':'viewport_is_exact_full_physical(self->contract_,viewports[0])',
    'physical scissor idempotence':'scissor_is_exact_full_physical(self->contract_,rects[0])',
    'physical viewport canonical cache':'self->cache_viewports(count,viewports,true)',
    'physical scissor canonical cache':'self->cache_scissors(count,rects,true)',
    'RC52 ambiguous viewport quarantine':'requestedViewportCount_=0;haveViewportState_=false',
    'RC52 ambiguous scissor quarantine':'requestedScissorCount_=0;haveScissorState_=false',
}.items():
    if token not in text:
        raise SystemExit('missing sentinel: '+name)

out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(text, encoding='utf-8', newline='\n')
print(f'RC55_CONTEXT_HOOKS_GENERATED={out}')
print('RC55_POLICY=exact full physical bound viewport/scissor is idempotent; RC52 quarantine retained')
