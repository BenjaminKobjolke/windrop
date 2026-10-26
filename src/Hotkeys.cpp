#include "AppState.h"
#include "CardWindow.h"

namespace Hotkeys
{
namespace
{
constexpr int kHotkeyF8  = 1;
constexpr int kHotkeyEsc = 2;

// --keyboard: the drag starts at once and these keys steer it.
// ponytail: fixed step, add Shift-accelerate if aiming feels slow.
constexpr int kKeyboardStep = 20;

struct KeyboardKey
{
    UINT vk;
    UINT mods;
    int  dx;
    int  dy;
};

// Moves repeat while held; dx == dy == 0 means drop. Hotkey id = 3 + index.
constexpr KeyboardKey kKeyboardKeys[] = {
    { 'W',       0,             0,              -kKeyboardStep },
    { 'A',       0,             -kKeyboardStep, 0 },
    { 'S',       0,             0,              kKeyboardStep },
    { 'D',       0,             kKeyboardStep,  0 },
    { VK_UP,     0,             0,              -kKeyboardStep },
    { VK_LEFT,   0,             -kKeyboardStep, 0 },
    { VK_DOWN,   0,             0,              kKeyboardStep },
    { VK_RIGHT,  0,             kKeyboardStep,  0 },
    { 'Q',       MOD_NOREPEAT,  0,              0 },
    { VK_SPACE,  MOD_NOREPEAT,  0,              0 },
    { VK_RETURN, MOD_NOREPEAT,  0,              0 },
};
constexpr int kHotkeyKeyboardFirst = 3;
constexpr int kKeyboardKeyCount =
    static_cast<int>(sizeof(kKeyboardKeys) / sizeof(kKeyboardKeys[0]));

#define WM_HOTKEY_CMD_REGISTER   (WM_USER + 10)
#define WM_HOTKEY_CMD_UNREGISTER (WM_USER + 11)

DWORD  g_hotkeyThreadId = 0;
HANDLE g_hHotkeyThread = nullptr;

void WakeupDragLoop()
{
    if (g_mainThreadId)
        PostThreadMessageW(g_mainThreadId, WM_KEYDOWN, VK_ESCAPE, 0);

    INPUT inputs[2] = {};
    inputs[0].type = INPUT_MOUSE;
    inputs[0].mi.dwFlags = MOUSEEVENTF_MOVE;
    inputs[0].mi.dx = 1;
    inputs[1].type = INPUT_MOUSE;
    inputs[1].mi.dwFlags = MOUSEEVENTF_MOVE;
    inputs[1].mi.dx = -1;
    SendInput(2, inputs, sizeof(INPUT));
}

void RequestDrop()
{
    g_shouldDrop = true;
    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
    WakeupDragLoop();
}

void MoveCursorBy(int dx, int dy)
{
    INPUT input = {};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;
    input.mi.dx = dx;
    input.mi.dy = dy;
    SendInput(1, &input, sizeof(INPUT));
}

// Runs a separate message pump so hotkeys are never blocked by DoDragDrop.
DWORD WINAPI HotkeyThreadProc(LPVOID)
{
    g_hotkeyThreadId = GetCurrentThreadId();

    auto doRegister = []() {
        RegisterHotKey(nullptr, kHotkeyF8, MOD_NOREPEAT, VK_F8);
        RegisterHotKey(nullptr, kHotkeyEsc, MOD_NOREPEAT, VK_ESCAPE);
        if (g_keyboard)
            for (int i = 0; i < kKeyboardKeyCount; ++i)
                RegisterHotKey(nullptr, kHotkeyKeyboardFirst + i,
                               kKeyboardKeys[i].mods, kKeyboardKeys[i].vk);
    };

    auto doUnregister = []() {
        UnregisterHotKey(nullptr, kHotkeyF8);
        UnregisterHotKey(nullptr, kHotkeyEsc);
        if (g_keyboard)
            for (int i = 0; i < kKeyboardKeyCount; ++i)
                UnregisterHotKey(nullptr, kHotkeyKeyboardFirst + i);
    };

    doRegister();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        if (msg.message == WM_HOTKEY)
        {
            if (msg.wParam == kHotkeyF8)
            {
                if (!g_isDragging)
                {
                    HWND targetWnd = CardWindow::FindTopmost();
                    if (targetWnd)
                    {
                        if (g_pState && targetWnd != g_pState->hwndCard)
                        {
                            doUnregister();
                            PostMessageW(targetWnd, WM_CLAIM_HOTKEY, 0, 0);
                        }
                        PostMessageW(targetWnd, WM_START_F8_DRAG, 0, 0);
                    }
                }
                else
                {
                    RequestDrop();
                }
            }
            else if (msg.wParam >= kHotkeyKeyboardFirst &&
                     msg.wParam < kHotkeyKeyboardFirst + kKeyboardKeyCount)
            {
                const KeyboardKey& key = kKeyboardKeys[msg.wParam - kHotkeyKeyboardFirst];
                if (key.dx || key.dy)
                    MoveCursorBy(key.dx, key.dy);
                else if (g_isDragging)
                    RequestDrop();
            }
            else if (msg.wParam == kHotkeyEsc)
            {
                if (!g_isDragging)
                {
                    HWND targetWnd = CardWindow::FindTopmost();
                    if (targetWnd)
                        PostMessageW(targetWnd, WM_CLOSE, 0, 0);
                }
                else
                {
                    g_shouldCancel = true;
                    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                    WakeupDragLoop();
                }
            }
        }
        else if (msg.message == WM_HOTKEY_CMD_REGISTER)
        {
            doRegister();
        }
        else if (msg.message == WM_HOTKEY_CMD_UNREGISTER)
        {
            doUnregister();
        }
        else if (msg.message == WM_QUIT)
        {
            break;
        }
    }

    doUnregister();
    return 0;
}
}

void Start()
{
    if (!g_hHotkeyThread)
        g_hHotkeyThread = CreateThread(nullptr, 0, HotkeyThreadProc, nullptr, 0, &g_hotkeyThreadId);
}

void Stop()
{
    if (g_hHotkeyThread)
    {
        if (g_hotkeyThreadId)
            PostThreadMessageW(g_hotkeyThreadId, WM_QUIT, 0, 0);
        WaitForSingleObject(g_hHotkeyThread, 1000);
        CloseHandle(g_hHotkeyThread);
        g_hHotkeyThread = nullptr;
        g_hotkeyThreadId = 0;
    }
}

void Claim()
{
    if (g_hotkeyThreadId)
        PostThreadMessageW(g_hotkeyThreadId, WM_HOTKEY_CMD_REGISTER, 0, 0);
}
}
