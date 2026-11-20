/* SPDX-License-Identifier: LGPL-2.1-or-later
 * Copyright (c) 2026 the wine-wayland-desktop contributors */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
static FILE *output;
static HWND main_window;
static BOOL CALLBACK find_main(HWND hwnd, LPARAM unused)
{
    WCHAR title[128];RECT rect;(void)unused;
    GetWindowTextW(hwnd,title,128);GetWindowRect(hwnd,&rect);
    if(IsWindowVisible(hwnd) && !wcscmp(title,L"LINE") && rect.right-rect.left>600) {main_window=hwnd;return FALSE;}
    return TRUE;
}
static LRESULT CALLBACK observe(int code, WPARAM message, LPARAM parameter)
{
    if(code==HC_ACTION) {
        const MSLLHOOKSTRUCT *mouse=(const void*)parameter;
        RECT outer,client;POINT origin={0,0};main_window=NULL;EnumWindows(find_main,0);
        if(main_window) {
            GetWindowRect(main_window,&outer);GetClientRect(main_window,&client);ClientToScreen(main_window,&origin);
            fprintf(output,"%lu\t%04llx\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%08lx\t%d\t%p\n",mouse->time,
                    (unsigned long long)message,mouse->pt.x,mouse->pt.y,mouse->pt.x-origin.x,mouse->pt.y-origin.y,
                    client.right,client.bottom,GetWindowLongW(main_window,GWL_STYLE),
                    PtInRect(&client,(POINT){mouse->pt.x-origin.x,mouse->pt.y-origin.y}),WindowFromPoint(mouse->pt));
            fflush(output);
        }
    }
    return CallNextHookEx(NULL,code,message,parameter);
}
static VOID CALLBACK finish(HWND hwnd,UINT message,UINT_PTR timer,DWORD time)
{(void)hwnd;(void)message;(void)time;KillTimer(NULL,timer);PostQuitMessage(0);}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE previous,LPWSTR command,int show)
{
    WCHAR path[MAX_PATH],*dot;MSG msg;HHOOK hook;(void)previous;(void)show;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if(!GetModuleFileNameW(NULL,path,MAX_PATH) || !(dot=wcsrchr(path,L'.')))return 1;
    wcscpy(dot,L".input.tsv");output=_wfopen(path,L"w");if(!output)return 2;
    fputs("time\tmessage\tscreen_x\tscreen_y\tclient_x\tclient_y\tclient_width\tclient_height\tstyle\tinside_client\twindow_at_point\n",output);fflush(output);
    hook=SetWindowsHookExW(WH_MOUSE_LL,observe,instance,0);
    if(!hook) {fprintf(output,"error\t%lu\n",GetLastError());fclose(output);return 3;}
    unsigned seconds=(unsigned)wcstoul(command,NULL,10);if(seconds<1 || seconds>180)seconds=60;
    SetTimer(NULL,0,seconds*1000,finish);
    while(GetMessageW(&msg,NULL,0,0)>0) {TranslateMessage(&msg);DispatchMessageW(&msg);}
    UnhookWindowsHookEx(hook);fclose(output);return 0;
}
