#include "tr.h"

int in_step = 0;
int mem_hit = 0;
u16 mem_hit_seg, mem_hit_off;

u8 far *mem_ptr(u16 seg, u16 off)
{
    return (u8 far *)MK_FP(seg, off);
}

u8 far *mem_off(u16 seg, u32 off)
{
    return (u8 far *)MK_FP((u16)(seg + (u16)(off >> 4)), (u16)(off & 0x0F));
}

u32 lin_addr(u16 seg, u16 off)
{
    return ((u32)seg << 4) + off;
}

static u8 far *lo_sh = 0;
#define lo_sh_valid 0

void mem_snapshot_low(void)
{

}

#define SH8(lin)  (lo_sh_valid && (u32)(lin) >= 0x700UL && (u32)(lin) < 0x10000UL)
#define SHN(lin, n) (lo_sh_valid && (u32)(lin) >= 0x700UL && \
                     (u32)(lin) + (n) <= 0x10000UL)

static void watch_check(u16 seg, u16 off, int wr, u8 oldv, u8 newv)
{
    int i;
    if (!in_step || no_break) return;
    for (i = 0; i < nbps; i++) {
        if (!bps[i].enabled) continue;
        if (bps[i].type != BP_MEM && bps[i].type != BP_WRITE) continue;
        if (bps[i].anyseg) {
            if (bps[i].off != off) continue;
        } else if (bps[i].seg != seg || bps[i].off != off) continue;
        if (bps[i].type == BP_WRITE && !wr) continue;
        if (bps[i].type == BP_WRITE && oldv == newv) continue;
        if (bps[i].has_cond && !cond_eval(bps[i].cond)) continue;
        mem_hit = 1;
        mem_hit_seg = seg;
        mem_hit_off = off;
        bp_break_hit = i;
    }
}

u8 rd8(u16 seg, u16 off)
{
    u8 v;
    u32 lin;
    lin = ((u32)seg << 4) + off;
    if (SH8(lin)) { v = lo_sh[lin]; watch_check(seg, off, 0, v, v); return v; }
    v = *(u8 far *)MK_FP(seg, off);
    watch_check(seg, off, 0, v, v);
    return v;
}

u16 rd16(u16 seg, u16 off)
{
    u16 v;
    u32 lin;
    lin = ((u32)seg << 4) + off;
    if (SHN(lin, 2)) {
        v = (u16)(lo_sh[lin] | ((u16)lo_sh[lin + 1] << 8));
        watch_check(seg, off, 0, (u8)v, (u8)v);
        return v;
    }
    v = *(u16 far *)MK_FP(seg, off);
    watch_check(seg, off, 0, (u8)v, (u8)v);
    return v;
}

u32 rd32(u16 seg, u16 off)
{
    u32 v;
    u16 i;
    u32 lin;
    lin = ((u32)seg << 4) + off;
    if (SHN(lin, 4)) {
        v = (u32)lo_sh[lin] | ((u32)lo_sh[lin + 1] << 8) |
            ((u32)lo_sh[lin + 2] << 16) | ((u32)lo_sh[lin + 3] << 24);
        return v;
    }
    v = *(u32 far *)MK_FP(seg, off);
    watch_check(seg, off, 0, (u8)v, (u8)v);
    return v;
}

void wr8(u16 seg, u16 off, u8 v)
{
    u8 far *p;
    u32 lin;
    lin = ((u32)seg << 4) + off;
    if (SH8(lin)) { watch_check(seg, off, 1, lo_sh[lin], v); lo_sh[lin] = v; return; }
    p = (u8 far *)MK_FP(seg, off);
    watch_check(seg, off, 1, *p, v);
    *p = v;
}

void wr16(u16 seg, u16 off, u16 v)
{
    u8 far *p;
    u32 lin;
    lin = ((u32)seg << 4) + off;
    if (SHN(lin, 2)) {
        watch_check(seg, off, 1, (u8)lo_sh[lin], (u8)v);
        lo_sh[lin] = (u8)v; lo_sh[lin + 1] = (u8)(v >> 8);
        return;
    }
    p = (u8 far *)MK_FP(seg, off);
    watch_check(seg, off, 1, (u8)*(u16 far *)p, (u8)v);
    *(u16 far *)p = v;
}

void wr32(u16 seg, u16 off, u32 v)
{
    u8 far *p;
    u32 lin = ((u32)seg << 4) + off;
    if (SHN(lin, 4)) {
        watch_check(seg, off, 1, (u8)lo_sh[lin], (u8)v);
        lo_sh[lin] = (u8)v; lo_sh[lin + 1] = (u8)(v >> 8);
        lo_sh[lin + 2] = (u8)(v >> 16); lo_sh[lin + 3] = (u8)(v >> 24);
        return;
    }
    p = (u8 far *)MK_FP(seg, off);
    watch_check(seg, off, 1, (u8)*(u32 far *)p, (u8)v);
    *(u32 far *)p = v;
}

void far_strcpy(char *d, const char far *s)
{
    while (*s) *d++ = *s++;
    *d = 0;
}
