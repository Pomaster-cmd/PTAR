#pragma once
// PTAR D3D11 GPU timer ring.
// Windows 8.1 / Direct3D 11.
// Supports non-flushing probes and bounded progress-capable GetData.
// No explicit ID3D11DeviceContext::Flush call.

#include <d3d11.h>
#include <stdint.h>

class PTARD3D11GpuTimerRing
{
public:
    PTARD3D11GpuTimerRing() : m_write(0), m_read(0), m_initialized(false)
    {
        for (int i=0;i<kSlots;i++)
        {
            m_slots[i].disjoint=0;
            m_slots[i].begin=0;
            m_slots[i].end=0;
            m_slots[i].pending=false;
            m_slots[i].open=false;
        }
    }

    ~PTARD3D11GpuTimerRing() { Shutdown(); }

    HRESULT Initialize(ID3D11Device* device)
    {
        if (!device) return E_INVALIDARG;
        Shutdown();

        D3D11_QUERY_DESC qd={};
        for (int i=0;i<kSlots;i++)
        {
            qd.Query=D3D11_QUERY_TIMESTAMP_DISJOINT;
            HRESULT hr=device->CreateQuery(&qd,&m_slots[i].disjoint);
            if (FAILED(hr)) { Shutdown(); return hr; }

            qd.Query=D3D11_QUERY_TIMESTAMP;
            hr=device->CreateQuery(&qd,&m_slots[i].begin);
            if (FAILED(hr)) { Shutdown(); return hr; }
            hr=device->CreateQuery(&qd,&m_slots[i].end);
            if (FAILED(hr)) { Shutdown(); return hr; }
        }
        m_write=0; m_read=0; m_initialized=true;
        return S_OK;
    }

    void Shutdown()
    {
        for (int i=0;i<kSlots;i++)
        {
            SafeRelease(m_slots[i].disjoint);
            SafeRelease(m_slots[i].begin);
            SafeRelease(m_slots[i].end);
            m_slots[i].pending=false;
            m_slots[i].open=false;
        }
        m_initialized=false;
        m_write=0; m_read=0;
    }

    bool Begin(ID3D11DeviceContext* ctx)
    {
        if (!m_initialized || !ctx) return false;
        Slot& s=m_slots[m_write];
        if (s.pending || s.open) return false;
        ctx->Begin(s.disjoint);
        ctx->End(s.begin);
        s.open=true;
        return true;
    }

    bool End(ID3D11DeviceContext* ctx)
    {
        if (!m_initialized || !ctx) return false;
        Slot& s=m_slots[m_write];
        if (!s.open) return false;
        ctx->End(s.end);
        ctx->End(s.disjoint);
        s.open=false;
        s.pending=true;
        m_write=(m_write+1)%kSlots;
        return true;
    }

    // Extended resolver.
    //
    // flags=0 allows the D3D11 runtime to submit/flush work if required.
    // flags=D3D11_ASYNC_GETDATA_DONOTFLUSH remains available for callers
    // that explicitly want a non-flushing probe.
    HRESULT TryResolveEx(
        ID3D11DeviceContext* ctx,
        double* milliseconds,
        UINT flags,
        bool* ready,
        bool* valid)
    {
        if (ready) *ready=false;
        if (valid) *valid=false;
        if (!m_initialized || !ctx || !milliseconds || !ready || !valid)
            return E_INVALIDARG;

        Slot& s=m_slots[m_read];
        if (!s.pending || s.open) return S_FALSE;

        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj={};
        HRESULT hr=ctx->GetData(s.disjoint,&dj,sizeof(dj),flags);
        if (hr==S_FALSE) return S_FALSE;
        if (FAILED(hr)) return hr;

        UINT64 t0=0,t1=0;
        hr=ctx->GetData(s.begin,&t0,sizeof(t0),flags);
        if (hr==S_FALSE) return S_FALSE;
        if (FAILED(hr)) return hr;

        hr=ctx->GetData(s.end,&t1,sizeof(t1),flags);
        if (hr==S_FALSE) return S_FALSE;
        if (FAILED(hr)) return hr;

        s.pending=false;
        m_read=(m_read+1)%kSlots;
        *ready=true;

        if (dj.Disjoint || dj.Frequency==0 || t1<t0)
        {
            *valid=false;
            return S_OK;
        }

        *milliseconds=(double)(t1-t0)*1000.0/(double)dj.Frequency;
        *valid=true;
        return S_OK;
    }

    // Legacy non-flushing probe kept for older project code/tests.
    bool TryResolve(ID3D11DeviceContext* ctx, double* milliseconds)
    {
        bool ready=false,valid=false;
        const HRESULT hr=TryResolveEx(
            ctx,milliseconds,D3D11_ASYNC_GETDATA_DONOTFLUSH,&ready,&valid);
        return hr==S_OK && ready && valid;
    }

private:
    enum { kSlots=8 };
    struct Slot
    {
        ID3D11Query* disjoint;
        ID3D11Query* begin;
        ID3D11Query* end;
        bool pending;
        bool open;
    };
    Slot m_slots[kSlots];
    int m_write;
    int m_read;
    bool m_initialized;

    template<class T> static void SafeRelease(T*& p)
    {
        if (p) { p->Release(); p=0; }
    }
};
