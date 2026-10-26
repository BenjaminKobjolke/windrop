#include "DataObject.h"
#include "EnumFormatEtc.h"
#include <cstring>
#include <stdexcept>

// ---------------------------------------------------------------------------
// Helper: duplicate an HGLOBAL block
// ---------------------------------------------------------------------------
static HGLOBAL DuplicateHGlobal(HGLOBAL hSrc)
{
    if (!hSrc) return nullptr;
    SIZE_T cb = GlobalSize(hSrc);
    void*  pSrc = GlobalLock(hSrc);
    if (!pSrc) return nullptr;

    HGLOBAL hDst = GlobalAlloc(GHND | GMEM_SHARE, cb);
    if (hDst)
    {
        void* pDst = GlobalLock(hDst);
        if (pDst) { memcpy(pDst, pSrc, cb); GlobalUnlock(hDst); }
        else      { GlobalFree(hDst); hDst = nullptr; }
    }
    GlobalUnlock(hSrc);
    return hDst;
}

// ---------------------------------------------------------------------------
// CDataObject
// ---------------------------------------------------------------------------
CDataObject::CDataObject(const std::vector<std::wstring>& files)
    : m_files(files), m_refCount(1)
{}

CDataObject::~CDataObject()
{
    // Release all stored media
    for (auto& sm : m_storedData)
        ReleaseStgMedium(&sm.med);
}

// ---------------------------------------------------------------------------
// IUnknown
// ---------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE CDataObject::QueryInterface(REFIID riid, void** ppvObject)
{
    if (!ppvObject) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IDataObject))
    {
        *ppvObject = static_cast<IDataObject*>(this);
        AddRef();
        return S_OK;
    }
    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE CDataObject::AddRef()  { return ++m_refCount; }
ULONG STDMETHODCALLTYPE CDataObject::Release()
{
    ULONG c = --m_refCount;
    if (!c) delete this;
    return c;
}

// ---------------------------------------------------------------------------
// Registered shell clipboard formats
// ---------------------------------------------------------------------------
static const UINT g_cfPreferredDropEffect = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
static const UINT g_cfShellUrlW           = RegisterClipboardFormatW(CFSTR_SHELLURL);
static const UINT g_cfFileNameW           = RegisterClipboardFormatW(CFSTR_FILENAMEW);
static const UINT g_cfFileNameA           = RegisterClipboardFormatW(CFSTR_FILENAMEA);

// ---------------------------------------------------------------------------
// IDataObject::GetData
//
// First checks stored data (drag image, drop descriptions, etc.),
// then handles native formats (CF_HDROP, PreferredDropEffect, UnicodeText, etc.)
// ---------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE CDataObject::GetData(FORMATETC* pfe, STGMEDIUM* pmed)
{
    if (!pfe || !pmed) return E_POINTER;

    // Check stored data first (drag images, drop descriptions, etc.)
    for (const auto& sm : m_storedData)
    {
        if (sm.fmt.cfFormat == pfe->cfFormat &&
            (sm.fmt.tymed & pfe->tymed) &&
            (sm.fmt.dwAspect == pfe->dwAspect || pfe->dwAspect == DVASPECT_CONTENT))
        {
            pmed->tymed = sm.med.tymed;
            pmed->pUnkForRelease = nullptr;

            switch (sm.med.tymed)
            {
            case TYMED_HGLOBAL:
                pmed->hGlobal = DuplicateHGlobal(sm.med.hGlobal);
                return pmed->hGlobal ? S_OK : E_OUTOFMEMORY;

            case TYMED_ISTREAM:
                pmed->pstm = sm.med.pstm;
                if (pmed->pstm) pmed->pstm->AddRef();
                return S_OK;

            case TYMED_ISTORAGE:
                pmed->pstg = sm.med.pstg;
                if (pmed->pstg) pmed->pstg->AddRef();
                return S_OK;

            default:
                if (sm.med.pUnkForRelease)
                {
                    *pmed = sm.med;
                    pmed->pUnkForRelease->AddRef();
                    return S_OK;
                }
                return DATA_E_FORMATETC;
            }
        }
    }

    if (!(pfe->tymed & TYMED_HGLOBAL)) return DATA_E_FORMATETC;

    HGLOBAL hData = nullptr;
    if (pfe->cfFormat == CF_HDROP)
    {
        hData = BuildHDrop();
    }
    else if (g_cfPreferredDropEffect && pfe->cfFormat == g_cfPreferredDropEffect)
    {
        hData = BuildPreferredDropEffect();
    }
    else if (pfe->cfFormat == CF_UNICODETEXT)
    {
        hData = BuildUnicodeText();
    }
    else if (pfe->cfFormat == CF_TEXT)
    {
        hData = BuildAnsiText();
    }
    else if (g_cfShellUrlW && pfe->cfFormat == g_cfShellUrlW)
    {
        hData = BuildShellUrl();
    }
    else if (g_cfFileNameW && pfe->cfFormat == g_cfFileNameW)
    {
        hData = BuildFileNameW();
    }
    else
    {
        return DATA_E_FORMATETC;
    }

    if (!hData) return E_OUTOFMEMORY;

    pmed->tymed          = TYMED_HGLOBAL;
    pmed->hGlobal        = hData;
    pmed->pUnkForRelease = nullptr;
    return S_OK;
}

// ---------------------------------------------------------------------------
// IDataObject::QueryGetData
// ---------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE CDataObject::QueryGetData(FORMATETC* pfe)
{
    if (!pfe) return E_POINTER;

    for (const auto& sm : m_storedData)
    {
        if (sm.fmt.cfFormat == pfe->cfFormat &&
            (sm.fmt.tymed & pfe->tymed) &&
            (sm.fmt.dwAspect == pfe->dwAspect || pfe->dwAspect == DVASPECT_CONTENT))
            return S_OK;
    }

    if (!(pfe->tymed & TYMED_HGLOBAL)) return DATA_E_FORMATETC;

    if (pfe->cfFormat == CF_HDROP ||
        (g_cfPreferredDropEffect && pfe->cfFormat == g_cfPreferredDropEffect) ||
        pfe->cfFormat == CF_UNICODETEXT ||
        pfe->cfFormat == CF_TEXT ||
        (g_cfShellUrlW && pfe->cfFormat == g_cfShellUrlW) ||
        (g_cfFileNameW && pfe->cfFormat == g_cfFileNameW))
    {
        return S_OK;
    }

    return DATA_E_FORMATETC;
}

// ---------------------------------------------------------------------------
// IDataObject::SetData
// ---------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE CDataObject::SetData(FORMATETC* pfe, STGMEDIUM* pmed, BOOL fRelease)
{
    if (!pfe || !pmed) return E_POINTER;

    for (auto& sm : m_storedData)
    {
        if (sm.fmt.cfFormat == pfe->cfFormat &&
            sm.fmt.dwAspect == pfe->dwAspect)
        {
            ReleaseStgMedium(&sm.med);
            sm.fmt = *pfe;

            if (fRelease)
            {
                sm.med = *pmed;
            }
            else
            {
                sm.med = {};
                sm.med.tymed = pmed->tymed;
                if (pmed->tymed == TYMED_HGLOBAL)
                    sm.med.hGlobal = DuplicateHGlobal(pmed->hGlobal);
                sm.med.pUnkForRelease = nullptr;
            }
            return S_OK;
        }
    }

    StoredMedium entry = {};
    entry.fmt = *pfe;

    if (fRelease)
    {
        entry.med = *pmed;
    }
    else
    {
        entry.med = {};
        entry.med.tymed = pmed->tymed;
        if (pmed->tymed == TYMED_HGLOBAL)
            entry.med.hGlobal = DuplicateHGlobal(pmed->hGlobal);
        entry.med.pUnkForRelease = nullptr;
    }

    m_storedData.push_back(entry);
    return S_OK;
}

// ---------------------------------------------------------------------------
// IDataObject::EnumFormatEtc
// ---------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE CDataObject::EnumFormatEtc(DWORD dwDirection, IEnumFORMATETC** ppEnum)
{
    if (!ppEnum) return E_POINTER;
    if (dwDirection != DATADIR_GET) return E_NOTIMPL;

    std::vector<FORMATETC> fmts;

    auto addFormat = [&](CLIPFORMAT cf) {
        if (!cf) return;
        FORMATETC fe = {};
        fe.cfFormat = cf;
        fe.ptd      = nullptr;
        fe.dwAspect = DVASPECT_CONTENT;
        fe.lindex   = -1;
        fe.tymed    = TYMED_HGLOBAL;
        fmts.push_back(fe);
    };

    addFormat(CF_HDROP);
    if (g_cfPreferredDropEffect)
        addFormat(static_cast<CLIPFORMAT>(g_cfPreferredDropEffect));
    addFormat(CF_UNICODETEXT);
    addFormat(CF_TEXT);
    if (g_cfShellUrlW)
        addFormat(static_cast<CLIPFORMAT>(g_cfShellUrlW));
    if (g_cfFileNameW)
        addFormat(static_cast<CLIPFORMAT>(g_cfFileNameW));

    for (const auto& sm : m_storedData)
        fmts.push_back(sm.fmt);

    auto* p = new (std::nothrow) CEnumFormatEtc(fmts);
    if (!p) return E_OUTOFMEMORY;
    *ppEnum = p;
    return S_OK;
}
