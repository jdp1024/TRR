#ifndef TR_H
#define TR_H

#include <dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mem.h>
#include <ctype.h>
#include <conio.h>
#include <alloc.h>
#include <bios.h>
#include <fcntl.h>
#include <io.h>
#include <sys\stat.h>

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned long  u32;
typedef signed short   s16;
typedef signed long    s32;

#define VERSION_STR  "TRR V2.52"
#define MAXLINES     50
#define MAXCOLS      80
#define MAXBP        24

#define R_AX 0
#define R_CX 1
#define R_DX 2
#define R_BX 3
#define R_SP 4
#define R_BP 5
#define R_SI 6
#define R_DI 7

#define S_ES 0
#define S_CS 1
#define S_SS 2
#define S_DS 3
#define S_FS 4
#define S_GS 5

#define F_CF 0x0001
#define F_PF 0x0004
#define F_AF 0x0010
#define F_ZF 0x0040
#define F_SF 0x0080
#define F_TF 0x0100
#define F_IF 0x0200
#define F_DF 0x0400
#define F_OF 0x0800

struct CPU {
    u32 r[8];
    u16 s[6];
    u32 eip;
    u32 flags;
    int mode32;
};

extern CPU cpu;
extern struct CPU saved_cpu;

u8   rd8 (u16 seg, u16 off);
u16  rd16(u16 seg, u16 off);
u32  rd32(u16 seg, u16 off);
void wr8 (u16 seg, u16 off, u8  v);
void wr16(u16 seg, u16 off, u16 v);
void wr32(u16 seg, u16 off, u32 v);
u8 far *mem_ptr(u16 seg, u16 off);
void mem_snapshot_low(void);
void far_call_host(u16 seg, u16 off);
u8 far *mem_off(u16 seg, u32 off);
u32 lin_addr(u16 seg, u16 off);


extern u16 psp_seg;
extern u16 img_seg;
extern u16 img_load_seg;
extern u16 dbg_top_seg;
extern u16 prog_top_seg;
extern u16 pool_top_seg;
extern u16 pool_limit_seg;
extern u16 img_alloc_paras;
extern u32 img_size;
extern u16 img_minalloc;
extern u16 img_maxalloc;
extern int prog_type;
extern char prog_name[128];
extern int child_psp_set;


extern int scr_rows;
extern int scr_inited;
extern unsigned char far *vram;
extern int color_set;

void scr_init(void);
void scr_done(void);
void scr_setlines(int rows);
void scr_clear(int attr);
void scr_putc(int x, int y, int ch, int attr);
void scr_puts(int x, int y, const char *s, int attr);
void scr_printf(int x, int y, int attr, const char *fmt, ...);
void scr_fill(int x, int y, int w, int h, int ch, int attr);
void scr_box(int x, int y, int w, int h, int attr, const char *title);
void scr_cursor(int on);
void scr_gotoxy(int x, int y);
int  scr_getkey(void);
void scr_save_user(void);
void scr_show_user(void);
int  scr_get_rows(void);

int disasm(u16 seg, u16 off, char *text, int maxlen, u8 *raw, int *rawlen);

int cpu_step_tr(void);
void cpu_do_int(int n);

extern int stop_reason;
extern u16 stop_addr_seg, stop_addr_off;


int load_program(const char *name);
u32 far_read(FILE *f, u8 far *dst, u32 n);
int load_com(const char *name);
int load_exe(const char *name);
void make_psp(void);
void free_program(void);
int write_exe(const char *name, u16 seg, u32 len, u16 cs, u16 ip, u16 ss, u16 sp);
int write_exe_image(const char *name);
int write_com(const char *name, u16 seg, u32 len);
void set_filename(const char *name);


#define BP_EXEC   0
#define BP_INT    1
#define BP_IO     2
#define BP_REG    3
#define BP_XB     4
#define BP_MEM    5
#define BP_WRITE  6
#define BP_KNL    7

struct BreakPoint {
    int type;
    int enabled;
    int hit;
    u16 seg, off;
    u8  bytes[8];
    int nbytes;
    int reg;
    u32 value;
    char cond[64];
    int  has_cond;
    int  anyseg;
};

extern struct BreakPoint bps[MAXBP];
extern int nbps;
extern int bp_break_hit;
extern u16 bp_break_addr;

void bp_clear_all(void);
int  bp_add(int type, u16 seg, u16 off);
void bp_eval_all(void);


int  cond_eval(const char *cond);


int eval_expr(const char *s, u32 *out);
int parse_addr(const char *s, u16 *seg, u16 *off, int defseg);
int parse_addr_ex(const char *s, u16 *seg, u16 *off, int defseg, int *has_seg);


void sym_init(void);
void sym_add(const char *name, u16 seg, u16 off);
const char *sym_find(u16 seg, u16 off, u16 *base);
void label_add(const char *name, u16 seg, u16 off);
void cmt_add(u16 seg, u16 off, const char *text);
const char *cmt_find(u16 seg, u16 off);
void cmt_save(void);
void cmt_load(void);


void cmd_loop(void);
int  do_command(char *line);
int  quit_requested(void);
void show_regs(void);
void show_mem(void);
void show_code(void);
void show_status(const char *msg);
void show_all(void);
void help_screen(const char *topic);
void batch_run(const char *fname);
void log_trace(void);
void compute_layout(void);
void sym_dump(void);
int  assemble_line(const char *line, u16 seg, u16 off);
int  reg_find(const char *name, int *size);
u32  reg_get(int id, int size);
void reg_set(int id, int size, u32 v);
int  sym_by_name(const char *name, u16 *seg, u16 *off);
int  bp_pre_check(void);
int  bp_post_check(u16 start_cs, u16 start_ip);
int  bp_io_check(u16 port, int wr);
void dump_step_ring(void);
extern u16 xms_off, xms_seg;
extern int xms_valid;


extern int code_y, code_h, mem_y, mem_h, prompt_y, status_y;
extern int status2_y, msg_y, mem_lines_user, code_base;
extern u16 code_base_cs;

extern int hold_view;
extern int follow_ip;
extern u16 view_seg, view_off;
extern char last_cmd[];
extern char msg_line[];


const char *int21_name(int ah);
const char *int13_name(int ah);
const char *int10_name(int ah);
const char *int15_name(int ah);


void far_strcpy(char *d, const char far *s);
int  parse_yesno(const char *s);
void delay_ms(int ms);
extern int line_count;
extern int in_step;
extern int no_break;
extern int mem_hit;
extern u16 mem_hit_seg, mem_hit_off;
extern int zoom_view;
extern int active_win;
extern int trace_int1;
extern int autojmp;
extern int autoint1;
extern int log_on;
extern int logs_on;
extern int log_count;
extern u32 log_buf[512];

#endif
