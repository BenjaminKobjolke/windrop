#pragma once
#include <windows.h>

namespace CardWindow
{
    HWND FindTopmost();
    LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
}
