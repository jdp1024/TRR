Introduction
============
TRR is a project for learning DOS programming by reimplementing the legendary
TR, the DOS debugger.

I started it at the end of 1998, after the final public release of TR
version 2.52. I was astonished that such a small EXE could virtualize a DOS
box.

I fully disassembled TR.EXE and reimplemented it function by function in C
(TR is a mix of C and assembly). I wrote the CPU emulator from scratch rather
than cloning TR. I stopped development after PKZIP.EXE could be run with its
first screen of intro text displayed.

Current Status
==============
For a long time, I thought I had lost the source code. Recently, I found it
in a stack of CD-ROMs behind my bookshelf.

I reformatted the source code, fixed tons of warnings, unified types, killed
some bugs, and made it work under DOSBox-X.

To celebrate its 28th anniversary, I improved it: it can now run PKZIP and
compress **ONE** small file into a valid ZIP archive.

Build
=====
The Makefile works when the project is located in `C:\PROJECTS\TRR` and
Borland C++ 3.1 is installed in `D:\BORLANDC`. Run DOSBox-X with
`-conf trr_dosbox.conf`, then build TRR with `BUILD.BAT`.

Known Issues
============
* TRR runs 10x slower than TR because it works differently from TR.

* To this day, the only executable I have tested is PKZIP.EXE from PKZIP 2.50.
