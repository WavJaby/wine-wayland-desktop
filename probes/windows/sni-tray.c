#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>

#define TRAY_MESSAGE (WM_APP + 1)
static NOTIFYICONDATAW icon;
static FILE *logfile;

static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == TRAY_MESSAGE)
    {
        fprintf(logfile, "callback\t%04x\t%llu\t%llu\n", LOWORD(lparam),
                (unsigned long long)wparam, (unsigned long long)lparam);
        fflush(logfile);
        return 0;
    }
    if (message == WM_TIMER)
    {
        DestroyWindow(hwnd);
        return 0;
    }
    if (message == WM_DESTROY)
    {
        fprintf(logfile, "delete\t%d\n", Shell_NotifyIconW(NIM_DELETE, &icon));
        fflush(logfile);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, LPWSTR command, int show)
{
    WNDCLASSW cls = {.lpfnWndProc = procedure, .hInstance = instance,
                    .lpszClassName = L"WineSniPortProbe"};
    WCHAR path[MAX_PATH], *dot;
    HWND hwnd;
    MSG msg;
    BOOL added;
    (void)previous; (void)command; (void)show;
    if (!GetModuleFileNameW(NULL, path, MAX_PATH) || !(dot = wcsrchr(path, L'.'))) return 1;
    wcscpy(dot, L".tsv");
    if (!(logfile = _wfopen(path, L"w"))) return 2;
    if (!RegisterClassW(&cls)) return 3;
    hwnd = CreateWindowW(cls.lpszClassName, L"SNI port probe", 0, 0, 0, 100, 100,
                         NULL, NULL, instance, NULL);
    if (!hwnd) return 4;
    icon.cbSize = sizeof(icon);
    icon.hWnd = hwnd;
    icon.uID = 1;
    icon.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    icon.uCallbackMessage = TRAY_MESSAGE;
    icon.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    wcscpy(icon.szTip, sizeof(void *) == 8 ? L"Wine 11.18 SNI probe x64" : L"Wine 11.18 SNI probe x86");
    added = Shell_NotifyIconW(NIM_ADD, &icon);
    fprintf(logfile, "add\t%d\tbits\t%zu\n", added, sizeof(void *) * 8);
    icon.uVersion = NOTIFYICON_VERSION_4;
    fprintf(logfile, "version\t%d\n", Shell_NotifyIconW(NIM_SETVERSION, &icon));
    fflush(logfile);
    if (!added) { DestroyWindow(hwnd); fclose(logfile); return 5; }
    SetTimer(hwnd, 1, 15000, NULL);
    while (GetMessageW(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    fclose(logfile);
    return 0;
}
