#!/usr/bin/env python3
# Call it in the platform folder without an argument,
# then boards.txt will be renamed to boards.txt.backup and boards.txt gets modified.
# The modification adds a menu entry for each MCU/board called "Debug Compile Flags" with
# four possible values "No Debug", "Debug", "Debug (no LTO)", and "Debug (no LTO, no optimizations)", which will add
# flags to .build.extra_flags
# Similarly, platform.txt is modfied. A few lines are added to allow debugging under Arduino IDE 2.X 

import os, sys

exception = ['8', 'attiny26'] # these boards do not supprt debugWIRE

def massage(lines):
    allboards = gatherboards(lines)
    extraflags = gatherextra(lines,allboards)
    newlines = []
    if len(allboards) > 0:
        lines.append("#################### DEBUG FLAG HANDLING ####################\n")
        lines.append("menu.debug=Debug Compile Flags\n")
        for b in allboards:
            newlines.append("\n");
            newlines.append(b + ".menu.debug.os=No Debug\n")
            newlines.append(b + ".menu.debug.os.build.debug=\n")
            if b in exception:
                newlines.append(b + ".debug.executable=\n")
            else:
                newlines.append(b + ".menu.debug.og=Debug\n")
                newlines.append(b + ".menu.debug.og.build.debug=-g3 -Og\n")
                newlines.append(b + ".menu.debug.o0=Debug (no comp. optim.)\n")
                newlines.append(b + ".menu.debug.o0.build.debug=-g3 -O0\n")
                newlines.append(b + ".menu.debug.no-lto=Debug (no LTO)\n")
                newlines.append(b + ".menu.debug.no-lto.build.debug=-g3 -Og -fno-lto\n")
                newlines.append(b + ".menu.debug.no-ltoo0=Debug (no LTO, no comp. optim.)\n")
                newlines.append(b + ".menu.debug.no-ltoo0.build.debug=-g3 -O0 -fno-lto\n")
                newlines.append(b + ".build.extra_flags={build.debug} " + extraflags.get(b,"\n"))
    return newlines

def gatherboards(lines):
    boardlist = []
    for l in lines:
        parts = l.split(".")
        if len(parts) > 1 and parts[1].find("name") >= 0:
            boardlist.append(parts[0])
    return boardlist

def gatherextra(lines, boards):
    d = { }
    for l in lines:
        if l.find("build.extra_flags=") >= 0:
            b = l.split('.')[0]
            d[b] = l[l.find("build.extra_flags=")+18:]
    return d


filnam = "boards.txt"

if not os.path.isfile(filnam):
    print(filnam, "does not exist")
    exit()
file = open(filnam)
alllines = file.readlines()
file.close()

if "#################### DEBUG FLAG HANDLING ####################\n" in alllines:
    print("Board file already contains debug flag handling")
    exit(1)

os.rename(filnam, filnam + ".backup")
newlines = massage(alllines)

file = open(filnam, "w")
file.writelines(alllines + newlines)
file.close()
if len(newlines) == 0:
    print("*** No boards found, no lines added")
else:
    print("Boards file successfully modified")

filnam = "platform.txt"

if not os.path.isfile(filnam):
    print(filnam, "does not exist")
    exit()
file = open(filnam)
alllines = file.readlines()
file.close()

if "# EXPERIMENTAL feature: optimization flags\n" in alllines:
    print("Platform file already contains debug optimization flags\n")
    exit(2)

os.rename(filnam, filnam + ".backup")

lastline = ""
optinserted = False
file = open(filnam, "w")
for line in alllines:
    if optinserted and "-Os" in line:
        line = line[:line.find("-Os")] + " {compiler.optimization_flags} " + line[line.find("-Os")+3:]
    file.write(line)
    if "# ---------------" in line and "# AVR compile variables" in lastline:
        file.write("\n")
        file.write("# EXPERIMENTAL feature: optimization flags\n")
        file.write("#  - this is alpha and may be subject to change without notice\n")
        file.write("compiler.optimization_flags=-Os\n")
        file.write("compiler.optimization_flags.release=-Os\n")
        file.write("compiler.optimization_flags.debug=-Og -g3\n")
        optinserted = True
    lastline = line

file.write("\n")

if optinserted:
    print("Found place to insert debug compiler optimization flags")
else:
    print("*** Did not find place to insert debug compiler optimization flags")
    exit(1)

file.write("# Debugger configuration (general options)\n")
file.write("# ----------------------------------------\n")
file.write("# EXPERIMENTAL feature:\n")
file.write("#  - this is alpha and may be subject to change without notice\n")
file.write("debug.executable={build.path}/{build.project_name}.elf\n")
file.write("debug.toolchain=gcc\n")
file.write("debug.toolchain.path={runtime.tools.dw-link-tools.path}\n")
file.write("\n")
file.write("debug.server=openocd\n")
file.write("debug.server.openocd.path={debug.toolchain.path}/dw-server\n")
file.write("#doesn't matter, but should be specified so that cortex-debug is happy\n")
file.write("debug.server.openocd.scripts_dir={debug.toolchain.path}/\n")
file.write("#doesn't matter, but should be specified so that cortex-debug is happy\n")
file.write("debug.server.openocd.script={debug.toolchain.path}/dw-server\n")

print("Platform file successfully modified")

file.close()

