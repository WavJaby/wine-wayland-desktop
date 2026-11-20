/* Broker gate probe: prints its Win32 pid, sleeps argv[1] seconds, exits with argv[2]. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv)
{
    int secs = argc > 1 ? atoi(argv[1]) : 0, code = argc > 2 ? atoi(argv[2]) : 0;
    printf("nsprobe winpid=%lu sleep=%d exit=%d\n", GetCurrentProcessId(), secs, code);
    fflush(stdout);
    Sleep(secs * 1000);
    printf("nsprobe done winpid=%lu\n", GetCurrentProcessId());
    return code;
}
