#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>
#include <vector>
#include "DataObject.h"
#include "DropSource.h"

inline constexpr int EXIT_OK           = 0;
inline constexpr int EXIT_BAD_ARGS     = 1;
inline constexpr int EXIT_FILE_MISSING = 2;
inline constexpr int EXIT_CANCELLED    = 3;
inline constexpr int EXIT_COM_ERROR    = 4;

inline constexpr const wchar_t* kCardClassName = L"WindropPayloadCardWnd";

#define WM_START_F8_DRAG (WM_APP + 1)
#define WM_CLAIM_HOTKEY  (WM_APP + 2)

struct WindropPayloadState
{
    std::vector<std::wstring> files;
    CDataObject*              pDataObj    = nullptr;
    CDropSource*              pDropSrc    = nullptr;
    HICON                     hIcon       = nullptr;
    std::wstring              displayTitle;
    std::wstring              displaySubtitle;
    int                       exitCode    = EXIT_CANCELLED;
    HWND                      hwndCard    = nullptr;
};

extern WindropPayloadState* g_pState;
extern DWORD                g_mainThreadId;
extern volatile bool        g_isDragging;
extern volatile bool        g_shouldDrop;
extern volatile bool        g_shouldCancel;
extern bool                 g_verbose;
extern bool                 g_keyboard;
