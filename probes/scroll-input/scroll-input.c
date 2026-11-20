/* SPDX-License-Identifier: LGPL-2.1-or-later
 * Copyright (c) 2026 the wine-wayland-desktop contributors */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <stdio.h>
#include <wchar.h>

static FILE *output;
static LARGE_INTEGER frequency, start;

static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    LARGE_INTEGER now;
    INPUT_MESSAGE_SOURCE source = {0};
    BOOL has_source;
    if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL ||
        message == WM_POINTERWHEEL || message == WM_POINTERHWHEEL ||
        (message == WM_KEYDOWN && wp == VK_SPACE))
    {
        QueryPerformanceCounter(&now);
        has_source = GetCurrentInputMessageSource(&source);
        fprintf(output, "%.3f\t%lu\t0x%x\t%d\t%u\t%u\t%d\n",
                (now.QuadPart - start.QuadPart) * 1000.0 / frequency.QuadPart,
                (unsigned long)GetMessageTime(), message,
                message == WM_KEYDOWN ? 0 : GET_WHEEL_DELTA_WPARAM(wp),
                (unsigned)source.deviceType, (unsigned)source.originId, has_source);
        fflush(output);
        return 0;
    }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, wp, lp);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, LPWSTR command, int show)
{
    WCHAR path[MAX_PATH], *extension;
    WNDCLASSW cls = {0};
    HWND window;
    MSG message;
    DWORD length;
    (void)previous; (void)command;
    length = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (!length || length >= MAX_PATH) return 1;
    extension = wcsrchr(path, L'.');
    if (!extension || extension - path + 5 >= MAX_PATH) return 1;
    wcscpy(extension, L".tsv");
    output = _wfopen(path, L"w");
    if (!output) return 2;
    fprintf(output, "elapsed_ms\tmessage_time\tmessage\tdelta\tdevice_type\torigin\tsource_available\n");
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    cls.lpfnWndProc = window_proc;
    cls.hInstance = instance;
    cls.lpszClassName = L"ScrollInputTimingProbe";
    cls.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    cls.hCursor = LoadCursorW(NULL, IDC_ARROW);
    if (!RegisterClassW(&cls)) { fclose(output); return 3; }
    window = CreateWindowW(cls.lpszClassName,
        L"Scroll input timing: scroll here; Space marks release; close to finish",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 800, 500,
        NULL, NULL, instance, NULL);
    if (!window) { fclose(output); return 4; }
    ShowWindow(window, show);
    while (GetMessageW(&message, NULL, 0, 0) > 0)
    { TranslateMessage(&message); DispatchMessageW(&message); }
    fclose(output);
    return 0;
}
