#include "tr.h"

static const char *i21[0x6D] = {
    "Terminate Process",
    "Character Input with Echo",
    "Character Output",
    "Aux Input",
    "Aux Output",
    "Printer Output",
    "Direct Console I/O",
    "Unfiltered Character Input without Echo",
    "Character Input without Echo",
    "Display String",
    "Buffered Keyboard Input",
    "Check Input Status",
    "Flush Input Buffer,Then Input",
    "Disk Reset",
    "Select Disk",
    "Open File (FCB)",
    "Close File(FCB)",
    "Find First File(FCB)",
    "Find Next File(FCB)",
    "Delete File(FCB)",
    "Sequential Read(FCB)",
    "Sequential Write(FCB)",
    "Create File(FCB)",
    "Rename File(FCB)",
    "Reserved(undoc)",
    "Get Current Drive",
    "Set Data Transfer Area (DTA) Address",
    "Get Drive Data",
    "Get Drive Data",
    "Random Read(FCB)",
    "Random Write(FCB)",
    "Get File Size(FCB)",
    "Set Relative Record Number(FCB)",
    "Random Block Read(FCB)",
    "Random Block Write(FCB)",
    "Parse Filename(FCB)",
    "Get Date",
    "Set Date",
    "Get Time",
    "Set Time",
    "Set Verify Flag",
    "Get DTA Address",
    "Get Date",
    "Set Date",
    "Get Time",
    "Set Time",
    "Set Verify Flag",
    "Get DTA Address",
    "Get MS-DOS Version Number",
    "Terminate and Stay Resident",
    "Get ptr to Drive Parameter Table(undoc)",
    "Get or Set Ctrl-Break Flag",
    "Get address of InDOS Flag",
    "Get Interrupt Vector",
    "Get Drive Allocation Information",
    "Get/set switch character(undoc)",
    "Get or Set Country Information",
    "Create Directory",
    "Delete Directory",
    "Set Current Directory",
    "Create File",
    "Open File",
    "Close File",
    "Read File or Device",
    "Write to File or Device",
    "Delete File",
    "Set File Pointer",
    "Get or Set File Attributes",
    "IOCTL (I/O Control)",
    "Duplicate Handle",
    "Redirect Handle",
    "Get Current Directory",
    "Allocate Memory Block",
    "Release Memory Block",
    "Resize Memory Block",
    "Execute Program (EXEC)",
    "Terminate Process with Return Code",
    "Get Return Code",
    "Find First File",
    "Find Next File",
    "Set Program Segment Prefix(PSP) Address",
    "Get PSP Address",
    "Get pointer to DOS \"INVARS\" (undoc)",
    "Generate Drive Parameter Table(undoc)",
    "Get Verify Flag",
    "Create PSP (undoc)",
    "Rename File",
    "Get/Set File Date and Time",
    "Get/Set Allocation Strategy",
    "Get Extended Error Information",
    "Create Temporary File",
    "Create New File",
    "Lock or Unlock Region of File",
    "Critical error information (undoc)",
    "Get Machine Name, Get/Set Printer Setup",
    "Device Reallocation",
    "Get fully qualified file name(undoc)",
    "Get PSP Address",
    "Get Lead Byte Table",
    "Set device driver look ahead(undo)",
    "Get Extended Country Information",
    "Get or Set Code Page",
    "Set Handle Count(sets max number of files)",
    "Commit File(forces buffer to be written out)",
    "Get/set disk serial number(undoc)",
    "Reserved",
    "Reserved",
    "Reserved",
    "Extended Open File(open/create/replace file)"
};

static const char *i10[0x1D] = {
    "Set Video Mode",
    "Set Cursor Type",
    "Set Cursor Position",
    "Get Cursor Infor",
    "Get Light Pen Position",
    "Set Display Page",
    "Init or Scroll Window Up",
    "Init or Scroll Window Down",
    "Read Char and Attr at Cursor",
    "Write Char and Attr at Cursor",
    "Write Character at Cursor",
    "Set Palette, Background,or Border",
    "Write Graphics Pixel",
    "Read Graphics Pixel",
    "Write Character in Teletype Mode",
    "Get Video Mode",
    "Set/get palette registers",
    "Character generator routine (Fonts)",
    "Video Config",
    "Write String in Teletype Mode",
    "Load LCD char font (convertible)",
    "Return physical display parms (convertible)",
    "Get/Set Display Combination Code",
    "Get Functionality/State Information",
    "Save or Restore Video State"
};

static const char *i13[0x1B] = {
    "Reset Disk System",
    "Get Disk System Status",
    "Read Sector",
    "Write Sector",
    "Verify Sector",
    "Format Track",
    "Format Bad Track",
    "Format Drive",
    "Get Drive Parameters",
    "Initialize Fixed Disk Characteristics",
    "Read Sector Long",
    "Write Sector Long",
    "Seek",
    "Reset Fixed Disk System",
    "Read Sector Buffer",
    "Write Sector Buffer",
    "Get Drive Status",
    "Recalibrate Drive",
    "Controller RAM Diagnostic",
    "Controller Drive Diagnostic",
    "Controller Internal Diagnostic",
    "Get Disk Type",
    "Get Disk Change Status",
    "Set Disk Type",
    "Set Media Type for Format",
    "Park Heads",
    "Format ESDI Drive"
};

const char *int21_name(int ah)
{
    if (ah < 0 || ah >= 0x6D) return 0;
    if (!i21[ah][0]) return 0;
    return i21[ah];
}

const char *int10_name(int ah)
{
    if (ah < 0 || ah >= 0x1D) return 0;
    return i10[ah];
}

const char *int13_name(int ah)
{
    if (ah < 0 || ah >= 0x1B) return 0;
    return i13[ah];
}

const char *int15_name(int ah)
{
    if (ah == 0x87) return "Move Extended Memory Block";
    if (ah == 0x88) return "Get Extended Memory Size";
    return 0;
}
