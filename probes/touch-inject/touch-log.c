/* Log everything a window is told about touch input, for a field-by-field Windows vs Wine comparison.
 * Usage: touch-log.exe <log> <handle|def> <ms> [x y w h]
 *   handle: consume WM_POINTER* like a touch-aware toolkit (return 0, SkipPointerFrameMessages)
 *   def:    pass everything to DefWindowProc (what the system does for an unaware window: gestures, mouse promotion)
 * Build: x86_64-w64-mingw32-gcc -O2 -mwindows -o touch-log.exe touch-log.c */
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *out;
static BOOL handle_pointer;
static LARGE_INTEGER freq, start;

static double now_ms(void)
{
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (c.QuadPart - start.QuadPart) * 1000.0 / freq.QuadPart;
}

static const char *msg_name(UINT msg)
{
    switch (msg)
    {
#define X(m) case m: return #m;
    X(WM_POINTERUPDATE) X(WM_POINTERDOWN) X(WM_POINTERUP) X(WM_POINTERENTER) X(WM_POINTERLEAVE) X(WM_POINTERACTIVATE)
    X(WM_POINTERCAPTURECHANGED) X(WM_POINTERWHEEL) X(WM_POINTERHWHEEL) X(WM_NCPOINTERUPDATE) X(WM_NCPOINTERDOWN) X(WM_NCPOINTERUP)
    X(WM_TOUCH) X(WM_GESTURE) X(WM_GESTURENOTIFY) X(WM_TOUCHHITTESTING)
    X(WM_MOUSEMOVE) X(WM_LBUTTONDOWN) X(WM_LBUTTONUP) X(WM_LBUTTONDBLCLK) X(WM_RBUTTONDOWN) X(WM_RBUTTONUP)
    X(WM_MOUSEWHEEL) X(WM_MOUSEHWHEEL) X(WM_CONTEXTMENU) X(WM_VSCROLL) X(WM_HSCROLL) X(WM_MOUSEACTIVATE) X(WM_MOUSELEAVE)
    X(WM_SETCURSOR) X(WM_NCHITTEST) X(WM_CAPTURECHANGED) X(WM_ACTIVATE) X(WM_SETFOCUS) X(WM_KILLFOCUS)
#undef X
    }
    return NULL;
}

static void log_touch_info(const char *what, const POINTER_TOUCH_INFO *t)
{
    const POINTER_INFO *p = &t->pointerInfo;
    fprintf(out, "    %s: type %u id %u frame %u flags %#x src %p target %p pix (%ld,%ld) himetric (%ld,%ld) pixraw (%ld,%ld) himraw (%ld,%ld) time %lu hist %u"
            " inputdata %d keystates %#lx perf %s button %d | touchFlags %#x mask %#x contact (%ld,%ld)-(%ld,%ld) raw (%ld,%ld)-(%ld,%ld) orient %u pressure %u\n",
            what, (unsigned)p->pointerType, p->pointerId, p->frameId, (unsigned)p->pointerFlags, (void *)p->sourceDevice, (void *)p->hwndTarget,
            p->ptPixelLocation.x, p->ptPixelLocation.y, p->ptHimetricLocation.x, p->ptHimetricLocation.y,
            p->ptPixelLocationRaw.x, p->ptPixelLocationRaw.y, p->ptHimetricLocationRaw.x, p->ptHimetricLocationRaw.y,
            p->dwTime, p->historyCount, p->InputData, p->dwKeyStates, p->PerformanceCount ? "set" : "0", (int)p->ButtonChangeType,
            (unsigned)t->touchFlags, (unsigned)t->touchMask, t->rcContact.left, t->rcContact.top, t->rcContact.right, t->rcContact.bottom,
            t->rcContactRaw.left, t->rcContactRaw.top, t->rcContactRaw.right, t->rcContactRaw.bottom, t->orientation, t->pressure);
}

static void log_pointer(HWND hwnd, UINT msg, WPARAM wp)
{
    UINT32 id = GET_POINTERID_WPARAM(wp), count = 0, entries = 0, pointers = 0;
    POINTER_INPUT_TYPE type = 0;
    POINTER_TOUCH_INFO info[8], single;
    POINTER_INFO pinfo;
    RECT dev, disp;
    BOOL ret;

    ret = GetPointerType(id, &type);
    fprintf(out, "    GetPointerType %d type %u err %lu\n", ret, (unsigned)type, ret ? 0 : GetLastError());
    ret = GetPointerFrameTouchInfo(id, &count, NULL);
    fprintf(out, "    GetPointerFrameTouchInfo(NULL) %d count %u err %lu\n", ret, count, ret ? 0 : GetLastError());
    count = 8; memset(info, 0, sizeof(info));
    if ((ret = GetPointerFrameTouchInfo(id, &count, info)))
    {
        UINT32 i;
        for (i = 0; i < count && i < 8; i++) log_touch_info("frame", &info[i]);
        ret = GetPointerDeviceRects(info[0].pointerInfo.sourceDevice, &dev, &disp);
        fprintf(out, "    GetPointerDeviceRects %d dev (%ld,%ld)-(%ld,%ld) disp (%ld,%ld)-(%ld,%ld)\n", ret, dev.left, dev.top, dev.right, dev.bottom,
                disp.left, disp.top, disp.right, disp.bottom);
    }
    else fprintf(out, "    GetPointerFrameTouchInfo failed err %lu\n", GetLastError());
    memset(&single, 0, sizeof(single));
    if (GetPointerTouchInfo(id, &single)) log_touch_info("single", &single);
    else fprintf(out, "    GetPointerTouchInfo failed err %lu\n", GetLastError());
    memset(&pinfo, 0, sizeof(pinfo));
    ret = GetPointerInfo(id, &pinfo);
    fprintf(out, "    GetPointerInfo %d flags %#x\n", ret, (unsigned)pinfo.pointerFlags);
    entries = 8; pointers = 1; memset(info, 0, sizeof(info));
    ret = GetPointerFrameTouchInfoHistory(id, &entries, &pointers, info);
    fprintf(out, "    GetPointerFrameTouchInfoHistory %d entries %u pointers %u err %lu\n", ret, entries, pointers, ret ? 0 : GetLastError());
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    const char *name = msg_name(msg);
    LRESULT ret;

    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    if (msg == WM_PAINT) { PAINTSTRUCT ps; HDC dc = BeginPaint(hwnd, &ps); FillRect(dc, &ps.rcPaint, (HBRUSH)GetStockObject(GRAY_BRUSH)); EndPaint(hwnd, &ps); return 0; }
    if (name && msg != WM_NCHITTEST && msg != WM_SETCURSOR)
        fprintf(out, "%9.1f %s wp %#llx lp %#llx extra %#llx msgtime %ld\n", now_ms(), name, (unsigned long long)wp, (unsigned long long)lp,
                (unsigned long long)GetMessageExtraInfo(), GetMessageTime());

    if (msg >= WM_NCPOINTERUPDATE && msg <= WM_POINTERHWHEEL)
    {
        log_pointer(hwnd, msg, wp);
        if (handle_pointer && msg >= WM_POINTERUPDATE && msg <= WM_POINTERUP)
        {
            BOOL skipped = SkipPointerFrameMessages(GET_POINTERID_WPARAM(wp));
            fprintf(out, "    handled, SkipPointerFrameMessages %d\n", skipped);
            return 0;
        }
    }
    if (msg == WM_GESTURE)
    {
        GESTUREINFO gi = { sizeof(gi) };
        if (GetGestureInfo((HGESTUREINFO)lp, &gi))
            fprintf(out, "    gesture id %lu flags %#lx at (%d,%d) args %#llx\n", gi.dwID, gi.dwFlags, gi.ptsLocation.x, gi.ptsLocation.y, (unsigned long long)gi.ullArguments);
    }
    ret = DefWindowProcW(hwnd, msg, wp, lp);
    if (name && msg >= WM_NCPOINTERUPDATE && msg <= WM_POINTERHWHEEL) fprintf(out, "    DefWindowProc -> %lld\n", (long long)ret);
    return ret;
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show)
{
    WNDCLASSW wc = {0};
    int x = 100, y = 100, w = 600, h = 600, duration;
    UINT32 device_count = 0;
    DWORD end;
    HWND hwnd;
    MSG msg;

    if (__argc < 4) return 2;
    if (!(out = fopen(__argv[1], "w"))) return 3;
    handle_pointer = !strcmp(__argv[2], "handle");
    duration = atoi(__argv[3]);
    if (__argc >= 8) { x = atoi(__argv[4]); y = atoi(__argv[5]); w = atoi(__argv[6]); h = atoi(__argv[7]); }
    SetProcessDPIAware();
    QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&start);

    wc.lpfnWndProc = wndproc; wc.hInstance = inst; wc.lpszClassName = L"TouchLog"; wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    RegisterClassW(&wc);
    hwnd = CreateWindowExW(WS_EX_TOPMOST, L"TouchLog", L"touch-log", WS_POPUP | WS_VISIBLE, x, y, w, h, NULL, NULL, inst, NULL);
    SetForegroundWindow(hwnd);

    GetPointerDevices(&device_count, NULL);
    fprintf(out, "mode %s digitizer %#x maxtouches %d pointerdevices %u mouseinpointer %d istouchwindow %d\n", __argv[2], GetSystemMetrics(SM_DIGITIZER),
            GetSystemMetrics(SM_MAXIMUMTOUCHES), device_count, IsMouseInPointerEnabled(), IsTouchWindow(hwnd, NULL));

    end = GetTickCount() + duration;
    while (GetTickCount() < end)
    {
        if (MsgWaitForMultipleObjects(0, NULL, FALSE, 50, QS_ALLINPUT) == WAIT_TIMEOUT) continue;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        fflush(out);
    }
    device_count = 0; GetPointerDevices(&device_count, NULL);
    fprintf(out, "end digitizer %#x maxtouches %d pointerdevices %u\n", GetSystemMetrics(SM_DIGITIZER), GetSystemMetrics(SM_MAXIMUMTOUCHES), device_count);
    fclose(out);
    return 0;
}
