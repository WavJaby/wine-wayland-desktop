/* SPDX-License-Identifier: LGPL-2.1-or-later
 * Copyright (c) 2026 the wine-wayland-desktop contributors */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
static FILE *logfile;
static int stage;
static BOOL raw_style, external;
static void record(HWND hwnd, const char *event, UINT message, WPARAM wp)
{
    RECT w,c; POINT origin={0,0};
    GetWindowRect(hwnd,&w);GetClientRect(hwnd,&c);ClientToScreen(hwnd,&origin);
    fprintf(logfile,"%lu\t%d\t%s\t%04x\t%llu\t%08lx\t%d\t%u\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\n",
            GetTickCount(),stage,event,message,(unsigned long long)wp,
            GetWindowLongW(hwnd,GWL_STYLE),IsZoomed(hwnd),GetDpiForWindow(hwnd),
            w.left,w.top,w.right,w.bottom,c.right,c.bottom,origin.x,origin.y);
    fflush(logfile);
}
static LRESULT CALLBACK procedure(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp)
{
    LRESULT result;
    if (msg==WM_NCCALCSIZE && wp) {
        NCCALCSIZE_PARAMS *p=(void*)lp;
        record(hwnd,"nccalc-enter",msg,wp);
        fprintf(logfile,"nccalc-input\t%ld\t%ld\t%ld\t%ld\n",p->rgrc[0].left,p->rgrc[0].top,p->rgrc[0].right,p->rgrc[0].bottom);
        /* A controlled frameless-window model: a maximized client fills work area. */
        if (IsZoomed(hwnd)) {
            MONITORINFO mi={.cbSize=sizeof(mi)};
            GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&mi);
            p->rgrc[0]=mi.rcWork;
        }
        fprintf(logfile,"nccalc-output\t%ld\t%ld\t%ld\t%ld\n",p->rgrc[0].left,p->rgrc[0].top,p->rgrc[0].right,p->rgrc[0].bottom);fflush(logfile);
        return 0;
    }
    if (msg==WM_STYLECHANGING || msg==WM_STYLECHANGED) {
        STYLESTRUCT *s=(void*)lp;
        record(hwnd,msg==WM_STYLECHANGED?"style-changed":"style-changing",msg,wp);
        fprintf(logfile,"style-values\t%08lx\t%08lx\n",s->styleOld,s->styleNew);fflush(logfile);
        if (msg==WM_STYLECHANGED && wp==(WPARAM)GWL_STYLE && ((s->styleOld^s->styleNew)&WS_MAXIMIZE))
            SetWindowPos(hwnd,NULL,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
    }
    if (msg==WM_WINDOWPOSCHANGING || msg==WM_WINDOWPOSCHANGED || msg==WM_SIZE || msg==WM_SYSCOMMAND)
        record(hwnd,"message",msg,wp);
    if (msg==WM_PAINT) {
        PAINTSTRUCT paint;RECT c,r;HBRUSH brush;HDC dc=BeginPaint(hwnd,&paint);
        GetClientRect(hwnd,&c);brush=CreateSolidBrush(RGB(32,32,32));FillRect(dc,&c,brush);DeleteObject(brush);
        COLORREF colors[4]={RGB(255,0,0),RGB(0,255,0),RGB(0,0,255),RGB(255,255,0)};
        for(int i=0;i<4;i++) {
            int x=(i&1)?c.right-60:20,y=(i&2)?c.bottom-60:20;
            SetRect(&r,x,y,x+40,y+40);brush=CreateSolidBrush(colors[i]);FillRect(dc,&r,brush);DeleteObject(brush);
        }
        EndPaint(hwnd,&paint);return 0;
    }
    if (msg==WM_TIMER) {
        record(hwnd,"timer",msg,wp);
        if (wp==1) {
            stage=1;
            if (external) { /* The nested compositor drives this transition. */ }
            else if (raw_style)SetWindowLongPtrW(hwnd,GWL_STYLE,GetWindowLongPtrW(hwnd,GWL_STYLE)|WS_MAXIMIZE);
            else ShowWindow(hwnd,SW_MAXIMIZE);
        } else if (wp==2) {
            stage=2;
            if (external) { /* The nested compositor drives this transition. */ }
            else if (raw_style)SetWindowLongPtrW(hwnd,GWL_STYLE,GetWindowLongPtrW(hwnd,GWL_STYLE)&~WS_MAXIMIZE);
            else ShowWindow(hwnd,SW_RESTORE);
        } else if (wp==3) {stage=3;record(hwnd,"final",msg,wp);DestroyWindow(hwnd);}
        KillTimer(hwnd,wp);return 0;
    }
    if (msg==WM_DESTROY) {PostQuitMessage(0);return 0;}
    result=DefWindowProcW(hwnd,msg,wp,lp);
    return result;
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE previous,LPWSTR command,int show)
{
    WCHAR path[MAX_PATH],*dot;WNDCLASSW wc={0};HWND hwnd;MSG msg;
    (void)previous;(void)show;
    if (!GetModuleFileNameW(NULL,path,MAX_PATH) || !(dot=wcsrchr(path,L'.')))return 1;
    wcscpy(dot,L".tsv");if(!(logfile=_wfopen(path,L"w")))return 2;
    raw_style=wcsstr(command,L"--raw-style")!=NULL;
    external=wcsstr(command,L"--external")!=NULL;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    fprintf(logfile,"metadata\traw_style=%d\tsystem_dpi=%u\tscreen=%dx%d\n",raw_style,GetDpiForSystem(),GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN));
    fprintf(logfile,"tick\tstage\tevent\tmessage\twparam\tstyle\tzoomed\tdpi\tleft\ttop\tright\tbottom\tclient_width\tclient_height\tclient_left\tclient_top\n");
    wc.lpfnWndProc=procedure;wc.hInstance=instance;wc.lpszClassName=L"StateHandoffProbe";wc.style=CS_HREDRAW|CS_VREDRAW;
    if(!RegisterClassW(&wc))return 3;
    hwnd=CreateWindowExW(0,wc.lpszClassName,L"State handoff probe (no LINE)",WS_OVERLAPPEDWINDOW,100,100,1301,995,NULL,NULL,instance,NULL);
    if(!hwnd)return 4;
    ShowWindow(hwnd,SW_SHOWNORMAL);record(hwnd,"shown",0,0);
    SetTimer(hwnd,1,4000,NULL);SetTimer(hwnd,2,8000,NULL);SetTimer(hwnd,3,14000,NULL);
    while(GetMessageW(&msg,NULL,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    fclose(logfile);return 0;
}
