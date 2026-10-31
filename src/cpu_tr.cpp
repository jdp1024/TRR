#include "tr.h"

typedef signed char s8;

extern int stop_reason;

static int t_osz;
static int t_asz;
static int t_segovr;
static int t_rep;


static u8 tf8(void)   { u8 v = rd8(cpu.s[S_CS], (u16)cpu.eip); cpu.eip++; return v; }
static u16 tf16(void) { u16 v = tf8(); v |= (u16)tf8() << 8; return v; }
static u32 tf32(void) { u32 v = tf16(); v |= (u32)tf16() << 16; return v; }
static u32 tfimm(void) { return t_osz == 32 ? tf32() : tf16(); }


static u32 tr_getreg(int i, int size)
{
    i &= 7;
    if (size == 8) {
        if (i < 4) return cpu.r[i] & 0xFF;
        return (cpu.r[i - 4] >> 8) & 0xFF;
    }
    if (size == 16) return cpu.r[i] & 0xFFFF;
    return cpu.r[i];
}
static void tr_setreg(int i, int size, u32 v)
{
    i &= 7;
    if (size == 8) {
        if (i < 4) cpu.r[i] = (cpu.r[i] & 0xFFFFFF00UL) | (v & 0xFF);
        else cpu.r[i - 4] = (cpu.r[i - 4] & 0xFFFF00FFUL) | ((v & 0xFF) << 8);
    } else if (size == 16) {
        cpu.r[i] = (cpu.r[i] & 0xFFFF0000UL) | (v & 0xFFFF);
    } else {
        cpu.r[i] = v;
    }
}
static u32 tr_mask(int size)
{
    return (size == 8) ? 0xFFUL : (size == 16) ? 0xFFFFUL : 0xFFFFFFFFUL;
}
static u32 signbit(int size)
{
    return (size == 8) ? 0x80UL : (size == 16) ? 0x8000UL : 0x80000000UL;
}
static int sgn(u32 v, int size) { return (v & signbit(size)) != 0; }
static u32 sext(u32 v, int size)
{
    v &= tr_mask(size);
    return (v & signbit(size)) ? (v | ~tr_mask(size)) : v;
}


static u32 tr_memrd(u16 seg, u16 off, int size)
{
    if (size == 8) return rd8(seg, off);
    if (size == 16) return rd16(seg, off);
    return rd32(seg, off);
}
static void tr_memwr(u16 seg, u16 off, int size, u32 v)
{
    if (size == 8) { wr8(seg, off, (u8)v); return; }
    if (size == 16) { wr16(seg, off, (u16)v); return; }
    wr32(seg, off, v);
}
struct TOp { int isreg; int reg; u16 off; u16 seg; };
static u32 tr_getop(struct TOp *o, int size)
{
    if (o->isreg) return tr_getreg(o->reg, size);
    return tr_memrd(o->seg, o->off, size);
}
static void tr_setop(struct TOp *o, int size, u32 v)
{
    if (o->isreg) { tr_setreg(o->reg, size, v); return; }
    tr_memwr(o->seg, o->off, size, v);
}


static void tr_push(int size, u32 v)
{
    u16 sp = (u16)((u16)cpu.r[R_SP] - (u16)(size / 8));
    cpu.r[R_SP] = sp;
    tr_memwr(cpu.s[S_SS], sp, size, v);
}
static u32 tr_pop(int size)
{
    u16 sp = (u16)cpu.r[R_SP];
    u32 v = tr_memrd(cpu.s[S_SS], sp, size);
    cpu.r[R_SP] = (u16)(sp + (u16)(size / 8));
    return v;
}


static void tr_set6(int of, int sf, int zf, int af, int pf, int cf)
{
    cpu.flags = (cpu.flags & 0xF72AUL)
              | ((u32)(of & 1) << 11) | ((u32)(sf & 1) << 7)
              | ((u32)(zf & 1) << 6)  | ((u32)(af & 1) << 4)
              | ((u32)(pf & 1) << 2)  | ((u32)(cf & 1));
}
static int tr_cf(void) { return (cpu.flags & F_CF) != 0; }
static int tr_zf(void) { return (cpu.flags & F_ZF) != 0; }
static int tr_df(void) { return (cpu.flags & 0x400UL) != 0; }
static int tr_of(void) { return (cpu.flags & F_OF) != 0; }
static int tr_sf(void) { return (cpu.flags & F_SF) != 0; }
static int tr_pfv(u32 r)
{
    u8 b = (u8)r; int n = 0;
    for (; b; b >>= 1) n += (b & 1);
    return (n & 1) ? 0 : 1;
}

static int cadd_c(u32 a, u32 b, int c, int size)
{
    return ((a & tr_mask(size)) + (b & tr_mask(size)) + (u32)c) > tr_mask(size);
}
static int cadd_o(u32 a, u32 b, int c, int size)
{
    u32 s = (a & tr_mask(size)) + (b & tr_mask(size)) + (u32)c;
    int am = sgn(a, size), bm = sgn(b, size), sm = sgn(s, size);
    return (am == bm && sm != am) ? 1 : 0;
}
static int csub_c(u32 a, u32 b, int c, int size)
{
    u32 m = tr_mask(size);
    return ((a & m) < (b & m)) || ((b & m) + (u32)c > m) ? 1 : 0;
}
static int csub_o(u32 a, u32 b, int c, int size)
{
    u32 r = (a & tr_mask(size)) - (b & tr_mask(size)) - (u32)c;
    int am = sgn(a, size), bm = sgn(b, size), rm = sgn(r, size);
    return (am != bm && rm != am) ? 1 : 0;
}
static int cadd_af(u32 a, u32 b, int c) { return (((a & 0xF) + (b & 0xF) + (u32)c) & 0x10) != 0; }
static int csub_af(u32 a, u32 b, int c) { return ((a & 0xF) < ((b & 0xF) + (u32)c)); }


static int xmod, xreg, xrm;

static u16 ea16(int rm, int *ssdef)
{
    *ssdef = 0;
    switch (rm & 7) {
    case 0: return (u16)(cpu.r[R_BX] + cpu.r[R_SI]);
    case 1: return (u16)(cpu.r[R_BX] + cpu.r[R_DI]);
    case 2: *ssdef = 1; return (u16)(cpu.r[R_BP] + cpu.r[R_SI]);
    case 3: *ssdef = 1; return (u16)(cpu.r[R_BP] + cpu.r[R_DI]);
    case 4: return (u16)cpu.r[R_SI];
    case 5: return (u16)cpu.r[R_DI];
    case 6: return 0;
    default: return (u16)cpu.r[R_BX];
    }
}
static void tr_modrm(struct TOp *o)
{
    u8 m = tf8();
    int ssdef = 0;
    xmod = m >> 6;
    xreg = (m >> 3) & 7;
    xrm = m & 7;
    o->reg = xrm;
    if (xmod == 3) { o->isreg = 1; return; }
    o->isreg = 0;
    if (t_asz == 32) {
        u32 base = 0;
        static const int b32[8] = { R_AX, R_CX, R_DX, R_BX, -1, R_BP, R_SI, R_DI };
        ssdef = 0;
        if (xrm == 4) {
            u8 sib = tf8();
            int scale = 1 << (sib >> 6);
            int idx = (sib >> 3) & 7, bse = sib & 7;
            if (bse == 5) { base = tf32(); ssdef = 1; }
            else base = cpu.r[b32[bse]];
            if (idx != 4) base += cpu.r[b32[idx]] * (u32)scale;
        } else if (xrm == 5) {
            base = tf32();
        } else {
            base = cpu.r[b32[xrm]];
        }
        if (xmod == 1) base += (u32)(s8)tf8();
        else if (xmod == 2) base += tf32();
        o->off = (u16)base;
        o->seg = (t_segovr >= 0) ? cpu.s[t_segovr]
               : (ssdef ? cpu.s[S_SS] : cpu.s[S_DS]);
        return;
    }
    o->off = ea16(xrm, &ssdef);
    if (xrm == 6) {
        if (xmod == 0) { o->off = tf16(); }
        else {
            ssdef = 1;
            o->off = (u16)cpu.r[R_BP];
            if (xmod == 1) o->off = (u16)(o->off + (u16)(s16)(s8)tf8());
            else o->off = (u16)(o->off + (u16)tf16());
        }
    }
    else if (xmod == 1) o->off = (u16)(o->off + (u16)(s16)(s8)tf8());
    else if (xmod == 2) o->off = (u16)(o->off + (u16)tf16());
    o->seg = (t_segovr >= 0) ? cpu.s[t_segovr]
           : (ssdef ? cpu.s[S_SS] : cpu.s[S_DS]);
}


#define OP_ADD 0
#define OP_OR  1
#define OP_ADC 2
#define OP_SBB 3
#define OP_AND 4
#define OP_SUB 5
#define OP_XOR 6
#define OP_CMP 7

static u32 tr_res;

static void tr_alu(int kind, u32 a, u32 b, int size, int cin)
{
    u32 m = tr_mask(size);
    u32 r = 0;
    int cf = 0, of = 0, af = 0;
    a &= m; b &= m;
    switch (kind) {
    case OP_ADD: r = a + b;
        cf = cadd_c(a, b, 0, size); of = cadd_o(a, b, 0, size); af = cadd_af(a, b, 0); break;
    case OP_OR:  r = a | b; break;
    case OP_ADC: r = a + b + (u32)cin;
        cf = cadd_c(a, b, cin, size); of = cadd_o(a, b, cin, size); af = cadd_af(a, b, cin); break;
    case OP_SBB: r = a - b - (u32)cin;
        cf = csub_c(a, b, cin, size); of = csub_o(a, b, cin, size); af = csub_af(a, b, cin); break;
    case OP_AND: r = a & b; break;
    case OP_SUB: r = a - b;
        cf = csub_c(a, b, 0, size); of = csub_o(a, b, 0, size); af = csub_af(a, b, 0); break;
    case OP_XOR: r = a ^ b; break;
    case OP_CMP: r = a - b;
        cf = csub_c(a, b, 0, size); of = csub_o(a, b, 0, size); af = csub_af(a, b, 0); break;
    default: break;
    }
    r &= m;
    if (kind == OP_OR || kind == OP_AND || kind == OP_XOR) { cf = 0; of = 0; af = 0; }
    tr_set6(of, sgn(r, size), r == 0, af, tr_pfv(r), cf);
    tr_res = r;
}
static void alu_rm_reg(int kind, int size)
{
    struct TOp dst;
    tr_modrm(&dst);
    tr_alu(kind, tr_getop(&dst, size), tr_getreg(xreg, size), size, tr_cf());
    if (kind != OP_CMP) tr_setop(&dst, size, tr_res);
}
static void alu_reg_rm(int kind, int size)
{
    struct TOp src;
    u32 a;
    tr_modrm(&src);
    a = tr_getreg(xreg, size);
    tr_alu(kind, a, tr_getop(&src, size), size, tr_cf());
    if (kind != OP_CMP) tr_setreg(xreg, size, tr_res);
}
static void alu_acc(int kind, int size, u32 imm)
{
    u32 a = tr_getreg(R_AX, size);
    tr_alu(kind, a, imm & tr_mask(size), size, tr_cf());
    if (kind != OP_CMP) tr_setreg(R_AX, size, tr_res);
}
static void alu_imm(int kind, int size, u32 imm, struct TOp *dst)
{
    tr_alu(kind, tr_getop(dst, size), imm & tr_mask(size), size, tr_cf());
    if (kind != OP_CMP) tr_setop(dst, size, tr_res);
}


static void op_incdec(int size, struct TOp *dst, int dec)
{
    u32 a = tr_getop(dst, size);
    u32 r;
    int cf = tr_cf(), of, af;
    if (dec) { r = (a - 1) & tr_mask(size); of = csub_o(a, 1, 0, size); af = csub_af(a, 1, 0); }
    else     { r = (a + 1) & tr_mask(size); of = cadd_o(a, 1, 0, size); af = cadd_af(a, 1, 0); }
    cpu.flags = (cpu.flags & (0xF72AUL | F_CF))
              | ((u32)(of & 1) << 11) | ((u32)sgn(r, size) << 7)
              | ((u32)(r == 0) << 6)  | ((u32)(af & 1) << 4)
              | ((u32)tr_pfv(r) << 2) | (u32)cf;
    tr_setop(dst, size, r);
}



static void tr_shift(int kind, u32 v, u32 cnt, int size, struct TOp *dst)
{
    int bits = size;
    u32 m = tr_mask(size);
    int cf = tr_cf(), of = (cpu.flags >> 11) & 1;
    u32 n, i;
    int msb, msb0 = sgn(v, size);
    v &= m;
    if (kind == 0 || kind == 1 || kind == 4) n = cnt & 31u;
    else if (kind == 2 || kind == 3) n = cnt % (u32)bits;
    else n = cnt % (u32)(bits + 1);
    if (cnt == 0) return;
    for (i = 0; i < n; i++) {
        switch (kind) {
        case 0: cf = sgn(v, size);  v = (v << 1) & m; break;
        case 1: cf = (int)(v & 1);  v >>= 1; break;
        case 4: msb = sgn(v, size); cf = (int)(v & 1);
                v = (v >> 1) | (msb ? signbit(size) : 0); break;
        case 2: msb = sgn(v, size); v = ((v << 1) | (u32)msb) & m; cf = msb; break;
        case 3: msb = (int)(v & 1); v = (v >> 1) | ((u32)msb << (bits - 1)); cf = msb; break;
        case 5: msb = sgn(v, size); v = ((v << 1) | (u32)cf) & m; cf = msb; break;
        case 6: msb = (int)(v & 1); v = (v >> 1) | ((u32)cf << (bits - 1)); cf = msb; break;
        default: break;
        }
        v &= m;
    }
    if (n == 1) {
        if (kind == 0)      of = sgn(v, size) ^ cf;
        else if (kind == 1) of = msb0 ^ sgn(v, size);
        else if (kind == 4) of = 0;
        else if (kind == 2) of = sgn(v, size) ^ (int)(v & 1);
        else if (kind == 3) of = sgn(v, size) ^ (int)((v >> (bits - 2)) & 1);
        else of = (cpu.flags >> 11) & 1;
    }
    tr_set6(of, sgn(v, size), v == 0, 0, tr_pfv(v), cf);
    if (dst) tr_setop(dst, size, v); else tr_res = v;
}


static void mul32(u32 a, u32 b, u32 *hi, u32 *lo)
{
    u32 a0 = a & 0xFFFF, a1 = a >> 16, b0 = b & 0xFFFF, b1 = b >> 16;
    u32 p0 = a0 * b0, mid = a0 * b1 + a1 * b0;
    u32 carry = (mid >> 16) + a1 * b1;
    u32 l = p0 + (mid << 16);
    *lo = l;
    *hi = carry + ((l < p0) ? 1u : 0u);
}
static int div64(u32 nhi, u32 nlo, u32 den, u32 *q, u32 *r)
{
    u32 qq = 0, rr = 0;
    int i;
    if (den == 0 || nhi >= den) return 0;
    for (i = 63; i >= 0; i--) {
        int carry = (int)((rr >> 31) & 1);
        rr = (rr << 1) | ((i >= 32 ? nhi : nlo) >> (i & 31) & 1);
        if (carry || rr >= den) {
            rr -= den;
            if (i < 32) qq |= (1UL << i);
        }
    }
    *q = qq; *r = rr;
    return 1;
}

static void grp_f67(int sub, int size, struct TOp *dst)
{
    u32 src, a;
    src = tr_getop(dst, size);
    switch (sub) {
    case 0:
        tr_alu(OP_AND, src, (size == 8 ? (u32)tf8() : tfimm()) & tr_mask(size),
               size, 0);
        break;
    case 2:
        tr_setop(dst, size, ~src & tr_mask(size));
        break;
    case 3: {
        u32 r = (0u - src) & tr_mask(size);
        int nz = (src & tr_mask(size)) != 0;
        int ov = (src & tr_mask(size)) == signbit(size);
        tr_set6(ov, sgn(r, size), r == 0, csub_af(0u, src, 0), tr_pfv(r), nz);
        tr_setop(dst, size, r);
        break;
    }
    case 4: {
        u32 lo, hi;
        a = tr_getreg(R_AX, size);
        if (size == 8)  { u32 p = (a & 0xFF) * (src & 0xFF); lo = p & 0xFF; hi = (p >> 8) & 0xFF; }
        else if (size == 16) { u32 p = (a & 0xFFFF) * (src & 0xFFFF); lo = p & 0xFFFF; hi = p >> 16; }
        else mul32(a, src, &hi, &lo);
        tr_setreg(R_AX, size, lo);
        if (size == 8)  tr_setreg(4, 8, hi);
        else if (size == 16) tr_setreg(R_DX, 16, hi);
        else cpu.r[R_DX] = hi;
        { int h = (hi != 0); tr_set6(h, sgn(lo, size), lo == 0, 0, tr_pfv(lo), h); }
        break;
    }
    case 5: {
        u32 lo, hi;
        int ovf = 0;
        a = tr_getreg(R_AX, size);
        if (size == 8) {
            s16 p = (s16)((s8)(u8)a * (s8)(u8)src);
            lo = (u32)p & 0xFF;
            ovf = (p != (s8)(u8)lo);
            tr_setreg(R_AX, 16, (u32)p & 0xFFFF);
        } else if (size == 16) {
            s32 p = (s32)((s16)(u16)a * (s16)(u16)src);
            lo = (u32)p & 0xFFFF; hi = (u32)(((u32)p) >> 16);
            ovf = (p != (s16)(u16)lo);
            tr_setreg(R_AX, 16, lo); tr_setreg(R_DX, 16, hi);
        } else {
            s32 x = (s32)a, y = (s32)src;
            int nneg = 0; s32 qv;
            u32 h, l;
            if (x < 0) { x = -x; nneg ^= 1; }
            if (y < 0) { y = -y; nneg ^= 1; }
            mul32((u32)x, (u32)y, &h, &l);
            if (nneg) { l = 0UL - l; h = 0UL - h - (l ? 1u : 0u); }
            qv = (s32)l;
            ovf = !((h == 0 && qv >= 0) || (h == 0xFFFFFFFFUL && qv < 0));
            cpu.r[R_AX] = l; cpu.r[R_DX] = h;
        }
        { u32 rr = tr_getreg(R_AX, size);
          tr_set6(ovf, sgn(rr, size), rr == 0, 0, tr_pfv(rr), ovf); }
        break;
    }
    case 6: {
        u32 q, r, den = src & tr_mask(size);
        if (den == 0) { stop_reason = 3; return; }
        if (size == 8) {
            u32 num = tr_getreg(R_AX, 16);
            q = num / den; r = num % den;
            if (q > 0xFF) { stop_reason = 3; return; }
            tr_setreg(R_AX, 16, ((r & 0xFF) << 8) | (q & 0xFF));
        } else if (size == 16) {
            u32 numh = tr_getreg(R_DX, 16), numl = tr_getreg(R_AX, 16);
            if (!div64(numh, numl, den, &q, &r)) { stop_reason = 3; return; }
            tr_setreg(R_AX, 16, q); tr_setreg(R_DX, 16, r);
        } else {
            if (!div64(cpu.r[R_DX], cpu.r[R_AX], den, &q, &r)) {
                stop_reason = 3; return;
            }
            cpu.r[R_AX] = q; cpu.r[R_DX] = r;
        }
        break;
    }
    case 7: {
        u32 den = src & tr_mask(size), ql, rl;
        if (den == 0) { stop_reason = 3; return; }
        if (size == 8) {
            s16 num = (s16)tr_getreg(R_AX, 16); s8 d = (s8)(u8)den; s16 q16, r16;
            if (d == 0) { stop_reason = 3; return; }
            q16 = num / d; r16 = num % d;
            if (q16 > 127 || q16 < -128) { stop_reason = 3; return; }
            tr_setreg(R_AX, 16, ((((u32)r16) & 0xFF) << 8) | (((u32)q16) & 0xFF));
        } else if (size == 16) {
            s32 num = (s32)((tr_getreg(R_DX, 16) << 16) | tr_getreg(R_AX, 16));
            s16 d = (s16)(u16)den; s32 q32, r32;
            if (d == 0) { stop_reason = 3; return; }
            q32 = num / d; r32 = num % d;
            if (q32 > 32767 || q32 < -32768) { stop_reason = 3; return; }
            tr_setreg(R_AX, 16, (u32)q32); tr_setreg(R_DX, 16, (u32)r32);
        } else {
            u32 nlo = cpu.r[R_AX], nhi = cpu.r[R_DX];
            s32 d32 = (s32)den;
            u32 aa = nlo, ah = nhi, da;
            int nq = ((s32)nhi < 0), dq = (d32 < 0);
            s32 q32, r32;
            da = dq ? (u32)(0L - d32) : (u32)d32;
            if (nq) { aa = 0UL - aa; ah = 0UL - ah - (aa ? 1u : 0u); }
            if (!div64(ah, aa, da, &ql, &rl)) { stop_reason = 3; return; }
            q32 = (s32)ql;
            if (nq ^ dq) q32 = -q32;
            r32 = (s32)rl;
            if (nq) r32 = -r32;
            cpu.r[R_AX] = (u32)q32; cpu.r[R_DX] = (u32)r32;
        }
        break;
    }
    default: break;
    }
}


static int cc_test(int cc)
{
    switch (cc & 15) {
    case 0:  return tr_of();
    case 1:  return !tr_of();
    case 2:  return tr_cf();
    case 3:  return !tr_cf();
    case 4:  return tr_zf();
    case 5:  return !tr_zf();
    case 6:  return tr_cf() || tr_zf();
    case 7:  return !tr_cf() && !tr_zf();
    case 8:  return tr_sf();
    case 9:  return !tr_sf();
    case 10: return (cpu.flags & F_PF) != 0;
    case 11: return (cpu.flags & F_PF) == 0;
    case 12: return tr_sf() != tr_of();
    case 13: return tr_sf() == tr_of();
    case 14: return tr_zf() || (tr_sf() != tr_of());
    default: return !tr_zf() && (tr_sf() == tr_of());
    }
}

static void grp_mov_imm(int size)
{
    struct TOp dst;
    u32 v;
    tr_modrm(&dst);
    v = (size == 8) ? (u32)tf8() : tfimm();
    tr_setop(&dst, size, v);
}
static void grp_8x(int size, int simm)
{
    struct TOp dst;
    u32 imm;
    tr_modrm(&dst);
    if (size == 8) imm = (u32)tf8();
    else if (simm) imm = sext((u32)(s8)tf8(), 8) & tr_mask(size);
    else imm = tfimm();
    alu_imm(xreg, size, imm, &dst);
}

static void grp_ff(int sub, struct TOp *dst)
{
    int size = t_osz;
    switch (sub) {
    case 0: op_incdec(size, dst, 0); break;
    case 1: op_incdec(size, dst, 1); break;
    case 2: {
        u32 t = tr_getop(dst, size);
        tr_push(size, cpu.eip);
        cpu.eip = t;
        break;
    }
    case 3: case 5: {
        u16 off16 = (u16)tr_getop(dst, 16);
        u16 seg = (u16)tr_memrd(dst->seg, (u16)(dst->off + 2), 16);
        if (seg >= 0xC000) {
            far_call_host(seg, off16);
            break;
        }
        if (sub == 3) { tr_push(16, cpu.s[S_CS]); tr_push(size, cpu.eip); }
        cpu.s[S_CS] = seg; cpu.eip = off16;
        break;
    }
    case 4: cpu.eip = tr_getop(dst, size); break;
    case 6: tr_push(size, tr_getop(dst, size)); break;
    case 7: tr_setop(dst, size, tr_pop(size)); break;
    default: break;
    }
}
static const int shift_kind[8] = { 2, 3, 5, 6, 0, 1, 0, 4 };
static void grp_shift(int size, int mode)
{
    struct TOp dst;
    u32 cnt;
    tr_modrm(&dst);
    if (mode == 1) cnt = cpu.r[R_CX] & 0xFF;
    else if (mode == 2) cnt = 1;
    else cnt = (u32)tf8();
    tr_shift(shift_kind[xreg & 7], tr_getop(&dst, size), cnt, size, &dst);
}


static void op_xchg(int size)
{
    struct TOp dst;
    u32 a, b;
    tr_modrm(&dst);
    a = tr_getreg(xreg, size);
    b = tr_getop(&dst, size);
    tr_setreg(xreg, size, b);
    tr_setop(&dst, size, a);
}
static void op_mov(int size, int rm_to_reg)
{
    struct TOp o;
    tr_modrm(&o);
    if (rm_to_reg) tr_setreg(xreg, size, tr_getop(&o, size));
    else tr_setop(&o, size, tr_getreg(xreg, size));
}
static void op_mov_reg_imm(int op, int size)
{
    u32 v = (size == 8) ? (u32)tf8() : tfimm();
    tr_setreg(op & 7, size, v);
}
static void op_mov_sreg(void)
{
    struct TOp o;
    tr_modrm(&o);
    cpu.s[xreg & 7] = (u16)tr_getop(&o, 16);
}
static void op_rm_sreg(void)
{
    struct TOp o;
    tr_modrm(&o);
    tr_setop(&o, 16, cpu.s[xreg & 7]);
}
static void op_lea(void)
{
    struct TOp o;
    tr_modrm(&o);
    if (o.isreg) return;
    tr_setreg(xreg, t_osz, o.off);
}
static void op_mov_moffs(int size, int acc_to_mem)
{
    u16 off = (t_asz == 32) ? (u16)tf32() : tf16();
    u16 seg = (t_segovr >= 0) ? cpu.s[t_segovr] : cpu.s[S_DS];
    if (acc_to_mem) tr_memwr(seg, off, size, tr_getreg(R_AX, size));
    else tr_setreg(R_AX, size, tr_memrd(seg, off, size));
}
static void op_seg(int idx, int push)
{
    if (push) tr_push(16, cpu.s[idx]);
    else cpu.s[idx] = (u16)tr_pop(16);
}


static void op_imul3(u32 imm)
{
    struct TOp src;
    int size = t_osz;
    u32 a;
    int ovf = 0;
    tr_modrm(&src);
    a = tr_getreg(xreg, size);
    if (size == 8) {
        s16 p = (s16)((s8)(u8)a * (s8)(u8)imm);
        tr_setreg(xreg, 8, (u32)p & 0xFF);
        ovf = (p != (s8)(u8)p) ? 1 : 0;
    } else if (size == 16) {
        s32 p = (s32)((s16)(u16)a * (s16)(u16)imm);
        tr_setreg(xreg, 16, (u32)p);
        ovf = (p != (s16)(u16)p) ? 1 : 0;
    } else {
        s32 x = (s32)a, y = (s32)imm;
        int nneg = 0; u32 h, l;
        if (x < 0) { x = -x; nneg ^= 1; }
        if (y < 0) { y = -y; nneg ^= 1; }
        mul32((u32)x, (u32)y, &h, &l);
        if (nneg) { l = 0UL - l; h = 0UL - h - (l ? 1u : 0u); }
        cpu.r[xreg] = l;
        ovf = !((h == 0 && (s32)l >= 0) || (h == 0xFFFFFFFFUL && (s32)l < 0));
    }
    { u32 r = tr_getreg(xreg, size);
      tr_set6(ovf, sgn(r, size), r == 0, 0, tr_pfv(r), ovf); }
}
static void grp_0f(void)
{
    u8 op2 = tf8();
    struct TOp o;
    int sz = t_osz;
    u32 v, r2;
    u32 bit;

    if (op2 >= 0x80 && op2 <= 0x8F) {
        u32 rel = tfimm();
        u32 nxt = cpu.eip;
        if (cc_test(op2 & 15))
            cpu.eip = (nxt + (sz == 32 ? (s32)rel : (s32)(s16)rel)) & 0xFFFF;
        return;
    }
    if (op2 >= 0x90 && op2 <= 0x9F) {
        tr_modrm(&o);
        tr_setop(&o, 8, cc_test(op2 & 15) ? 1UL : 0UL);
        return;
    }
    if (op2 >= 0x40 && op2 <= 0x4F) {
        tr_modrm(&o);
        if (cc_test(op2 & 15))
            tr_setreg(xreg, sz, tr_getop(&o, sz));
        return;
    }
    switch (op2) {
    case 0xA0: tr_push(16, cpu.s[S_FS]); return;
    case 0xA1: cpu.s[S_FS] = (u16)tr_pop(16); return;
    case 0xA8: tr_push(16, cpu.s[S_GS]); return;
    case 0xA9: cpu.s[S_GS] = (u16)tr_pop(16); return;

    case 0xA3: case 0xAB: case 0xB3: case 0xBB: {
        u32 idx;
        tr_modrm(&o);
        idx = tr_getreg(xreg, sz);
        if (o.isreg) {
            v = tr_getop(&o, sz);
            bit = idx & (u32)(sz - 1);
            cpu.flags = (cpu.flags & ~F_CF) | ((v >> bit) & 1UL);
            if (op2 == 0xAB) v |= 1UL << bit;
            if (op2 == 0xB3) v &= ~(1UL << bit);
            if (op2 == 0xBB) v ^= 1UL << bit;
            if (op2 != 0xA3) tr_setop(&o, sz, v);
        } else {
            u32 byteoff = (u32)((u16)(idx >> 3));
            v = tr_memrd(o.seg, (u16)(o.off + byteoff), 8);
            cpu.flags = (cpu.flags & ~F_CF) | ((v >> (idx & 7)) & 1UL);
            if (op2 != 0xA3) {
                if (op2 == 0xAB) v |= 1UL << (idx & 7);
                if (op2 == 0xB3) v &= ~(1UL << (idx & 7));
                if (op2 == 0xBB) v ^= 1UL << (idx & 7);
                tr_memwr(o.seg, (u16)(o.off + byteoff), 8, v);
            }
        }
        return;
    }
    case 0xBA: {
        int sub;
        u8 immv;
        tr_modrm(&o);
        sub = xreg; immv = tf8();
        v = tr_getop(&o, sz);
        bit = (u32)immv % (u32)sz;
        cpu.flags = (cpu.flags & ~F_CF) | ((v >> bit) & 1UL);
        if (sub == 5) v |= 1UL << bit;
        if (sub == 6) v &= ~(1UL << bit);
        if (sub == 7) v ^= 1UL << bit;
        if (sub >= 5) tr_setop(&o, sz, v);
        return;
    }
    case 0xA4: case 0xA5: case 0xAC: case 0xAD: {
        u32 src, cnt;
        tr_modrm(&o);
        src = tr_getreg(xreg, sz);
        v = tr_getop(&o, sz);
        cnt = (op2 == 0xA4 || op2 == 0xAC) ? (u32)tf8()
                                           : (cpu.r[R_CX] & 0x1FUL);
        cnt &= 0x1FUL;
        if (cnt == 0) return;
        if (op2 == 0xA4 || op2 == 0xA5) {
            r2 = (v << cnt) | (cnt < (u32)sz ? (src >> ((u32)sz - cnt)) : 0);
            cpu.flags = (cpu.flags & ~F_CF)
                      | (cnt ? ((v >> ((u32)sz - cnt)) & 1UL) : 0UL);
        } else {
            r2 = (v >> cnt) | (cnt < (u32)sz ? (src << ((u32)sz - cnt)) : 0);
            cpu.flags = (cpu.flags & ~F_CF) | ((v >> (cnt - 1)) & 1UL);
        }
        r2 &= tr_mask(sz);
        tr_setop(&o, sz, r2);
        cpu.flags = (cpu.flags & ~(F_SF | F_ZF | F_PF))
                  | (sgn(r2, sz) ? F_SF : 0) | (r2 == 0 ? F_ZF : 0)
                  | (tr_pfv(r2) ? F_PF : 0);
        return;
    }
    case 0xAF: {
        u32 a, b;
        int ovf = 0;
        tr_modrm(&o);
        a = tr_getreg(xreg, sz); b = tr_getop(&o, sz);
        if (sz == 8) {
            s16 p = (s16)((s8)(u8)a * (s8)(u8)b);
            tr_setreg(xreg, 8, (u32)p & 0xFF);
            ovf = (p != (s8)(u8)p) ? 1 : 0;
        } else if (sz == 16) {
            s32 p = (s32)((s16)(u16)a * (s16)(u16)b);
            tr_setreg(xreg, 16, (u32)p);
            ovf = (p != (s16)(u16)p) ? 1 : 0;
        } else {
            s32 x = (s32)a, y = (s32)b;
            int nneg = 0; u32 h, l;
            if (x < 0) { x = -x; nneg ^= 1; }
            if (y < 0) { y = -y; nneg ^= 1; }
            mul32((u32)x, (u32)y, &h, &l);
            if (nneg) { l = 0UL - l; h = 0UL - h - (l ? 1UL : 0UL); }
            cpu.r[xreg] = l;
            ovf = !((h == 0 && (s32)l >= 0) || (h == 0xFFFFFFFFUL && (s32)l < 0));
        }
        { u32 rr = tr_getreg(xreg, sz);
          tr_set6(ovf, sgn(rr, sz), rr == 0, 0, tr_pfv(rr), ovf); }
        return;
    }
    case 0xB0: case 0xB1: {
        int sz2 = (op2 == 0xB0) ? 8 : sz;
        u32 acc = tr_getreg(R_AX, sz2), rmv, rv;
        tr_modrm(&o);
        rv  = tr_getreg(xreg, sz2);
        rmv = tr_getop(&o, sz2);
        tr_alu(OP_CMP, acc, rmv, sz2, 0);
        if (acc == rmv) tr_setop(&o, sz2, rv);
        else tr_setreg(R_AX, sz2, rmv);
        return;
    }
    case 0xB4: case 0xB5: {
        u32 off;
        tr_modrm(&o);
        off = tr_getop(&o, sz);
        cpu.s[op2 == 0xB4 ? S_ES : S_DS] =
            (u16)tr_memrd(o.seg, (u16)(o.off + (sz / 8)), 16);
        tr_setreg(xreg, sz, off);
        return;
    }
    case 0xB6: case 0xB7: case 0xBE: case 0xBF: {
        int sz2 = (op2 == 0xB6 || op2 == 0xBE) ? 8 : 16;
        tr_modrm(&o);
        v = tr_getop(&o, sz2);
        if (op2 == 0xB6) v &= 0xFFUL;
        if (op2 == 0xB7) v &= 0xFFFFUL;
        if (op2 == 0xBE) v = sext(v, 8);
        if (op2 == 0xBF) v = sext(v, 16);
        tr_setreg(xreg, sz, v & tr_mask(sz));
        return;
    }
    case 0xC0: case 0xC1: {
        int sz2 = (op2 == 0xC0) ? 8 : sz;
        u32 a, b;
        tr_modrm(&o);
        a = tr_getop(&o, sz2); b = tr_getreg(xreg, sz2);
        tr_alu(OP_ADD, a, b, sz2, 0);
        tr_setop(&o, sz2, tr_res);
        tr_setreg(xreg, sz2, a);
        return;
    }
    case 0xC8: case 0xC9: case 0xCA: case 0xCB:
    case 0xCC: case 0xCD: case 0xCE: case 0xCF: {
        v = cpu.r[op2 & 7];
        cpu.r[op2 & 7] = ((v & 0x000000FFUL) << 24) | ((v & 0x0000FF00UL) << 8)
                       | ((v & 0x00FF0000UL) >> 8)  | ((v & 0xFF000000UL) >> 24);
        return;
    }
    case 0x06: return;
    case 0x20: case 0x21: case 0x22: case 0x23:
        tr_modrm(&o); return;
    case 0xA2: {
        u32 leaf = cpu.r[R_AX];
        if (leaf <= 1) {
            cpu.r[R_AX] = 0; cpu.r[R_BX] = 0;
            cpu.r[R_CX] = 0; cpu.r[R_DX] = 0;
        }
        return;
    }

    default:
        {
            FILE *bf = fopen("C:\\BAD0F.LOG", "a");
            if (bf) { fprintf(bf, "BAD0F %02X at %04X:%04X\n",
                              op2, cpu.s[S_CS], (u16)cpu.eip);
                      fclose(bf); }
        }
        stop_reason = 2;
        return;
    }
}


static void op_pushf(void)
{
    u32 v = (t_osz == 32) ? (cpu.flags | 2UL) : ((cpu.flags & 0xFFFF) | 2UL);
    tr_push(t_osz, v);
}
static void op_popf(void)
{
    u32 v = tr_pop(t_osz) | 2;
    if (t_osz == 16) cpu.flags = (cpu.flags & 0xFFFF0000UL) | (v & 0xFFFF);
    else cpu.flags = v;
}
static void op_sahf(void)
{
    u32 a = (cpu.r[R_AX] >> 8) & 0xFF;
    cpu.flags = (cpu.flags & ~(u32)0xD5) | (a & 0xD5);
}
static void op_lahf(void)
{
    cpu.r[R_AX] = (cpu.r[R_AX] & 0xFFFF00FFUL) | ((cpu.flags >> 8) & 0xFF) << 8;
}
static void op_pusha(void)
{
    int sz = (t_osz == 32) ? 32 : 16;
    u32 sp = cpu.r[R_SP];
    tr_push(sz, cpu.r[R_AX]); tr_push(sz, cpu.r[R_CX]);
    tr_push(sz, cpu.r[R_DX]); tr_push(sz, cpu.r[R_BX]);
    tr_push(sz, sp);
    tr_push(sz, cpu.r[R_BP]); tr_push(sz, cpu.r[R_SI]); tr_push(sz, cpu.r[R_DI]);
}
static void op_popa(void)
{
    int sz = (t_osz == 32) ? 32 : 16;
    cpu.r[R_DI] = tr_pop(sz); cpu.r[R_SI] = tr_pop(sz);
    cpu.r[R_BP] = tr_pop(sz);
    tr_pop(sz);
    cpu.r[R_BX] = tr_pop(sz); cpu.r[R_DX] = tr_pop(sz);
    cpu.r[R_CX] = tr_pop(sz); cpu.r[R_AX] = tr_pop(sz);
}

static void tr_int(int n)
{
    cpu_do_int(n);
}
static void op_iret(void)
{
    u32 ip = tr_pop(16);
    u32 cs = tr_pop(16);
    u32 f = tr_pop(16) | 2;
    u16 fcs0 = (u16)cpu.s[S_CS];
    cpu.flags = (cpu.flags & 0xFFFF0000UL) | f;
    cpu.s[S_CS] = (u16)cs; cpu.eip = ip;
}
static void op_enter(void)
{
    u32 sz = (u32)tf16();
    int lvl = tf8() & 31;
    int bpw = (t_osz == 32) ? 4 : 2;
    u32 tmp, bp0;
    int i;
    bp0 = tr_getreg(R_BP, t_osz);
    tr_push(t_osz, bp0);
    tr_setreg(R_BP, t_osz, cpu.r[R_SP]);
    tmp = cpu.r[R_SP];
    for (i = 1; i < lvl; i++) {
        u32 v;
        tmp = (u16)(tmp - (u32)bpw);
        v = tr_memrd(cpu.s[S_SS], (u16)(tmp - (u32)bpw), bpw * 8);
        tr_push(t_osz, v);
    }
    if (lvl) tr_push(t_osz, cpu.r[R_SP]);
    cpu.r[R_SP] = (u16)((u16)cpu.r[R_SP] - (u16)sz);
}
static void op_leave(void)
{
    u32 bp = tr_getreg(R_BP, t_osz);
    cpu.r[R_SP] = (u16)bp;
    tr_setreg(R_BP, t_osz, tr_pop(t_osz));
}
static void op_ret(int addimm, int isfar)
{
    u32 extra = addimm ? (u32)tf16() : 0;
    u16 fcs0 = (u16)cpu.s[S_CS];
    cpu.eip = tr_pop(t_osz);
    if (isfar) {
        cpu.s[S_CS] = (u16)tr_pop(16);
    }
    cpu.r[R_SP] = (u16)((u16)cpu.r[R_SP] + (u16)extra);
}


static void op_daa(void)
{
    u32 al = cpu.r[R_AX] & 0xFF;
    int low = (int)(al & 0x0F);
    int cf = tr_cf(), af = (cpu.flags & F_AF) != 0;
    u32 add = 0;
    u32 r;
    if (low > 9 || af) add += 6;
    if (al + add > 0xFF) cf = 1;
    if (al > 0x99 || tr_cf()) { add += 0x60; cf = 1; }
    r = (al + add) & 0xFF;
    cpu.r[R_AX] = (cpu.r[R_AX] & 0xFFFFFF00UL) | r;
    tr_set6(tr_of(), sgn(r, 8), r == 0, (low > 9 || af) ? 1 : 0, tr_pfv(r), cf);
}
static void op_das(void)
{
    u32 al = cpu.r[R_AX] & 0xFF;
    int low = (int)(al & 0x0F);
    int cf = tr_cf(), af = (cpu.flags & F_AF) != 0;
    u32 r = al;
    int borrow = 0;
    if (low > 9 || af) { if (al < 6) borrow = 1; r -= 6; }
    if (al > 0x99 || cf) { borrow = 1; r -= 0x60; }
    r &= 0xFF;
    cpu.r[R_AX] = (cpu.r[R_AX] & 0xFFFFFF00UL) | r;
    tr_set6(tr_of(), sgn(r, 8), r == 0, (low > 9) ? 1 : af, tr_pfv(r),
            borrow || cf);
}
static void op_aaa(void)
{
    u32 al = cpu.r[R_AX] & 0xFF;
    if ((al & 0x0F) > 9 || (cpu.flags & F_AF)) {
        u32 ah = ((cpu.r[R_AX] >> 8) + 1) & 0xFF;
        cpu.r[R_AX] = (cpu.r[R_AX] & 0xFFFF0000UL) | (ah << 8) | ((al + 6) & 0xFF);
        cpu.flags |= F_AF | F_CF;
    } else cpu.flags &= ~(u32)(F_AF | F_CF);
    cpu.r[R_AX] &= 0x00FF00FFUL;
}
static void op_aas(void)
{
    u32 al = cpu.r[R_AX] & 0xFF;
    if ((al & 0x0F) > 9 || (cpu.flags & F_AF)) {
        u32 ah = ((cpu.r[R_AX] >> 8) - 1) & 0xFF;
        cpu.r[R_AX] = (cpu.r[R_AX] & 0xFFFF0000UL) | (ah << 8) | ((al - 6) & 0xFF);
        cpu.flags |= F_AF | F_CF;
    } else cpu.flags &= ~(u32)(F_AF | F_CF);
    cpu.r[R_AX] &= 0x00FF00FFUL;
}
static void op_aam(u32 base)
{
    u32 al, q, r;
    if (base == 0) { stop_reason = 3; return; }
    al = cpu.r[R_AX] & 0xFF;
    q = al / base; r = al % base;
    cpu.r[R_AX] = (cpu.r[R_AX] & 0xFFFFFF00UL) | ((q & 0xFF) << 8) | (r & 0xFF);
    tr_set6(0, sgn(q, 8), q == 0, 0, tr_pfv(q), 0);
}
static void op_aad(u32 base)
{
    u32 v = (cpu.r[R_AX] & 0xFF) + (cpu.r[R_AX] >> 8) * base;
    cpu.r[R_AX] = (cpu.r[R_AX] & 0xFFFF00FFUL) | (v & 0xFF);
    tr_set6(0, sgn(v, 8), (v & 0xFF) == 0, 0, tr_pfv(v), 0);
}


static u32 str_si(void) { return t_asz == 32 ? cpu.r[R_SI] : (u32)(u16)cpu.r[R_SI]; }
static u32 str_di(void) { return t_asz == 32 ? cpu.r[R_DI] : (u32)(u16)cpu.r[R_DI]; }
static void str_add(int which, int size, int dec)
{
    u32 step = (u32)(size / 8);
    u32 v = (which == 0) ? cpu.r[R_SI] : cpu.r[R_DI];
    if (dec) v -= step; else v += step;
    if (t_asz == 16) v &= 0xFFFFUL;
    if (which == 0) cpu.r[R_SI] = v; else cpu.r[R_DI] = v;
}

static void op_str(int sub)
{
    int size = (sub & 1) ? t_osz : 8;
    int dec = tr_df() ? 1 : 0;
    u16 sseg = (t_segovr >= 0) ? cpu.s[t_segovr] : cpu.s[S_DS];
    if (t_rep && (t_asz == 32 ? cpu.r[R_CX] : (u32)(u16)cpu.r[R_CX]) == 0)
        return;
    for (;;) {
        u32 si = str_si(), di = str_di();
        u16 soff = (u16)si, doff = (u16)di;
        int iscmp = (sub == 6 || sub == 7 || sub == 0xE || sub == 0xF);
        switch (sub) {
        case 4: case 5:
            tr_memwr(cpu.s[S_ES], doff, size, tr_memrd(sseg, soff, size));
            str_add(0, size, dec); str_add(1, size, dec); break;
        case 6: case 7:
            tr_alu(OP_SUB, tr_memrd(sseg, soff, size),
                   tr_memrd(cpu.s[S_ES], doff, size), size, 0);
            str_add(0, size, dec); str_add(1, size, dec); break;
        case 0xA: case 0xB:
            tr_memwr(cpu.s[S_ES], doff, size, tr_getreg(R_AX, size));
            str_add(1, size, dec); break;
        case 0xC: case 0xD:
            tr_setreg(R_AX, size, tr_memrd(sseg, soff, size));
            str_add(0, size, dec); break;
        case 0xE: case 0xF:
            tr_alu(OP_SUB, tr_getreg(R_AX, size),
                   tr_memrd(cpu.s[S_ES], doff, size), size, 0);
            str_add(1, size, dec); break;
        default: break;
        }
        if (!t_rep) break;
        if (t_asz == 32) {
            cpu.r[R_CX]--;
        } else {
            u16 c = (u16)cpu.r[R_CX] - 1;
            cpu.r[R_CX] = (cpu.r[R_CX] & 0xFFFF0000UL) | c;
        }
        if (iscmp) {
            if (t_rep == 0xF3 && !tr_zf()) break;
            if (t_rep == 0xF2 &&  tr_zf()) break;
        }
        if ((t_asz == 32 ? cpu.r[R_CX] : (u32)(u16)cpu.r[R_CX]) == 0) break;
    }
}


static u16 tr_inw(u16 p)  { u16 v = inportb(p); v |= (u16)inportb((u16)(p + 1)) << 8; return v; }
static u32 tr_ind(u16 p)  { return (u32)tr_inw(p) | ((u32)tr_inw((u16)(p + 2)) << 16); }
static void tr_outw(u16 p, u16 v) { outportb(p, (u8)v); outportb((u16)(p + 1), (u8)(v >> 8)); }
static void tr_outd(u16 p, u32 v) { tr_outw(p, (u16)v); tr_outw((u16)(p + 2), (u16)(v >> 16)); }

static int tr_exec_one(void)
{
    int op;
    t_osz = 16; t_asz = 16; t_segovr = -1; t_rep = 0;

    for (;;) {
        op = tf8();
        switch (op) {


        case 0x26: t_segovr = S_ES; continue;
        case 0x2E: t_segovr = S_CS; continue;
        case 0x36: t_segovr = S_SS; continue;
        case 0x3E: t_segovr = S_DS; continue;
        case 0x64: t_segovr = S_FS; continue;
        case 0x65: t_segovr = S_GS; continue;
        case 0x66: t_osz = (t_osz == 16) ? 32 : 16; continue;
        case 0x67: t_asz = (t_asz == 16) ? 32 : 16; continue;
        case 0xF2: t_rep = 0xF2; continue;
        case 0xF3: t_rep = 0xF3; continue;
        case 0xF0: continue;


        case 0x6E: t_segovr = S_CS; continue;
        case 0x6F: t_segovr = S_SS; continue;


        case 0x00: alu_rm_reg(OP_ADD, 8); break;   case 0x01: alu_rm_reg(OP_ADD, t_osz); break;
        case 0x02: alu_reg_rm(OP_ADD, 8); break;   case 0x03: alu_reg_rm(OP_ADD, t_osz); break;
        case 0x08: alu_rm_reg(OP_OR, 8);  break;   case 0x09: alu_rm_reg(OP_OR, t_osz); break;
        case 0x0A: alu_reg_rm(OP_OR, 8);  break;   case 0x0B: alu_reg_rm(OP_OR, t_osz); break;
        case 0x10: alu_rm_reg(OP_ADC, 8); break;   case 0x11: alu_rm_reg(OP_ADC, t_osz); break;
        case 0x12: alu_reg_rm(OP_ADC, 8); break;   case 0x13: alu_reg_rm(OP_ADC, t_osz); break;
        case 0x18: alu_rm_reg(OP_SBB, 8); break;   case 0x19: alu_rm_reg(OP_SBB, t_osz); break;
        case 0x1A: alu_reg_rm(OP_SBB, 8); break;   case 0x1B: alu_reg_rm(OP_SBB, t_osz); break;
        case 0x20: alu_rm_reg(OP_AND, 8); break;   case 0x21: alu_rm_reg(OP_AND, t_osz); break;
        case 0x22: alu_reg_rm(OP_AND, 8); break;   case 0x23: alu_reg_rm(OP_AND, t_osz); break;
        case 0x28: alu_rm_reg(OP_SUB, 8); break;   case 0x29: alu_rm_reg(OP_SUB, t_osz); break;
        case 0x2A: alu_reg_rm(OP_SUB, 8); break;   case 0x2B: alu_reg_rm(OP_SUB, t_osz); break;
        case 0x30: alu_rm_reg(OP_XOR, 8); break;   case 0x31: alu_rm_reg(OP_XOR, t_osz); break;
        case 0x32: alu_reg_rm(OP_XOR, 8); break;   case 0x33: alu_reg_rm(OP_XOR, t_osz); break;
        case 0x38: alu_rm_reg(OP_CMP, 8); break;   case 0x39: alu_rm_reg(OP_CMP, t_osz); break;
        case 0x3A: alu_reg_rm(OP_CMP, 8); break;   case 0x3B: alu_reg_rm(OP_CMP, t_osz); break;
        case 0x04: alu_acc(OP_ADD, 8, tf8());   break;
        case 0x05: alu_acc(OP_ADD, t_osz, tfimm()); break;
        case 0x0C: alu_acc(OP_OR, 8, tf8());    break;
        case 0x0D: alu_acc(OP_OR, t_osz, tfimm()); break;
        case 0x14: alu_acc(OP_ADC, 8, tf8());   break;
        case 0x15: alu_acc(OP_ADC, t_osz, tfimm()); break;
        case 0x1C: alu_acc(OP_SBB, 8, tf8());   break;
        case 0x1D: alu_acc(OP_SBB, t_osz, tfimm()); break;
        case 0x24: alu_acc(OP_AND, 8, tf8());   break;
        case 0x25: alu_acc(OP_AND, t_osz, tfimm()); break;
        case 0x2C: alu_acc(OP_SUB, 8, tf8());   break;
        case 0x2D: alu_acc(OP_SUB, t_osz, tfimm()); break;
        case 0x34: alu_acc(OP_XOR, 8, tf8());   break;
        case 0x35: alu_acc(OP_XOR, t_osz, tfimm()); break;
        case 0x3C: alu_acc(OP_CMP, 8, tf8());   break;
        case 0x3D: alu_acc(OP_CMP, t_osz, tfimm()); break;
        case 0x06: op_seg(S_ES, 1); break;
        case 0x07: op_seg(S_ES, 0); break;
        case 0x0E: op_seg(S_CS, 1); break;
        case 0x17: op_seg(S_SS, 0); break;
        case 0x16: op_seg(S_SS, 1); break;
        case 0x1F: op_seg(S_DS, 0); break;
        case 0x1E: op_seg(S_DS, 1); break;
        case 0x0F: grp_0f(); break;


        case 0x80: grp_8x(8, 0); break;
        case 0x82: grp_8x(8, 0); break;
        case 0x81: grp_8x(t_osz, 0); break;
        case 0x83: grp_8x(t_osz, 1); break;
        case 0x84: case 0x85: {
            struct TOp o;
            int sz = (op == 0x84) ? 8 : t_osz;
            tr_modrm(&o);
            tr_alu(OP_AND, tr_getop(&o, sz), tr_getreg(xreg, sz), sz, 0);
            break;
        }
        case 0x86: op_xchg(8); break;
        case 0x87: op_xchg(t_osz); break;
        case 0x88: op_mov(8, 0); break;
        case 0x89: op_mov(t_osz, 0); break;
        case 0x8A: op_mov(8, 1); break;
        case 0x8B: op_mov(t_osz, 1); break;
        case 0x8C: op_rm_sreg(); break;
        case 0x8D: op_lea(); break;
        case 0x8E: op_mov_sreg(); break;
        case 0x8F: {
            struct TOp o;
            tr_modrm(&o);
            tr_setop(&o, 16, tr_pop(16));
            break;
        }
        case 0x62: {
            struct TOp o;
            u32 v, lo, hi;
            int sz = t_osz;
            tr_modrm(&o);
            v = tr_getreg(xreg, sz);
            lo = tr_getop(&o, sz);
            hi = tr_memrd(o.seg, (u16)(o.off + (sz / 8)), sz);
            if ((s32)sext(v, sz) < (s32)sext(lo, sz) ||
                (s32)sext(v, sz) > (s32)sext(hi, sz))
                tr_int(5);
            break;
        }


        case 0xA0: op_mov_moffs(8, 0); break;
        case 0xA1: op_mov_moffs(t_osz, 0); break;
        case 0xA2: op_mov_moffs(8, 1); break;
        case 0xA3: op_mov_moffs(t_osz, 1); break;
        case 0xA8: alu_acc(OP_AND, 8, tf8()); break;
        case 0xA9: alu_acc(OP_AND, t_osz, tfimm()); break;
        case 0xB0: case 0xB1: case 0xB2: case 0xB3:
        case 0xB4: case 0xB5: case 0xB6: case 0xB7:
            op_mov_reg_imm(op, 8); break;
        case 0xB8: case 0xB9: case 0xBA: case 0xBB:
        case 0xBC: case 0xBD: case 0xBE: case 0xBF:
            op_mov_reg_imm(op, t_osz); break;
        case 0x69: op_imul3(tfimm()); break;
        case 0x6B: op_imul3(sext((u32)(s8)tf8(), 8) & tr_mask(t_osz)); break;


        case 0xA4: case 0xA5: op_str(op & 0xF); break;
        case 0xA6: case 0xA7: op_str(op & 0xF); break;
        case 0xAA: case 0xAB: op_str(op & 0xF); break;
        case 0xAC: case 0xAD: op_str(op & 0xF); break;
        case 0xAE: case 0xAF: op_str(op & 0xF); break;

        case 0x40: case 0x41: case 0x42: case 0x43:
        case 0x44: case 0x45: case 0x46: case 0x47:
        case 0x48: case 0x49: case 0x4A: case 0x4B:
        case 0x4C: case 0x4D: case 0x4E: case 0x4F: {
            struct TOp o;
            o.isreg = 1; o.reg = op & 7;
            op_incdec(t_osz, &o, (op & 8) ? 1 : 0);
            break;
        }


        case 0x50: case 0x51: case 0x52: case 0x53:
        case 0x54: case 0x55: case 0x56: case 0x57:
            tr_push(t_osz, tr_getreg(op & 7, t_osz)); break;
        case 0x58: case 0x59: case 0x5A: case 0x5B:
        case 0x5C: case 0x5D: case 0x5E: case 0x5F:
            tr_setreg(op & 7, t_osz, tr_pop(t_osz)); break;
        case 0x68: tr_push(t_osz, tfimm()); break;
        case 0x6A: tr_push(t_osz, sext((u32)(s8)tf8(), 8) & tr_mask(t_osz)); break;
        case 0x60: op_pusha(); break;
        case 0x61: op_popa(); break;
        case 0x9C: op_pushf(); break;
        case 0x9D: op_popf(); break;


        case 0xC0: grp_shift(8, 0); break;
        case 0xC1: grp_shift(t_osz, 0); break;
        case 0xC6: grp_mov_imm(8); break;
        case 0xC7: grp_mov_imm(t_osz); break;


        case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75:
        case 0x76: case 0x77: case 0x78: case 0x79: case 0x7A: case 0x7B:
        case 0x7C: case 0x7D: case 0x7E: case 0x7F: {
            u32 rel = (u32)(s8)tf8();
            u32 nxt = cpu.eip;
            if (cc_test(op & 15))
                cpu.eip = (nxt + (s16)rel) & 0xFFFF;
            break;
        }


        case 0x90: break;
        case 0x91: case 0x92: case 0x93: case 0x94:
        case 0x95: case 0x96: case 0x97:
            if (t_osz == 32) {
                u32 a = cpu.r[R_AX], b = cpu.r[op & 7];
                cpu.r[R_AX] = b; cpu.r[op & 7] = a;
            } else {
                u32 a = cpu.r[R_AX] & 0xFFFF, b = cpu.r[op & 7] & 0xFFFF;
                cpu.r[R_AX]   = (cpu.r[R_AX]   & 0xFFFF0000UL) | b;
                cpu.r[op & 7] = (cpu.r[op & 7] & 0xFFFF0000UL) | a;
            }
            break;
        case 0x98:
            if (t_osz == 32) cpu.r[R_AX] = sext(cpu.r[R_AX] & 0xFFFF, 16);
            else cpu.r[R_AX] = (cpu.r[R_AX] & 0xFFFF0000UL)
                             | (sext((u16)cpu.r[R_AX], 8) & 0xFFFF);
            break;
        case 0x99:
            if (t_osz == 32)
                cpu.r[R_DX] = (cpu.r[R_AX] & 0x80000000UL) ? 0xFFFFFFFFUL : 0;
            else
                cpu.r[R_DX] = ((u16)cpu.r[R_AX] & 0x8000) ? 0xFFFF : 0;
            break;
        case 0x9A: {
            u16 off = tf16(), seg = tf16();
            if (seg >= 0xC000) {
                far_call_host(seg, off);
                break;
            }
            tr_push(16, cpu.s[S_CS]); tr_push(t_osz, cpu.eip);
            cpu.s[S_CS] = seg; cpu.eip = off;
            break;
        }
        case 0x9E: op_sahf(); break;
        case 0x9F: op_lahf(); break;


        case 0xC2: op_ret(1, 0); break;
        case 0xC3: op_ret(0, 0); break;
        case 0xC4: {
            struct TOp o;
            tr_modrm(&o);
            tr_setreg(xreg, t_osz, tr_getop(&o, 16));
            cpu.s[S_ES] = (u16)tr_memrd(o.seg, (u16)(o.off + 2), 16);
            break;
        }
        case 0xC5: {
            struct TOp o;
            tr_modrm(&o);
            tr_setreg(xreg, t_osz, tr_getop(&o, 16));
            cpu.s[S_DS] = (u16)tr_memrd(o.seg, (u16)(o.off + 2), 16);
            break;
        }
        case 0xC8: op_enter(); break;
        case 0xC9: op_leave(); break;
        case 0xCA: op_ret(1, 1); break;
        case 0xCB: op_ret(0, 1); break;
        case 0xCC: tr_int(3); break;
        case 0xCD: {
            int pre = stop_reason;
            u8 v = tf8();
            tr_int((int)v);
            if (stop_reason && !pre) cpu.eip -= 2;
            break;
        }
        case 0xCE: if (tr_of()) tr_int(4); break;
        case 0xCF: op_iret(); break;

        case 0xD0: grp_shift(8, 2); break;
        case 0xD1: grp_shift(t_osz, 2); break;
        case 0xD2: grp_shift(8, 1); break;
        case 0xD3: grp_shift(t_osz, 1); break;
        case 0xD4: { u32 b = tf8(); if (b == 0) op_daa(); else op_aam(b); break; }
        case 0xD5: { u32 b = tf8(); if (b == 0) op_das(); else op_aad(b); break; }
        case 0xD6: break;
        case 0xD7: {
            u16 segb = (t_segovr >= 0) ? cpu.s[t_segovr] : cpu.s[S_DS];
            u32 base = (t_asz == 32) ? cpu.r[R_BX] : (u32)(u16)cpu.r[R_BX];
            tr_setreg(R_AX, 8, tr_memrd(segb, (u16)(base + (cpu.r[R_AX] & 0xFF)), 8));
            break;
        }
        case 0xD8: case 0xD9: case 0xDA: case 0xDB:
        case 0xDC: case 0xDD: case 0xDE: case 0xDF:
            break;

        case 0xE0: case 0xE1: case 0xE2: {
            u32 rel = (u32)(s8)tf8();
            u32 nxt = cpu.eip;
            u32 c;
            if (t_asz == 32) { c = cpu.r[R_CX] - 1; cpu.r[R_CX] = c; }
            else {
                u16 c16 = (u16)cpu.r[R_CX] - 1;
                cpu.r[R_CX] = (cpu.r[R_CX] & 0xFFFF0000UL) | c16;
                c = c16;
            }
            c &= (t_asz == 32) ? 0xFFFFFFFFUL : 0xFFFFUL;
            if ((op == 0xE0 && c && !tr_zf()) ||
                (op == 0xE1 && c &&  tr_zf()) ||
                (op == 0xE2 && c))
                cpu.eip = (nxt + (s16)rel) & 0xFFFF;
            break;
        }
        case 0xE3: {
            u32 rel = (u32)(s8)tf8();
            u32 nxt = cpu.eip;
            u32 c = (t_asz == 32) ? cpu.r[R_CX] : (u32)(u16)cpu.r[R_CX];
            if (c == 0)
                cpu.eip = (nxt + (s16)rel) & 0xFFFF;
            break;
        }
        case 0xE4: { u16 p = tf8();
                     tr_setreg(R_AX, 8, (u32)inportb(p)); break; }
        case 0xE5: { u16 p = tf8();
                     tr_setreg(R_AX, t_osz,
                               (t_osz == 32) ? tr_ind(p) : (u32)tr_inw(p)); break; }
        case 0xE6: { u16 p = tf8();
                     outportb(p, (u8)(cpu.r[R_AX] & 0xFF)); break; }
        case 0xE7: { u16 p = tf8();
                     if (t_osz == 32) tr_outd(p, cpu.r[R_AX]);
                     else tr_outw(p, (u16)cpu.r[R_AX]);
                     break; }
        case 0xE8: {
            u32 rel = (t_osz == 32) ? tf32() : (u32)(u16)(s16)tf16();
            u32 nxt = cpu.eip;
            tr_push(t_osz, nxt);
            cpu.eip = (t_osz == 32) ? (nxt + rel)
                                    : ((nxt + (s16)(u16)rel) & 0xFFFF);
            break;
        }
        case 0xE9: {
            u32 rel = (t_osz == 32) ? tf32() : (u32)(u16)(s16)tf16();
            u32 nxt = cpu.eip;
            cpu.eip = (t_osz == 32) ? (nxt + rel)
                                    : ((nxt + (s16)(u16)rel) & 0xFFFF);
            break;
        }
        case 0xEA: {
            u16 off = tf16(), seg = tf16();
            cpu.s[S_CS] = seg; cpu.eip = off;
            break;
        }
        case 0xEB: {
            u32 rel = (u32)(u16)(s16)(s8)tf8();
            u32 nxt = cpu.eip;
            cpu.eip = (nxt + (s16)(u16)rel) & 0xFFFF;
            break;
        }
        case 0xEC: tr_setreg(R_AX, 8, (u32)inportb((u16)(cpu.r[R_DX] & 0xFFFF))); break;
        case 0xED: { u16 p = (u16)(cpu.r[R_DX] & 0xFFFF);
                     tr_setreg(R_AX, t_osz,
                               (t_osz == 32) ? tr_ind(p) : (u32)tr_inw(p)); break; }
        case 0xEE: outportb((u16)(cpu.r[R_DX] & 0xFFFF), (u8)(cpu.r[R_AX] & 0xFF)); break;
        case 0xEF: { u16 p = (u16)(cpu.r[R_DX] & 0xFFFF);
                     if (t_osz == 32) tr_outd(p, cpu.r[R_AX]);
                     else tr_outw(p, (u16)cpu.r[R_AX]);
                     break; }


        case 0xF1: stop_reason = 2; break;
        case 0xF4: break;
        case 0xF5: cpu.flags ^= F_CF; break;
        case 0xF6: case 0xF7: {
            struct TOp o;
            tr_modrm(&o);
            grp_f67(xreg, (op == 0xF6) ? 8 : t_osz, &o);
            break;
        }
        case 0xF8: cpu.flags &= ~F_CF; break;
        case 0xF9: cpu.flags |= F_CF; break;
        case 0xFA: cpu.flags &= ~0x200UL; break;
        case 0xFB: cpu.flags |= 0x200UL; break;
        case 0xFC: cpu.flags &= ~0x400UL; break;
        case 0xFD: cpu.flags |= 0x400UL; break;
        case 0xFE: {
            struct TOp o;
            int sub;
            tr_modrm(&o);
            sub = xreg;
            op_incdec(8, &o, sub);
            break;
        }
        case 0xFF: {
            struct TOp o;
            int sub;
            tr_modrm(&o);
            sub = xreg;
            grp_ff(sub, &o);
            break;
        }

        default:
            stop_reason = 2;
            break;
        }
        break;
    }
    return stop_reason;
}

int cpu_step_tr(void)
{
    u16 scs, sip;

    stop_reason = 0;
    mem_hit = 0;
    in_step = 1;
    bp_break_hit = -1;

    scs = cpu.s[S_CS];
    sip = (u16)cpu.eip;

    if (bp_pre_check()) {
        in_step = 0;
        stop_reason = 3;
        stop_addr_seg = scs;
        stop_addr_off = sip;
        return 3;
    }

    {
        int ter = tr_exec_one();
        if (ter) {
            in_step = 0;
            stop_addr_seg = scs;
            stop_addr_off = sip;
            return stop_reason;
        }
    }

    if (mem_hit) {
        in_step = 0;
        stop_reason = 3;
        stop_addr_seg = mem_hit_seg;
        stop_addr_off = mem_hit_off;
        return 3;
    }
    if (bp_post_check(scs, sip)) {
        in_step = 0;
        stop_reason = 3;
        stop_addr_seg = cpu.s[S_CS];
        stop_addr_off = (u16)cpu.eip;
        return 3;
    }
    in_step = 0;
    return 0;
}
