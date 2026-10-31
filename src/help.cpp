#include "tr.h"

static const char *help_lines[] = {
    "<F5>    -Zoom Current Window",
    "<F6>    -To/From Code Windows",
    "<F7>    -Here",
    "A [address] -Assemble",
    "AUTOJMP [ON]|[OFF] -set autojmp on or off",
    "BC [bpnum]    -BreakPoint Clear",
    "BD [bpnum]    -BreakPoint Disable",
    "BE [bpnum]    -BreakPoint Enable",
    "BL      -BreakPoint List",
    "BP [seg:]offset -Break Point if Exec",
    "BP conditions   -BreakPoint if condition is true",
    "BPKNL [count]   -Break if found new kernel",
    "BPINT NUM [conditions]    -Break Point if Interrupt",
    "BPIO NUM [conditions] -Break Point if I/O",
    "BPXB bytes [conditions]   -Break Point if specify code",
    "BPM [seg:]offset -Break Point if R/W",
    "BPREG REG|SEG [conditions] -Break Point if REG|SEG change",
    "BPW seg:offset   -Break Point if Change",
    "COLOR [01 02 03 04 05 06 07 08 09 10] -Set/Display Color Set",
    "CMT seg:offset 'comments' -Add comment in filename.cmt",
    "D [address]   -Memory dump",
    "D [range]>filename   -Memory dump to file",
    "DO filename -Do batch cmd file",
    "INT1 [ON|OFF] -Run each code use int1",
    "E [range]   -edit data",
    "F range bytes    -Fill",
    "G [[seg:]offset] -Go",
    "G [conditions]   -Go",
    "GG [[seg:]offset]    -Go uncontrol",
    "GO??? -any BP??? cammand can use as GO??? to do a onetime brkpoint",
    "L [address][filename] -Read file to ds:address,default ds:100",
    "L range drive start sectors -Load disk sectors into memory",
    "LABEL seg:offset 'labelname'-Define label name",
    "LINES 25|50 -Change screen lines",
    "LOG [on]|[off]   -Set log ON or OFF",
    "M range [seg]:offset -Memory copy",
    "N filename  -set filename for cmd 'W' 'RELOAD'",
    "P       -program step(F10)",
    "PP      -Go until next code",
    "PRET    -Go until ret|retf|iret",
    "R reg num   -set register value",
    "R32     -16bit/32bit Register Display",
    "RS      -Restore User Screen(F4)",
    "S range bytes    -Search",
    "LOGS [on]|[off]   -Save history file ON or OFF",
    "STACK   -View Stack for Calls",
    "SYMBOLS -Display all symbol",
    "T       -program trace(F8)",
    "TT      -Trace use int1(not recommend)",
    "U [address]   -Unassemble",
    "U [range]>filename  -Unassemble to file",
    "VER     -Version and Copyright",
    "VLOG    -View History",
    "W [range][filename] -Write filename(set by 'N') length BX:CX",
    "Q       -quit",
    "? [expression]   -Help",
    "......and more and more, read readme.txt",
    0
};

static int help_count(void)
{
    int n = 0;
    while (help_lines[n]) n++;
    return n;
}

void help_screen(const char *topic)
{
    int total = help_count();
    int page, first = 0;
    int i;

    page = scr_rows - 2;
    if (page < 4) page = 4;
    if (topic && topic[0] == '?') {
        first = total - page;
        if (first < 0) first = 0;
    }

    for (;;) {
        int k;
        scr_fill(0, 0, 80, scr_rows, ' ', 0x1F);
        scr_fill(1, 0, 60, scr_rows - 1, ' ', 0x31);
        for (i = 0; i < page && first + i < total; i++)
            scr_puts(2, 1 + i, help_lines[first + i], 0x31);
        scr_fill(0, scr_rows - 1, 80, 1, ' ', 0x74);
        scr_puts(0, scr_rows - 1,
                 "Press Enter to Restore Window Size, PgUp/PgDn/Up/Dn to move cursor",
                 0x74);
        k = scr_getkey();
        if (k == 0x1B || k == 0x0D) break;
        if (k == 0x4800) { if (first > 0) first--; }
        else if (k == 0x5000) { if (first < total - page) first++; }
        else if (k == 0x4900) { first -= page; if (first < 0) first = 0; }
        else if (k == 0x5100) {
            first += page;
            if (first > total - page) first = total - page;
            if (first < 0) first = 0;
        }
    }
    show_all();
}
