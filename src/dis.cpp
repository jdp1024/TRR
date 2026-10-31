#include "tr.h"

static const char *rn8[8]  = {"AL","CL","DL","BL","AH","CH","DH","BH"};
static const char *rn16[8] = {"AX","CX","DX","BX","SP","BP","SI","DI"};
static const char *rn32[8] = {"EAX","ECX","EDX","EBX","ESP","EBP","ESI","EDI"};
static const char *srn[6]  = {"ES","CS","SS","DS","FS","GS"};
static const char *cc[16] = {"O","NO","B","NB","Z","NE","BE","A",
                             "S","NS","P","NP","L","GE","LE","G"};
static const char *alu[8] = {"ADD","OR","ADC","SBB","AND","SUB","XOR","CMP"};
static const char *shn[8] = {"ROL","ROR","RCL","RCR","SHL","SHR","???","SAR"};
static const char *grp3[8] = {"TEST","TEST","NOT","NEG","MUL","IMUL","DIV","IDIV"};

struct Dec {
    u8  raw[16];
    int len;
    u8  op, op2;
    int segovr;
    int rep;
    int lock;
    int osz;
    int asz;
    u8  modrm;
    int mod, reg, rm;
    int rm_is_reg;
    u16 ea;
    int ea_seg;
    int ea_has_disp;
    u32 disp;
    int imm_size;
    u32 imm;
    int rel_size;
    s32 rel;
    u8  twobyte;
    int bad;
    char mnem[16];
    char ops[96];
};

static u8 getb(Dec *d, u16 seg, u16 *off)
{
    u8 v = rd8(seg, *off);
    if (d->len < 16) d->raw[d->len] = v;
    d->len++;
    (*off)++;
    return v;
}

static u16 getw(Dec *d, u16 seg, u16 *off)
{
    u16 v = getb(d, seg, off);
    v |= (u16)getb(d, seg, off) << 8;
    return v;
}

static u32 getd(Dec *d, u16 seg, u16 *off)
{
    u32 v = getw(d, seg, off);
    v |= (u32)getw(d, seg, off) << 16;
    return v;
}

static const char *regname(int i, int size)
{
    if (size == 8) return rn8[i & 7];
    if (size == 32) return rn32[i & 7];
    return rn16[i & 7];
}

static void app(char *s, int max, const char *t)
{
    int n = strlen(s);
    if (n < max - 1) strncat(s, t, max - 1 - n);
}

static void append_num(char *s, int max, u32 v)
{
    char b[16];
    if (v < 10) sprintf(b, "%lu", v);
    else sprintf(b, "%04lX", v);
    app(s, max, b);
}

static void append_rm(Dec *d, char *s, int max, int size, int kw = 0)
{
    char b[48];
    if (d->mod == 3) {
        app(s, max, regname(d->rm, size));
        return;
    }

    app(s, max, "[");
    if (kw == 8) app(s, max, "BYTE ");
    else if (kw == 16) app(s, max, "WORD ");
    else if (kw == 32) app(s, max, "DWORD ");
    if (d->segovr >= 0) {
        app(s, max, srn[d->segovr]);
        app(s, max, ":");
    }

    switch (d->rm) {
    case 0: strcpy(b, "BX+SI"); break;
    case 1: strcpy(b, "DI+BX"); break;
    case 2: strcpy(b, "SI+BP"); break;
    case 3: strcpy(b, "BP+DI"); break;
    case 4: strcpy(b, "SI"); break;
    case 5: strcpy(b, "DI"); break;
    case 6: strcpy(b, "BP"); break;
    default: strcpy(b, "BX"); break;
    }
    if (d->mod == 1) {
        s32 dv = (signed char)(d->disp & 0xFF);
        app(s, max, b);
        if (dv < 0) sprintf(b, "-%02lX", (unsigned long)(-dv));
        else sprintf(b, "+%04lX", (unsigned long)dv);
        app(s, max, b);
    } else if (d->mod == 2) {
        app(s, max, b);
        sprintf(b, "+%04lX", (unsigned long)d->disp);
        app(s, max, b);
    } else {
        if (d->rm == 6) { sprintf(b, "%04lX", (unsigned long)d->disp); app(s, max, b); }
        else app(s, max, b);
    }
    app(s, max, "]");
}

static void append_imm(char *s, int max, u32 v, int size)
{
    char b[24];
    if (size == 8) sprintf(b, "%02lX", v & 0xFF);
    else if (size == 32) sprintf(b, "%08lX", v);
    else sprintf(b, "%04lX", v & 0xFFFF);
    app(s, max, b);
}

static void get_modrm(Dec *d, u16 seg, u16 *off)
{
    d->modrm = getb(d, seg, off);
    d->mod = d->modrm >> 6;
    d->reg = (d->modrm >> 3) & 7;
    d->rm  = d->modrm & 7;
    d->rm_is_reg = (d->mod == 3);
    d->ea = 0; d->disp = 0;
    if (d->mod == 3) return;
    if (d->mod == 1) {
        d->disp = (u8)getb(d, seg, off);
        d->ea_has_disp = 1;
    } else if (d->mod == 2 || d->rm == 6) {
        d->disp = getw(d, seg, off);
        d->ea_has_disp = 1;
    }
    d->ea_seg = (d->rm == 2 || d->rm == 3) ? S_SS : S_DS;
}

static void get_imm(Dec *d, u16 seg, u16 *off, int size)
{
    d->imm_size = size;
    if (size == 8) d->imm = getb(d, seg, off);
    else if (size == 16) d->imm = getw(d, seg, off);
    else d->imm = getd(d, seg, off);
}

static void decode(Dec *d, u16 seg, u16 off0)
{
    u16 off = off0;
    u8 b;
    int i;
    memset(d, 0, sizeof(Dec));
    d->segovr = -1;
    d->osz = 16;
    d->asz = 16;

    for (i = 0; i < 8; i++) {
        b = getb(d, seg, &off);
        if (b == 0x26) d->segovr = S_ES;
        else if (b == 0x2E) d->segovr = S_CS;
        else if (b == 0x36) d->segovr = S_SS;
        else if (b == 0x3E) d->segovr = S_DS;
        else if (b == 0x64) d->segovr = S_FS;
        else if (b == 0x65) d->segovr = S_GS;
        else if (b == 0x66) d->osz = 32;
        else if (b == 0x67) d->asz = 32;
        else break;
    }
    d->op = b;
    if (b == 0xF3 || b == 0xF2 || b == 0xF0) {
        strcpy(d->mnem, b == 0xF3 ? "REPZ" : (b == 0xF2 ? "REPNZ" : "LOCK"));
        d->ops[0] = 0;
        return;
    }
    if (b == 0x0F) {
        d->twobyte = 1;
        d->op2 = getb(d, seg, &off);
    }

    d->mnem[0] = 0; d->ops[0] = 0;

    if (!d->twobyte) {
        u8 op = d->op;
        int w = op & 1;
        int osz = w ? d->osz : 8;

        if (op < 0x40 && (op & 7) < 4 && op != 0x0F) {
            int aluop = (op >> 3) & 7;
            int dir = (op >> 1) & 1;
            strcpy(d->mnem, alu[aluop]);
            get_modrm(d, seg, &off);
            if (dir) {

                app(d->ops, sizeof d->ops, regname(d->reg, osz));
                app(d->ops, sizeof d->ops, ",");
                append_rm(d, d->ops, sizeof d->ops, osz);
            } else {
                append_rm(d, d->ops, sizeof d->ops, osz);
                app(d->ops, sizeof d->ops, ",");
                app(d->ops, sizeof d->ops, regname(d->reg, osz));
            }
            d->imm_size = 0;
        } else switch (op) {
        case 0x04: case 0x0C: case 0x14: case 0x1C:
        case 0x24: case 0x2C: case 0x34: case 0x3C:
        case 0x05: case 0x0D: case 0x15: case 0x1D:
        case 0x25: case 0x2D: case 0x35: case 0x3D:
            strcpy(d->mnem, alu[(op >> 3) & 7]);
            app(d->ops, sizeof d->ops, regname(0, (op & 1) ? d->osz : 8));
            app(d->ops, sizeof d->ops, ",");
            get_imm(d, seg, &off, (op & 1) ? d->osz : 8);
            append_imm(d->ops, sizeof d->ops, d->imm, d->imm_size);
            break;
        case 0x06: strcpy(d->mnem, "PUSH"); app(d->ops, sizeof d->ops, "ES"); break;
        case 0x07: strcpy(d->mnem, "POP");  app(d->ops, sizeof d->ops, "ES"); break;
        case 0x0E: strcpy(d->mnem, "PUSH"); app(d->ops, sizeof d->ops, "CS"); break;
        case 0x16: strcpy(d->mnem, "PUSH"); app(d->ops, sizeof d->ops, "SS"); break;
        case 0x17: strcpy(d->mnem, "POP");  app(d->ops, sizeof d->ops, "SS"); break;
        case 0x1E: strcpy(d->mnem, "PUSH"); app(d->ops, sizeof d->ops, "DS"); break;
        case 0x1F: strcpy(d->mnem, "POP");  app(d->ops, sizeof d->ops, "DS"); break;
        case 0x27: strcpy(d->mnem, "DAA"); break;
        case 0x2F: strcpy(d->mnem, "DAS"); break;
        case 0x37: strcpy(d->mnem, "AAA"); break;
        case 0x3F: strcpy(d->mnem, "AAS"); break;
        case 0x40: case 0x41: case 0x42: case 0x43:
        case 0x44: case 0x45: case 0x46: case 0x47:
        case 0x48: case 0x49: case 0x4A: case 0x4B:
        case 0x4C: case 0x4D: case 0x4E: case 0x4F:
            strcpy(d->mnem, (op & 8) ? "DEC" : "INC");
            app(d->ops, sizeof d->ops, regname(op & 7, d->osz));
            break;
        case 0x50: case 0x51: case 0x52: case 0x53:
        case 0x54: case 0x55: case 0x56: case 0x57:
        case 0x58: case 0x59: case 0x5A: case 0x5B:
        case 0x5C: case 0x5D: case 0x5E: case 0x5F:
            strcpy(d->mnem, (op & 8) ? "POP" : "PUSH");
            app(d->ops, sizeof d->ops, regname(op & 7, d->osz));
            break;
        case 0x60: strcpy(d->mnem, "PUSHA"); break;
        case 0x61: strcpy(d->mnem, "POPA"); break;
        case 0x62: strcpy(d->mnem, "BOUND"); get_modrm(d, seg, &off);
            app(d->ops, sizeof d->ops, regname(d->reg, d->osz)); app(d->ops, sizeof d->ops, ",");
            append_rm(d, d->ops, sizeof d->ops, d->osz); break;
        case 0x68: strcpy(d->mnem, "PUSH");
            get_imm(d, seg, &off, d->osz);
            append_imm(d->ops, sizeof d->ops, d->imm, d->osz); break;
        case 0x69: case 0x6B:
            strcpy(d->mnem, "IMUL"); get_modrm(d, seg, &off);
            app(d->ops, sizeof d->ops, regname(d->reg, d->osz));
            app(d->ops, sizeof d->ops, ","); append_rm(d, d->ops, sizeof d->ops, d->osz);
            app(d->ops, sizeof d->ops, ",");
            get_imm(d, seg, &off, op == 0x6B ? 8 : d->osz);
            append_imm(d->ops, sizeof d->ops, d->imm, op == 0x6B ? 8 : d->osz);
            break;
        case 0x6A: strcpy(d->mnem, "PUSH");
            get_imm(d, seg, &off, 8);
            append_imm(d->ops, sizeof d->ops, d->imm, 8); break;
        case 0x70: case 0x71: case 0x72: case 0x73:
        case 0x74: case 0x75: case 0x76: case 0x77:
        case 0x78: case 0x79: case 0x7A: case 0x7B:
        case 0x7C: case 0x7D: case 0x7E: case 0x7F:
            sprintf(d->mnem, "J%s", cc[op & 15]);
            d->rel = (signed char)getb(d, seg, &off); d->rel_size = 8;
            sprintf(d->ops, "%04X", (u16)(off + d->rel));
            break;
        case 0x80: case 0x81: case 0x82: case 0x83:
            get_modrm(d, seg, &off);
            strcpy(d->mnem, alu[d->reg]);
            append_rm(d, d->ops, sizeof d->ops, op == 0x80 || op == 0x82 ? 8 : d->osz,
                      op == 0x80 || op == 0x82 ? 8 : d->osz);
            app(d->ops, sizeof d->ops, ",");
            get_imm(d, seg, &off, op == 0x83 ? 8 : (op == 0x80 || op == 0x82 ? 8 : d->osz));
            append_imm(d->ops, sizeof d->ops, d->imm, d->imm_size);
            break;
        case 0x84: case 0x85:
            strcpy(d->mnem, "TEST"); get_modrm(d, seg, &off);
            append_rm(d, d->ops, sizeof d->ops, osz); app(d->ops, sizeof d->ops, ",");
            app(d->ops, sizeof d->ops, regname(d->reg, osz)); break;
        case 0x86: case 0x87:
            strcpy(d->mnem, "XCHG"); get_modrm(d, seg, &off);
            append_rm(d, d->ops, sizeof d->ops, osz); app(d->ops, sizeof d->ops, ",");
            app(d->ops, sizeof d->ops, regname(d->reg, osz)); break;
        case 0x88: case 0x89: case 0x8A: case 0x8B:
            strcpy(d->mnem, "MOV"); get_modrm(d, seg, &off);
            if (op & 2) {
                app(d->ops, sizeof d->ops, regname(d->reg, osz));
                app(d->ops, sizeof d->ops, ",");
                append_rm(d, d->ops, sizeof d->ops, osz);
            } else {
                append_rm(d, d->ops, sizeof d->ops, osz);
                app(d->ops, sizeof d->ops, ",");
                app(d->ops, sizeof d->ops, regname(d->reg, osz));
            }
            break;
        case 0x8C:
            strcpy(d->mnem, "MOV"); get_modrm(d, seg, &off);
            append_rm(d, d->ops, sizeof d->ops, 16); app(d->ops, sizeof d->ops, ",");
            app(d->ops, sizeof d->ops, srn[d->reg & 7]); break;
        case 0x8D:
            strcpy(d->mnem, "LEA"); get_modrm(d, seg, &off);
            app(d->ops, sizeof d->ops, regname(d->reg, d->osz)); app(d->ops, sizeof d->ops, ",");
            append_rm(d, d->ops, sizeof d->ops, d->osz); break;
        case 0x8E:
            strcpy(d->mnem, "MOV"); get_modrm(d, seg, &off);
            app(d->ops, sizeof d->ops, srn[d->reg & 7]); app(d->ops, sizeof d->ops, ",");
            append_rm(d, d->ops, sizeof d->ops, 16); break;
        case 0x8F:
            strcpy(d->mnem, "POP"); get_modrm(d, seg, &off);
            append_rm(d, d->ops, sizeof d->ops, d->osz); break;
        case 0x90: strcpy(d->mnem, "NOP"); break;
        case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97:
            strcpy(d->mnem, "XCHG");
            app(d->ops, sizeof d->ops, regname(0, d->osz)); app(d->ops, sizeof d->ops, ",");
            app(d->ops, sizeof d->ops, regname(op & 7, d->osz)); break;
        case 0x98: strcpy(d->mnem, d->osz == 32 ? "CWDE" : "CBW"); break;
        case 0x99: strcpy(d->mnem, d->osz == 32 ? "CDQ" : "CWD"); break;
        case 0x9A:
            strcpy(d->mnem, "CALL FAR");
            d->imm = getw(d, seg, &off);
            d->imm |= (u32)getw(d, seg, &off) << 16;
            sprintf(d->ops, "%04X:%04X", (unsigned)(d->imm >> 16), (unsigned)(d->imm & 0xFFFF));
            break;
        case 0x9B: strcpy(d->mnem, "WAIT"); break;
        case 0x9C: strcpy(d->mnem, "PUSHF"); break;
        case 0x9D: strcpy(d->mnem, "POPF"); break;
        case 0x9E: strcpy(d->mnem, "SAHF"); break;
        case 0x9F: strcpy(d->mnem, "LAHF"); break;
        case 0xA0: case 0xA1: case 0xA2: case 0xA3:
            strcpy(d->mnem, "MOV");
            d->disp = getw(d, seg, &off);
            {
                char b[24];
                b[0] = 0;
                if (d->segovr >= 0) {
                    app(b, sizeof b, srn[d->segovr]);
                    app(b, sizeof b, ":");
                }
                if (!(op & 2)) {
                    app(d->ops, sizeof d->ops, regname(0, osz));
                    app(d->ops, sizeof d->ops, ",");
                    app(d->ops, sizeof d->ops, "[");
                    sprintf(b + strlen(b), "%04lX", (unsigned long)d->disp);
                    app(d->ops, sizeof d->ops, b);
                    app(d->ops, sizeof d->ops, "]");
                } else {
                    app(d->ops, sizeof d->ops, "[");
                    sprintf(b + strlen(b), "%04lX", (unsigned long)d->disp);
                    app(d->ops, sizeof d->ops, b);
                    app(d->ops, sizeof d->ops, "]");
                    app(d->ops, sizeof d->ops, ",");
                    app(d->ops, sizeof d->ops, regname(0, osz));
                }
            }
            break;
        case 0xA4: strcpy(d->mnem, "MOVSB"); break;
        case 0xA5: strcpy(d->mnem, d->osz == 32 ? "MOVSD" : "MOVSW"); break;
        case 0xA6: strcpy(d->mnem, "CMPSB"); break;
        case 0xA7: strcpy(d->mnem, d->osz == 32 ? "CMPSD" : "CMPSW"); break;
        case 0xA8: case 0xA9:
            strcpy(d->mnem, "TEST");
            app(d->ops, sizeof d->ops, regname(0, osz)); app(d->ops, sizeof d->ops, ",");
            get_imm(d, seg, &off, osz);
            append_imm(d->ops, sizeof d->ops, d->imm, osz);
            break;
        case 0xAA: strcpy(d->mnem, "STOSB"); break;
        case 0xAB: strcpy(d->mnem, d->osz == 32 ? "STOSD" : "STOSW"); break;
        case 0xAC: strcpy(d->mnem, "LODSB"); break;
        case 0xAD: strcpy(d->mnem, d->osz == 32 ? "LODSD" : "LODSW"); break;
        case 0xAE: strcpy(d->mnem, "SCASB"); break;
        case 0xAF: strcpy(d->mnem, d->osz == 32 ? "SCASD" : "SCASW"); break;
        case 0xB0: case 0xB1: case 0xB2: case 0xB3:
        case 0xB4: case 0xB5: case 0xB6: case 0xB7:
        case 0xB8: case 0xB9: case 0xBA: case 0xBB:
        case 0xBC: case 0xBD: case 0xBE: case 0xBF:
            strcpy(d->mnem, "MOV");
            app(d->ops, sizeof d->ops, regname(op & 7, (op < 0xB8) ? 8 : d->osz));
            app(d->ops, sizeof d->ops, ",");
            get_imm(d, seg, &off, (op < 0xB8) ? 8 : d->osz);
            append_imm(d->ops, sizeof d->ops, d->imm, d->imm_size);
            break;
        case 0xC0: case 0xC1:
            get_modrm(d, seg, &off); strcpy(d->mnem, shn[d->reg]);
            append_rm(d, d->ops, sizeof d->ops, osz); app(d->ops, sizeof d->ops, ",");
            get_imm(d, seg, &off, 8); append_imm(d->ops, sizeof d->ops, d->imm, 8); break;
        case 0xC2:
            strcpy(d->mnem, "RET"); d->imm = getw(d, seg, &off);
            append_imm(d->ops, sizeof d->ops, d->imm, 16); break;
        case 0xC3: strcpy(d->mnem, "RET"); break;
        case 0xC4: strcpy(d->mnem, "LES"); get_modrm(d, seg, &off);
            app(d->ops, sizeof d->ops, regname(d->reg, d->osz)); app(d->ops, sizeof d->ops, ",");
            append_rm(d, d->ops, sizeof d->ops, d->osz); break;
        case 0xC5: strcpy(d->mnem, "LDS"); get_modrm(d, seg, &off);
            app(d->ops, sizeof d->ops, regname(d->reg, d->osz)); app(d->ops, sizeof d->ops, ",");
            append_rm(d, d->ops, sizeof d->ops, d->osz); break;
        case 0xC6: case 0xC7:
            strcpy(d->mnem, "MOV"); get_modrm(d, seg, &off);
            append_rm(d, d->ops, sizeof d->ops, op == 0xC6 ? 8 : d->osz,
                      op == 0xC6 ? 8 : d->osz);
            app(d->ops, sizeof d->ops, ",");
            get_imm(d, seg, &off, op == 0xC6 ? 8 : d->osz);
            append_imm(d->ops, sizeof d->ops, d->imm, d->imm_size); break;
        case 0xC8:
            strcpy(d->mnem, "ENTER"); d->imm = getw(d, seg, &off);
            sprintf(d->ops, "%04X,%02X", (unsigned)d->imm, getb(d, seg, &off)); break;
        case 0xC9: strcpy(d->mnem, "LEAVE"); break;
        case 0xCA:
            strcpy(d->mnem, "RETF"); d->imm = getw(d, seg, &off);
            append_imm(d->ops, sizeof d->ops, d->imm, 16); break;
        case 0xCB: strcpy(d->mnem, "RETF"); break;
        case 0xCC: strcpy(d->mnem, "INT3"); break;
        case 0xCD:
            strcpy(d->mnem, "INT"); d->imm = getb(d, seg, &off);
            append_imm(d->ops, sizeof d->ops, d->imm, 8);
            {
                const char *nm = 0;
                if (d->imm == 0x21 && d->ops[0]) nm = 0;
                if (d->imm == 0x21) nm = 0;
                (void)nm;
            }
            break;
        case 0xCE: strcpy(d->mnem, "INTO"); break;
        case 0xCF: strcpy(d->mnem, "IRET"); break;
        case 0xD0: case 0xD1: case 0xD2: case 0xD3:
            get_modrm(d, seg, &off); strcpy(d->mnem, shn[d->reg]);
            append_rm(d, d->ops, sizeof d->ops, osz);
            app(d->ops, sizeof d->ops, (op & 2) ? ",CL" : ",1");
            break;
        case 0xD4: strcpy(d->mnem, "AAM"); d->imm = getb(d, seg, &off);
            append_imm(d->ops, sizeof d->ops, d->imm, 8); break;
        case 0xD5: strcpy(d->mnem, "AAD"); d->imm = getb(d, seg, &off);
            append_imm(d->ops, sizeof d->ops, d->imm, 8); break;
        case 0xD6: strcpy(d->mnem, "SALC"); break;
        case 0xD7: strcpy(d->mnem, "XLAT"); break;
        case 0xD8: case 0xD9: case 0xDA: case 0xDB:
        case 0xDC: case 0xDD: case 0xDE: case 0xDF:
            strcpy(d->mnem, "ESC"); d->ops[0] = 0; get_modrm(d, seg, &off);
            sprintf(d->ops, "%d", op - 0xD8); break;
        case 0xE0: strcpy(d->mnem, "LOOPNZ"); d->rel = (signed char)getb(d, seg, &off);
            sprintf(d->ops, "%04X", (u16)(off + d->rel)); break;
        case 0xE1: strcpy(d->mnem, "LOOPZ"); d->rel = (signed char)getb(d, seg, &off);
            sprintf(d->ops, "%04X", (u16)(off + d->rel)); break;
        case 0xE2: strcpy(d->mnem, "LOOP"); d->rel = (signed char)getb(d, seg, &off);
            sprintf(d->ops, "%04X", (u16)(off + d->rel)); break;
        case 0xE3: strcpy(d->mnem, "JCXZ"); d->rel = (signed char)getb(d, seg, &off);
            sprintf(d->ops, "%04X", (u16)(off + d->rel)); break;
        case 0xE4: case 0xE5:
            strcpy(d->mnem, "IN"); app(d->ops, sizeof d->ops, op == 0xE4 ? "AL" : "AX");
            app(d->ops, sizeof d->ops, ","); d->imm = getb(d, seg, &off);
            append_imm(d->ops, sizeof d->ops, d->imm, 8); break;
        case 0xE6: case 0xE7:
            strcpy(d->mnem, "OUT"); d->imm = getb(d, seg, &off);
            append_imm(d->ops, sizeof d->ops, d->imm, 8);
            app(d->ops, sizeof d->ops, ","); app(d->ops, sizeof d->ops, op == 0xE6 ? "AL" : "AX"); break;
        case 0xE8:
            strcpy(d->mnem, "CALL"); d->rel = (s16)getw(d, seg, &off); d->rel_size = 16;
            sprintf(d->ops, "%04X", (u16)(off + d->rel)); break;
        case 0xE9:
            strcpy(d->mnem, "JMP"); d->rel = (s16)getw(d, seg, &off); d->rel_size = 16;
            sprintf(d->ops, "%04X", (u16)(off + d->rel)); break;
        case 0xEA:
            strcpy(d->mnem, "JMP FAR");
            d->imm = getw(d, seg, &off);
            d->imm |= (u32)getw(d, seg, &off) << 16;
            sprintf(d->ops, "%04X:%04X", (unsigned)(d->imm >> 16), (unsigned)(d->imm & 0xFFFF));
            break;
        case 0xEB:
            strcpy(d->mnem, "JMP"); d->rel = (signed char)getb(d, seg, &off);
            sprintf(d->ops, "%04X", (u16)(off + d->rel)); break;
        case 0xEC: strcpy(d->mnem, "IN"); app(d->ops, sizeof d->ops, "AL,DX"); break;
        case 0xED: strcpy(d->mnem, "IN"); app(d->ops, sizeof d->ops, "AX,DX"); break;
        case 0xEE: strcpy(d->mnem, "OUT"); app(d->ops, sizeof d->ops, "DX,AL"); break;
        case 0xEF: strcpy(d->mnem, "OUT"); app(d->ops, sizeof d->ops, "DX,AX"); break;
        case 0xF4: strcpy(d->mnem, "HLT"); break;
        case 0xF5: strcpy(d->mnem, "CMC"); break;
        case 0xF6: case 0xF7:
            get_modrm(d, seg, &off); strcpy(d->mnem, grp3[d->reg]);
            {
                int s = op == 0xF6 ? 8 : d->osz;
                int has = (d->reg < 2 || d->reg >= 6);
                append_rm(d, d->ops, sizeof d->ops, s, has ? s : 0);
                if (has) {
                    app(d->ops, sizeof d->ops, ",");
                    get_imm(d, seg, &off, s);
                    append_imm(d->ops, sizeof d->ops, d->imm, d->imm_size);
                }
            }
            break;
        case 0xF8: strcpy(d->mnem, "CLC"); break;
        case 0xF9: strcpy(d->mnem, "STC"); break;
        case 0xFA: strcpy(d->mnem, "CLI"); break;
        case 0xFB: strcpy(d->mnem, "STI"); break;
        case 0xFC: strcpy(d->mnem, "CLD"); break;
        case 0xFD: strcpy(d->mnem, "STD"); break;
        case 0xFE:
            get_modrm(d, seg, &off); strcpy(d->mnem, d->reg == 1 ? "DEC" : "INC");
            append_rm(d, d->ops, sizeof d->ops, 8); break;
        case 0xFF:
            get_modrm(d, seg, &off);
            if (d->reg == 0) strcpy(d->mnem, "INC");
            else if (d->reg == 1) strcpy(d->mnem, "DEC");
            else if (d->reg == 2) strcpy(d->mnem, "CALL");
            else if (d->reg == 3) strcpy(d->mnem, "CALL FAR");
            else if (d->reg == 4) strcpy(d->mnem, "JMP");
            else if (d->reg == 5) strcpy(d->mnem, "JMP FAR");
            else if (d->reg == 6) strcpy(d->mnem, "PUSH");
            else if (d->reg == 7) strcpy(d->mnem, "POP");
            append_rm(d, d->ops, sizeof d->ops, (d->reg == 0 || d->reg == 1) ? 8 : d->osz);
            if (d->reg == 3 || d->reg == 5) {
                if (d->ops[0] == '[') {
                    char tmp[80];
                    sprintf(tmp, "[WORD %s", d->ops + 1);
                    strncpy(d->ops, tmp, sizeof d->ops - 1);
                    d->ops[sizeof d->ops - 1] = 0;
                }
            }
            break;
        default:
            d->bad = 1;
            sprintf(d->mnem, "DB");
            sprintf(d->ops, "%02X", d->op);
            break;
        }
    } else {
        u8 o2 = d->op2;
        int osz = d->osz;
        if (o2 >= 0x80 && o2 <= 0x8F) {
            sprintf(d->mnem, "J%s", cc[o2 & 15]);
            d->rel = (s16)getw(d, seg, &off);
            sprintf(d->ops, "%04X", (u16)(off + d->rel));
        } else if (o2 >= 0x90 && o2 <= 0x9F) {
            sprintf(d->mnem, "SET%s", cc[o2 & 15]);
            get_modrm(d, seg, &off); append_rm(d, d->ops, sizeof d->ops, 8);
        } else if (o2 >= 0x40 && o2 <= 0x4F) {
            sprintf(d->mnem, "CMOV%s", cc[o2 & 15]);
            get_modrm(d, seg, &off);
            app(d->ops, sizeof d->ops, regname(d->reg, osz)); app(d->ops, sizeof d->ops, ",");
            append_rm(d, d->ops, sizeof d->ops, osz);
        } else switch (o2) {
        case 0x00: strcpy(d->mnem, "SLDT"); get_modrm(d, seg, &off);
            append_rm(d, d->ops, sizeof d->ops, 16); break;
        case 0x01: strcpy(d->mnem, "SGDT"); get_modrm(d, seg, &off);
            append_rm(d, d->ops, sizeof d->ops, 16); break;
        case 0x02: strcpy(d->mnem, "LAR"); get_modrm(d, seg, &off);
            app(d->ops, sizeof d->ops, regname(d->reg, osz)); app(d->ops, sizeof d->ops, ",");
            append_rm(d, d->ops, sizeof d->ops, 16); break;
        case 0x03: strcpy(d->mnem, "LSL"); get_modrm(d, seg, &off);
            app(d->ops, sizeof d->ops, regname(d->reg, osz)); app(d->ops, sizeof d->ops, ",");
            append_rm(d, d->ops, sizeof d->ops, 16); break;
        case 0x06: strcpy(d->mnem, "CLTS"); break;
        case 0x20: case 0x21: case 0x22: case 0x23:
            get_modrm(d, seg, &off);
            strcpy(d->mnem, (o2 == 0x20 || o2 == 0x22) ? "MOV" : "MOV");
            app(d->ops, sizeof d->ops, regname(d->reg, 32));
            app(d->ops, sizeof d->ops, ",");
            sprintf(d->ops + strlen(d->ops), "CR%d", d->rm);
            break;
        case 0xA0: strcpy(d->mnem, "PUSH FS"); break;
        case 0xA1: strcpy(d->mnem, "POP FS"); break;
        case 0xA3: strcpy(d->mnem, "BT"); get_modrm(d, seg, &off);
            append_rm(d, d->ops, sizeof d->ops, osz); app(d->ops, sizeof d->ops, ",");
            app(d->ops, sizeof d->ops, regname(d->reg, osz)); break;
        case 0xA4: case 0xA5:
            strcpy(d->mnem, "SHLD"); get_modrm(d, seg, &off);
            append_rm(d, d->ops, sizeof d->ops, osz); app(d->ops, sizeof d->ops, ",");
            app(d->ops, sizeof d->ops, regname(d->reg, osz)); app(d->ops, sizeof d->ops, ",");
            if (o2 == 0xA4) { d->imm = getb(d, seg, &off); append_imm(d->ops, sizeof d->ops, d->imm, 8); }
            else app(d->ops, sizeof d->ops, "CL");
            break;
        case 0xA8: strcpy(d->mnem, "PUSH GS"); break;
        case 0xA9: strcpy(d->mnem, "POP GS"); break;
        case 0xAB: strcpy(d->mnem, "BTS"); get_modrm(d, seg, &off);
            append_rm(d, d->ops, sizeof d->ops, osz); app(d->ops, sizeof d->ops, ",");
            app(d->ops, sizeof d->ops, regname(d->reg, osz)); break;
        case 0xAC: case 0xAD:
            strcpy(d->mnem, "SHRD"); get_modrm(d, seg, &off);
            append_rm(d, d->ops, sizeof d->ops, osz); app(d->ops, sizeof d->ops, ",");
            app(d->ops, sizeof d->ops, regname(d->reg, osz)); app(d->ops, sizeof d->ops, ",");
            if (o2 == 0xAC) { d->imm = getb(d, seg, &off); append_imm(d->ops, sizeof d->ops, d->imm, 8); }
            else app(d->ops, sizeof d->ops, "CL");
            break;
        case 0xAF: strcpy(d->mnem, "IMUL"); get_modrm(d, seg, &off);
            app(d->ops, sizeof d->ops, regname(d->reg, osz)); app(d->ops, sizeof d->ops, ",");
            append_rm(d, d->ops, sizeof d->ops, osz); break;
        case 0xB0: case 0xB1: strcpy(d->mnem, "CMPXCHG"); get_modrm(d, seg, &off);
            append_rm(d, d->ops, sizeof d->ops, o2 == 0xB0 ? 8 : osz); app(d->ops, sizeof d->ops, ",");
            app(d->ops, sizeof d->ops, regname(d->reg, o2 == 0xB0 ? 8 : osz)); break;
        case 0xB2: strcpy(d->mnem, "LSS"); get_modrm(d, seg, &off);
            app(d->ops, sizeof d->ops, regname(d->reg, osz)); app(d->ops, sizeof d->ops, ",");
            append_rm(d, d->ops, sizeof d->ops, osz); break;
        case 0xB3: strcpy(d->mnem, "BTR"); get_modrm(d, seg, &off);
            append_rm(d, d->ops, sizeof d->ops, osz); app(d->ops, sizeof d->ops, ",");
            app(d->ops, sizeof d->ops, regname(d->reg, osz)); break;
        case 0xB4: strcpy(d->mnem, "LFS"); get_modrm(d, seg, &off);
            app(d->ops, sizeof d->ops, regname(d->reg, osz)); app(d->ops, sizeof d->ops, ",");
            append_rm(d, d->ops, sizeof d->ops, osz); break;
        case 0xB5: strcpy(d->mnem, "LGS"); get_modrm(d, seg, &off);
            app(d->ops, sizeof d->ops, regname(d->reg, osz)); app(d->ops, sizeof d->ops, ",");
            append_rm(d, d->ops, sizeof d->ops, osz); break;
        case 0xB6: case 0xB7: case 0xBE: case 0xBF:
            strcpy(d->mnem, (o2 == 0xB6 || o2 == 0xB7) ? "MOVZX" : "MOVSX");
            get_modrm(d, seg, &off);
            app(d->ops, sizeof d->ops, regname(d->reg, osz)); app(d->ops, sizeof d->ops, ",");
            append_rm(d, d->ops, sizeof d->ops, (o2 == 0xB6 || o2 == 0xBE) ? 8 : 16); break;
        case 0xBA: strcpy(d->mnem, "BT"); get_modrm(d, seg, &off);
            append_rm(d, d->ops, sizeof d->ops, osz); app(d->ops, sizeof d->ops, ",");
            d->imm = getb(d, seg, &off); append_imm(d->ops, sizeof d->ops, d->imm, 8); break;
        case 0xBB: strcpy(d->mnem, "BTC"); get_modrm(d, seg, &off);
            append_rm(d, d->ops, sizeof d->ops, osz); app(d->ops, sizeof d->ops, ",");
            app(d->ops, sizeof d->ops, regname(d->reg, osz)); break;
        case 0xBC: case 0xBD:
            strcpy(d->mnem, o2 == 0xBC ? "BSF" : "BSR"); get_modrm(d, seg, &off);
            app(d->ops, sizeof d->ops, regname(d->reg, osz)); app(d->ops, sizeof d->ops, ",");
            append_rm(d, d->ops, sizeof d->ops, osz); break;
        case 0xC0: case 0xC1: strcpy(d->mnem, "XADD"); get_modrm(d, seg, &off);
            append_rm(d, d->ops, sizeof d->ops, o2 == 0xC0 ? 8 : osz); app(d->ops, sizeof d->ops, ",");
            app(d->ops, sizeof d->ops, regname(d->reg, o2 == 0xC0 ? 8 : osz)); break;
        case 0xC8: case 0xC9: case 0xCA: case 0xCB: case 0xCC: case 0xCD: case 0xCE: case 0xCF:
            strcpy(d->mnem, "BSWAP");
            app(d->ops, sizeof d->ops, regname(o2 & 7, 32)); break;
        default:
            d->bad = 1; sprintf(d->mnem, "DB 0F,%02X", o2);
            break;
        }
    }
}

int disasm(u16 seg, u16 off, char *text, int maxlen, u8 *raw, int *rawlen)
{
    Dec d;
    int i;
    char line[128];
    char mn[40];

    decode(&d, seg, off);
    strcpy(mn, d.mnem);
    sprintf(line, "%-9s %s", mn, d.ops);
    strncpy(text, line, maxlen - 1);
    text[maxlen - 1] = 0;
    if (raw && rawlen) {
        *rawlen = d.len;
        for (i = 0; i < d.len && i < 16; i++) raw[i] = d.raw[i];
    }
    return d.len ? d.len : 1;
}
