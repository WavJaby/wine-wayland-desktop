/* Virtual touchscreen + keyboard through /dev/uinput: events take the real path (kernel -> libinput -> compositor -> client).
 * Usage: uinput-inject <width> <height> <cmd>...   coordinates in output pixels (the touchscreen maps onto one output)
 *   tap <x> <y>
 *   hold <x> <y> <ms>
 *   swipe <x> <y1> <y2> <ms>      vertical drag, one frame per 10 ms
 *   wait <ms>
 *   type-file <path>              type the file's text (US layout, printable ASCII; trailing newline ignored), then nothing else
 *   key <linux-keycode>           press and release one key
 * Build: gcc -O2 -o uinput-inject uinput-inject.c */
#include <fcntl.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static void emit(int fd, int type, int code, int value)
{
    struct input_event ev = {0};
    ev.type = type; ev.code = code; ev.value = value;
    if (write(fd, &ev, sizeof(ev)) != sizeof(ev)) perror("write");
}

static int open_touch(int width, int height)
{
    struct uinput_setup setup = { .id = { BUS_VIRTUAL, 0x1234, 0x5678, 1 }, .name = "probe virtual touchscreen" };
    struct uinput_abs_setup abs = {0};
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) { perror("/dev/uinput"); exit(1); }
    ioctl(fd, UI_SET_EVBIT, EV_KEY); ioctl(fd, UI_SET_KEYBIT, BTN_TOUCH);
    ioctl(fd, UI_SET_EVBIT, EV_ABS);
    ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_DIRECT);
#define ABS(c, max, res) abs.code = c; abs.absinfo.minimum = 0; abs.absinfo.maximum = max; abs.absinfo.resolution = res; ioctl(fd, UI_ABS_SETUP, &abs)
    ABS(ABS_X, width - 1, 10); ABS(ABS_Y, height - 1, 10);
    ABS(ABS_MT_SLOT, 0, 0); ABS(ABS_MT_TRACKING_ID, 65535, 0);
    ABS(ABS_MT_POSITION_X, width - 1, 10); ABS(ABS_MT_POSITION_Y, height - 1, 10);
    ioctl(fd, UI_DEV_SETUP, &setup); ioctl(fd, UI_DEV_CREATE);
    return fd;
}

static int open_keyboard(void)
{
    struct uinput_setup setup = { .id = { BUS_VIRTUAL, 0x1234, 0x5679, 1 }, .name = "probe virtual keyboard" };
    int i, fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) { perror("/dev/uinput"); exit(1); }
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    for (i = 1; i < 128; i++) ioctl(fd, UI_SET_KEYBIT, i);
    ioctl(fd, UI_DEV_SETUP, &setup); ioctl(fd, UI_DEV_CREATE);
    return fd;
}

static void contact(int fd, int x, int y1, int y2, int ms)
{
    static int tracking_id;
    int t;
    emit(fd, EV_ABS, ABS_MT_SLOT, 0); emit(fd, EV_ABS, ABS_MT_TRACKING_ID, ++tracking_id);
    emit(fd, EV_ABS, ABS_MT_POSITION_X, x); emit(fd, EV_ABS, ABS_MT_POSITION_Y, y1);
    emit(fd, EV_KEY, BTN_TOUCH, 1); emit(fd, EV_ABS, ABS_X, x); emit(fd, EV_ABS, ABS_Y, y1); emit(fd, EV_SYN, SYN_REPORT, 0);
    for (t = 10; t <= ms; t += 10)
    {
        int y = y1 + (y2 - y1) * t / (ms ? ms : 1);
        usleep(10000);
        if (y1 == y2) continue;
        emit(fd, EV_ABS, ABS_MT_POSITION_Y, y); emit(fd, EV_ABS, ABS_Y, y); emit(fd, EV_SYN, SYN_REPORT, 0);
    }
    emit(fd, EV_ABS, ABS_MT_TRACKING_ID, -1); emit(fd, EV_KEY, BTN_TOUCH, 0); emit(fd, EV_SYN, SYN_REPORT, 0);
    printf("contact x=%d y=%d->%d %d ms\n", x, y1, y2, ms);
}

static void key(int fd, int code, int shift)
{
    if (shift) { emit(fd, EV_KEY, KEY_LEFTSHIFT, 1); emit(fd, EV_SYN, SYN_REPORT, 0); usleep(8000); }
    emit(fd, EV_KEY, code, 1); emit(fd, EV_SYN, SYN_REPORT, 0); usleep(12000);
    emit(fd, EV_KEY, code, 0); emit(fd, EV_SYN, SYN_REPORT, 0); usleep(12000);
    if (shift) { emit(fd, EV_KEY, KEY_LEFTSHIFT, 0); emit(fd, EV_SYN, SYN_REPORT, 0); usleep(8000); }
}

/* US layout: index in each string = position on the row, plain then shifted */
static int type_char(int fd, char c)
{
    static const struct { const char *plain, *shifted; int first; } rows[] =
    {
        { "1234567890-=", "!@#$%^&*()_+", KEY_1 },
        { "qwertyuiop[]", "QWERTYUIOP{}", KEY_Q },
        { "asdfghjkl;'", "ASDFGHJKL:\"", KEY_A },
        { "zxcvbnm,./", "ZXCVBNM<>?", KEY_Z },
    };
    const char *p;
    unsigned int i;
    if (c == ' ') { key(fd, KEY_SPACE, 0); return 1; }
    if (c == '`' || c == '~') { key(fd, KEY_GRAVE, c == '~'); return 1; }
    if (c == '\\' || c == '|') { key(fd, KEY_BACKSLASH, c == '|'); return 1; }
    for (i = 0; i < sizeof(rows) / sizeof(rows[0]); i++)
    {
        if ((p = strchr(rows[i].plain, c))) { key(fd, rows[i].first + (p - rows[i].plain), 0); return 1; }
        if ((p = strchr(rows[i].shifted, c))) { key(fd, rows[i].first + (p - rows[i].shifted), 1); return 1; }
    }
    return 0;
}

int main(int argc, char **argv)
{
    int i, touch = -1, kbd = -1, width, height;
    if (argc < 4) return 2;
    width = atoi(argv[1]); height = atoi(argv[2]);
    for (i = 3; i < argc; i++)
    {
        if (!strcmp(argv[i], "type-file") || !strcmp(argv[i], "key")) { if (kbd < 0) kbd = open_keyboard(); }
        else if (strcmp(argv[i], "wait") && touch < 0) touch = open_touch(width, height);
    }
    sleep(2); /* libinput and the compositor need to pick the devices up */

    for (i = 3; i < argc; i++)
    {
        if (!strcmp(argv[i], "wait") && i + 1 < argc) { usleep(atoi(argv[i + 1]) * 1000); i += 1; }
        else if (!strcmp(argv[i], "tap") && i + 2 < argc) { contact(touch, atoi(argv[i + 1]), atoi(argv[i + 2]), atoi(argv[i + 2]), 60); i += 2; }
        else if (!strcmp(argv[i], "hold") && i + 3 < argc) { contact(touch, atoi(argv[i + 1]), atoi(argv[i + 2]), atoi(argv[i + 2]), atoi(argv[i + 3])); i += 3; }
        else if (!strcmp(argv[i], "swipe") && i + 4 < argc) { contact(touch, atoi(argv[i + 1]), atoi(argv[i + 2]), atoi(argv[i + 3]), atoi(argv[i + 4])); i += 4; }
        else if (!strcmp(argv[i], "key") && i + 1 < argc) { key(kbd, atoi(argv[i + 1]), 0); i += 1; }
        else if (!strcmp(argv[i], "type-file") && i + 1 < argc)
        {
            char buf[512]; size_t n, j, typed = 0; FILE *f = fopen(argv[i + 1], "r");
            if (!f) { perror(argv[i + 1]); break; }
            n = fread(buf, 1, sizeof(buf), f); fclose(f);
            while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) n--;
            for (j = 0; j < n; j++) typed += type_char(kbd, buf[j]);
            memset(buf, 0, sizeof(buf));
            printf("typed %zu of %zu characters\n", typed, n);
            i += 1;
        }
        else { printf("bad command %s\n", argv[i]); break; }
        fflush(stdout);
    }
    usleep(300000);
    if (touch >= 0) { ioctl(touch, UI_DEV_DESTROY); close(touch); }
    if (kbd >= 0) { ioctl(kbd, UI_DEV_DESTROY); close(kbd); }
    return 0;
}
