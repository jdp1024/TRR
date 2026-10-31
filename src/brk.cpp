#include "tr.h"

struct BreakPoint bps[MAXBP];
int nbps = 0;
int bp_break_hit = -1;
u16 bp_break_addr = 0;

int no_break = 0;
static CPU regs_before;

int sym_by_name(const char *name, u16 *seg, u16 *off);

void bp_clear_all(void)
{
    nbps = 0;
    bp_break_hit = -1;
}

int bp_add(int type, u16 seg, u16 off)
{
    if (nbps >= MAXBP) return -1;
    memset(&bps[nbps], 0, sizeof(struct BreakPoint));
    bps[nbps].type = type;
    bps[nbps].enabled = 1;
    bps[nbps].seg = seg;
    bps[nbps].off = off;
    nbps++;
    return nbps - 1;
}


static const char *regnames[16] = {
    "AL","CL","DL","BL","AH","CH","DH","BH",
    "AX","CX","DX","BX","SP","BP","SI","DI"
};
static const char *segnames[6] = {"ES","CS","SS","DS","FS","GS"};

int reg_find(const char *name, int *size)
{
    int i;
    char up[8];
    for (i = 0; name[i] && i < 7; i++) up[i] = toupper(name[i]);
    up[i] = 0;
    for (i = 0; i < 8; i++) if (strcmp(up, regnames[i]) == 0) { *size = 8; return i; }
    for (i = 0; i < 8; i++) if (strcmp(up, regnames[i + 8]) == 0) { *size = 16; return i; }
    if (strcmp(up, "EAX") == 0) { *size = 32; return 0; }
    if (strcmp(up, "ECX") == 0) { *size = 32; return 1; }
    if (strcmp(up, "EDX") == 0) { *size = 32; return 2; }
    if (strcmp(up, "EBX") == 0) { *size = 32; return 3; }
    if (strcmp(up, "ESP") == 0) { *size = 32; return 4; }
    if (strcmp(up, "EBP") == 0) { *size = 32; return 5; }
    if (strcmp(up, "ESI") == 0) { *size = 32; return 6; }
    if (strcmp(up, "EDI") == 0) { *size = 32; return 7; }
    if (strcmp(up, "IP") == 0) { *size = 16; return 100; }
    if (strcmp(up, "FL") == 0 || strcmp(up, "FLAGS") == 0) { *size = 16; return 101; }
    for (i = 0; i < 6; i++)
        if (strcmp(up, segnames[i]) == 0) { *size = 16; return 200 + i; }
    return -1;
}

u32 reg_get(int id, int size)
{
    if (id >= 0 && id < 8) {
        if (size == 8) {
            if (id < 4) return cpu.r[id] & 0xFF;
            return (cpu.r[id - 4] >> 8) & 0xFF;
        }
        if (size == 16) return cpu.r[id] & 0xFFFF;
        return cpu.r[id];
    }
    if (id == 100) return (u16)cpu.eip;
    if (id == 101) return cpu.flags & 0xFFFF;
    if (id >= 200 && id < 206) return cpu.s[id - 200];
    return 0;
}

void reg_set(int id, int size, u32 v)
{
    if (id >= 0 && id < 8) {
        if (size == 8) {
            if (id < 4) cpu.r[id] = (cpu.r[id] & 0xFFFFFF00UL) | (v & 0xFF);
            else cpu.r[id - 4] = (cpu.r[id - 4] & 0xFFFF00FFUL) | ((v & 0xFF) << 8);
            return;
        }
        if (size == 16) { cpu.r[id] = (cpu.r[id] & 0xFFFF0000UL) | (v & 0xFFFF); return; }
        cpu.r[id] = v;
        return;
    }
    if (id == 100) { cpu.eip = v & 0xFFFF; return; }
    if (id == 101) { cpu.flags = (cpu.flags & 0xFFFF0000UL) | (v & 0xFFFF); return; }
    if (id >= 200 && id < 206) { cpu.s[id - 200] = (u16)v; return; }
}


static const char *ep;
static u32 ev_expr(void);

static void ev_skip(void) { while (*ep == ' ' || *ep == 9) ep++; }

static u32 ev_primary(void)
{
    u32 v = 0;
    int size, id;
    char name[40];
    int n = 0;

    ev_skip();
    if (*ep == '(') {
        ep++;
        v = ev_expr();
        ev_skip();
        if (*ep == ')') ep++;
        return v;
    }
    if (*ep == '-') { ep++; return (u32)(-(s32)ev_primary()); }
    if (*ep == '+') { ep++; return ev_primary(); }
    if (*ep == '#') { ep++; return psp_seg; }
    if (*ep == '$') { ep++; return (u16)cpu.eip; }
    if (*ep == '\'') {
        ep++;
        if (*ep) v = (u8)*ep++;
        if (*ep == '\'') ep++;
        return v;
    }
    while ((isalnum(*ep) || *ep == '_' || *ep == '@') && n < 39) name[n++] = *ep++;
    name[n] = 0;
    if (!n) { ep++; return 0; }
    {
        int i, allhex = 1;
        if (n >= 2 && name[0] == '0' && (name[1] == 'x' || name[1] == 'X')) {
            for (i = 2; i < n; i++) {
                int c = toupper(name[i]);
                v = v * 16 + (c <= '9' ? c - '0' : c - 'A' + 10);
            }
            return v;
        }
        for (i = 0; i < n; i++)
            if (!isxdigit(name[i])) { allhex = 0; break; }
        if (allhex) {
            for (i = 0; i < n; i++) {
                int c = toupper(name[i]);
                v = v * 16 + (c <= '9' ? c - '0' : c - 'A' + 10);
            }
            return v;
        }
    }
    id = reg_find(name, &size);
    if (id >= 0) return reg_get(id, size);
    {
        u16 sg, of;
        if (sym_by_name(name, &sg, &of)) return of;
    }
    return 0;
}

static u32 ev_expr(void)
{
    u32 v = ev_primary();
    for (;;) {
        ev_skip();
        if (*ep == '+') { ep++; v += ev_primary(); }
        else if (*ep == '*') { ep++; v *= ev_primary(); }
        else if (*ep == '/') { ep++; { u32 d = ev_primary(); if (d) v /= d; } }
        else break;
    }
    return v;
}

int eval_expr(const char *s, u32 *out)
{
    ep = s;
    *out = ev_expr();
    ev_skip();
    return (*ep == 0) ? 0 : 1;
}

int parse_addr_ex(const char *s, u16 *seg, u16 *off, int defseg, int *has_seg)
{
    char buf[80];
    char *p;
    int bracket = 0;
    const char *q = s;
    u32 v1, v2;

    *has_seg = 0;
    while (*q == ' ') q++;
    strncpy(buf, q, 79); buf[79] = 0;
    for (p = buf; *p; p++) {
        if (*p == '[') bracket = 1;
        else if (*p == ']') bracket = 0;
        else if (*p == ':' && !bracket) break;
    }
    if (!*p) return parse_addr(s, seg, off, defseg);
    *p = 0;
    if (eval_expr(buf, &v1)) return 1;
    if (eval_expr(p + 1, &v2)) return 1;
    *seg = (u16)v1; *off = (u16)v2; *has_seg = 1;
    return 0;
}

int parse_addr(const char *s, u16 *seg, u16 *off, int defseg)
{
    char buf[80];
    char *p;
    u32 v;
    int k = 0;
    const char *q = s;
    int bracket = 0;

    while (*q == ' ') q++;
    if (!*q) return 1;
    strncpy(buf, q, 79); buf[79] = 0;

    for (p = buf; *p; p++) {
        if (*p == '[') bracket = 1;
        else if (*p == ']') bracket = 0;
        else if (*p == ':' && !bracket) { k = 1; break; }
    }
    if (k) {
        *p = 0;
        if (eval_expr(buf, &v)) return 1;
        *seg = (u16)v;
        if (eval_expr(p + 1, &v)) return 1;
        *off = (u16)v;
    } else {
        if (eval_expr(buf, &v)) return 1;
        *off = (u16)v;
        if (defseg >= 0) *seg = (u16)defseg;
        else {
            u16 sg, of;
            char nm[40];
            int n = 0;
            const char *r = s;
            while (*r == ' ') r++;
            while ((isalnum(*r) || *r == '_') && n < 39) nm[n++] = *r++;
            nm[n] = 0;
            if (sym_by_name(nm, &sg, &of)) *seg = sg;
            else *seg = cpu.s[S_DS];
        }
    }
    return 0;
}


int cond_eval(const char *cond)
{
    const char *p = cond;
    while (*p) {
        char name[16], op[3];
        int n = 0, o = 0, size, id;
        u32 rhs, lhs;
        int ok = 0;

        while (*p == ' ' || *p == 9 || *p == ',') p++;
        if (!*p) break;
        while ((isalnum(*p) || *p == '_') && n < 15) name[n++] = *p++;
        name[n] = 0;
        if (!n) { p++; continue; }
        while (*p == ' ' || *p == 9) p++;
        while ((*p == '=' || *p == '<' || *p == '>' || *p == '!') && o < 2) op[o++] = *p++;
        op[o] = 0;
        while (*p == ' ' || *p == 9) p++;
        {
            char val[64];
            int vn = 0;
            int br = 0;
            while (*p && vn < 63) {
                if (*p == '[') br++;
                if (*p == ']') br--;
                if ((*p == ' ' || *p == 9) && br == 0) break;
                val[vn++] = *p++;
            }
            val[vn] = 0;
            if (eval_expr(val, &rhs)) return 0;
        }
        id = reg_find(name, &size);
        if (id < 0) return 0;
        lhs = reg_get(id, size);
        if (!op[0] || strcmp(op, "=") == 0 || strcmp(op, "==") == 0) ok = (lhs == rhs);
        else if (strcmp(op, "!=") == 0 || strcmp(op, "<>") == 0) ok = (lhs != rhs);
        else if (strcmp(op, ">") == 0) ok = (lhs > rhs);
        else if (strcmp(op, "<") == 0) ok = (lhs < rhs);
        else if (strcmp(op, ">=") == 0) ok = (lhs >= rhs);
        else if (strcmp(op, "<=") == 0) ok = (lhs <= rhs);
        if (!ok) return 0;
    }
    return 1;
}


static void snapshot_regs(void)
{
    regs_before = cpu;
}

int bp_pre_check(void)
{
    int i;
    u8 op;
    snapshot_regs();
    if (no_break) return 0;
    for (i = 0; i < nbps; i++) {
        if (!bps[i].enabled) continue;
        if (bps[i].type == BP_EXEC) {
            if ((bps[i].anyseg ? (bps[i].off == (u16)cpu.eip)
                               : (bps[i].seg == cpu.s[S_CS] && bps[i].off == (u16)cpu.eip))) {
                if (bps[i].has_cond && !cond_eval(bps[i].cond)) continue;
                bp_break_hit = i;
                return 1;
            }
        } else if (bps[i].type == BP_XB) {
            int k, match = 1;
            for (k = 0; k < bps[i].nbytes; k++)
                if (rd8(cpu.s[S_CS], (u16)(cpu.eip + k)) != bps[i].bytes[k]) { match = 0; break; }
            if (match) {
                if (bps[i].has_cond && !cond_eval(bps[i].cond)) continue;
                bp_break_hit = i;
                return 1;
            }
        } else if (bps[i].type == BP_INT) {
            op = rd8(cpu.s[S_CS], (u16)cpu.eip);
            if (op == 0xCD) {
                if (rd8(cpu.s[S_CS], (u16)(cpu.eip + 1)) == (u8)bps[i].off) {
                    if (bps[i].has_cond && !cond_eval(bps[i].cond)) continue;
                    bp_break_hit = i;
                    return 1;
                }
            } else if (op == 0xCC && bps[i].off == 3) {
                if (bps[i].has_cond && !cond_eval(bps[i].cond)) continue;
                bp_break_hit = i;
                return 1;
            }
        }
    }
    return 0;
}

int bp_io_check(u16 port, int wr)
{
    int i;
    if (no_break) return 0;
    for (i = 0; i < nbps; i++) {
        if (!bps[i].enabled || bps[i].type != BP_IO) continue;
        if (bps[i].off != port) continue;
        if (bps[i].seg && bps[i].seg != (u16)wr) continue;
        if (bps[i].has_cond && !cond_eval(bps[i].cond)) continue;
        bp_break_hit = i;
        return 1;
    }
    return 0;
}

int bp_post_check(u16 start_cs, u16 start_ip)
{
    int i;
    if (no_break) return 0;
    for (i = 0; i < nbps; i++) {
        if (!bps[i].enabled) continue;
        if (bps[i].type == BP_REG) {
            int size, id = bps[i].reg;
            u32 now, oldv;
            if (id < 0) continue;
            size = bps[i].nbytes ? bps[i].nbytes : 16;
            now = reg_get(id, size);
            if (id >= 0 && id < 8) {
                u32 v = regs_before.r[id];
                if (size == 8) oldv = (id < 4) ? (v & 0xFF) : ((v >> 8) & 0xFF);
                else if (size == 16) oldv = v & 0xFFFF;
                else oldv = v;
            } else if (id == 100) {
                oldv = (u16)regs_before.eip;
            } else if (id == 101) {
                oldv = regs_before.flags & 0xFFFF;
            } else if (id >= 200 && id < 206) {
                oldv = regs_before.s[id - 200];
            } else {
                oldv = 0;
            }
            if (size == 16) oldv &= 0xFFFF;
            if (now != oldv) {
                if (bps[i].has_cond && !cond_eval(bps[i].cond)) continue;
                bp_break_hit = i;
                return 1;
            }
        } else if (bps[i].type == BP_KNL) {
            if (cpu.s[S_CS] != start_cs) {
                if (bps[i].has_cond && !cond_eval(bps[i].cond)) continue;
                bp_break_hit = i;
                return 1;
            }
        }
    }
    return 0;
}
