#include "AppState.h"
#include "CardWindow.h"
#include "Hotkeys.h"
#include <cstdio>
#include <cwchar>

namespace CardWindow
{
namespace
{
struct TopWindropFinder
{
    HWND topHwnd = nullptr;
};

BOOL CALLBACK EnumWindropWindowsProc(HWND hwnd, LPARAM lParam)
{
    wchar_t clsName[64] = {};
    if (GetClassNameW(hwnd, clsName, 64) && wcscmp(clsName, kCardClassName) == 0)
    {
        if (IsWindowVisible(hwnd))
        {
            auto* pFinder = reinterpret_cast<TopWindropFinder*>(lParam);
            if (!pFinder->topHwnd)
                pFinder->topHwnd = hwnd;
            return FALSE;
        }
    }
    return TRUE;
}

void ExecuteOLEDrag(HWND hwnd)
{
    if (!g_pState || !g_pState->pDataObj || !g_pState->pDropSrc)
        return;

    ShowWindow(hwnd, SW_HIDE);
    g_isDragging   = true;
    g_shouldDrop   = false;
    g_shouldCancel = false;

    if (g_verbose)
        fwprintf(stderr, L"Dragging... hover over target window and press [F8] or left-click to drop ([Esc] to cancel).\n");
    else
        fwprintf(stderr, L"Dragging...\n");
    fflush(stderr);

    DWORD dwEffect = 0;
    DWORD dwOK     = DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK;

    HRESULT hr = DoDragDrop(g_pState->pDataObj, g_pState->pDropSrc, dwOK, &dwEffect);

    g_isDragging = false;

    // Release synthesized mouse button if any
    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);

    if (hr == DRAGDROP_S_DROP)
    {
        if (dwEffect & DROPEFFECT_MOVE)
        {
            wprintf(L"Drop completed (Move).\n");
            g_pState->exitCode = EXIT_OK;
        }
        else if (dwEffect & (DROPEFFECT_COPY | DROPEFFECT_LINK))
        {
            wprintf(L"Drop completed (Copy).\n");
            g_pState->exitCode = EXIT_OK;
        }
        else
        {
            wprintf(L"Drop completed.\n");
            g_pState->exitCode = EXIT_OK;
        }
    }
    else
    {
        wprintf(L"Drag canceled.\n");
        g_pState->exitCode = EXIT_CANCELLED;
    }

    DestroyWindow(hwnd);
}
}

HWND FindTopmost()
{
    TopWindropFinder finder;
    EnumWindows(EnumWindropWindowsProc, reinterpret_cast<LPARAM>(&finder));
    return finder.topHwnd;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_START_F8_DRAG)
    {
        // Move the card under the cursor so synthesized mouse down starts OLE drag.
        POINT pt = {};
        GetCursorPos(&pt);
        SetWindowPos(hwnd, HWND_TOPMOST, pt.x - 20, pt.y - 20, 40, 40, SWP_SHOWWINDOW);
        mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
        return 0;
    }

    if (msg == WM_CLAIM_HOTKEY)
    {
        Hotkeys::Claim();
        return 0;
    }

    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        // Double buffer to avoid any flicker
        HDC memDC      = CreateCompatibleDC(hdc);
        HBITMAP memBm  = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HBITMAP oldBm  = static_cast<HBITMAP>(SelectObject(memDC, memBm));

        // Dark Fluent acrylic style background
        HBRUSH bgBrush   = CreateSolidBrush(RGB(32, 33, 36));
        HPEN   borderPen = CreatePen(PS_SOLID, 2, RGB(0, 162, 237)); // Glowing Cyan Accent
        HBRUSH oldBrush  = static_cast<HBRUSH>(SelectObject(memDC, bgBrush));
        HPEN   oldPen    = static_cast<HPEN>(SelectObject(memDC, borderPen));

        RoundRect(memDC, rc.left, rc.top, rc.right, rc.bottom, 16, 16);

        SelectObject(memDC, oldBrush);
        SelectObject(memDC, oldPen);
        DeleteObject(bgBrush);
        DeleteObject(borderPen);

        // Draw 32x32 Shell Icon
        if (g_pState && g_pState->hIcon)
            DrawIconEx(memDC, 14, (rc.bottom - 32) / 2, g_pState->hIcon, 32, 32, 0, nullptr, DI_NORMAL);

        SetBkMode(memDC, TRANSPARENT);
        HFONT hFontTitle = CreateFontW(
            -13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HFONT hFontSub = CreateFontW(
            -11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        if (g_pState)
        {
            HFONT oldFont = static_cast<HFONT>(SelectObject(memDC, hFontTitle));
            SetTextColor(memDC, RGB(248, 249, 250));
            RECT rcTitle = { 56, 14, rc.right - 14, 34 };
            DrawTextW(memDC, g_pState->displayTitle.c_str(), -1, &rcTitle,
                      DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

            SelectObject(memDC, hFontSub);
            SetTextColor(memDC, RGB(154, 160, 166));
            RECT rcSub = { 56, 36, rc.right - 14, 54 };
            DrawTextW(memDC, g_pState->displaySubtitle.c_str(), -1, &rcSub,
                      DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            SelectObject(memDC, oldFont);
        }

        DeleteObject(hFontTitle);
        DeleteObject(hFontSub);
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBm);
        DeleteObject(memBm);
        DeleteDC(memDC);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN:
        ExecuteOLEDrag(hwnd);
        return 0;

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE)
        {
            wprintf(L"Drag canceled.\n");
            if (g_pState) g_pState->exitCode = EXIT_CANCELLED;
            DestroyWindow(hwnd);
            return 0;
        }
        if (wp == VK_F8)
        {
            PostMessageW(hwnd, WM_START_F8_DRAG, 0, 0);
            return 0;
        }
        break;

    case WM_RBUTTONUP:
    case WM_CLOSE:
        wprintf(L"Drag canceled.\n");
        if (g_pState) g_pState->exitCode = EXIT_CANCELLED;
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
    {
        Hotkeys::Stop();
        HWND nextWnd = FindTopmost();
        if (nextWnd && nextWnd != hwnd)
            PostMessageW(nextWnd, WM_CLAIM_HOTKEY, 0, 0);
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
}
