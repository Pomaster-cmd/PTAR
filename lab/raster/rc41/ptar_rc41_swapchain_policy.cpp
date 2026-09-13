#include "ptar_rc41_swapchain_policy.h"

namespace ptar_rc41 {

static HMODULE allocation_base(const void* p) noexcept {
    if(!p) return nullptr;
    MEMORY_BASIC_INFORMATION mbi{};
    if(!VirtualQuery(p,&mbi,sizeof(mbi))) return nullptr;
    return static_cast<HMODULE>(mbi.AllocationBase);
}

CallerDomain classify_caller_module(const void* returnAddress,HMODULE gameModule,HMODULE runtimeModule,HMODULE sidecarModule) noexcept {
    const HMODULE caller=allocation_base(returnAddress);
    if(!caller) return CallerDomain::Unknown;
    if(runtimeModule && caller==runtimeModule) return CallerDomain::Runtime;
    if(sidecarModule && caller==sidecarModule) return CallerDomain::Sidecar;
    if(gameModule && caller==gameModule) return CallerDomain::Game;
    return CallerDomain::Unknown;
}

} // namespace ptar_rc41
