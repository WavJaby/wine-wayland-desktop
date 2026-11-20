#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellscalingapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *events;
static HWND child, grandchild;
static BOOL custom_size;
static unsigned int dpi_messages;
static MONITORINFOEXW monitors[16];
static HMONITOR handles[16];
static unsigned int monitor_count;

static BOOL CALLBACK collect_monitor(HMONITOR monitor, HDC dc, RECT *rect, LPARAM param)
{
    (void)dc; (void)rect; (void)param;
    if (monitor_count == 16) return FALSE;
    handles[monitor_count] = monitor;
    monitors[monitor_count].cbSize = sizeof(monitors[monitor_count]);
    GetMonitorInfoW(monitor, (MONITORINFO *)&monitors[monitor_count]);
    monitor_count++;
    return TRUE;
}

static void enumerate(void)
{
    unsigned int i, j;
    monitor_count = 0;
    EnumDisplayMonitors(NULL, NULL, collect_monitor, 0);
    for (i = 0; i < monitor_count; i++)
        for (j = i + 1; j < monitor_count; j++)
            if (monitors[j].rcMonitor.left < monitors[i].rcMonitor.left)
            {
                MONITORINFOEXW info = monitors[i]; HMONITOR handle = handles[i];
                monitors[i] = monitors[j]; handles[i] = handles[j];
                monitors[j] = info; handles[j] = handle;
            }
}

static void record(HWND hwnd, const char *event)
{
    RECT rect, client_rect;
    MONITORINFO info = {.cbSize = sizeof(info)};
    UINT x = 0, y = 0;
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    GetWindowRect(hwnd, &rect); GetClientRect(hwnd, &client_rect);
    GetMonitorInfoW(monitor, &info);
    GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &x, &y);
    fprintf(events, "%lu\t%s\t%u\t%u\t%u\t%u\t%u\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\n",
            GetTickCount(), event, GetDpiForWindow(hwnd), child ? GetDpiForWindow(child) : 0,
            GetDpiForSystem(), x, dpi_messages, rect.left, rect.top,
            rect.right - rect.left, rect.bottom - rect.top,
            client_rect.right, client_rect.bottom, info.rcMonitor.left, info.rcMonitor.top);
    fflush(events);
}

static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_DPICHANGED_BEFOREPARENT || message == WM_DPICHANGED_AFTERPARENT)
    {
        fprintf(events, "child-notification\t%u\t%ld\t%u\t%lu\t%lu\t%ld\n",
                message, (long)GetWindowLongPtrW(hwnd, GWLP_ID), GetDpiForWindow(hwnd),
                GetCurrentThreadId(), (unsigned long)wparam, (long)lparam);
        fflush(events);
        return 0;
    }
    if (message == WM_GETDPISCALEDSIZE)
    {
        SIZE *size = (SIZE *)lparam;
        fprintf(events, "scaled-size-input\t%u\t%ld\t%ld\t%u\t%lu\n",
                (unsigned int)wparam, size->cx, size->cy, GetDpiForWindow(hwnd), GetCurrentThreadId());
        fflush(events);
        if (custom_size) { size->cx = 613; size->cy = 457; return TRUE; }
        return FALSE;
    }
    if (message == WM_DPICHANGED)
    {
        RECT rect = *(RECT *)lparam;
        dpi_messages++;
        fprintf(events, "dpi-message\t%u\t%u\t%u\t%ld\t%ld\t%ld\t%ld\n",
                LOWORD(wparam), HIWORD(wparam), GetDpiForWindow(hwnd),
                rect.left, rect.top, rect.right, rect.bottom);
        record(hwnd, "dpi-handler-before");
        SetWindowPos(hwnd, NULL, rect.left, rect.top, rect.right - rect.left,
                     rect.bottom - rect.top, SWP_NOZORDER | SWP_NOACTIVATE);
        record(hwnd, "dpi-handler-after");
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    }
    if (GetParent(hwnd)) return DefWindowProcW(hwnd, message, wparam, lparam);
    if (message == WM_DISPLAYCHANGE) record(hwnd, "display-change");
    if (message == WM_TIMER)
    {
        if (wparam == 2) { DestroyWindow(hwnd); return 0; }
        record(hwnd, "sample");
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    if (message == WM_PAINT)
    {
        PAINTSTRUCT paint; HDC dc = BeginPaint(hwnd, &paint);
        WCHAR text[128]; RECT rect; HBRUSH brush = CreateSolidBrush(RGB(32, 32, 32));
        GetClientRect(hwnd, &rect); FillRect(dc, &rect, brush); DeleteObject(brush);
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(255, 255, 255));
        swprintf(text, 128, L"Window DPI %u, child %u, system %u, notifications %u",
                 GetDpiForWindow(hwnd), child ? GetDpiForWindow(child) : 0,
                 GetDpiForSystem(), dpi_messages);
        TextOutW(dc, 20, 20, text, lstrlenW(text));
        EndPaint(hwnd, &paint); return 0;
    }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

int main(int argc, char **argv)
{
    WNDCLASSW wc = {0}; HWND hwnd; MSG message; unsigned int i;
    DPI_AWARENESS_CONTEXT context = DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2;
    if (argc > 1 && !strcmp(argv[1], "--move"))
    {
        unsigned int index = argc > 2 ? atoi(argv[2]) : 0;
        SetThreadDpiAwarenessContext(context); enumerate();
        hwnd = FindWindowW(L"PerMonitorScaleProbe", NULL);
        if (!hwnd || index >= monitor_count) return 2;
        return !SetWindowPos(hwnd, NULL, monitors[index].rcMonitor.left + 100,
                             monitors[index].rcMonitor.top + 100, 0, 0,
                             SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (argc > 1 && !strcmp(argv[1], "per-monitor-v1")) context = DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE;
    if (argc > 1 && !strcmp(argv[1], "per-monitor-custom")) custom_size = TRUE;
    if (argc > 1 && !strcmp(argv[1], "system")) context = DPI_AWARENESS_CONTEXT_SYSTEM_AWARE;
    if (argc > 1 && !strcmp(argv[1], "unaware")) context = DPI_AWARENESS_CONTEXT_UNAWARE;
    SetProcessDpiAwarenessContext(context);
    events = fopen("per-monitor-events.tsv", "w"); if (!events) return 3;
    enumerate();
    for (i = 0; i < monitor_count; i++)
    {
        UINT x = 0, y = 0; GetDpiForMonitor(handles[i], MDT_EFFECTIVE_DPI, &x, &y);
        fprintf(events, "monitor\t%u\t%ld\t%ld\t%ld\t%ld\t%u\t%u\n", i,
                monitors[i].rcMonitor.left, monitors[i].rcMonitor.top,
                monitors[i].rcMonitor.right, monitors[i].rcMonitor.bottom, x, y);
    }
    fprintf(events, "tick\tevent\twindow_dpi\tchild_dpi\tsystem_dpi\tmonitor_dpi\tdpi_messages\tx\ty\twidth\theight\tclient_width\tclient_height\tmonitor_x\tmonitor_y\n");
    wc.lpfnWndProc = procedure; wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"PerMonitorScaleProbe";
    if (!RegisterClassW(&wc)) return 4;
    hwnd = CreateWindowExW(0, wc.lpszClassName, L"Per-monitor scale probe (no LINE)",
                          WS_OVERLAPPEDWINDOW, 100, 100, 800, 600, NULL, NULL, wc.hInstance, NULL);
    if (!hwnd) return 5;
    child = CreateWindowExW(0, wc.lpszClassName, L"DPI child", WS_CHILD | WS_VISIBLE,
                           20, 60, 100, 40, hwnd, (HMENU)1, wc.hInstance, NULL);
    grandchild = CreateWindowExW(0, wc.lpszClassName, L"DPI grandchild", WS_CHILD | WS_VISIBLE,
                                5, 5, 50, 20, child, (HMENU)2, wc.hInstance, NULL);
    fprintf(events, "context\t%u\t%u\t%u\t%lu\n",
            AreDpiAwarenessContextsEqual(GetWindowDpiAwarenessContext(hwnd), DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2),
            GetDpiForWindow(child), GetDpiForWindow(grandchild), GetCurrentThreadId());
    ShowWindow(hwnd, SW_SHOW); record(hwnd, "shown");
    SetTimer(hwnd, 1, 500, NULL); SetTimer(hwnd, 2, argc > 2 ? strtoul(argv[2], NULL, 10) : 32000, NULL);
    while (GetMessageW(&message, NULL, 0, 0) > 0)
    { TranslateMessage(&message); DispatchMessageW(&message); }
    fprintf(events, "probe-exit\t0\n");
    fclose(events); return 0;
}
