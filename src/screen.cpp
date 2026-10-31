#include "tr.h"
#include <stdarg.h>

unsigned char far *vram = (unsigned char far *)0xB8000000UL;
int scr_rows = 25;
int scr_inited = 0;
int color_set = 1;
int line_count = 25;

static unsigned char far *saved_screen = 0;
static int saved_rows = 25;
static int cursor_on = 1;
static int cur_x = 0, cur_y = 0;
static unsigned char cur_attr = 0x07;


static unsigned char pal[11][6] = {
    {0},
    {0x1F,0x1E,0x07,0x70,0x1F},
    {0x17,0x17,0x07,0x70,0x17},
    {0x2F,0x2E,0x07,0x70,0x2F},
    {0x3F,0x3E,0x07,0x70,0x3F},
    {0x4F,0x4E,0x07,0x70,0x4F},
    {0x5F,0x5E,0x07,0x70,0x5F},
    {0x6F,0x6E,0x07,0x70,0x6F},
    {0x8F,0x8E,0x07,0x70,0x8F},
    {0x0F,0x0E,0x07,0x70,0x0F},
    {0x70,0x70,0x07,0x0F,0x70}
};

static int pal_index(int kind)
{
    if (color_set < 1 || color_set > 10) color_set = 1;
    return pal[color_set][kind];
}

int scr_get_rows(void) { return scr_rows; }

static void crtc(int reg, int val)
{
    outportb(0x3D4, reg);
    outportb(0x3D5, val);
}

void scr_setlines(int rows)
{
    if (rows != 25 && rows != 50) return;
    if (rows == 50) {
        union REGS r;
        r.h.ah = 0x00; r.h.al = 0x03;
        int86(0x10, &r, &r);
        r.h.ah = 0x11; r.h.al = 0x12; r.h.bl = 0;
        int86(0x10, &r, &r);
        crtc(0x09, 0x07);
        crtc(0x0A, 0x0F);
        crtc(0x0B, 0x00);
        crtc(0x12, 0x31);
        crtc(0x13, 0x28);
        crtc(0x14, 0x00);

        r.h.ah = 0x11; r.h.al = 0x12; r.h.bl = 0;
        int86(0x10, &r, &r);
        crtc(0x09, 0x07);
        scr_rows = 50;
    } else {
        union REGS r;
        r.h.ah = 0x00; r.h.al = 0x03;
        int86(0x10, &r, &r);
        scr_rows = 25;
    }
    line_count = scr_rows;
    vram = (unsigned char far *)0xB8000000UL;
    scr_clear(0x07);
}

void scr_init(void)
{
    saved_rows = scr_rows;
    saved_screen = (unsigned char far *)farmalloc((unsigned long)scr_rows * 80 * 2);
    if (saved_screen) {
        movedata(0xB800, 0, FP_SEG(saved_screen), FP_OFF(saved_screen),
                 (unsigned)scr_rows * 80 * 2);
    }
    setvect(0x1B, getvect(0x1B));
    scr_inited = 1;
    scr_cursor(0);
}

void scr_done(void)
{
    if (saved_screen) {
        movedata(FP_SEG(saved_screen), FP_OFF(saved_screen), 0xB800, 0,
                 (unsigned)saved_rows * 80 * 2);
        farfree(saved_screen);
        saved_screen = 0;
    }
    scr_cursor(1);
}

void scr_save_user(void)
{
    int i;
    unsigned char far *p = vram;
    for (i = 0; i < scr_rows * 80 * 2; i++) p[i] = p[i];
}

void scr_show_user(void)
{
    if (saved_screen) {
        movedata(FP_SEG(saved_screen), FP_OFF(saved_screen), 0xB800, 0,
                 (unsigned)saved_rows * 80 * 2);
    } else {
        scr_clear(0x07);
    }
    scr_puts(0, scr_rows - 1, "Press any key to return to TR", 0x70);
}

void scr_cursor(int on)
{
    union REGS r;
    cursor_on = on;
    r.h.ah = 0x01;
    r.h.ch = on ? 0x06 : 0x20;
    r.h.cl = on ? 0x07 : 0x00;
    int86(0x10, &r, &r);
}

void scr_gotoxy(int x, int y)
{
    union REGS r;
    if (x < 0) x = 0;
    if (x > 79) x = 79;
    if (y < 0) y = 0;
    if (y >= scr_rows) y = scr_rows - 1;
    cur_x = x; cur_y = y;
    r.h.ah = 0x02; r.h.bh = 0;
    r.h.dh = (unsigned char)y; r.h.dl = (unsigned char)x;
    int86(0x10, &r, &r);
}

void scr_putc(int x, int y, int ch, int attr)
{
    unsigned off;
    if (x < 0 || x >= 80 || y < 0 || y >= scr_rows) return;
    off = (unsigned)(y * 80 + x) * 2;
    vram[off] = (unsigned char)ch;
    vram[off + 1] = (unsigned char)attr;
}

void scr_puts(int x, int y, const char *s, int attr)
{
    while (*s && x < 80) {
        scr_putc(x, y, (unsigned char)*s, attr);
        x++; s++;
    }
}

void scr_printf(int x, int y, int attr, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsprintf(buf, fmt, ap);
    va_end(ap);
    scr_puts(x, y, buf, attr);
}

void scr_fill(int x, int y, int w, int h, int ch, int attr)
{
    int i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            scr_putc(x + i, y + j, ch, attr);
}

void scr_clear(int attr)
{
    scr_fill(0, 0, 80, scr_rows, ' ', attr);
}

void scr_box(int x, int y, int w, int h, int attr, const char *title)
{
    int i;
    if (w < 2 || h < 2) return;
    scr_putc(x, y, 0xDA, attr);
    scr_putc(x + w - 1, y, 0xBF, attr);
    scr_putc(x, y + h - 1, 0xC0, attr);
    scr_putc(x + w - 1, y + h - 1, 0xD9, attr);
    for (i = 1; i < w - 1; i++) {
        scr_putc(x + i, y, 0xC4, attr);
        scr_putc(x + i, y + h - 1, 0xC4, attr);
    }
    for (i = 1; i < h - 1; i++) {
        scr_putc(x, y + i, 0xB3, attr);
        scr_putc(x + w - 1, y + i, 0xB3, attr);
    }
    if (title && *title) {
        scr_putc(x + 1, y, ' ', attr);
        scr_puts(x + 2, y, title, pal_index(1));
        scr_putc(x + 2 + strlen(title), y, ' ', attr);
    }
}

int scr_getkey(void)
{
    int k;
    scr_cursor(1);
    k = bioskey(0);
    scr_cursor(0);

    if ((k & 0xFF) != 0) return k & 0xFF;
    return k;
}

void delay_ms(int ms)
{
    long i;
    for (i = 0; i < (long)ms * 300L; i++) ;
}
