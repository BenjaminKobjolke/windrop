// windrop.exe — CLI Windows Drag-and-Drop Utility
//
// Usage:  windrop <file1> [file2 ...]
// Flow:   - Option A (Keyboard): Hover over target and press [F8] to drag → press [F8] or left-click to drop!
//         - Option B (Mouse): Grab & drag the floating card directly to drop!
//         - Dismiss: Press [Esc] or right-click on card anytime.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <ole2.h>
#include <cstdio>
#include <cwchar>
#include <new>
#include <string>
#include <vector>

#include "AppState.h"
#include "CardWindow.h"
#include "DragImage.h"
#include "Hotkeys.h"
#include "Utils.h"

WindropPayloadState* g_pState         = nullptr;
DWORD                g_mainThreadId   = 0;
volatile bool        g_isDragging     = false;
volatile bool        g_shouldDrop     = false;
volatile bool        g_shouldCancel   = false;
bool                 g_verbose        = false;
bool                 g_keyboard       = false;

// ── Entry point ──
int wmain(int argc, wchar_t* argv[])
{
    std::vector<std::wstring> rawArgs;
    for (int i = 1; i < argc; ++i)
    {
        std::wstring arg = argv[i];
        if (arg == L"--version" || arg == L"-v" || arg == L"-V")
        {
            Utils::PrintVersion();
            return EXIT_OK;
        }
        if (arg == L"--help" || arg == L"-h" || arg == L"-?" || arg == L"/?")
        {
            Utils::PrintUsage(stdout);
            return EXIT_OK;
        }
        if (arg == L"--verbose")
        {
            g_verbose = true;
            continue;
        }
        if (arg == L"--keyboard")
        {
            g_keyboard = true;
            continue;
        }
        rawArgs.emplace_back(arg);
    }

    if (rawArgs.empty()) { Utils::PrintUsage(stderr); return EXIT_BAD_ARGS; }

    g_mainThreadId = GetCurrentThreadId();

    // Enable Per-Monitor V2 DPI awareness
    typedef BOOL(WINAPI* PFN_SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT);
    auto pfnSetDpi = reinterpret_cast<PFN_SetProcessDpiAwarenessContext>(GetProcAddress(
        GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"));
    if (pfnSetDpi)
        pfnSetDpi(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    std::vector<std::wstring> missing;
    std::vector<std::wstring> files = Utils::ResolvePaths(rawArgs, missing);

    for (const auto& m : missing)
        Utils::PrintError(L"File not found: " + m);
    if (!missing.empty()) return EXIT_FILE_MISSING;
    if (files.empty())    { Utils::PrintUsage(stderr); return EXIT_BAD_ARGS; }

    HRESULT hr = OleInitialize(nullptr);
    if (FAILED(hr))
    {
        Utils::PrintError(L"OleInitialize failed");
        return EXIT_COM_ERROR;
    }

    HINSTANCE hInst = GetModuleHandleW(nullptr);

    // Register Window Class for the card
    WNDCLASSEXW wc   = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = CardWindow::WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_HAND);
    wc.lpszClassName = kCardClassName;
    RegisterClassExW(&wc);

    WindropPayloadState state;
    state.files = files;
    state.pDataObj = new (std::nothrow) CDataObject(files);
    state.pDropSrc = new (std::nothrow) CDropSource(&g_shouldDrop, &g_shouldCancel);

    if (!state.pDataObj || !state.pDropSrc)
    {
        Utils::PrintError(L"Out of memory allocating COM objects");
        if (state.pDropSrc) state.pDropSrc->Release();
        if (state.pDataObj) state.pDataObj->Release();
        UnregisterClassW(kCardClassName, hInst);
        OleUninitialize();
        return EXIT_COM_ERROR;
    }

    // Extract Shell Icon
    SHFILEINFOW sfi = {};
    if (SHGetFileInfoW(files[0].c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON))
        state.hIcon = sfi.hIcon;

    // Build Titles
    if (files.size() == 1)
    {
        size_t lastSlash = files[0].find_last_of(L"\\/");
        state.displayTitle = (lastSlash != std::wstring::npos) ? files[0].substr(lastSlash + 1) : files[0];
    }
    else
    {
        state.displayTitle = std::to_wstring(files.size()) + L" files";
    }
    state.displaySubtitle = g_keyboard
        ? L"WASD/Arrows Move \x2022 Q/Space/Enter Drop \x2022 Esc Exit"
        : L"Drag or [F8] Engage \x2022 Right-Click Exit";

    g_pState = &state;

    // Count existing Windrop windows to cascade position
    struct CascadeCount { int count = 0; } cascade;
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        wchar_t cls[64] = {};
        if (GetClassNameW(h, cls, 64) && wcscmp(cls, kCardClassName) == 0)
            reinterpret_cast<CascadeCount*>(lp)->count++;
        return TRUE;
    }, reinterpret_cast<LPARAM>(&cascade));

    // Positioning
    POINT pt;
    GetCursorPos(&pt);

    const int kWidth  = 280;
    const int kHeight = 70;
    int offset = (cascade.count % 6) * 30;
    int posX = pt.x - 20 + offset;
    int posY = pt.y - 20 + offset;

    HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    if (GetMonitorInfoW(hMon, &mi))
    {
        if (posX + kWidth > mi.rcWork.right)   posX = mi.rcWork.right - kWidth - 10;
        if (posX < mi.rcWork.left)             posX = mi.rcWork.left + 10;
        if (posY + kHeight > mi.rcWork.bottom) posY = mi.rcWork.bottom - kHeight - 10;
        if (posY < mi.rcWork.top)              posY = mi.rcWork.top + 10;
    }

    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        kCardClassName, L"windrop",
        WS_POPUP,
        posX, posY, kWidth, kHeight,
        nullptr, nullptr, hInst, nullptr);

    if (!hwnd)
    {
        Utils::PrintError(L"Failed to create windrop payload window");
        state.pDropSrc->Release();
        state.pDataObj->Release();
        if (state.hIcon) DestroyIcon(state.hIcon);
        UnregisterClassW(kCardClassName, hInst);
        OleUninitialize();
        return EXIT_COM_ERROR;
    }

    state.hwndCard = hwnd;

    // Apply rounded window region
    HRGN hRgn = CreateRoundRectRgn(0, 0, kWidth + 1, kHeight + 1, 16, 16);
    SetWindowRgn(hwnd, hRgn, TRUE);

    // Attach Shell Drag Image to card for direct mouse drags
    DragImage::AttachShellImage(hwnd, files, state.pDataObj);

    // Start dedicated hotkey thread for F8 and Escape (safe alternative to low-level keyboard hooks)
    Hotkeys::Start();

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    // The caller already aimed the cursor, so engage the drag like F8 would.
    if (g_keyboard)
        PostMessageW(hwnd, WM_START_F8_DRAG, 0, 0);

    if (g_verbose)
    {
        fwprintf(stderr, L"\nwindrop ready\n\n");
        fwprintf(stderr, L"Controls:\n");
        fwprintf(stderr, L"  - Mouse:    Drag the floating card directly into any window\n");
        fwprintf(stderr, L"  - Keyboard: Hover over target window and press [F8] to drag -> press [F8] or left-click to drop\n");
        if (g_keyboard)
            fwprintf(stderr, L"  - --keyboard: [WASD]/arrows move, [Q]/[Space]/[Enter] drop\n");
        fwprintf(stderr, L"  - Dismiss:  Press [Esc] or right-click on card\n\n");
    }

    fwprintf(stderr, L"Payload (%zu file%s):\n", files.size(), files.size() == 1 ? L"" : L"s");
    for (const auto& f : files)
        fwprintf(stderr, L"  - %s\n", f.c_str());
    fwprintf(stderr, L"\n");
    fflush(stderr);

    // Message pump
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Hotkeys::Stop();

    if (state.hIcon)
        DestroyIcon(state.hIcon);

    state.pDropSrc->Release();
    state.pDataObj->Release();
    UnregisterClassW(kCardClassName, hInst);
    OleUninitialize();

    return state.exitCode;
}
