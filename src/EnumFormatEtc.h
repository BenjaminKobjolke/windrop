#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objidl.h>
#include <vector>
#include <atomic>
#include <new>

// ---------------------------------------------------------------------------
// Simple IEnumFORMATETC implementation so callers can discover our formats.
// Enumerates CF_HDROP plus any formats stored via SetData().
// ---------------------------------------------------------------------------
class CEnumFormatEtc : public IEnumFORMATETC
{
public:
    CEnumFormatEtc(const std::vector<FORMATETC>& fmts)
        : m_fmts(fmts), m_index(0), m_refCount(1)
    {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IEnumFORMATETC))
        { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef()  override { return ++m_refCount; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG c=--m_refCount; if(!c) delete this; return c; }

    HRESULT STDMETHODCALLTYPE Next(ULONG celt, FORMATETC* rgelt, ULONG* pceltFetched) override
    {
        if (!rgelt) return E_POINTER;
        ULONG fetched = 0;
        while (fetched < celt && m_index < m_fmts.size())
        {
            rgelt[fetched] = m_fmts[m_index];
            ++m_index;
            ++fetched;
        }
        if (pceltFetched) *pceltFetched = fetched;
        return fetched == celt ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Skip(ULONG celt) override { m_index += celt; return S_OK; }
    HRESULT STDMETHODCALLTYPE Reset()          override { m_index = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE Clone(IEnumFORMATETC** ppEnum) override
    {
        if (!ppEnum) return E_POINTER;
        auto* p = new (std::nothrow) CEnumFormatEtc(m_fmts);
        if (!p) return E_OUTOFMEMORY;
        p->m_index = m_index;
        *ppEnum = p;
        return S_OK;
    }

private:
    std::vector<FORMATETC> m_fmts;
    size_t                 m_index;
    std::atomic<ULONG>     m_refCount;
};
