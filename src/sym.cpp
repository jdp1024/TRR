#include "tr.h"

#define MAXSYM 192
#define MAXCMT 96

struct Sym { char name[32]; u16 seg, off; int used; };
struct Cmt { u16 seg, off; char text[60]; int used; };

static Sym syms[MAXSYM];
static Cmt cmts[MAXCMT];
static int nsyms, ncmts;

void sym_init(void)
{
    nsyms = ncmts = 0;
}

void sym_add(const char *name, u16 seg, u16 off)
{
    if (nsyms >= MAXSYM) return;
    strncpy(syms[nsyms].name, name, 31);
    syms[nsyms].name[31] = 0;
    syms[nsyms].seg = seg;
    syms[nsyms].off = off;
    syms[nsyms].used = 1;
    nsyms++;
}

const char *sym_find(u16 seg, u16 off, u16 *base)
{
    int i;
    for (i = 0; i < nsyms; i++) {
        if (syms[i].seg == seg && syms[i].off == off) {
            if (base) *base = syms[i].off;
            return syms[i].name;
        }
    }
    return 0;
}

int sym_by_name(const char *name, u16 *seg, u16 *off)
{
    int i;
    for (i = 0; i < nsyms; i++)
        if (strcmp(syms[i].name, name) == 0) {
            *seg = syms[i].seg; *off = syms[i].off; return 1;
        }
    return 0;
}

void label_add(const char *name, u16 seg, u16 off)
{
    sym_add(name, seg, off);
}

void cmt_add(u16 seg, u16 off, const char *text)
{
    int i;
    for (i = 0; i < ncmts; i++)
        if (cmts[i].used && cmts[i].seg == seg && cmts[i].off == off) {
            strncpy(cmts[i].text, text, 59);
            cmts[i].text[59] = 0;
            return;
        }
    if (ncmts >= MAXCMT) return;
    cmts[ncmts].seg = seg;
    cmts[ncmts].off = off;
    strncpy(cmts[ncmts].text, text, 59);
    cmts[ncmts].text[59] = 0;
    cmts[ncmts].used = 1;
    ncmts++;
}

const char *cmt_find(u16 seg, u16 off)
{
    int i;
    for (i = 0; i < ncmts; i++)
        if (cmts[i].used && cmts[i].seg == seg && cmts[i].off == off)
            return cmts[i].text;
    return 0;
}

void cmt_save(void)
{
    FILE *f;
    int i;
    char fn[80];
    if (prog_name[0]) {
        char *p;
        strcpy(fn, prog_name);
        p = strrchr(fn, '.');
        if (p) strcpy(p, ".cmt");
        else strcat(fn, ".cmt");
    } else strcpy(fn, "TR.CMT");
    f = fopen(fn, "w");
    if (!f) return;
    for (i = 0; i < ncmts; i++)
        if (cmts[i].used)
            fprintf(f, "%04X:%04X '%s'\n", cmts[i].seg, cmts[i].off, cmts[i].text);
    fclose(f);
}

void cmt_load(void)
{
    FILE *f;
    char fn[80], line[100], text[64];
    unsigned seg, off;
    if (prog_name[0]) {
        char *p;
        strcpy(fn, prog_name);
        p = strrchr(fn, '.');
        if (p) strcpy(p, ".cmt");
        else strcat(fn, ".cmt");
    } else strcpy(fn, "TR.CMT");
    f = fopen(fn, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "%x:%x '%63[^']'", &seg, &off, text) == 3)
            cmt_add((u16)seg, (u16)off, text);
    }
    fclose(f);
}

void sym_dump(void)
{
    int i, row = 0;
    char b[100];
    scr_fill(0, code_y, 80, code_h, ' ', 0x07);
    scr_puts(0, code_y, "Symbols", 0x70);
    for (i = 0; i < nsyms && row < code_h - 1; i++, row++) {
        sprintf(b, "%-24s %04X:%04X", syms[i].name, syms[i].seg, syms[i].off);
        scr_puts(0, code_y + 1 + row, b, 0x07);
    }
    if (nsyms == 0) scr_puts(0, code_y + 1, "(no symbols; use LABEL or SYM)", 0x07);
    hold_view = 1;
    show_status("Symbols");
}
