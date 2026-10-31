#include "tr.h"

u16 psp_seg = 0;
u16 img_seg = 0;
u32 img_size = 0;
u16 img_minalloc = 0;
u16 img_maxalloc = 0xFFFF;
int prog_type = 0;
char prog_name[128] = "";
int child_psp_set = 0;
u16 img_load_seg = 0;
u16 img_alloc_paras = 0;
u16 dbg_top_seg = 0;
u16 prog_top_seg = 0;
u16 pool_top_seg = 0;
u16 pool_limit_seg = 0;

static u8 far *hdr_buf = 0;


static u8  exe_hdr[0x20];
static int have_exe = 0;
static u16 exe_nrel = 0;
static u16 exe_cs = 0, exe_ip = 0, exe_ss = 0, exe_sp = 0;
static u16 exe_minalloc = 0;
static u16 exe_reloff = 0;
static u8 far *exe_rel = 0;

struct AllocBlk { u16 seg; u8 far *p; };

static u8 iobounce[0x2000];
u32 far_read(FILE *f, u8 far *dst, u32 n)
{
    u32 got = 0;
    while (got < n) {
        u32 want = n - got;
        size_t r;
        if (want > (u32)sizeof iobounce) want = (u32)sizeof iobounce;
        r = fread(iobounce, 1, (unsigned)want, f);
        if (!r) break;
        _fmemcpy(dst + got, iobounce, r);
        got += (u32)r;
        if ((u32)r < want) break;
    }
    return got;
}
static struct AllocBlk alloblk[16];
static int nalloblk = 0;
int notice_overlay = 0;

static u16 alloc_paras(u16 paras)
{
    u32 bytes = (u32)paras * 16UL + 32UL;
    u8 far *p;
    u32 lin;
    u16 seg;

    if (nalloblk >= 16) return 0;
    p = (u8 far *)farmalloc(bytes);
    if (!p) return 0;
    lin = ((u32)FP_SEG(p) << 4) + (u16)FP_OFF(p);
    lin = (lin + 15UL) & ~15UL;
    seg = (u16)(lin >> 4);
    alloblk[nalloblk].seg = seg;
    alloblk[nalloblk].p = p;
    nalloblk++;
    return seg;
}

void free_paras(u16 seg)
{
    int i;
    for (i = 0; i < nalloblk; i++) {
        if (alloblk[i].seg == seg) {
            farfree(alloblk[i].p);
            alloblk[i] = alloblk[nalloblk - 1];
            nalloblk--;
            return;
        }
    }
}

void free_paras(u16 seg);

static u16 alloc_program_mem(u16 need, u16 *got)
{
    static u16 tries[6] = { 0x9000, 0x8000, 0x7000, 0x6000, 0x4000, 0x2000 };
    int i;
    u16 seg;
    for (i = 0; i < 6; i++) {
        if (tries[i] < need) continue;
        seg = alloc_paras(tries[i]);
        if (seg) { *got = tries[i]; return seg; }
    }
    seg = alloc_paras(need);
    if (seg) { *got = need; return seg; }
    return 0;
}

void free_program(void)
{
    if (psp_seg) {

        psp_seg = 0;
        img_seg = 0;
    }
}

void set_filename(const char *name)
{
    strncpy(prog_name, name, 127);
    prog_name[127] = 0;
}

static u16 env_seg = 0;

static u16 build_env(void)
{
    union REGS r;
    u16 parent, es;
    u8 far *src;
    u8 far *p;
    u16 o, i;
    const char *s;

    if (!env_seg) {
        env_seg = alloc_paras(0x100);
        if (!env_seg) return 0;
    }
    r.x.ax = 0x5100;
    int86(0x21, &r, &r);
    parent = r.x.bx;
    es = *(u16 far *)MK_FP(parent, 0x2C);
    p = (u8 far *)mem_ptr(env_seg, 0);
    o = 0;
    if (es) {
        src = (u8 far *)MK_FP(es, 0);
        for (i = 0; o < 0x0FE0; i++) {
            u8 c = src[i];
            if (c == 0 && src[i + 1] == 0) break;
            p[o++] = c;
        }
        p[o++] = 0;
    } else {
        s = "PATH=.";
        while (*s && o < 0x0FE0) p[o++] = *s++;
        p[o++] = 0;
    }
    p[o++] = 0;
    p[o++] = 1; p[o++] = 0;
    s = prog_name;
    while (*s && o < 0x0FF0) p[o++] = *s++;
    p[o++] = 0;
    p[o] = 0;
    return env_seg;
}

static void build_psp(u16 seg, u16 paras)
{
    union REGS r;
    struct SREGS sr;
    u16 parent;


    r.x.ax = 0x5100;
    int86(0x21, &r, &r);
    parent = r.x.bx;

    r.x.ax = 0x2600;
    r.x.dx = seg;
    int86(0x21, &r, &r);

    wr16(seg, 0x00, 0x20CD);
    wr16(seg, 0x02, (u16)(seg + paras));
    wr8(seg, 0x05, 0x9A);
    wr16(seg, 0x06, 0xFEF0);
    wr16(seg, 0x08, 0x1DF0);
    wr16(seg, 0x0A, *(u16 far *)MK_FP(0, 0x22 * 4));
    wr16(seg, 0x0C, *(u16 far *)MK_FP(0, 0x22 * 4 + 2));
    wr16(seg, 0x0E, *(u16 far *)MK_FP(0, 0x23 * 4));
    wr16(seg, 0x10, *(u16 far *)MK_FP(0, 0x23 * 4 + 2));
    wr16(seg, 0x12, *(u16 far *)MK_FP(0, 0x24 * 4));
    wr16(seg, 0x14, *(u16 far *)MK_FP(0, 0x24 * 4 + 2));
    wr16(seg, 0x16, parent);
    wr16(seg, 0x2C, build_env() ? env_seg : *(u16 far *)MK_FP(parent, 0x2C));

    {
        u16 psize = *(u16 far *)MK_FP(parent, 0x18);
        u16 pptr = *(u16 far *)MK_FP(parent, 0x34);
        u16 i, n;
        if (psize == 0 || psize > 0x40) psize = 0x14;
        n = (psize < 0x14) ? psize : 0x14;
        for (i = 0; i < n; i++)
            wr8(seg, (u16)(0x18 + i), rd8(parent, (u16)(pptr + i)));
        wr16(seg, 0x18, n);
        wr16(seg, 0x34, 0x18);
        wr16(seg, 0x32, n);
    }
    wr8(seg, 0x50, 0xCD);
    wr8(seg, 0x51, 0x21);
    wr8(seg, 0x52, 0xCB);
    {
        u8 far *own = (u8 far *)MK_FP(parent, 0x80);
        static u8 tbuf[132];
        unsigned len, i, j;
        len = own[0];
        i = 0;
        while (i < len && own[1 + i] == ' ')
            i++;
        while (i < len && own[1 + i] != ' ' && own[1 + i] != 0x0D)
            i++;
        j = 0;
        while (i < len && j < 125 && own[1 + i] != 0x0D)
            tbuf[j++] = own[1 + i++];
        while (j && (tbuf[j - 1] == ' ' || tbuf[j - 1] == '\t'))
            j--;
        wr8(seg, 0x80, (u8)j);
        for (i = 0; i < j; i++) wr8(seg, (u16)(0x81 + i), tbuf[i]);
        wr8(seg, (u16)(0x81 + j), 0x0D);
        for (i = 0x82 + j; i < 0x90; i++) wr8(seg, (u16)i, 0);
        if (j) {
            i = 0;
            while (i < j && tbuf[i] == ' ') i++;
            while (i < j && tbuf[i] != ' ') i++;
            if (i < j) i++;
            {
                unsigned k = 0;

                while (i + k < j && k < 0x6E)
                    wr8(seg, (u16)(0x90 + k), tbuf[i + k]), k++;
                wr8(seg, (u16)(0x90 + k), 0x0D);
                wr8(seg, (u16)(0x91 + k), 0x00);
            }
        } else {
            wr8(seg, 0x90, 0x00);
            wr8(seg, 0x91, 0x00);
        }
    }
    r.x.ax = 0x1A00;
    r.x.dx = 0x80;
    sr.ds = seg;
    int86x(0x21, &r, &r, &sr);
    child_psp_set = 0;
}

static int read_header(FILE *f, u16 *hdr_paras, u32 *imgsize, u16 *reloc_off,
                       u16 *nreloc, u16 *cs, u16 *ip, u16 *ss, u16 *sp, u16 *minalloc)
{
    u8 h[0x1C];
    u16 lfarlc;
    int n;

    if (fseek(f, 0, SEEK_SET) != 0) return 1;
    n = fread(h, 1, 0x1C, f);
    if (n < 0x1C) return 1;
    if (h[0] != 'M' || h[1] != 'Z') return 2;
    {
        u16 e_cblp = h[2] | (h[3] << 8);
        u16 e_cp = h[4] | (h[5] << 8);
        u16 e_crlc = h[6] | (h[7] << 8);
        u16 e_cparhdr = h[8] | (h[9] << 8);
        u16 e_minalloc = h[0x0A] | (h[0x0B] << 8);
        u16 e_maxalloc = h[0x0C] | (h[0x0D] << 8);
        u16 e_ss = h[0x0E] | (h[0x0F] << 8);
        u16 e_sp = h[0x10] | (h[0x11] << 8);
        u16 e_ip = h[0x14] | (h[0x15] << 8);
        u16 e_cs = h[0x16] | (h[0x17] << 8);
        lfarlc = h[0x18] | (h[0x19] << 8);
        *hdr_paras = e_cparhdr;
        {
            u32 fsize = (u32)(e_cp - 1) * 512UL + (e_cblp ? e_cblp : 512);
            u32 hsize = (u32)e_cparhdr * 16UL;
            *imgsize = (fsize > hsize) ? (fsize - hsize) : 0;
        }
        *reloc_off = lfarlc;
        *nreloc = e_crlc;
        *cs = e_cs; *ip = e_ip; *ss = e_ss; *sp = e_sp;
        *minalloc = e_minalloc;
        img_maxalloc = e_maxalloc;
        (void)e_maxalloc;
    }
    return 0;
}

int load_exe(const char *name)
{
    FILE *f;
    u16 hdr_paras, reloc_off, nreloc, cs, ip, ss, sp, minalloc;
    u16 need, paras, i;
    u32 imgsize;
    u16 seg;
    f = fopen(name, "rb");
    if (!f) return 1;
    if (read_header(f, &hdr_paras, &imgsize, &reloc_off, &nreloc, &cs, &ip, &ss, &sp, &minalloc)) {
        fclose(f); return 2;
    }
    have_exe = 1;
    exe_cs = cs; exe_ip = ip; exe_ss = ss; exe_sp = sp;
    exe_minalloc = minalloc; exe_nrel = nreloc; exe_reloff = reloc_off;
    fseek(f, 0, SEEK_SET);
    fread(exe_hdr, 1, 0x20, f);
    if (exe_rel) { farfree(exe_rel); exe_rel = 0; }
    if (nreloc && nreloc < 1024) {
        u32 rb = (u32)nreloc * 4UL;
        exe_rel = (u8 far *)farmalloc(rb ? rb : 1);
        if (exe_rel) {
            fseek(f, (long)reloc_off, SEEK_SET);
            {
                u32 got = 0;
                while (got < rb) {
                    u32 ch = rb - got;
                    if (ch > 0x2000UL) ch = 0x2000UL;
                    if (far_read(f, &exe_rel[got], ch) != ch) break;
                    got += ch;
                }
            }
        }
    }
    if (imgsize == 0) imgsize = 16;
    need = (u16)(imgsize / 16 + 1 + minalloc + 0x10);
    paras = need;
    if (paras < 0x800) paras = 0x800;
    if (paras > 0x9000) paras = 0x9000;
    seg = alloc_program_mem(paras, &paras);
    if (!seg) { fclose(f); return 3; }
    if (!(seg & 1) && paras > 1) { seg++; paras--; }

    psp_seg = img_seg = seg;
    img_alloc_paras = paras;
    dbg_top_seg = (u16)(seg + paras);
    img_load_seg = seg + 0x10;
    _fmemset(mem_ptr(seg, 0), 0, (size_t)((u32)paras * 16UL > 0xF000UL ? 0xF000UL : (u32)paras * 16UL));
    build_psp(seg, paras);

    if (fseek(f, (long)hdr_paras * 16L, SEEK_SET) != 0) { fclose(f); return 1; }
    {
        u32 got = 0;
        while (got < imgsize) {
            u32 chunk = imgsize - got;
            u16 dseg, doff;
            if (chunk > 0x4000UL) chunk = 0x4000UL;
            dseg = (u16)(img_load_seg + (u16)(got >> 4));
            doff = (u16)(got & 0x0F);
            if ((u32)doff + chunk > 0x10000UL) chunk = 0x10000UL - doff;
            if (far_read(f, mem_ptr(dseg, doff), chunk) != chunk) break;
            got += chunk;
        }
        img_size = got;
    }
    {
        u32 fsz, need_sz;
        long saved = ftell(f);
        if (fseek(f, 0L, SEEK_END) == 0) fsz = (u32)ftell(f);
        else fsz = 0;
        fseek(f, saved, SEEK_SET);
        need_sz = 32UL + (u32)nreloc * 16UL + imgsize;
        notice_overlay = (fsz > need_sz) ? 1 : 0;

    }

    for (i = 0; i < nreloc; i++) {
        u16 ro, rs;
        if (fseek(f, (long)reloc_off + (long)i * 4L, SEEK_SET) != 0) break;
        if (fread(&ro, 2, 1, f) != 1) break;
        if (fread(&rs, 2, 1, f) != 1) break;
        {
            u16 v = rd16((u16)(img_load_seg + rs), ro);
            wr16((u16)(img_load_seg + rs), ro, (u16)(v + img_load_seg));
        }
    }
    fclose(f);
    {
        u16 total = img_alloc_paras;
        u16 imgp = (u16)(((img_size + 15) / 16) + 0x10);
        u16 prog = (u16)(imgp + 0x1000);
        if (prog >= total - 0x100) prog = (u16)(total / 2);
        prog_top_seg = (u16)(psp_seg + prog);
        pool_top_seg = prog_top_seg;
        pool_limit_seg = (u16)(psp_seg + total);
        wr16(psp_seg, 0x02, prog_top_seg);
    }

    memset(&cpu, 0, sizeof cpu);
    cpu.s[S_CS] = (u16)(img_load_seg + cs);
    cpu.eip = ip;
    cpu.s[S_SS] = (u16)(img_load_seg + ss);
    cpu.r[R_SP] = sp;
    cpu.s[S_DS] = seg;
    cpu.s[S_ES] = seg;
    cpu.r[R_DX] = seg;
    cpu.r[R_SI] = 0x0100;
    code_base = 0;
    code_base_cs = 0xFFFF;
    cpu.flags = 0x0282;
    img_minalloc = minalloc;
    prog_type = 2;
    strncpy(prog_name, name, 127);
    cmt_load();
    return 0;
}

int load_com(const char *name)
{
    FILE *f;
    long flen;
    u16 paras, seg;
    u32 got = 0;

    f = fopen(name, "rb");
    if (!f) return 1;
    fseek(f, 0, SEEK_END);
    flen = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (flen <= 0 || flen > 0x10000L) { fclose(f); return 2; }

    paras = (u16)((flen + 0x100L + 15) / 16 + 0x400);
    if (paras > 0x9000) paras = 0x9000;
    seg = alloc_program_mem(paras, &paras);
    if (!seg) { fclose(f); return 3; }

    psp_seg = img_seg = seg;
    img_alloc_paras = paras;
    dbg_top_seg = (u16)(seg + paras);
    img_load_seg = seg;
    build_psp(seg, paras);

    {
        u8 far *p = mem_ptr(seg, 0x100);
        got = far_read(f, p, flen);
    }
    fclose(f);
    img_size = got;
    {
        u16 total = img_alloc_paras;
        u16 imgp = (u16)((img_size + 15) / 16 + 0x10);
        u16 prog = (u16)(imgp + 0x1000);
        if (prog >= total - 0x100) prog = (u16)(total / 2);
        prog_top_seg = (u16)(seg + prog);
        pool_top_seg = prog_top_seg;
        pool_limit_seg = (u16)(seg + total);
        wr16(seg, 0x02, prog_top_seg);
    }

    memset(&cpu, 0, sizeof cpu);
    cpu.s[S_CS] = seg;
    cpu.s[S_DS] = seg;
    cpu.s[S_ES] = seg;
    cpu.s[S_SS] = seg;
    cpu.eip = 0x100;
    cpu.r[R_SP] = 0xFFFE;
    cpu.r[R_DX] = seg;
    cpu.r[R_SI] = 0x0100;
    code_base = 0x00FC;
    code_base_cs = seg;
    wr16(seg, 0xFFFE, 0);
    cpu.flags = 0x0282;
    img_minalloc = 0;
    prog_type = 1;
    strncpy(prog_name, name, 127);
    cmt_load();
    return 0;
}

int load_program(const char *name)
{
    int r;
    FILE *f;
    u8 b[2];

    free_program();
    sym_init();
    strncpy(prog_name, name, 127);
    prog_name[127] = 0;
    if (!env_seg) build_env();
    f = fopen(name, "rb");
    if (!f) return 1;
    if (fread(b, 1, 2, f) != 2) { fclose(f); return 2; }
    fclose(f);
    if (b[0] == 'M' && b[1] == 'Z') r = load_exe(name);
    else r = load_com(name);
    if (r == 0) saved_cpu = cpu;
    return r;
}

static void set_name_ext(char *dst, const char *src, const char *ext)
{
    char *p;
    strcpy(dst, src);
    p = strrchr(dst, '.');
    if (p) strcpy(p, ext);
    else strcat(dst, ext);
}

int write_com(const char *name, u16 seg, u32 len)
{
    FILE *f;
    u8 far *p;
    u32 i;
    char fn[80];
    if (!name || !*name) { strcpy(fn, "OUT.COM"); } else set_name_ext(fn, name, ".COM");
    f = fopen(fn, "wb");
    if (!f) return 1;
    for (i = 0; i < len; i++) fputc(*mem_off(seg, (u32)0x100 + i), f);
    fclose(f);
    return 0;
}

int write_exe_image(const char *name)
{
    FILE *f;
    char fn[80];
    unsigned hdr_bytes, hdr_paras, img_paras;
    unsigned size, tail;
    u32 i;
    u8 h[0x20];

    if (!have_exe || !psp_seg) return 2;
    if (!name || !*name) strcpy(fn, "OUT.EXE"); else set_name_ext(fn, name, ".EXE");
    hdr_bytes = 0x20 + (unsigned)exe_nrel * 4;
    hdr_paras = (hdr_bytes + 15) / 16;
    img_paras = (unsigned)((img_size + 15) / 16);
    size = hdr_paras * 16 + img_paras * 16;
    tail = size;
    (void)tail;


    for (i = 0; i < exe_nrel; i++) {
        u16 ro, rs, v;
        u8 far *p = exe_rel;
        ro = p[i * 4] | (p[i * 4 + 1] << 8);
        rs = p[i * 4 + 2] | (p[i * 4 + 3] << 8);
        v = rd16((u16)(img_load_seg + rs), ro);
        wr16((u16)(img_load_seg + rs), ro, (u16)(v - img_load_seg));
    }

    f = fopen(fn, "wb");
    if (!f) {
        for (i = 0; i < exe_nrel; i++) {
            u16 ro, rs, v;
            u8 far *p = exe_rel;
            ro = p[i * 4] | (p[i * 4 + 1] << 8);
            rs = p[i * 4 + 2] | (p[i * 4 + 3] << 8);
            v = rd16((u16)(img_load_seg + rs), ro);
            wr16((u16)(img_load_seg + rs), ro, (u16)(v + img_load_seg));
        }
        return 1;
    }
    memcpy(h, exe_hdr, 0x20);
    h[2] = (u8)(size & 0xFF); h[3] = (u8)(size >> 8);
    {
        unsigned pages = (size + 511) / 512;
        h[4] = (u8)(pages & 0xFF); h[5] = (u8)(pages >> 8);
    }
    h[6] = (u8)(exe_nrel & 0xFF); h[7] = (u8)(exe_nrel >> 8);
    h[8] = (u8)(hdr_paras & 0xFF); h[9] = (u8)(hdr_paras >> 8);
    h[0x0A] = (u8)(exe_minalloc & 0xFF); h[0x0B] = (u8)(exe_minalloc >> 8);
    h[0x0C] = 0xFF; h[0x0D] = 0xFF;
    h[0x0E] = (u8)(exe_ss & 0xFF); h[0x0F] = (u8)(exe_ss >> 8);
    h[0x10] = (u8)(exe_sp & 0xFF); h[0x11] = (u8)(exe_sp >> 8);
    h[0x14] = (u8)(exe_ip & 0xFF); h[0x15] = (u8)(exe_ip >> 8);
    h[0x16] = (u8)(exe_cs & 0xFF); h[0x17] = (u8)(exe_cs >> 8);
    h[0x18] = 0x20; h[0x19] = 0;
    h[0x1C] = 0; h[0x1D] = 0;
    fwrite(h, 1, 0x20, f);
    for (i = 0; i < exe_nrel; i++) {
        u16 ro, rs;
        u8 far *p = exe_rel;
        ro = p[i * 4] | (p[i * 4 + 1] << 8);
        rs = p[i * 4 + 2] | (p[i * 4 + 3] << 8);
        fputc((u8)ro, f); fputc((u8)(ro >> 8), f);
        fputc((u8)rs, f); fputc((u8)(rs >> 8), f);
    }
    for (i = 0x20 + (u32)exe_nrel * 4; i < (u32)hdr_paras * 16; i++) fputc(0, f);
    {
        for (i = 0; i < img_size; i++) fputc(*mem_off(img_load_seg, i), f);
    }
    for (i = img_size; i < (u32)img_paras * 16; i++) fputc(0, f);
    fclose(f);

    for (i = 0; i < exe_nrel; i++) {
        u16 ro, rs, v;
        u8 far *p = exe_rel;
        ro = p[i * 4] | (p[i * 4 + 1] << 8);
        rs = p[i * 4 + 2] | (p[i * 4 + 3] << 8);
        v = rd16((u16)(img_load_seg + rs), ro);
        wr16((u16)(img_load_seg + rs), ro, (u16)(v + img_load_seg));
    }
    return 0;
}

int write_exe(const char *name, u16 seg, u32 len, u16 cs, u16 ip, u16 ss, u16 sp)
{
    FILE *f;
    u8 far *p;
    u32 i;
    char fn[80];
    unsigned hdr_paras = 2;
    unsigned img_paras = (unsigned)((len + 15) / 16);
    unsigned size = (hdr_paras + img_paras) * 16;
    unsigned tail = (unsigned)((hdr_paras + img_paras) * 16 - len);

    if (!name || !*name) strcpy(fn, "OUT.EXE"); else set_name_ext(fn, name, ".EXE");
    f = fopen(fn, "wb");
    if (!f) return 1;

    fputc('M', f); fputc('Z', f);
    {
        u8 h[0x20];
        int k;
        unsigned lastpage = size % 512;
        unsigned pages = (size + 511) / 512;
        for (k = 0; k < 0x20; k++) h[k] = 0;
        h[2] = (u8)(lastpage & 0xFF); h[3] = (u8)(lastpage >> 8);
        h[4] = (u8)(pages & 0xFF); h[5] = (u8)(pages >> 8);
        h[6] = 0; h[7] = 0;
        h[8] = 2; h[9] = 0;
        h[0x0A] = 0x00; h[0x0B] = 0x10;
        h[0x0C] = 0xFF; h[0x0D] = 0xFF;
        h[0x0E] = (u8)(ss & 0xFF); h[0x0F] = (u8)(ss >> 8);
        h[0x10] = (u8)(sp & 0xFF); h[0x11] = (u8)(sp >> 8);
        h[0x14] = (u8)(ip & 0xFF); h[0x15] = (u8)(ip >> 8);
        h[0x16] = (u8)(cs & 0xFF); h[0x17] = (u8)(cs >> 8);
        h[0x18] = 0x20; h[0x19] = 0;
        h[0x1C] = 0; h[0x1D] = 0;
        for (k = 0; k < 0x20; k++) fputc(h[k], f);
    }
    (void)tail;
    p = mem_ptr(seg, 0);
    for (i = 0; i < len; i++) fputc(p[i], f);
    for (i = len; i < (u32)img_paras * 16UL; i++) fputc(0, f);
    fclose(f);
    return 0;
}
