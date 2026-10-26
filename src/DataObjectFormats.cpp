#include "DataObject.h"
#include <cstring>
#include <cwchar>

static HGLOBAL CopyToHGlobal(const void* src, size_t bytes)
{
    HGLOBAL hGlobal = GlobalAlloc(GHND | GMEM_SHARE, bytes);
    if (!hGlobal) return nullptr;

    void* dest = GlobalLock(hGlobal);
    if (!dest) { GlobalFree(hGlobal); return nullptr; }

    memcpy(dest, src, bytes);
    GlobalUnlock(hGlobal);
    return hGlobal;
}

// ---------------------------------------------------------------------------
// BuildHDrop
//
// Layout of HGLOBAL for CF_HDROP:
//   [DROPFILES header]
//   [wchar_t* path1 \0]
//   [wchar_t* path2 \0]
//   ...
//   [wchar_t* pathN \0]
//   [\0]               ← extra terminator (double-null)
// ---------------------------------------------------------------------------
HGLOBAL CDataObject::BuildHDrop() const
{
    size_t pathChars = 1; // final double-NUL
    for (const auto& f : m_files)
        pathChars += f.size() + 1;

    const size_t headerSize = sizeof(DROPFILES);
    const size_t totalBytes = headerSize + pathChars * sizeof(wchar_t);

    HGLOBAL hGlobal = GlobalAlloc(GHND | GMEM_SHARE, totalBytes);
    if (!hGlobal) return nullptr;

    auto* pDrop = static_cast<DROPFILES*>(GlobalLock(hGlobal));
    if (!pDrop) { GlobalFree(hGlobal); return nullptr; }

    pDrop->pFiles = static_cast<DWORD>(headerSize);
    pDrop->pt     = {0, 0};
    pDrop->fNC    = FALSE;
    pDrop->fWide  = TRUE;

    wchar_t* dest = reinterpret_cast<wchar_t*>(
        reinterpret_cast<BYTE*>(pDrop) + headerSize);

    for (const auto& f : m_files)
    {
        std::wmemcpy(dest, f.c_str(), f.size());
        dest += f.size();
        *dest++ = L'\0';
    }
    *dest = L'\0';

    GlobalUnlock(hGlobal);
    return hGlobal;
}

// ---------------------------------------------------------------------------
// BuildPreferredDropEffect (CFSTR_PREFERREDDROPEFFECT)
// Advertises that we support both Copy and Move operations.
// ---------------------------------------------------------------------------
HGLOBAL CDataObject::BuildPreferredDropEffect() const
{
    HGLOBAL hGlobal = GlobalAlloc(GHND | GMEM_SHARE, sizeof(DWORD));
    if (!hGlobal) return nullptr;

    auto* pData = static_cast<DWORD*>(GlobalLock(hGlobal));
    if (!pData) { GlobalFree(hGlobal); return nullptr; }

    *pData = DROPEFFECT_COPY | DROPEFFECT_MOVE;
    GlobalUnlock(hGlobal);
    return hGlobal;
}

// ---------------------------------------------------------------------------
// BuildUnicodeText (CF_UNICODETEXT)
// Formatted paths separated by newlines for text boxes, search bars, editors.
// ---------------------------------------------------------------------------
HGLOBAL CDataObject::BuildUnicodeText() const
{
    if (m_files.empty()) return nullptr;

    std::wstring text;
    for (size_t i = 0; i < m_files.size(); ++i)
    {
        if (i > 0) text += L"\r\n";
        text += m_files[i];
    }

    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    return CopyToHGlobal(text.c_str(), bytes);
}

// ---------------------------------------------------------------------------
// BuildAnsiText (CF_TEXT)
// ANSI fallback string.
// ---------------------------------------------------------------------------
HGLOBAL CDataObject::BuildAnsiText() const
{
    if (m_files.empty()) return nullptr;

    std::string text;
    for (size_t i = 0; i < m_files.size(); ++i)
    {
        if (i > 0) text += "\r\n";
        int len = WideCharToMultiByte(CP_ACP, 0, m_files[i].c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (len > 1)
        {
            std::string s(len - 1, '\0');
            WideCharToMultiByte(CP_ACP, 0, m_files[i].c_str(), -1, &s[0], len, nullptr, nullptr);
            text += s;
        }
    }

    size_t bytes = text.size() + 1;
    return CopyToHGlobal(text.c_str(), bytes);
}

// ---------------------------------------------------------------------------
// BuildShellUrl (UniformResourceLocatorW)
// file:/// URL for browsers, Electron apps, and URL drops.
// ---------------------------------------------------------------------------
HGLOBAL CDataObject::BuildShellUrl() const
{
    if (m_files.empty()) return nullptr;

    std::wstring url = L"file:///";
    for (wchar_t ch : m_files[0])
    {
        if (ch == L'\\') url += L'/';
        else url += ch;
    }

    size_t bytes = (url.size() + 1) * sizeof(wchar_t);
    return CopyToHGlobal(url.c_str(), bytes);
}

// ---------------------------------------------------------------------------
// BuildFileNameW (FileNameW)
// Single filename string for older drop targets.
// ---------------------------------------------------------------------------
HGLOBAL CDataObject::BuildFileNameW() const
{
    if (m_files.empty()) return nullptr;

    size_t bytes = (m_files[0].size() + 1) * sizeof(wchar_t);
    return CopyToHGlobal(m_files[0].c_str(), bytes);
}
