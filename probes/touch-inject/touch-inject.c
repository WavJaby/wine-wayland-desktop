/* Inject real touch input into the largest visible window of a process (Windows 8+ InjectTouchInput), and screenshot it.
 * Must run in the interactive session (schtasks /IT), not from an ssh service session.
 * Usage: touch-inject.exe <exe-name> <log> <cmd>...   coordinates are fractions of the window rect
 *   shot <file.bmp>              save the window's screen area
 *   tap <fx> <fy>
 *   hold <fx> <fy> <ms>
 *   swipe <fx> <fy1> <fy2> <ms>  vertical drag, linear, one frame per 10 ms
 *   wait <ms>
 * A single <cmd> of the form @file reads the commands from that file (a scheduled task's command line is length-limited,
 * and a .cmd wrapper opens a console window on top of the target).
 * Build: x86_64-w64-mingw32-gcc -O2 -o touch-inject.exe touch-inject.c -lgdi32 */
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *out;
static DWORD target_pid[64];
static int target_pid_count;
static HWND best;
static LONG best_area;

static BOOL CALLBACK enum_proc(HWND hwnd, LPARAM lp)
{
    DWORD pid; RECT r; int i;
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) return TRUE;
    GetWindowThreadProcessId(hwnd, &pid);
    for (i = 0; i < target_pid_count; i++) if (target_pid[i] == pid) break;
    if (i == target_pid_count) return TRUE;
    GetWindowRect(hwnd, &r);
    if ((r.right - r.left) * (r.bottom - r.top) > best_area) { best_area = (r.right - r.left) * (r.bottom - r.top); best = hwnd; }
    return TRUE;
}

static void find_pids(const char *exe)
{
    PROCESSENTRY32 pe = { sizeof(pe) };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (Process32First(snap, &pe)) do
        if (!_stricmp(pe.szExeFile, exe) && target_pid_count < 64) target_pid[target_pid_count++] = pe.th32ProcessID;
    while (Process32Next(snap, &pe));
    CloseHandle(snap);
}

static void shot(const RECT *r, const char *file)
{
    int w = r->right - r->left, h = r->bottom - r->top;
    HDC screen = GetDC(NULL), mem = CreateCompatibleDC(screen);
    BITMAPINFO bi = { { sizeof(BITMAPINFOHEADER), w, -h, 1, 24, BI_RGB } };
    DWORD stride = (w * 3 + 3) & ~3, size = stride * h;
    void *bits;
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    BITMAPFILEHEADER fh = { 0x4d42, sizeof(fh) + sizeof(BITMAPINFOHEADER) + size, 0, 0, sizeof(fh) + sizeof(BITMAPINFOHEADER) };
    FILE *f;
    SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, w, h, screen, r->left, r->top, SRCCOPY);
    GdiFlush();
    if ((f = fopen(file, "wb")))
    {
        fwrite(&fh, sizeof(fh), 1, f); fwrite(&bi.bmiHeader, sizeof(BITMAPINFOHEADER), 1, f); fwrite(bits, size, 1, f);
        fclose(f);
    }
    fprintf(out, "shot %s %dx%d %s\n", file, w, h, f ? "ok" : "FAILED");
    DeleteObject(bmp); DeleteDC(mem); ReleaseDC(NULL, screen);
}

static BOOL touch(POINTER_FLAGS flags, int x, int y)
{
    POINTER_TOUCH_INFO info = {0};
    info.pointerInfo.pointerType = PT_TOUCH;
    info.pointerInfo.pointerId = 0;
    info.pointerInfo.ptPixelLocation.x = x;
    info.pointerInfo.ptPixelLocation.y = y;
    info.pointerInfo.pointerFlags = flags;
    info.touchFlags = TOUCH_FLAG_NONE;
    info.touchMask = TOUCH_MASK_CONTACTAREA | TOUCH_MASK_ORIENTATION | TOUCH_MASK_PRESSURE;
    info.orientation = 90;
    info.pressure = 32000;
    SetRect(&info.rcContact, x - 2, y - 2, x + 2, y + 2);
    if (InjectTouchInput(1, &info)) return TRUE;
    fprintf(out, "InjectTouchInput flags %#x failed: %lu\n", (unsigned)flags, GetLastError());
    return FALSE;
}

#define DOWN (POINTER_FLAG_DOWN | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT)
#define MOVE (POINTER_FLAG_UPDATE | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT)

/* contact from (x,y1) to (x,y2) over ms; y1 == y2 is a hold. An idle contact is cancelled by the system, so keep sending frames. */
static void drag(int x, int y1, int y2, int ms)
{
    int t;
    if (!touch(DOWN, x, y1)) return;
    for (t = 10; t <= ms; t += 10) { Sleep(10); touch(MOVE, x, y1 + (y2 - y1) * t / (ms ? ms : 1)); }
    touch(POINTER_FLAG_UP, x, y2);
    fprintf(out, "contact x=%d y=%d->%d %d ms\n", x, y1, y2, ms);
}

int main(int argc, char **argv)
{
    RECT r; int i; char cls[128], title[128];
    static char buf[4096]; static char *file_argv[256];
    if (argc < 4) return 2;
    if (argv[3][0] == '@')
    {
        FILE *f = fopen(argv[3] + 1, "r"); char *tok; size_t n;
        if (!f) return 4;
        n = fread(buf, 1, sizeof(buf) - 1, f); buf[n] = 0; fclose(f);
        file_argv[0] = argv[0]; file_argv[1] = argv[1]; file_argv[2] = argv[2]; argc = 3;
        for (tok = strtok(buf, " \r\n"); tok && argc < 256; tok = strtok(NULL, " \r\n")) file_argv[argc++] = tok;
        argv = file_argv;
    }
    if (!(out = fopen(argv[2], "w"))) return 3;
    SetProcessDPIAware();
    timeBeginPeriod(1); /* Sleep(10) otherwise lasts ~15 ms and every duration runs 1.5x long */
    find_pids(argv[1]);
    EnumWindows(enum_proc, 0);
    if (!best) { fprintf(out, "no visible window for %s (%d processes)\n", argv[1], target_pid_count); fclose(out); return 1; }
    GetClassNameA(best, cls, sizeof(cls)); GetWindowTextA(best, title, sizeof(title));
    SetForegroundWindow(best);
    Sleep(500);
    GetWindowRect(best, &r);
    fprintf(out, "hwnd %p class %s title \"%s\" rect (%ld,%ld)-(%ld,%ld) foreground %d digitizer %#x maxtouches %d\n", (void *)best, cls, title,
            r.left, r.top, r.right, r.bottom, GetForegroundWindow() == best, GetSystemMetrics(SM_DIGITIZER), GetSystemMetrics(SM_MAXIMUMTOUCHES));
    if (!InitializeTouchInjection(1, TOUCH_FEEDBACK_DEFAULT)) fprintf(out, "InitializeTouchInjection failed: %lu\n", GetLastError());
    fprintf(out, "after init: digitizer %#x maxtouches %d\n", GetSystemMetrics(SM_DIGITIZER), GetSystemMetrics(SM_MAXIMUMTOUCHES));

#define FX(a) (r.left + (int)((r.right - r.left) * atof(a)))
#define FY(a) (r.top + (int)((r.bottom - r.top) * atof(a)))
    for (i = 3; i < argc; i++)
    {
        if (!strcmp(argv[i], "shot") && i + 1 < argc) { shot(&r, argv[i + 1]); i += 1; }
        else if (!strcmp(argv[i], "wait") && i + 1 < argc) { Sleep(atoi(argv[i + 1])); i += 1; }
        else if (!strcmp(argv[i], "tap") && i + 2 < argc) { drag(FX(argv[i + 1]), FY(argv[i + 2]), FY(argv[i + 2]), 60); i += 2; }
        else if (!strcmp(argv[i], "hold") && i + 3 < argc) { drag(FX(argv[i + 1]), FY(argv[i + 2]), FY(argv[i + 2]), atoi(argv[i + 3])); i += 3; }
        else if (!strcmp(argv[i], "swipe") && i + 4 < argc) { drag(FX(argv[i + 1]), FY(argv[i + 2]), FY(argv[i + 3]), atoi(argv[i + 4])); i += 4; }
        else { fprintf(out, "bad command %s\n", argv[i]); break; }
        fflush(out);
    }
    fclose(out);
    return 0;
}
