#include "tr.h"
#include <stdarg.h>

int hold_view = 0;
int follow_ip = 1;
u16 view_seg = 0;
u16 view_off = 0;
char last_cmd[128] = "";
char msg_line[80] = "";

int zoom_view = 0;
int active_win = 1;
int trace_int1 = 0;
int autojmp = 1;
int autoint1 = 0;
int log_on = 0;
int logs_on = 0;
int log_count = 0;
u32 log_buf[512];

int reg_h = 2;
int mem_y = 2;
int mem_h = 2;
int code_y = 4;
int code_h = 16;
int status_y = 20;
int status2_y = 21;
int prompt_y = 23;
int msg_y = 24;
int mem_lines_user = 0;
int code_base = 0;
u16 code_base_cs = 0xFFFF;

#define TITLE_LINE   "TRR V2.52 from TR by Liu TaoTao. Nov 30 1998"
#define SHARE_LINE   "This is a ShareWare,please register. '?' for Help"
#define WELCOME_LINE " Welcome to use TRR."

static char hist[8][128];
static int nhist = 0;

static void draw_msg_ring(void);

void compute_layout(void)
{
    reg_h = cpu.mode32 ? 3 : 2;
    mem_y = reg_h;
    status_y = scr_rows - 5;
    status2_y = scr_rows - 4;
    prompt_y = scr_rows - 2;
    msg_y = scr_rows - 1;

    if (zoom_view == 1) mem_h = status_y - mem_y - 1;
    else if (zoom_view == 2) mem_h = 1;
    else mem_h = mem_lines_user ? mem_lines_user : 2;
    if (mem_h < 1) mem_h = 1;
    if (mem_h > status_y - mem_y - 2) mem_h = status_y - mem_y - 2;

    code_y = mem_y + mem_h;
    code_h = status_y - code_y;
    if (code_h < 2) code_h = 2;
}

static u32 ui_prev_r[8];
static u16 ui_prev_ds, ui_prev_es, ui_prev_ss, ui_prev_cs, ui_prev_ip;
static u32 ui_prev_flags;
static char msg_ring[3][80];
static int ring_used = 0;

static void ui_snap_core(void)
{
    int i;
    for (i = 0; i < 8; i++) ui_prev_r[i] = cpu.r[i];
    ui_prev_ds = cpu.s[S_DS]; ui_prev_es = cpu.s[S_ES];
    ui_prev_ss = cpu.s[S_SS]; ui_prev_cs = cpu.s[S_CS];
    ui_prev_ip = (u16)cpu.eip;
    ui_prev_flags = cpu.flags;
}

static int ui_first_run = 0;

void ui_snapshot_reset(void)
{
    ui_snap_core();
    ui_first_run = 1;
    ring_used = 0;
    msg_ring[0][0] = msg_ring[1][0] = msg_ring[2][0] = 0;
}

void ui_snap_run(void)
{
    ui_snap_core();
}

void ui_snap_run_g(void)
{
    if (ui_first_run) {
        ui_first_run = 0;
        cpu.flags |= F_PF;
    }
    ui_snap_core();
}

void msg_echo(const char *s)
{
    if (ring_used < 3) { strncpy(msg_ring[ring_used], s, 79); msg_ring[ring_used][79] = 0; ring_used++; }
    else {
        strcpy(msg_ring[0], msg_ring[1]);
        strcpy(msg_ring[1], msg_ring[2]);
        strncpy(msg_ring[2], s, 79); msg_ring[2][79] = 0;
    }
    scr_fill(0, prompt_y, 80, 1, ' ', 0x17);
    draw_msg_ring();
}

static void draw_flags(int x, int y)
{
    static const char letters[9] = {'o','d','i','s','z','a','p','c','t'};
    static const u32 masks[9] = {F_OF,F_DF,F_IF,F_SF,F_ZF,F_AF,F_PF,F_CF,F_TF};
    int i;
    for (i = 0; i < 9; i++) {
        char b[3];
        int on = (cpu.flags & masks[i]) ? 1 : 0;
        int same = ((cpu.flags ^ ui_prev_flags) & masks[i]) ? 0 : 1;
        b[0] = on ? (char)toupper(letters[i]) : letters[i];
        b[1] = 0;
        scr_puts(x + i * 2, y, b, same ? (on ? 0x0B : 0x07) : 0x60);
    }
}


static void reg_field(int x, int y, const char *name, u16 val, u16 prev)
{
    char b[16];
    sprintf(b, "%s=", name);
    scr_puts(x, y, b, 0x07);
    sprintf(b, "%04X", val);
    scr_puts(x + 3, y, b, (val == prev) ? 0x0B : 0x0E);
}

void show_regs(void)
{
    static const char *n8[8] = {"AX","BX","CX","DX","SP","BP","SI","DI"};
    static const int r8[8] = {R_AX,R_BX,R_CX,R_DX,R_SP,R_BP,R_SI,R_DI};
    static const char *n4[4] = {"DS","ES","SS","CS"};
    static const int is[4] = {S_DS, S_ES, S_SS, S_CS};
    u16 sv[4];
    int i;
    if (cpu.mode32) {
        scr_printf(0, 0, 0x07, "EAX=%08lX  EBX=%08lX  ECX=%08lX  EDX=%08lX  SP=%04X",
                   cpu.r[0], cpu.r[3], cpu.r[1], cpu.r[2], (unsigned)(cpu.r[4] & 0xFFFF));
        scr_printf(0, 1, 0x07, "EBP=%08lX  ESI=%08lX  EDI=%08lX  FS=%04X  GS=%04X",
                   cpu.r[5], cpu.r[6], cpu.r[7], cpu.s[S_FS], cpu.s[S_GS]);
        scr_printf(0, 2, 0x07, "DS=%04X  ES=%04X  SS=%04X  CS=%04X  IP=%04X  ",
                   cpu.s[S_DS], cpu.s[S_ES], cpu.s[S_SS], cpu.s[S_CS], (unsigned)(cpu.eip & 0xFFFF));
        draw_flags(45, 2);
    } else {
        for (i = 0; i < 8; i++)
            reg_field(i * 9, 0, n8[i], (u16)cpu.r[r8[i]], (u16)ui_prev_r[r8[i]]);
        sv[0] = cpu.s[S_DS]; sv[1] = cpu.s[S_ES]; sv[2] = cpu.s[S_SS]; sv[3] = cpu.s[S_CS];
        for (i = 0; i < 4; i++)
            reg_field(i * 9, 1, n4[i], sv[i],
                      is[i] == S_DS ? ui_prev_ds : is[i] == S_ES ? ui_prev_es :
                      is[i] == S_SS ? ui_prev_ss : ui_prev_cs);
        reg_field(36, 1, "IP", (u16)cpu.eip, ui_prev_ip);
        scr_puts(43, 1, "  ", 0x07);
        draw_flags(45, 1);
    }
}

void show_mem(void)
{
    int row, i;
    char b[8];
    u16 a;

    for (row = 0; row < mem_h; row++) {
        a = (u16)(view_off + row * 16);
        scr_printf(0, mem_y + row, 0x07, "%04X:%04X  ", view_seg, a);
        for (i = 0; i < 16; i++) {
            int x = 11 + i * 3;
            sprintf(b, "%02X", rd8(view_seg, (u16)(a + i)));
            scr_puts(x, mem_y + row, b, 0x07);
            if (i == 7) scr_putc(34, mem_y + row, '-', 0x07);
        }
        for (i = 0; i < 16; i++) {
            u8 c = rd8(view_seg, (u16)(a + i));
            scr_putc(60 + i, mem_y + row, (c >= 32) ? c : '.', 0x07);
        }
    }
}

void show_code(void)
{
    int row, i;
    char text[128], tmp[24];
    u16 seg, off, a;
    u8 raw[16];
    int rl;
    int use_follow = follow_ip;
    int fresh = 0;

    if (use_follow) {
        seg = cpu.s[S_CS];
        off = (u16)cpu.eip;
        if (code_base_cs != seg) {
            code_base = off;
            code_base_cs = seg;
            fresh = 1;
        }
        {
            u16 p = code_base;
            int j, vis = 0, l2, rl2;
            char t2[128];
            u8 r2[16];
            if (!fresh) {
                for (j = 0; j < code_h; j++) {
                    if (p == off) { vis = 1; break; }
                    l2 = disasm(seg, p, t2, sizeof t2, r2, &rl2);
                    p = (u16)(p + (l2 ? l2 : 1));
                }
            } else if (off == 0) {
                vis = 1;
            }
            if (!vis) {
                u16 b = (u16)(off - 0x10);
                int found = 0;
                for (; ; b = (u16)(b + 1)) {
                    p = b;
                    for (j = 0; j < 5; j++) {
                        l2 = disasm(seg, p, t2, sizeof t2, r2, &rl2);
                        p = (u16)(p + (l2 ? l2 : 1));
                        if (p == off) { found = 1; break; }
                    }
                    if (found) break;
                    if (b == off) break;
                }
                code_base = found ? b : off;
                code_base_cs = seg;
            }
        }
    } else {
        seg = view_seg;
        code_base = view_off;
    }
    a = code_base;
    for (row = 0; row < code_h; row++) {
        int len;
        int cur;
        const char *cm;
        len = disasm(seg, a, text, sizeof text - 8, raw, &rl);
        cur = (use_follow && seg == cpu.s[S_CS] && a == (u16)cpu.eip);
        if (cur) scr_fill(0, code_y + row, 75, 1, ' ', 0x0E);
        scr_printf(0, code_y + row, cur ? 0x0E : 0x07, "%04X:%04X  ", seg, a);
        for (i = 0; i < rl && i < 8; i++) {
            sprintf(tmp, "%02X", raw[i]);
            scr_puts(11 + i * 2, code_y + row, tmp, cur ? 0x0E : 0x07);
        }
        scr_puts(27, code_y + row, text, cur ? 0x0E : 0x07);
        cm = cmt_find(seg, a);
        if (cm) scr_puts(27 + strlen(text) + 2, code_y + row, cm, cur ? 0x02 : 0x07);
        else if (cur && strncmp(text, "INT", 3) == 0) {
            int n = 0;
            char *p = strchr(text, ' ');
            if (p) n = (int)strtol(p + 1, 0, 16);
            if (n == 0x21) {
                const char *nm = int21_name((int)(cpu.r[0] >> 8));
                if (nm) scr_puts(27 + strlen(text) + 1, code_y + row, nm, 0x02);
            } else if (n == 0x10) {
                const char *nm = int10_name((int)(cpu.r[0] >> 8));
                if (nm) scr_puts(27 + strlen(text) + 1, code_y + row, nm, 0x02);
            } else if (n == 0x13) {
                const char *nm = int13_name((int)(cpu.r[0] >> 8));
                if (nm) scr_puts(27 + strlen(text) + 1, code_y + row, nm, 0x02);
            }
        }
        a = (u16)(a + len);
    }
}

void show_status(const char *msg)
{
    if (msg) {
        strncpy(msg_line, msg, 79);
        msg_line[79] = 0;
    }
    scr_fill(0, prompt_y, 80, 1, ' ', 0x17);
    if (msg_line[0]) scr_puts(0, prompt_y, msg_line, 0x17);
}

static void draw_msg_ring(void)
{
    int i;
    for (i = 0; i < 3; i++) {
        scr_fill(0, status_y + i, 80, 1, ' ', 0x17);
        if (i < ring_used) scr_puts(0, status_y + i, msg_ring[i], 0x17);
    }
}

void show_all(void)
{
    union REGS r;
    if (!scr_inited) return;
    if (hold_view) return;
    r.h.ah = 0x0F;
    int86(0x10, &r, &r);
    if ((r.h.al & 0x7F) != 3 && (r.h.al & 0x7F) != 7 && scr_rows == 25) {
        r.h.ah = 0x00; r.h.al = 0x03;
        int86(0x10, &r, &r);
    }
    compute_layout();
    scr_fill(0, 0, 80, scr_rows, ' ', 0x07);
    show_regs();
    show_mem();
    show_code();
    draw_msg_ring();
    scr_fill(0, prompt_y, 80, 1, ' ', 0x17);
    scr_fill(0, msg_y, 80, 1, ' ', 0x76);
    scr_puts(0, msg_y, WELCOME_LINE, 0x76);
}


static void redraw_prompt(const char *buf, int len, int pos)
{
    char tmp[100];
    (void)len;
    scr_fill(0, prompt_y, 80, 1, ' ', 0x17);
    strncpy(tmp, buf, 60);
    tmp[60] = 0;
    scr_puts(0, prompt_y, tmp, 0x17);
    scr_gotoxy(pos, prompt_y);
}

static int read_line(char *out)
{
    char buf[80];
    int len = 0, pos = 0;
    int hindex = nhist;
    int k;

    buf[0] = 0;
    for (;;) {
        redraw_prompt(buf, len, pos);
        k = scr_getkey();
        if (k == 0x1B) { out[0] = 0; return 0; }
        if (k == 0x0D) {
            buf[len] = 0;
            strcpy(out, buf);
            if (len) {
                if (nhist < 8) { strcpy(hist[nhist], buf); nhist++; }
                else {
                    int i;
                    for (i = 1; i < 8; i++) strcpy(hist[i - 1], hist[i]);
                    strcpy(hist[7], buf);
                }
            }
            return 1;
        }
        if (k == 0x08) {
            if (pos > 0) {
                int i;
                for (i = pos - 1; i < len - 1; i++) buf[i] = buf[i + 1];
                len--; pos--; buf[len] = 0;
            }
            continue;
        }
        if (k == 0x4B00) { if (pos > 0) pos--; continue; }
        if (k == 0x4D00) { if (pos < len) pos++; continue; }
        if (k == 0x4700) { pos = 0; continue; }
        if (k == 0x4F00) { pos = len; continue; }
        if (k == 0x4800 && nhist > 0) {
            if (hindex > 0) hindex--;
            strcpy(buf, hist[hindex]); len = strlen(buf); pos = len;
            continue;
        }
        if (k == 0x5000 && nhist > 0) {
            if (hindex < nhist - 1) { hindex++; strcpy(buf, hist[hindex]); }
            else { hindex = nhist; buf[0] = 0; }
            len = strlen(buf); pos = len;
            continue;
        }
        if (k == 0x3E00) { scr_show_user(); while (!scr_getkey()) ; show_all(); continue; }
        if (k == 0x4300) {
            int idx;
            if (follow_ip) idx = bp_add(BP_EXEC, cpu.s[S_CS], (u16)cpu.eip);
            else idx = bp_add(BP_EXEC, view_seg, view_off);
            show_status(idx >= 0 ? "BreakPoint set" : "Too many Break Points");
            show_all();
            continue;
        }
        if (k == 0x4200) { do_command("T"); show_all(); continue; }
        if (k == 0x4400) { do_command("P"); show_all(); continue; }
        if (k == 0x3F00) { zoom_view = (zoom_view + 1) % 3; show_all(); continue; }
        if (k == 0x4000) { active_win = !active_win; continue; }
        if (k == 0x4100) {

            cpu.s[S_CS] = view_seg; cpu.eip = view_off; follow_ip = 1; show_all();
            continue;
        }
        if (k == 0x4900) { view_off = (u16)(view_off - 0x100); follow_ip = 0; show_all(); continue; }
        if (k == 0x5100) { view_off = (u16)(view_off + 0x100); follow_ip = 0; show_all(); continue; }
        if (k == 0x3B00) { help_screen(0); show_all(); continue; }
        if (k == '?' && len == 0) {
            help_screen("?");
            show_all();
            continue;
        }
        if (k >= 32 && k < 127 && len < 60) {
            int i;
            for (i = len; i > pos; i--) buf[i] = buf[i - 1];
            buf[pos] = (char)k;
            len++; pos++; buf[len] = 0;
            continue;
        }
    }
}

void cmd_loop(void)
{
    char line[80];
    for (;;) {
        show_all();
        if (msg_line[0]) scr_puts(0, prompt_y, msg_line, 0x17);
        if (!read_line(line)) { show_status(""); continue; }
        if (!line[0]) continue;
        strncpy(last_cmd, line, 127);
        msg_echo(line);
        if (do_command(line)) break;
    }
}

extern void cmd_init(void);

static void release_extra_memory(u16 keep)
{
    union REGS r;
    struct SREGS sr;
    u16 psp, top;
    r.x.ax = 0x5100;
    int86(0x21, &r, &r);
    psp = r.x.bx;
    top = rd16(psp, 0x02);
    if (top <= (u16)(psp + keep)) return;
    r.x.ax = 0x4A00;
    r.x.bx = keep;
    sr.es = psp;
    int86x(0x21, &r, &r, &sr);
    { FILE *df = fopen("INT.LOG", "a"); if (df) { fprintf(df, "SHRINK psp=%04X top=%04X keep=%u cf=%d\n", psp, top, keep, r.x.cflag ? 1 : 0); fclose(df); } }
}

static void init_cpu_from_psp(void)
{
    union REGS r;
    u16 psp, seg;
    u8 far *p;
    u32 i;

    r.x.ax = 0x5100;
    int86(0x21, &r, &r);
    psp = r.x.bx;


    p = (u8 far *)farmalloc(0x1000UL);
    seg = psp;
    if (p) {
        for (i = 0; i < 0x1000UL; i++) p[i] = 0;
        seg = FP_SEG(p);
        if (FP_OFF(p)) seg++;
        movedata(psp, 0, seg, 0, 0x100);
    }

    memset(&cpu, 0, sizeof cpu);
    cpu.s[S_DS] = seg;
    cpu.s[S_ES] = seg;
    cpu.s[S_SS] = seg;
    cpu.s[S_CS] = seg;
    cpu.eip = 0x100;
    cpu.r[R_SP] = 0xFFFE;
    cpu.r[R_DX] = seg;
    cpu.r[R_SI] = 0x0100;
    cpu.flags = 0x0202;
    code_base = 0x00FC;
    code_base_cs = seg;
}

static void scrub_trcore_env(void)
{
    union REGS rr;
    u16 psp, es;
    u8 far *p;
    unsigned long i, j, term, end, adv, k, cnt;

    rr.x.ax = 0x5100;
    int86(0x21, &rr, &rr);
    psp = rr.x.bx;
    es = *(u16 far *)MK_FP(psp, 0x002C);
    if (!es) return;
    p = (u8 far *)MK_FP(es, 0);
    term = 0;
    while (!(p[term] == 0 && p[term + 1] == 0)) {
        term++;
        if (term > 0x7000UL) return;
    }

    end = term + 2;
    cnt = (unsigned long)p[end] | ((unsigned long)p[end + 1] << 8);
    if (cnt >= 1 && cnt <= 0x20) {
        j = end + 2;
        while (cnt--) {
            while (p[j]) j++;
            j++;
            if (j > 0x7FFFUL) { end = term + 2; break; }
        }
        if (j <= 0x7FFFUL) end = j;
    }
    i = 0;
    while (p[i]) {
        static const char *names[] = { "TRCORE=", "TRHB=", "TRNORET=", "TRNOPF=",
                                       "TRB0P=", "TRB0V=", "TRN4A=", 0 };
        const char *nm;
        int ni = 0, kl, ok;
        int hit = 0;
        adv = 0;
        while (names[ni]) {
            nm = names[ni];
            ok = 1; kl = 0;
            while (nm[kl]) {
                if ((unsigned long)(i + kl) >= term ||
                    p[i + kl] != (u8)nm[kl]) { ok = 0; break; }
                kl++;
            }
            if (ok) { hit = 1; adv = (unsigned long)kl; break; }
            ni++;
        }
        if (hit) {
            while (p[i + adv]) adv++;
            adv++;
            for (k = i; k + adv < end; k++) p[k] = p[k + adv];
            for (; k < end; k++) p[k] = 0;
            end -= adv;
        } else {
            j = i;
            while (p[j]) j++;
            i = j + 1;
        }
    }
}

int main(int argc, char **argv)
{
    int i;
    union REGS r;

    scr_init();
    scr_setlines(25);
    scrub_trcore_env();
    sym_init();
    bp_clear_all();
    init_cpu_from_psp();
    compute_layout();
    cmd_init();
    msg_line[0] = 0;

    if (argc > 1) {
        for (i = 1; i < argc; i++) {
            if (argv[i][0] != '-') {
                if (load_program(argv[i]) != 0) show_status("File Load Error: not enough memory ?");
                break;
            }
        }
    }
    if (!psp_seg) {
        psp_seg = 0;
    }
    view_seg = cpu.s[S_DS];
    view_off = 0;
    if (psp_seg) { follow_ip = 1; view_seg = cpu.s[S_DS]; }
    ui_snapshot_reset();
    batch_run("AUTORUN.TR");
    if (!quit_requested()) cmd_loop();

    cmt_save();
    free_program();
    if (prog_type == 2) {
        extern int notice_overlay;
        if (notice_overlay) printf("file has overlay\n");
        printf("file has no symbol");
    }
    r.h.ah = 0x00; r.h.al = 0x03;
    int86(0x10, &r, &r);
    scr_done();
    return 0;
}
