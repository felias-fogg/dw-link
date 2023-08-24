#!/usr/bin/env python3
# Call it in the platform folder without an argument,
# then boards.txt will be renamed to boards.txt.backup and boards.txt gets modified
# If it is called with an argument, then it is expected to be the boards.txt file,
# the file is renamed to *.backup and the original file is modified
# The modification adds a menu entry for each MCU/board called "Debug Compile Flags" with
# four possible values "No Debug", "Debug", "Debug (no LTO)", and "Debug (no LTO, no optimizations)", which will add
# flags to .build.extra_flags


import os, sys

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
            newlines.append(b + ".menu.debug.og=Debug\n")
            newlines.append(b + ".menu.debug.og.build.debug=-Og\n")
            newlines.append(b + ".menu.debug.o0=Debug (no comp. optim.)\n")
            newlines.append(b + ".menu.debug.o0.build.debug=-O0\n")
            newlines.append(b + ".menu.debug.no-lto=Debug (no LTO)\n")
            newlines.append(b + ".menu.debug.no-lto.build.debug=-Og -fno-lto\n")
            newlines.append(b + ".menu.debug.no-ltoo0=Debug (no LTO, no comp. optim.)\n")
            newlines.append(b + ".menu.debug.no-ltoo0.build.debug=-O0 -fno-lto\n")
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


if len(sys.argv) > 2:
    print("Call script with one or none argument only")
    exit()
if len(sys.argv) == 2:
    filnam = sys.argv[1]
else:
    filnam = "boards.txt"

if not os.path.isfile(filnam):
    print(filnam, "does not exist")
    exit()
os.rename(filnam, filnam + ".backup")
file = open(filnam + ".backup")
alllines = file.readlines()
file.close()

newlines = massage(alllines)

file = open(filnam, "w")
file.writelines(alllines + newlines)
file.close()
if len(newlines) == 0:
    print("No boards found, no lines added")
else:
    print("Boards file successfully modified")
