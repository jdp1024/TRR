#include "tr.h"

static int step_core(void) { return cpu_step_tr(); }

static char *args = "";
static int batch_depth = 0;
static int quit_flag = 0;
static char cmd_name[32];
static int trace_count = 0;

static void c_help(void);
static void c_a(void);
static void c_autoint1(void);
static void c_autojmp(void);
static void c_bc(void);
static void c_bd(void);
static void c_be(void);
static void c_bl(void);
static void c_bp(void);
static void c_bpknl(void);
static void c_bpint(void);
static void c_bpio(void);
static void c_bpw(void);
static void c_bpm(void);
static void c_bpreg(void);
static void c_bpxb(void);
static void c_color(void);
static void c_cmt(void);
static void c_delay(void);
static void c_do(void);
static void c_int1(void);
static void c_move(void);
static void c_fill(void);
static void c_go(void);
static void c_getknl(void);
static void c_gg(void);
static void c_vlog(void);
static void c_stack(void);
static void c_label(void);
static void c_lines(void);
static void c_key(void);
static void c_r(void);
static void c_reload(void);
static void c_redraw(void);
static void c_symbols(void);
static void c_u(void);
static void c_pret(void);
static void c_quit(void);
static void c_p(void);
static void c_pp(void);
static void c_msg(void);
static void c_wd(void);
static void c_r32(void);
static void c_rs(void);
static void c_t(void);
static void c_d(void);
static void c_search(void);
static void c_edit(void);
static void c_load(void);
static void c_asm(void);
static void c_name(void);
static void c_ver(void);
static void c_write(void);
static void c_wcmd(void);
static void c_wcod(void);
static void c_wreg(void);
static void c_wmem(void);
static void c_log(void);
static void c_logs(void);
static void c_logpro(void);
static void c_sym(void);
static void c_mkexe(void);
static void c_view(void);
static void c_internal(void);

struct CmdEnt { const char *name; void (*fn)(void); };

static struct CmdEnt commands[] = {
    {"?",        c_help},
    {"A",        c_asm},
    {"AUTOINT1", c_autoint1},
    {"AUTOJMP",  c_autojmp },
    {"BC",       c_bc},
    {"BD",       c_bd},
    {"BE",       c_be},
    {"BL",       c_bl},
    {"BP",       c_bp},
    {"BPKNL",    c_bpknl},
    {"BPINT",    c_bpint},
    {"BPIO",     c_bpio},
    {"BPW",      c_bpw},
    {"BPM",      c_bpm},
    {"BPREG",    c_bpreg},
    {"BPXB",     c_bpxb},
    {"COLOR",    c_color},
    {"CMT",      c_cmt},
    {"DELAY",    c_delay},
    {"DO",       c_do},
    {"INT1",     c_int1},
    {"M",        c_move},
    {"F",        c_fill},
    {"G",        c_go},
    {"GETKNL",   c_getknl},
    {"GO",       c_go},
    {"GS",       c_getknl},
    {"GG",       c_gg},
    {"VLOG",     c_vlog},
    {"STACK",    c_stack},
    {"LABEL",    c_label},
    {"LINES",    c_lines},
    {"KEY",      c_key},
    {"R",        c_r},
    {"RELOAD",   c_reload},
    {"REDRAW",   c_redraw},
    {"SYMBOLS",  c_symbols},
    {"U",        c_u},
    {"PRET",     c_pret},
    {"Q",        c_quit},
    {"P",        c_p},
    {"PP",       c_pp},
    {"MSG",      c_msg},
    {"WD",       c_wd},
    {"R32",      c_r32},
    {"RS",       c_rs},
    {"T",        c_t},
    {"TT",       c_t},
    {"D",        c_d},
    {"S",        c_search},
    {"E",        c_edit},
    {"L",        c_load},
    {"N",        c_name},
    {"VER",      c_ver},
    {"W",        c_write},
    {"WCMD",     c_wcmd},
    {"WCOD",     c_wcod},
    {"WREG",     c_wreg},
    {"WMEM",     c_wmem},
    {"LOG",      c_log},
    {"LOGS",     c_logs},
    {"LOGPRO",   c_logpro},
    {"SYM",      c_sym},
    {"MKEXE",    c_mkexe},
    {"EXE1",     c_internal},
    {"VIEW",     c_view},
    {"WEXE1",    c_internal},
    {"EXE2",     c_internal},
    {"WEXE2",    c_internal},
    {0, 0, 0}
};

void cmd_init(void)
{
    cpu.mode32 = 0;
}

static void trim(char *s)
{
    int n;
    while (*s == ' ' || *s == 9) memmove(s, s + 1, strlen(s));
    n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == 9 || s[n - 1] == 13 || s[n - 1] == 10)) s[--n] = 0;
}


static char *next_tok(char **p)
{
    static char tok[128];
    char *s = *p;
    int n = 0;
    while (*s == ' ' || *s == 9 || *s == ',') s++;
    if (!*s) { *p = s; return 0; }
    if (*s == '\'') {
        tok[n++] = *s++;
        while (*s && *s != '\'' && n < 126) tok[n++] = *s++;
        if (*s == '\'') tok[n++] = *s++;
        tok[n] = 0;
        *p = s;
        return tok;
    }
    while (*s && *s != ' ' && *s != 9 && *s != ',') tok[n++] = *s++;
    tok[n] = 0;
    *p = s;
    return tok;
}

static int token_addr(char *t, u16 *seg, u16 *off, int defseg)
{
    if (!t) return 1;
    return parse_addr(t, seg, off, defseg);
}


static int parse_range(char **p, u16 *seg, u16 *off, u32 *len)
{
    char *t1 = next_tok(p), *t2;
    u16 s1, o1, s2, o2;
    if (!t1) return 1;
    if (token_addr(t1, &s1, &o1, -1)) return 1;
    t2 = next_tok(p);
    if (t2 && t2[0] != '>') {
        if (token_addr(t2, &s2, &o2, s1)) return 1;
        *seg = s1; *off = o1;
        *len = (u32)(((u32)s2 * 16 + o2) - ((u32)s1 * 16 + o1));
        if ((s32)*len < 0) *len = 0;
        return 0;
    }
    *seg = s1; *off = o1; *len = 1;
    return 0;
}

static int run_loop(int maxsteps, int bump)
{
    long n = 0;
    extern void ui_snap_run(void);
    extern void ui_snap_run_g(void);
    if (bump) {
        ui_snap_run_g();
        scr_clear(0x07);
        scr_gotoxy(0, 0);
    } else ui_snap_run();
    while (maxsteps == 0 || n < maxsteps) {
        int r = step_core();
        if (log_on || logs_on) log_trace();
        if (r) return r;
        n++;
        if (n > 2000000000L) {
            dump_step_ring();
            stop_reason = 5;
            return 5;
        }
        if ((n & 0x0FFF) == 0) {
            int k = bioskey(1);
            if (k) {
                int a = k & 0xFF;
                if (a == 0x1B || a == 0x04 || a == 0x03) {
                    bioskey(0);
                    dump_step_ring();
                    stop_reason = 4;
                    return 4;
                }
            }
        }
    }
    return 0;
}

static void show_stop(int r)
{
    char b[80];
    if (r == 1) show_status("Program Terminate");
    else if (r == 2) show_status("Unknown Code, Use 'TT' or 'GG [address]' or 'autoint1 on' to run anyway");
    else if (r == 4) show_status("Stopped by user (Esc/Ctrl-D)");
    else if (r == 6) show_status("Protected-mode program (DOS/16M) - only real mode can be traced");
    else if (r == 7) show_status("Program requires XMS driver (INT 2Fh AH=43h) - not implemented");
    else if (r == 3) {
        if (bp_break_hit >= 0)
            sprintf(b, "Break Due to BreakPoint %d at %04X:%04X", bp_break_hit,
                    stop_addr_seg, stop_addr_off);
        else
            sprintf(b, "Break Due to BreakPoint ? at %04X:%04X", stop_addr_seg, stop_addr_off);
        show_status(b);
    }
    follow_ip = 1;
}


static void step_over(void)
{
    u16 ip = (u16)cpu.eip;
    u16 cs = cpu.s[S_CS];
    u8 op = rd8(cs, ip);
    int iscall = (op == 0xE8 || op == 0x9A || op == 0xCC || op == 0xCD);
    char text[80];
    u8 raw[16];
    int rl;
    int len;
    int r;
    int saved_n;
    extern void ui_snap_run(void);
    ui_snap_run();

    if (op == 0xFF) {
        u8 m = rd8(cs, (u16)(ip + 1));
        int reg = (m >> 3) & 7;
        if (reg == 2 || reg == 3) iscall = 1;
    }
    if (!iscall) {
        r = step_core();
        if (r) show_stop(r);
        return;
    }
    len = disasm(cs, ip, text, sizeof text, raw, &rl);
    saved_n = nbps;
    if (bp_add(BP_EXEC, cs, (u16)(ip + len)) < 0) return;
    r = run_loop(0, 0);
    nbps = saved_n;
    if (r && r != 3) show_stop(r);
}


static int run_to(u16 seg, u16 off, int guard)
{
    int r;
    extern void ui_snap_run(void);
    ui_snap_run();
    while (guard-- > 0) {
        r = step_core();
        if (log_on || logs_on) log_trace();
        if (r) return r;
        if (cpu.s[S_CS] == seg && (u16)cpu.eip == off) return 0;
    }
    return -1;
}


static void c_help(void)
{
    char *t = next_tok(&args);
    help_screen(t);
}

static void c_ver(void)
{
    show_status("TRR V2.52");
    scr_printf(0, scr_rows - 2, 0x0E, "(C) 1997-98 Liu TaoTao");
}

static void c_quit(void)
{
    quit_flag = 1;
}

static void c_redraw(void)
{
    show_all();
}

static void c_rs(void)
{
    scr_show_user();
    scr_getkey();
    show_all();
}

static void c_lines(void)
{
    char *t = next_tok(&args);
    int n = t ? atoi(t) : 0;
    if (n == 25 || n == 50) {
        scr_setlines(n);
        compute_layout();
        show_all();
    } else show_status("LINES 25|50");
}

static void c_color(void)
{
    char *t = next_tok(&args);
    if (!t) {
        char b[40];
        sprintf(b, "Color Set %d", color_set);
        show_status(b);
        return;
    }
    color_set = atoi(t);
    if (color_set < 1 || color_set > 10) color_set = 1;
    show_all();
}

static void c_delay(void)
{
    char *t = next_tok(&args);
    delay_ms(t ? atoi(t) : 100);
}

static void c_key(void)
{
    char *t = next_tok(&args);
    if (t) return;
    scr_getkey();
}

static void c_msg(void)
{
    show_status(args);
}

static void c_r32(void)
{
    cpu.mode32 = !cpu.mode32;
    compute_layout();
    show_all();
    show_status(cpu.mode32 ? "32bit register display" : "16bit register display");
}

static void c_int1(void)
{
    char *t = next_tok(&args);
    if (!t) { show_status(trace_int1 ? "INT1 is ON" : "INT1 is OFF"); return; }
    trace_int1 = parse_yesno(t);
}

static void c_autoint1(void)
{
    char *t = next_tok(&args);
    if (!t) { show_status(autoint1 ? "AUTOINT1 is ON" : "AUTOINT1 is OFF"); return; }
    autoint1 = parse_yesno(t);
}

static void c_autojmp(void)
{
    char *t = next_tok(&args);
    if (!t) { show_status(autojmp ? "AUTOJMP is ON" : "AUTOJMP is OFF"); return; }
    autojmp = parse_yesno(t);
}

int parse_yesno(const char *s)
{
    if (!s) return 1;
    if (toupper(s[0]) == 'O' && toupper(s[1]) == 'N') return 1;
    if (toupper(s[0]) == 'O' && toupper(s[1]) == 'F') return 0;
    if (s[0] == '1') return 1;
    return 0;
}

static void c_r(void)
{
    char *t = next_tok(&args);
    if (!t) { show_regs(); return; }
    {
        int size, id = reg_find(t, &size);
        char *v;
        if (id < 0) { show_status("Bad register"); return; }
        v = next_tok(&args);
        if (!v) {
            char b[80];
            u32 val = reg_get(id, size);
            sprintf(b, "%s=%lX", t, val);
            show_status(b);
            return;
        }
        {
            u32 val;
            if (id == 101) {
                static const char *fl = "odiszapct";
                static const u32 fm[9] = {F_OF,F_DF,F_IF,F_SF,F_ZF,F_AF,F_PF,F_CF,F_TF};
                int k, any = 0;
                for (k = 0; v[k]; k++) {
                    const char *q = strchr(fl, (char)tolower(v[k]));
                    if (q) {
                        int n = (int)(q - fl);
                        if (islower(v[k])) cpu.flags |= fm[n];
                        else cpu.flags &= ~fm[n];
                        any = 1;
                    }
                }
                if (any) { show_regs(); return; }
            }
            if (eval_expr(v, &val) == 0) {
                reg_set(id, size, val);
                show_regs();
            }
        }
    }
}

static void c_d(void)
{
    char *t = next_tok(&args);
    char *save;
    u16 seg = view_seg, off = view_off;
    u32 len = 0;
    int tofile = 0;
    char fname[80];

    if (t && t[0] == '>') { tofile = 1; strncpy(fname, t + 1, 79); fname[79] = 0; t = 0; }
    if (t) {
        if (token_addr(t, &seg, &off, -1)) { show_status("Bad address"); return; }
        save = args;
        {
            char *t2 = next_tok(&save);
            if (t2 && t2[0] == '>') { tofile = 1; strncpy(fname, t2 + 1, 79); fname[79] = 0; }
            else if (t2) {
                u16 s2, o2;
                if (parse_addr(t2, &s2, &o2, seg) == 0)
                    len = (u32)(((u32)s2 * 16 + o2) - ((u32)seg * 16 + off));
            }
        }
    }
    if (tofile) {
        FILE *f;
        u32 i;
        if (!len) len = 256;
        f = fopen(fname, "wb");
        if (!f) { show_status("Cannot Open file"); return; }
        for (i = 0; i < len; i++) fputc(rd8(seg, (u16)(off + i)), f);
        fclose(f);
        show_status("File Write OK");
        return;
    }
    view_seg = seg; view_off = off; follow_ip = 0;
    show_mem();
    show_status("Dump");
}

static void c_u(void)
{
    char *t = next_tok(&args);
    u16 seg, off;
    u32 len = 0;
    char *save = args;
    char *t2;
    int tofile = 0;
    char fname[80];
    int n, row;

    if (!t || t[0] == '>') {
        seg = cpu.s[S_CS]; off = (u16)cpu.eip;
        if (t && t[0] == '>') { tofile = 1; strncpy(fname, t + 1, 79); fname[79] = 0; }
    } else {
        if (token_addr(t, &seg, &off, cpu.s[S_CS])) { show_status("Bad address"); return; }
    }
    t2 = next_tok(&save);
    if (t2 && t2[0] == '>') { tofile = 1; strncpy(fname, t2 + 1, 79); fname[79] = 0; }
    else if (t2) {
        u16 s2, o2;
        if (parse_addr(t2, &s2, &o2, seg) == 0)
            len = (u32)(((u32)s2 * 16 + o2) - ((u32)seg * 16 + off));
    }

    if (tofile) {
        FILE *f;
        u32 i;
        char text[100];
        u8 raw[16];
        int rl;
        if (!len) len = 0x40;
        f = fopen(fname, "w");
        if (!f) { show_status("Cannot Open file"); return; }
        for (i = 0; i < len; ) {
            int l = disasm(seg, (u16)(off + i), text, sizeof text, raw, &rl);
            fprintf(f, "%04X:%04X  %s\n", seg, (u16)(off + i), text);
            i += l;
        }
        fclose(f);
        show_status("File Write OK");
        return;
    }
    view_seg = seg; view_off = off;
    follow_ip = (seg == cpu.s[S_CS] && off == (u16)cpu.eip);
    {
        char text[100];
        u8 raw[16];
        int rl;
        u32 a = off;
        if (!len) len = (u32)code_h * 8;
        row = 0;
        for (n = 0; n < (int)len && row < code_h; n++) {
            int l = disasm(seg, (u16)a, text, sizeof text, raw, &rl);
            char line[160];
            sprintf(line, "%04X:%04X  %-12s %s", seg, (u16)a, "", text);
            scr_puts(0, code_y + row, line, 0x07);
            a = (u16)(a + l); row++;
        }
    }
    show_status("Unassemble");
}

static void c_t(void)
{
    char *t = next_tok(&args);
    long n = t ? strtol(t, 0, 16) : 1L;
    long i;
    int r = 0;
    extern void ui_snap_run(void);
    ui_snap_run();
    if (n <= 0) n = 1;
    for (i = 0; i < n; i++) {
        r = step_core();
        if (log_on || logs_on) log_trace();
        if (r) break;
    }
    if (r) show_stop(r);
    follow_ip = 1;
}

static void c_p(void)
{
    step_over();
    follow_ip = 1;
}

static void c_pp(void)
{
    step_over();
    follow_ip = 1;
}

static void c_pret(void)
{
    u16 ret = rd16(cpu.s[S_SS], (u16)cpu.r[R_SP]);
    u32 sp = cpu.r[R_SP];
    int guard = 4000000;
    int r;
    extern void ui_snap_run(void);
    ui_snap_run();
    while (guard-- > 0) {
        r = step_core();
        if (log_on || logs_on) log_trace();
        if (r) { show_stop(r); return; }
        if ((u16)cpu.eip == ret && cpu.r[R_SP] > sp) break;
    }
    follow_ip = 1;
}

static int exec_native(void)
{
    struct ExecBlk {
        u16 env;
        u16 coff, cseg;
        u16 f1off, f1seg;
        u16 f2off, f2seg;
    };
    static struct ExecBlk eb;
    static char tail[130];
    union REGS r;
    struct SREGS sr;
    if (!prog_name[0]) return 1;
    tail[0] = 0;
    tail[1] = 0x0D;
    tail[2] = 0;

    eb.env = 0;
    eb.coff = FP_OFF(tail);
    eb.cseg = FP_SEG(tail);
    eb.f1off = 0x5C; eb.f1seg = psp_seg ? psp_seg : FP_SEG(tail);
    eb.f2off = 0x6C; eb.f2seg = psp_seg ? psp_seg : FP_SEG(tail);

    r.h.ah = 0x4B; r.h.al = 0;
    r.x.dx = FP_OFF(prog_name);
    r.x.bx = FP_OFF(&eb);
    sr.ds = FP_SEG(prog_name);
    sr.es = FP_SEG(&eb);
    int86x(0x21, &r, &r, &sr);
    return r.x.cflag ? (int)r.x.ax : 0;
}

static void native_run(void)
{
    int err;
    int rows = line_count;
    scr_setlines(rows == 50 ? 50 : 25);
    err = exec_native();
    if (!err) {
        scr_puts(0, scr_rows - 1, "Native run finished - press any key", 0x70);
        scr_getkey();
    }
    scr_setlines(rows == 50 ? 50 : 25);
    compute_layout();
    show_all();
    if (err) {
        char b[64];
        sprintf(b, "EXEC failed, DOS error %d", err);
        show_status(b);
    } else {
        show_status("Returned from native run");
    }
}

static void c_go(void)
{
    char *t = next_tok(&args);
    char *c;
    int r;
    if (t) {
        u16 seg, off;
        int hs = 0;
        if (parse_addr_ex(t, &seg, &off, cpu.s[S_CS], &hs) == 0) {
            char cond[64];
            char *c = next_tok(&args);
            int idx;
            idx = bp_add(BP_EXEC, seg, off);
            if (!hs) bps[idx].anyseg = 1;
            if (c) { strncpy(bps[idx].cond, c, 63); bps[idx].has_cond = 1; }
            r = run_loop(0, 1);
            if (r == 7) { show_status("Program requires XMS driver - not implemented"); bp_clear_all(); return; }
            show_stop(r);
            bp_clear_all();
            return;
        }

        {
            char cond[80];
            strcpy(cond, t);
            while ((c = next_tok(&args)) != 0) { strcat(cond, " "); strncat(cond, c, 20); }
            {
                int idx = bp_add(BP_EXEC, cpu.s[S_CS], (u16)cpu.eip);
                strncpy(bps[idx].cond, cond, 63); bps[idx].has_cond = 1;
                r = run_loop(0, 1);
                show_stop(r);
                bp_clear_all();
            }
            return;
        }
    }
    r = run_loop(0, 1);
    if (r == 7) { show_status("Program requires XMS driver - not implemented"); return; }
    show_stop(r);
}

static void c_gg(void)
{
    char *t = next_tok(&args);
    int r;
    u16 seg = cpu.s[S_CS], off = (u16)cpu.eip;
    if (t) { if (parse_addr(t, &seg, &off, cpu.s[S_CS])) return; cpu.s[S_CS] = seg; cpu.eip = off; }
    no_break = 1;
    r = run_loop(0, 1);
    no_break = 0;
    show_stop(r);
}

static void c_goknl(int count)
{
    int i;
    int r;
    u16 cs = cpu.s[S_CS];
    extern void ui_snap_run(void);
    ui_snap_run();
    for (i = 0; i < (count > 0 ? count : 1); i++) {
        do {
            r = step_core();
            if (log_on || logs_on) log_trace();
            if (r) { show_stop(r); return; }
        } while (cpu.s[S_CS] == cs);
        cs = cpu.s[S_CS];
    }
    follow_ip = 1;
}

static void c_getknl(void)
{
    c_goknl(1);
}

static void c_bp(void)
{
    char *t = next_tok(&args);
    u16 seg, off;
    int idx;
    if (!t) { show_status("BP [seg:]offset"); return; }
    {
        int hs = 0;
        if (parse_addr_ex(t, &seg, &off, cpu.s[S_CS], &hs)) { show_status("Bad address"); return; }
        idx = bp_add(BP_EXEC, seg, off);
        if (idx < 0) { show_status("Too many Break Points"); return; }
        if (!hs) bps[idx].anyseg = 1;
    }
    {
        char *c = next_tok(&args);
        if (c) { strncpy(bps[idx].cond, c, 63); bps[idx].has_cond = 1; }
    }
    show_status("BreakPoint set");
}

static void c_bpint(void)
{
    char *t = next_tok(&args);
    int n, idx;
    if (!t) { show_status("BPINT NUM [conditions]"); return; }
    n = (int)strtol(t, 0, 16);
    idx = bp_add(BP_INT, 0, (u16)n);
    if (idx < 0) { show_status("Too many Break Points"); return; }
    {
        char *c = next_tok(&args);
        if (c) { strncpy(bps[idx].cond, c, 63); bps[idx].has_cond = 1; }
    }
    show_status("BreakPoint set");
}

static void c_bpio(void)
{
    char *t = next_tok(&args);
    int n, idx;
    if (!t) { show_status("BPIO NUM [conditions]"); return; }
    n = (int)strtol(t, 0, 16);
    idx = bp_add(BP_IO, 0, (u16)n);
    if (idx < 0) { show_status("Too many Break Points"); return; }
    {
        char *c = next_tok(&args);
        if (c) { strncpy(bps[idx].cond, c, 63); bps[idx].has_cond = 1; }
    }
    show_status("BreakPoint set");
}

static void c_bpm(void)
{
    char *t = next_tok(&args);
    u16 seg, off;
    int idx;
    if (!t) { show_status("BPM [seg:]offset"); return; }
    if (parse_addr(t, &seg, &off, -1)) { show_status("Bad address"); return; }
    idx = bp_add(BP_MEM, seg, off);
    if (idx < 0) { show_status("Too many Break Points"); return; }
    {
        char *c = next_tok(&args);
        if (c) { strncpy(bps[idx].cond, c, 63); bps[idx].has_cond = 1; }
    }
    show_status("BreakPoint set");
}

static void c_bpw(void)
{
    char *t = next_tok(&args);
    u16 seg, off;
    int idx;
    int hs = 0;
    if (!t) { show_status("BPW seg:offset"); return; }
    if (parse_addr_ex(t, &seg, &off, cpu.s[S_DS], &hs)) { show_status("Bad address"); return; }
    idx = bp_add(BP_WRITE, seg, off);
    if (idx < 0) { show_status("Too many Break Points"); return; }
    if (!hs) bps[idx].anyseg = 1;
    {
        char *c = next_tok(&args);
        if (c) { strncpy(bps[idx].cond, c, 63); bps[idx].has_cond = 1; }
    }
    show_status("BreakPoint set");
}

static void c_bpreg(void)
{
    char *t = next_tok(&args);
    int size, id, idx;
    if (!t) { show_status("BPREG REG|SEG [conditions]"); return; }
    id = reg_find(t, &size);
    if (id < 0) { show_status("Bad register"); return; }
    idx = bp_add(BP_REG, 0, 0);
    if (idx < 0) { show_status("Too many Break Points"); return; }
    bps[idx].reg = id;
    bps[idx].nbytes = size;
    {
        char *c = next_tok(&args);
        if (c) { strncpy(bps[idx].cond, c, 63); bps[idx].has_cond = 1; }
    }
    show_status("BreakPoint set");
}

static void c_bpxb(void)
{
    int n = 0, idx;
    char *t;
    u8 bytes[8];
    while ((t = next_tok(&args)) != 0 && n < 8) {
        if (t[0] == '\'') { if (t[1]) bytes[n++] = (u8)t[1]; }
        else bytes[n++] = (u8)strtol(t, 0, 16);
    }
    if (!n) { show_status("BPXB bytes [conditions]"); return; }
    idx = bp_add(BP_XB, 0, 0);
    if (idx < 0) { show_status("Too many Break Points"); return; }
    memcpy(bps[idx].bytes, bytes, n);
    bps[idx].nbytes = n;
    {
        char *c = next_tok(&args);
        if (c) { strncpy(bps[idx].cond, c, 63); bps[idx].has_cond = 1; }
    }
    show_status("BreakPoint set");
}

static void c_bpknl(void)
{
    char *t = next_tok(&args);
    int idx = bp_add(BP_KNL, 0, 0);
    if (idx < 0) { show_status("Too many Break Points"); return; }
    bps[idx].value = t ? (u32)atoi(t) : 1;
    show_status("BreakPoint set");
}

static void c_bl(void)
{
    int i, row = 0;
    char b[80];
    scr_fill(0, code_y, 80, code_h, ' ', 0x07);
    scr_puts(0, code_y, "Num Type Fl Address/Condition", 0x70);
    row = 1;
    for (i = 0; i < nbps && row < code_h; i++, row++) {
        const char *tn = "?";
        switch (bps[i].type) {
        case BP_EXEC: tn = "BP  "; break;
        case BP_INT: tn = "INT "; break;
        case BP_IO: tn = "IO  "; break;
        case BP_REG: tn = "REG "; break;
        case BP_XB: tn = "XB  "; break;
        case BP_MEM: tn = "MEM "; break;
        case BP_WRITE: tn = "W   "; break;
        case BP_KNL: tn = "KNL "; break;
        }
        if (bps[i].type == BP_REG) {
            static const char *rn[16] = {
                "AL","CL","DL","BL","AH","CH","DH","BH",
                "AX","CX","DX","BX","SP","BP","SI","DI"
            };
            static const char *sn[6] = {"ES","CS","SS","DS","FS","GS"};
            const char *name = "?";
            int id = bps[i].reg;
            if (bps[i].nbytes == 8 && id >= 0 && id < 8) name = rn[id];
            else if (bps[i].nbytes == 16 && id >= 0 && id < 8) name = rn[8 + id];
            else if (bps[i].nbytes == 32 && id >= 0 && id < 8)
                name = (id == 0) ? "EAX" : (id == 1) ? "ECX" : (id == 2) ? "EDX" :
                       (id == 3) ? "EBX" : (id == 4) ? "ESP" : (id == 5) ? "EBP" :
                       (id == 6) ? "ESI" : "EDI";
            else if (id == 100) name = "IP";
            else if (id == 101) name = "FLAGS";
            else if (id >= 200 && id < 206) name = sn[id - 200];
            sprintf(b, "%2d  %s %c  %s %s", i, tn, bps[i].enabled ? 'E' : 'D',
                    name, bps[i].cond);
        } else if (bps[i].type == BP_INT) {
            sprintf(b, "%2d  %s %c  INT %02Xh %s", i, tn, bps[i].enabled ? 'E' : 'D',
                    bps[i].off, bps[i].cond);
        } else if (bps[i].type == BP_IO) {
            sprintf(b, "%2d  %s %c  PORT %04Xh %s", i, tn, bps[i].enabled ? 'E' : 'D',
                    bps[i].off, bps[i].cond);
        } else if (bps[i].type == BP_KNL) {
            sprintf(b, "%2d  %s %c  %s", i, tn, bps[i].enabled ? 'E' : 'D', bps[i].cond);
        } else if (bps[i].type == BP_XB) {
            int k, p;
            p = sprintf(b, "%2d  %s %c  ", i, tn, bps[i].enabled ? 'E' : 'D');
            for (k = 0; k < bps[i].nbytes; k++) p += sprintf(b + p, "%02X ", bps[i].bytes[k]);
            strcat(b, bps[i].cond);
        } else {
            sprintf(b, "%2d  %s %c  %04X:%04X %s", i, tn, bps[i].enabled ? 'E' : 'D',
                    bps[i].seg, bps[i].off, bps[i].cond);
        }
        scr_puts(0, code_y + row, b, 0x07);
    }
    hold_view = 1;
    show_status("BreakPoint List");
}

static void c_bc(void)
{
    char *t = next_tok(&args);
    int i;
    if (!t) { bp_clear_all(); show_status("All BreakPoints cleared"); return; }
    i = atoi(t);
    if (i >= 0 && i < nbps) { bps[i].enabled = 0; }
}

static void c_bd(void)
{
    char *t = next_tok(&args);
    int i = atoi(t);
    if (i >= 0 && i < nbps) bps[i].enabled = 0;
}

static void c_be(void)
{
    char *t = next_tok(&args);
    int i = atoi(t);
    if (i >= 0 && i < nbps) bps[i].enabled = 1;
}

static void c_do(void)
{
    char *t = next_tok(&args);
    if (t) batch_run(t);
}

static void c_label(void)
{
    char *t = next_tok(&args);
    u16 seg, off;
    char *name;
    if (!t) { show_status("LABEL seg:offset 'labelname'"); return; }
    if (parse_addr(t, &seg, &off, cpu.s[S_CS])) { show_status("Bad address"); return; }
    name = next_tok(&args);
    if (name) {
        char nm[32];
        int n = 0;
        if (*name == '\'') name++;
        while (*name && *name != '\'' && n < 31) nm[n++] = *name++;
        nm[n] = 0;
        sym_add(nm, seg, off);
        show_status("Label added");
    }
}

static void c_cmt(void)
{
    char *t = next_tok(&args);
    u16 seg, off;
    char *txt;
    if (!t) { show_status("CMT seg:offset 'comments'"); return; }
    if (parse_addr(t, &seg, &off, cpu.s[S_CS])) { show_status("Bad address"); return; }
    txt = next_tok(&args);
    if (txt) {
        char nm[60];
        int n = 0;
        if (*txt == '\'') txt++;
        while (*txt && *txt != '\'' && n < 59) nm[n++] = *txt++;
        nm[n] = 0;
        cmt_add(seg, off, nm);
        show_status("Comment added");
    }
}

static void c_symbols(void)
{
    extern void sym_dump(void);
    sym_dump();
}

static void c_sym(void)
{
    c_symbols();
}

static void c_stack(void)
{
    int i, row = 0;
    char b[80];
    u16 bp = (u16)cpu.r[R_BP];
    scr_fill(0, code_y, 80, code_h, ' ', 0x07);
    scr_puts(0, code_y, "Stack (BP chain)", 0x70);
    for (i = 0; i < 24 && row < code_h - 1; i++) {
        u16 next, ret;
        if (bp < 4) break;
        next = rd16(cpu.s[S_SS], bp);
        ret = rd16(cpu.s[S_SS], (u16)(bp + 2));
        sprintf(b, "BP=%04X  RET=%04X", bp, ret);
        scr_puts(0, code_y + 1 + row, b, 0x07);
        row++;
        if (next <= bp) break;
        bp = next;
    }
    hold_view = 1;
    show_status("Stack");
}

static void c_search(void)
{
    u16 seg, off;
    u32 len;
    char *p = args;
    u8 pat[64];
    int npat = 0;
    char *t;
    u32 i, found = 0;

    if (parse_range(&p, &seg, &off, &len)) { show_status("S range bytes"); return; }
    while ((t = next_tok(&p)) != 0 && npat < 64) {
        if (t[0] == '\'') { int k; for (k = 1; t[k] && t[k] != '\''; k++) pat[npat++] = (u8)t[k]; }
        else pat[npat++] = (u8)strtol(t, 0, 16);
    }
    if (!npat) { show_status("S range bytes"); return; }
    for (i = 0; i + npat <= len; i++) {
        int k, ok = 1;
        for (k = 0; k < npat; k++)
            if (rd8(seg, (u16)(off + i + k)) != pat[k]) { ok = 0; break; }
        if (ok) {
            char b[40];
            sprintf(b, "Found at %04X:%04X", seg, (u16)(off + i));
            show_status(b);
            found++;
            if (found > 20) break;
        }
    }
    if (!found) show_status("Not found");
}

static void c_fill(void)
{
    u16 seg, off;
    u32 len;
    char *p = args;
    char *t;
    u8 v;
    u32 i;

    if (parse_range(&p, &seg, &off, &len)) { show_status("F range bytes"); return; }
    t = next_tok(&p);
    if (!t) { show_status("F range bytes"); return; }
    if (t[0] == '\'') v = (u8)t[1];
    else v = (u8)strtol(t, 0, 16);
    for (i = 0; i < len; i++) wr8(seg, (u16)(off + i), v);
    show_all();
}

static void c_move(void)
{
    u16 seg, off, dseg, doff;
    u32 len;
    char *p = args;
    char *t;

    if (parse_range(&p, &seg, &off, &len)) { show_status("M range dest"); return; }
    t = next_tok(&p);
    if (!t) { show_status("M range dest"); return; }
    if (parse_addr(t, &dseg, &doff, seg)) { show_status("Bad address"); return; }
    if (dseg == seg && doff > off && doff < off + len) {
        u32 i;
        for (i = len; i > 0; i--) wr8(dseg, (u16)(doff + i - 1), rd8(seg, (u16)(off + i - 1)));
    } else {
        u32 i;
        for (i = 0; i < len; i++) wr8(dseg, (u16)(doff + i), rd8(seg, (u16)(off + i)));
    }
    show_all();
}

static void c_edit(void)
{
    char *t = next_tok(&args);
    u16 seg, off;
    if (!t) { show_status("E [range]"); return; }
    if (parse_addr(t, &seg, &off, -1)) { show_status("Bad address"); return; }
    {
        char *v = next_tok(&args);
        if (v) {
            while (v) {
                if (v[0] == '\'') wr8(seg, off++, (u8)v[1]);
                else wr8(seg, off++, (u8)strtol(v, 0, 16));
                v = next_tok(&args);
            }
        } else {

            char b[40];
            for (;;) {
                int k;
                sprintf(b, "%04X:%04X  %02X  ", seg, off, rd8(seg, off));
                show_status(b);
                scr_gotoxy(20, status_y);
                k = scr_getkey();
                if (k == 0x1B) break;
                if (k == 0x0D) { off++; continue; }
                if (k == '-') { off--; continue; }
                if (k == '+') { off++; continue; }
                if (isxdigit(k)) {
                    int hi = (k <= '9') ? k - '0' : (toupper(k) - 'A' + 10);
                    int lo;
                    scr_putc(20, status_y, k, 0x0F);
                    k = scr_getkey();
                    lo = (k <= '9') ? k - '0' : (toupper(k) - 'A' + 10);
                    wr8(seg, off, (u8)(hi * 16 + lo));
                    off++;
                }
            }
        }
    }
    show_all();
}

static int looks_like_path(const char *s)
{
    if (s[0] == '\\' || s[0] == '/') return 1;
    if (((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z')) &&
        s[1] == ':' && (s[2] == '\\' || s[2] == '/' || s[2] == 0))
        return 1;
    return 0;
}

static void c_load(void)
{
    char *t = next_tok(&args);
    u16 seg = cpu.s[S_DS], off = 0x100;
    char fname[80];
    FILE *f;
    long len;
    u32 got;
    u8 far *p;

    if (!t) t = 0;
    if (t) {
        if (!looks_like_path(t) && parse_addr(t, &seg, &off, -1) == 0) {
            char n2[80], n3[80], n4[80];
            char *t2 = next_tok(&args);
            char *t3, *t4;
            n2[0] = n3[0] = n4[0] = 0;
            if (t2) { strncpy(n2, t2, 79); n2[79] = 0; }
            t3 = next_tok(&args); if (t3) { strncpy(n3, t3, 79); n3[79] = 0; }
            t4 = next_tok(&args); if (t4) { strncpy(n4, t4, 79); n4[79] = 0; }
            if (n2[0] && n3[0] && n4[0]) {

                union REGS r;
                struct SREGS sr;
                int drive = (int)strtol(n2, 0, 16);
                int start = (int)strtol(n3, 0, 16);
                int count = (int)strtol(n4, 0, 16);
                r.h.al = (u8)count;
                r.x.cx = (u16)start;
                r.x.dx = (u16)drive;
                r.x.bx = off;
                sr.ds = seg;
                int86x(0x25, &r, &r, &sr);
                show_status(r.x.cflag ? "Disk read error" : "File Load OK");
                return;
            }
            if (n2[0]) { strncpy(fname, n2, 79); fname[79] = 0; }
            else { strncpy(fname, prog_name, 79); fname[79] = 0; }
        } else {
            strncpy(fname, t, 79);
            fname[79] = 0;
        }
    } else strncpy(fname, prog_name, 79);
    fname[79] = 0;
    if (!fname[0]) { show_status("L [address][filename]"); return; }
    f = fopen(fname, "rb");
    if (!f) { show_status("Cannot Open file"); return; }
    fseek(f, 0, SEEK_END); len = ftell(f); fseek(f, 0, SEEK_SET);
    got = 0;
    while (got < (u32)len) {
        u32 chunk = (u32)len - got;
        u32 pos = (u32)off + got;
        u16 dseg = (u16)(seg + (u16)(pos >> 4));
        u16 doff = (u16)(pos & 0x0F);
        if (chunk > 0x4000UL) chunk = 0x4000UL;
        if ((u32)doff + chunk > 0x10000UL) chunk = 0x10000UL - doff;
        if (far_read(f, mem_ptr(dseg, doff), chunk) != chunk) break;
        got += chunk;
    }
    fclose(f);
    view_seg = seg; view_off = off; follow_ip = 0;
    show_all();
    show_status("File Load OK");
}

static void c_write(void)
{
    char *t = next_tok(&args);
    u16 seg = cpu.s[S_DS], off = 0x100;
    u32 len = 0;
    char fname[80];
    FILE *f;
    u32 i;

    if (t) {
        if (!looks_like_path(t) && parse_addr(t, &seg, &off, -1) == 0) {
            char *t2 = next_tok(&args);
            if (t2) {
                u16 s2, o2;
                if (t2[0] == '>') strncpy(fname, t2 + 1, 79);
                else if (!looks_like_path(t2) && parse_addr(t2, &s2, &o2, seg) == 0) {
                    len = (u32)(((u32)s2 * 16 + o2) - ((u32)seg * 16 + off));
                    t2 = next_tok(&args);
                    if (t2) strncpy(fname, t2, 79);
                    else strncpy(fname, prog_name, 79);
                } else strncpy(fname, t2, 79);
            } else strncpy(fname, prog_name, 79);
        } else {
            strncpy(fname, t, 79);
        }
    } else strncpy(fname, prog_name, 79);
    fname[79] = 0;
    if (!len) len = ((u32)cpu.r[R_BX] << 16) + (u16)cpu.r[R_CX];
    if (!len) len = 0x100;
    if (!fname[0]) { show_status("No filename"); return; }
    f = fopen(fname, "wb");
    if (!f) { show_status("Cannot Open file"); return; }
    for (i = 0; i < len; i++) fputc(rd8(seg, (u16)(off + i)), f);
    fclose(f);
    show_status("File Write OK");
}

static void c_name(void)
{
    char *t = next_tok(&args);
    if (t) {
        strncpy(prog_name, t, 127);
        prog_name[127] = 0;
        show_status("Filename set");
    }
}

static void c_reload(void)
{
    extern void ui_snapshot_reset(void);
    if (!prog_name[0]) { show_status("No filename"); return; }
    load_program(prog_name);
    ui_snapshot_reset();
    view_seg = cpu.s[S_DS];
    view_off = 0;
    follow_ip = 1;
    show_all();
}

static void c_mkexe(void)
{
    char *t = next_tok(&args);
    if (!psp_seg) { show_status("No program"); return; }
    if (prog_type == 1) {
        if (write_com(t, psp_seg, img_size) == 0) show_status("File Write OK");
        else show_status("Write Error");
    } else {
        if (write_exe_image(t) == 0) show_status("Write OK");
        else show_status("Write Error");
    }
}

static void c_view(void)
{
    show_all();
}

static void c_wcmd(void) { show_all(); }
static void c_wcod(void) { show_code(); }
static void c_wreg(void) { show_regs(); }
static void c_wmem(void) { show_mem(); }
static void c_wd(void)
{
    char *t = next_tok(&args);
    if (t) {
        int n = atoi(t);
        if (n > 0 && n < 20) {
            mem_lines_user = n;
            compute_layout();
        }
    }
    show_all();
}
static void c_internal(void) { show_status("internal command"); }

static void c_log(void)
{
    char *t = next_tok(&args);
    if (!t) { show_status(log_on ? "LOG is ON" : "LOG is OFF"); return; }
    log_on = parse_yesno(t);
    if (!log_on) show_status("LOG is OFF");
    else { log_count = 0; show_status("LOG is ON"); }
}

static void c_logs(void)
{
    char *t = next_tok(&args);
    if (!t) { show_status(logs_on ? "LOGS is ON" : "LOGS is OFF"); return; }
    logs_on = parse_yesno(t);
}

static void c_vlog(void)
{
    int i, row = 0;
    char b[80];
    scr_fill(0, code_y, 80, code_h, ' ', 0x07);
    scr_puts(0, code_y, "History (CS:IP)", 0x70);
    for (i = 0; i < log_count && i < 512 && row < code_h - 1; i++) {
        u32 v = log_buf[i];
        sprintf(b, "%04X:%04X", (unsigned)(v >> 16), (unsigned)(v & 0xFFFF));
        scr_puts(0, code_y + 1 + row, b, 0x07);
        row++;
    }
    hold_view = 1;
    show_status("VLOG");
}

void log_trace(void)
{
    if (log_count < 512) {
        log_buf[log_count++] = ((u32)cpu.s[S_CS] << 16) | (u16)cpu.eip;
    }
    if (logs_on) {
        FILE *f = fopen("LOG.DAT", "a");
        if (f) {
            fprintf(f, "%04X:%04X\n", cpu.s[S_CS], (u16)cpu.eip);
            fclose(f);
        }
    }
}

static void c_logpro(void)
{
    FILE *f = fopen("LOGPRO.DAT", "w");
    int i;
    if (!f) { show_status("Cannot Open file"); return; }
    for (i = 0; i < log_count && i < 4096; i++)
        fprintf(f, "%04X:%04X\n", (unsigned)(log_buf[i] >> 16), (unsigned)(log_buf[i] & 0xFFFF));
    fclose(f);
    show_status("LOGPRO.DAT written");
}


static int asm_reg(const char *s, int *size)
{
    return reg_find(s, size);
}

static void c_asm(void)
{
    char *t = next_tok(&args);
    u16 seg, off;
    char line[100];

    if (t) { if (parse_addr(t, &seg, &off, cpu.s[S_CS])) { show_status("Bad address"); return; } }
    else { seg = cpu.s[S_CS]; off = (u16)cpu.eip; }

    for (;;) {
        char text[80];
        u8 raw[16];
        int rl;
        char b[90];
        int n;
        disasm(seg, off, text, sizeof text, raw, &rl);
        sprintf(b, "%04X:%04X  %s", seg, off, text);
        show_status(b);
        scr_gotoxy(0, msg_y);
        scr_fill(0, msg_y, 80, 1, ' ', 0x07);
        scr_puts(0, msg_y, "ASM>", 0x0F);
        n = 0;
        for (;;) {
            int k = scr_getkey();
            if (k == 0x1B) { show_all(); return; }
            if (k == 0x0D) break;
            if (k == 0x08 && n > 0) { n--; scr_putc(5 + n, msg_y, ' ', 0x07); continue; }
            if (k >= 32 && k < 127 && n < 70) {
                line[n++] = (char)k;
                scr_putc(5 + n - 1, msg_y, k, 0x0F);
            }
        }
        line[n] = 0;
        if (!n) { scr_getkey(); }
        else {
            int len = assemble_line(line, seg, off);
            if (len > 0) off = (u16)(off + len);
            else { show_status("Bad Command"); }
        }
    }
}

int assemble_line(const char *line, u16 seg, u16 off)
{
    char mn[16], op1[32], op2[32];
    int n;
    int size;
    n = sscanf(line, "%15s %31[^,],%31s", mn, op1, op2);
    if (n <= 0) return 0;
    {
        int i;
        for (i = 0; mn[i]; i++) mn[i] = toupper(mn[i]);
    }
    if (n == 1) {
        if (!strcmp(mn, "NOP")) { wr8(seg, off, 0x90); return 1; }
        if (!strcmp(mn, "RET")) { wr8(seg, off, 0xC3); return 1; }
        if (!strcmp(mn, "RETF")) { wr8(seg, off, 0xCB); return 1; }
        if (!strcmp(mn, "IRET")) { wr8(seg, off, 0xCF); return 1; }
        if (!strcmp(mn, "CLI")) { wr8(seg, off, 0xFA); return 1; }
        if (!strcmp(mn, "STI")) { wr8(seg, off, 0xFB); return 1; }
        if (!strcmp(mn, "CLD")) { wr8(seg, off, 0xFC); return 1; }
        if (!strcmp(mn, "STD")) { wr8(seg, off, 0xFD); return 1; }
        if (!strcmp(mn, "CLC")) { wr8(seg, off, 0xF8); return 1; }
        if (!strcmp(mn, "STC")) { wr8(seg, off, 0xF9); return 1; }
        if (!strcmp(mn, "HLT")) { wr8(seg, off, 0xF4); return 1; }
        if (!strcmp(mn, "PUSHF")) { wr8(seg, off, 0x9C); return 1; }
        if (!strcmp(mn, "POPF")) { wr8(seg, off, 0x9D); return 1; }
        if (!strcmp(mn, "CBW")) { wr8(seg, off, 0x98); return 1; }
        if (!strcmp(mn, "CWD")) { wr8(seg, off, 0x99); return 1; }
        return 0;
    }
    if (!strcmp(mn, "INT")) {
        u32 v; if (eval_expr(op1, &v)) return 0;
        wr8(seg, off, 0xCD); wr8(seg, (u16)(off+1), (u8)v); return 2;
    }
    if (!strcmp(mn, "JMP")) {
        u32 v; if (eval_expr(op1, &v)) return 0;
        wr8(seg, off, 0xE9);
        wr16(seg, (u16)(off+1), (u16)(v - off - 3)); return 3;
    }
    if (!strcmp(mn, "CALL")) {
        u32 v; if (eval_expr(op1, &v)) return 0;
        wr8(seg, off, 0xE8);
        wr16(seg, (u16)(off+1), (u16)(v - off - 3)); return 3;
    }
    if (!strcmp(mn, "PUSH") || !strcmp(mn, "POP") || !strcmp(mn, "INC") ||
        !strcmp(mn, "DEC") || !strcmp(mn, "MOV")) {
        int size, id = asm_reg(op1, &size);
        if (id >= 0 && id < 8) {
            u8 base = 0;
            if (!strcmp(mn, "PUSH")) base = 0x50;
            else if (!strcmp(mn, "POP")) base = 0x58;
            else if (!strcmp(mn, "INC")) base = 0x40;
            else if (!strcmp(mn, "DEC")) base = 0x48;
            if (base) {
                if (size == 16) { wr8(seg, off, (u8)(base + id)); return 1; }
                if (size == 8) return 0;
            }
            if (!strcmp(mn, "MOV") && n >= 3) {
                u32 v;
                if (eval_expr(op2, &v)) return 0;
                if (size == 8) { wr8(seg, off, (u8)(0xB0 + (id & 7))); wr8(seg,(u16)(off+1),(u8)v); return 2; }
                if (size == 16) { wr8(seg, off, (u8)(0xB8 + id)); wr16(seg,(u16)(off+1),(u16)v); return 3; }
            }
        }

        if (!strcmp(mn, "MOV") && n >= 3) {
            u32 v;
            if (eval_expr(op2, &v)) return 0;
            if (op1[0] == '[') {

                u16 a, sg = cpu.s[S_DS];
                char *p = op1 + 1;
                char *q = strchr(p, ']');
                if (q) *q = 0;
                if (parse_addr(p, &sg, &a, -1) == 0) {
                    wr8(seg, off, 0xC7); wr8(seg,(u16)(off+1),0x06);
                    wr16(seg,(u16)(off+2),(u16)a); wr16(seg,(u16)(off+4),(u16)v);
                    return 6;
                }
            }
        }
        return 0;
    }

    {
        static const char *an[8] = {"ADD","OR","ADC","SBB","AND","SUB","XOR","CMP"};
        int a;
        for (a = 0; a < 8; a++) {
            if (!strcmp(mn, an[a]) && n >= 3) {
                int size, id = asm_reg(op1, &size);
                u32 v;
                if (id < 0 || size != 16) return 0;
                if (eval_expr(op2, &v)) return 0;
                wr8(seg, off, (u8)(0x81));
                wr8(seg, (u16)(off + 1), (u8)(0xC0 | (a << 3) | (id & 7)));
                wr16(seg, (u16)(off + 2), (u16)v);
                return 4;
            }
        }
    }
    return 0;
}


extern void msg_echo(const char *s);

void batch_run(const char *fname)
{
    FILE *f;
    char line[130];
    if (batch_depth > 4) return;
    f = fopen(fname, "r");
    if (!f) return;
    batch_depth++;
    while (fgets(line, sizeof line, f)) {
        trim(line);
        if (!line[0] || line[0] == ';') continue;
        if (toupper(line[0]) == 'K' && toupper(line[1]) == 'E' &&
            toupper(line[2]) == 'Y' && line[3] == ' ')
            continue;
        msg_echo(line);
        if (do_command(line)) break;
        show_all();
    }
    batch_depth--;
    fclose(f);
}

int quit_requested(void)
{
    return quit_flag;
}

int do_command(char *line)
{
    char *p = line;
    char *w;
    int i;
    hold_view = 0;
    trim(line);
    if (!line[0] || line[0] == ';') return 0;
    w = next_tok(&p);
    if (!w) return 0;
    strncpy(cmd_name, w, 31);
    cmd_name[31] = 0;
    for (i = 0; cmd_name[i]; i++) cmd_name[i] = toupper(cmd_name[i]);
    args = p;


    if (strncmp(cmd_name, "GO", 2) == 0 && strlen(cmd_name) > 2) {
        char sub[32];
        strcpy(sub, cmd_name + 2);
        for (i = 0; commands[i].name; i++) {
            if (!strcmp(commands[i].name, sub) &&
                (strncmp(sub, "BP", 2) == 0 || strncmp(sub, "B", 1) == 0)) {
                int r;
                char tmp[160];
                sprintf(tmp, "%s %s", sub, args);
                do_command(tmp);
                r = run_loop(0, 1);
                show_stop(r);
                bp_clear_all();
                return quit_flag;
            }
        }
    }
    for (i = 0; commands[i].name; i++) {
        if (!strcmp(commands[i].name, cmd_name)) {
            commands[i].fn();
            return quit_flag;
        }
    }
    show_status("Bad Command");
    return 0;
}
