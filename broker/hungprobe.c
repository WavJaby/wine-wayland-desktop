/* SPDX-License-Identifier: LGPL-2.1-or-later
 * Copyright (c) 2026 the wine-wayland-desktop contributors */
/* Lists top-level "Notepad" class windows; WM_NULL with a 3 s timeout decides responsive vs hung. */
#include <windows.h>
#include <stdio.h>
static BOOL CALLBACK cb(HWND h, LPARAM lp)
{
    char cls[64]; DWORD pid; DWORD_PTR r;
    GetClassNameA(h, cls, sizeof(cls));
    if (strcmp(cls, "Notepad")) return TRUE;
    GetWindowThreadProcessId(h, &pid);
    BOOL ok = SendMessageTimeoutW(h, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 3000, &r) != 0;
    printf("notepad hwnd=%p pid=%04lx visible=%d %s\n", h, pid, IsWindowVisible(h), ok ? "responsive" : "HUNG");
    (*(int *)lp)++;
    return TRUE;
}
int main(void)
{
    int n = 0;
    EnumWindows(cb, (LPARAM)&n);
    printf("notepad windows=%d\n", n);
    return 0;
}
