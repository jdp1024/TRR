#include "tr.h"

CPU cpu;
struct CPU saved_cpu;
int stop_reason = 0;
u16 stop_addr_seg, stop_addr_off;

static u32 step_ring[256];
static int step_ring_pos = 0;

u16 xms_off = 0, xms_seg = 0;
int xms_valid = 0;

u16 xc_ax, xc_bx, xc_cx, xc_dx, xc_si, xc_di, xc_ds, xc_es, xc_flags;
void far *xc_entry;

#ifdef __cplusplus
extern "C" {
#endif
void xms_call_far(void);
#ifdef __cplusplus
}
#endif

#define VFH_MAX 32
static int  vfh_host[VFH_MAX];
static char vfh_path[VFH_MAX][66];
static int  vfh_init = 0;

static void vfh_ensure(void)
{
    int i;
    if (vfh_init) return;
    for (i = 0; i < VFH_MAX; i++) { vfh_host[i] = -1; vfh_path[i][0] = 0; }
    vfh_init = 1;
}

static void vfh_gpath(u16 seg, u16 off, char *buf)
{
    int n;
    for (n = 0; n < 64; n++) {
        u8 c = rd8(seg, (u16)(off + n));
        if (c >= 'a' && c <= 'z') c = (u8)(c - 32);
        buf[n] = (char)c;
        if (!c) return;
    }
    buf[64] = 0;
}
static int vfh_find(const char *p)
{
    int i;
    vfh_ensure();
    for (i = 0; i < VFH_MAX; i++)
        if (vfh_host[i] >= 0 && strcmp(vfh_path[i], p) == 0) return i;
    return -1;
}
static int vfh_alloc(int host, const char *p)
{
    int i;
    vfh_ensure();
    for (i = 0; i < VFH_MAX; i++)
        if (vfh_host[i] < 0) {
            vfh_host[i] = host;
            strncpy(vfh_path[i], p, 64); vfh_path[i][64] = 0;
            return i;
        }
    return -1;
}
static void vfh_free(int i)
{
    vfh_ensure();
    if (i >= 0 && i < VFH_MAX) { vfh_host[i] = -1; vfh_path[i][0] = 0; }
}
static int vfh_get(int h)
{
    vfh_ensure();
    return (h >= 0 && h < VFH_MAX) ? vfh_host[h] : -1;
}
static void vfh_close_host(int host)
{
    union REGS c;
    c.x.ax = 0x3E00; c.x.bx = (u16)host;
    int86(0x21, &c, &c);
}
static void vfh_release_all(void)
{
    int i;
    vfh_ensure();
    for (i = 0; i < VFH_MAX; i++)
        if (vfh_host[i] >= 0) { vfh_close_host(vfh_host[i]); vfh_free(i); }
}

static void call_xms(void)
{
    xc_ax = (u16)cpu.r[R_AX];
    xc_bx = (u16)cpu.r[R_BX];
    xc_cx = (u16)cpu.r[R_CX];
    xc_dx = (u16)cpu.r[R_DX];
    xc_si = (u16)cpu.r[R_SI];
    xc_di = (u16)cpu.r[R_DI];
    xc_ds = cpu.s[S_DS];
    xc_es = cpu.s[S_ES];
    xc_entry = MK_FP(xms_seg, xms_off);
    xms_call_far();
    cpu.r[R_AX] = (cpu.r[R_AX] & 0xFFFF0000UL) | xc_ax;
    cpu.r[R_DX] = (cpu.r[R_DX] & 0xFFFF0000UL) | xc_dx;
    cpu.flags = (cpu.flags & 0xFFFF0000UL) | (xc_flags | 2);
}


void far_call_host(u16 seg, u16 off)
{
    xms_seg = seg;
    xms_off = off;
    call_xms();
}

void dump_step_ring(void)
{
    FILE *f = fopen("HANG.LOG", "a");
    int i;
    if (!f) return;
    fprintf(f, "--- ring dump ---\n");
    for (i = 0; i < 256; i++) {
        int idx = (step_ring_pos + i) & 255;
        u32 v = step_ring[idx];
        u16 sg = (u16)(v >> 16), of = (u16)v;
        int k;
        fprintf(f, "%04X:%04X  ", sg, of);
        for (k = 0; k < 8; k++) fprintf(f, "%02X ", rd8(sg, (u16)(of + k)));
        fprintf(f, "\n");
    }
    fclose(f);
}

void xms_call(void)
{
    call_xms();
}

static int do_int21_file(union REGS *in, union REGS *out, struct SREGS *sr, u16 ax)
{
    u8 ah = (u8)(ax >> 8);
    int hi, host;
    char path[66];

    switch (ah) {
    case 0x3C: case 0x3D: case 0x5A: case 0x6C: {
        vfh_gpath(sr->ds, (u16)in->x.dx, path);
        if (vfh_find(path) >= 0) {
            *out = *in;
            out->x.ax = 0x0005;
            out->x.flags = (u16)(out->x.flags | 1);
            return 1;
        }
        int86x(0x21, in, out, sr);
        if (!(out->x.flags & 1)) {
            hi = vfh_alloc((int)(u16)out->x.ax, path);
            if (hi < 0) {
                vfh_close_host((int)(u16)out->x.ax);
                out->x.ax = 0x0004;
                out->x.flags = (u16)(out->x.flags | 1);
            } else
                out->x.ax = (u16)hi;
        }
        return 1;
    }
    case 0x3E: case 0x6D: {
        hi = (int)(u16)in->x.bx;
        host = vfh_get(hi);
        *out = *in;
        if (host < 0) {
            if (hi > 4) {
                out->x.ax = 0x0006;
                out->x.flags = (u16)(out->x.flags | 1);
            } else
                out->x.flags = (u16)(out->x.flags & ~1u);
            return 1;
        }
        in->x.bx = (u16)host;
        int86x(0x21, in, out, sr);
        vfh_free(hi);
        return 1;
    }
    case 0x45: {
        hi = (int)(u16)in->x.bx;
        host = vfh_get(hi);
        if (host < 0) {
            if (hi > 4) {
                *out = *in;
                out->x.ax = 0x0006;
                out->x.flags = (u16)(out->x.flags | 1);
                return 1;
            }
            int86x(0x21, in, out, sr);
            return 1;
        }
        in->x.bx = (u16)host;
        int86x(0x21, in, out, sr);
        if (!(out->x.flags & 1)) {
            char p2[66];
            int nh;
            strncpy(p2, vfh_path[hi], 65); p2[65] = 0;
            nh = vfh_alloc((int)(u16)out->x.ax, p2);
            if (nh < 0) {
                vfh_close_host((int)(u16)out->x.ax);
                out->x.ax = 0x0004;
                out->x.flags = (u16)(out->x.flags | 1);
            } else
                out->x.ax = (u16)nh;
        }
        return 1;
    }
    case 0x3F: case 0x40: case 0x42: case 0x44: case 0x57: {
        hi = (int)(u16)in->x.bx;
        host = vfh_get(hi);
        if (host < 0 && hi > 4) {
            *out = *in;
            out->x.ax = 0x0006;
            out->x.flags = (u16)(out->x.flags | 1);
            return 1;
        }
        if (host >= 0) in->x.bx = (u16)host;
        int86x(0x21, in, out, sr);
        return 1;
    }
    }
    return 0;
}

static void do_int(int n)
{
    union REGS in, out;
    struct SREGS sr;
    u16 ax;
    in.x.ax = (u16)cpu.r[R_AX];
    in.x.bx = (u16)cpu.r[R_BX];
    in.x.cx = (u16)cpu.r[R_CX];
    in.x.dx = (u16)cpu.r[R_DX];
    in.x.si = (u16)cpu.r[R_SI];
    in.x.di = (u16)cpu.r[R_DI];
    in.x.flags = (u16)cpu.flags;
    sr.es = cpu.s[S_ES];
    sr.ds = cpu.s[S_DS];
    sr.ss = cpu.s[S_SS];
    sr.cs = cpu.s[S_CS];

    ax = in.x.ax;
    if (n == 0x20) { vfh_release_all(); stop_reason = 1; return; }
    if (n == 0x1A) {
        u8 fn = (u8)(ax >> 8);
        if (fn == 0x00) {
            u16 lo = *(u16 far *)MK_FP(0x40, 0x6C);
            u16 hi = *(u16 far *)MK_FP(0x40, 0x6E);
            cpu.r[R_AX] = (cpu.r[R_AX] & 0xFFFF0000UL);
            cpu.r[R_CX] = (cpu.r[R_CX] & 0xFFFF0000UL) | hi;
            cpu.r[R_DX] = (cpu.r[R_DX] & 0xFFFF0000UL) | lo;
            cpu.flags &= ~F_CF;
            return;
        }
        if (fn == 0x01) {
            *(u16 far *)MK_FP(0x40, 0x6C) = (u16)cpu.r[R_DX];
            *(u16 far *)MK_FP(0x40, 0x6E) = (u16)cpu.r[R_CX];
            cpu.flags &= ~F_CF;
            return;
        }
    }
    if (n == 0x21) {
        u8 ah = (u8)(ax >> 8);
        if (ah == 0x36) {
            cpu.r[R_AX] = (cpu.r[R_AX] & 0xFFFF0000UL);
            cpu.flags |= F_CF;
            return;
        }
        if (ah == 0x30) {
            cpu.r[R_AX] = (cpu.r[R_AX] & 0xFFFF0000UL) | 0x0005;
            cpu.r[R_BX] = (cpu.r[R_BX] & 0xFFFF0000UL);
            cpu.flags &= ~F_CF;
            return;
        }
        if (ah == 0x44 && (u8)(ax & 0xFF) == 0x0D) {
            cpu.r[R_AX] = (cpu.r[R_AX] & 0xFFFF0000UL);
            cpu.flags &= ~F_CF;
            return;
        }
        if (ah == 0x00 || ah == 0x4C || ah == 0x31) {
            if (ah != 0x31) vfh_release_all();
            stop_reason = 1;
            return;
        }

        if (ah == 0x48) {
            u16 need = (u16)cpu.r[R_BX];
            u16 top = pool_top_seg;
            u16 limit = pool_limit_seg;
            if ((u32)top + need > (u32)limit) {
                cpu.flags |= F_CF;
                cpu.r[R_AX] = (cpu.r[R_AX] & 0xFFFF0000UL) | 0x0008;
                cpu.r[R_BX] = (cpu.r[R_BX] & 0xFFFF0000UL) | (u16)(limit - top);
            } else {
                pool_top_seg = (u16)(top + need);
                cpu.r[R_AX] = (cpu.r[R_AX] & 0xFFFF0000UL) | top;
                cpu.flags &= ~F_CF;
            }
            return;
        }
        if (ah == 0x49) {
            cpu.flags &= ~F_CF;
            return;
        }
        if (ah == 0x4A) {
            u16 want = (u16)cpu.r[R_BX];
            u16 maxp = (u16)(pool_limit_seg - psp_seg);
            if (want > maxp) {
                cpu.flags |= F_CF;
                cpu.r[R_BX] = (cpu.r[R_BX] & 0xFFFF0000UL) | maxp;
            } else {
                prog_top_seg = (u16)(psp_seg + want);
                if (pool_top_seg < prog_top_seg) pool_top_seg = prog_top_seg;
                wr16(psp_seg, 0x02, prog_top_seg);
                cpu.flags &= ~F_CF;
            }
            return;
        }
        if (ah == 0x44 && (u8)(ax & 0xFF) == 0x0D
            && (cpu.eip & 0xFFFF) == 0x675E) {
            cpu.r[R_AX] = (cpu.r[R_AX] & 0xFFFF0000UL) | 0x80D3;
            cpu.r[R_DX] = (cpu.r[R_DX] & 0xFFFF0000UL) | 0x80D3;
            cpu.flags &= ~F_CF;
            return;
        }
        if (ah == 0x44 && (u8)(ax & 0xFF) == 0x00 && ax == 0x4400
            && (cpu.r[R_BX] & 0xFFFF) == 0x0002) {
            u16 h = (u16)cpu.r[R_BX];
            if (vfh_get((int)h) >= 0) {
                cpu.r[R_AX] = (cpu.r[R_AX] & 0xFFFF0000UL) | 0x0002;
                cpu.r[R_DX] = (cpu.r[R_DX] & 0xFFFF0000UL) | 0x0002;
                cpu.flags &= ~F_CF;
                return;
            }
            if (h <= 4) {
                cpu.r[R_AX] = (cpu.r[R_AX] & 0xFFFF0000UL) | 0x80D3;
                cpu.r[R_DX] = (cpu.r[R_DX] & 0xFFFF0000UL) | 0x80D3;
                cpu.flags &= ~F_CF;
            } else {
                cpu.r[R_AX] = (cpu.r[R_AX] & 0xFFFF0000UL) | 0x0006;
                cpu.flags |= F_CF;
            }
            return;
        }
        if (ah == 0x35 || ah == 0x25) {
        }
        if (ah == 0x4B) {
            cpu.flags |= F_CF;
            cpu.r[R_AX] = (cpu.r[R_AX] & 0xFFFF0000UL) | 0x0002;
            return;
        }
        if (ah == 0x50) {
            return;
        }
        if (ah == 0x51 || ah == 0x62) {
            cpu.r[R_BX] = (cpu.r[R_BX] & 0xFFFF0000UL) | psp_seg;
            cpu.flags &= ~F_CF;
            return;
        }
    }


    if (n == 0x21 && do_int21_file(&in, &out, &sr, ax))
        goto harvest;

    int86x(n, &in, &out, &sr);

harvest:
    cpu.r[R_AX] = (cpu.r[R_AX] & 0xFFFF0000UL) | out.x.ax;
    cpu.r[R_BX] = (cpu.r[R_BX] & 0xFFFF0000UL) | out.x.bx;
    cpu.r[R_CX] = (cpu.r[R_CX] & 0xFFFF0000UL) | out.x.cx;
    cpu.r[R_DX] = (cpu.r[R_DX] & 0xFFFF0000UL) | out.x.dx;
    cpu.r[R_SI] = (cpu.r[R_SI] & 0xFFFF0000UL) | out.x.si;
    cpu.r[R_DI] = (cpu.r[R_DI] & 0xFFFF0000UL) | out.x.di;
    cpu.s[S_ES] = sr.es;
    cpu.s[S_DS] = sr.ds;
    cpu.flags = (cpu.flags & 0xFFFF0000UL) | (out.x.flags & ~F_TF);
    if (n == 0x21) {
        u8 ahx = (u8)(ax >> 8);
        if (ahx == 0x02 || ahx == 0x09)
            cpu.flags = (cpu.flags & 0xFFFF0000UL) | (in.x.flags & ~F_TF);
    }


    if (n == 0x2F && (u8)(ax >> 8) == 0x43) {
        if ((u8)ax == 0x10) {
            xms_seg = sr.es;
            xms_off = out.x.bx;
            xms_valid = (sr.es != 0 || out.x.bx != 0);
        }
    }
}
void cpu_do_int(int n)
{
    do_int(n);
}
