#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>

namespace ptar_rc41 {

class TargetClassifier {
public:
    TargetClassifier() noexcept;
    ~TargetClassifier();
    TargetClassifier(const TargetClassifier&) = delete;
    TargetClassifier& operator=(const TargetClassifier&) = delete;

    bool set_primary_resource(ID3D11Resource* resource) noexcept;
    void clear_primary() noexcept;
    bool is_primary(ID3D11RenderTargetView* rtv) const noexcept;
    bool is_primary(ID3D11DepthStencilView* dsv) const noexcept;
    bool any_primary(UINT count, ID3D11RenderTargetView* const* rtvs,ID3D11DepthStencilView* dsv=nullptr) const noexcept;

private:
    mutable SRWLOCK lock_;
    IUnknown* primaryIdentity_;
};

} // namespace ptar_rc41
