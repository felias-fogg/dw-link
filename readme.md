# dw-link 

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![License: CCBY4.0](https://img.shields.io/badge/License-CCBY4.0-blue.svg)](https://creativecommons.org/licenses/by/4.0/)
[![Commits since latest](https://img.shields.io/github/commits-since/felias-fogg/dw-link/latest?include_prereleases)](https://github.com/felias-fogg/dw-link/commits/master)
![Hit Counter](https://visitor-badge.laobi.icu/badge?page_id=felias-fogg_dw-link)
[![Build Status](https://github.com/felias-fogg/dw-link/workflows/Build/badge.svg)](https://github.com/felias-fogg/dw-link/actions)

![cover](docs/pics/uno-debug2.png)

This is an Arduino sketch that turns your Arduino UNO into a hardware debugger for the classic ATtinys and the ATmegaX8s, such as the ATmega328. And since version 2.2.0, you can use dw-link as a (STK500 v1) programmer as well.

The debugger speaks [debugWIRE](https://en.wikipedia.org/wiki/DebugWIRE) and implements a [gdbServer](https://en.wikipedia.org/wiki/Gdbserver).  This means that you can use GDB or any IDE that integrates GDB to debug your program running on the target hardware (e.g. an ATtiny).  And it is all platform independent, i.e., you can use it under macOS, Linux, or Windows.

Why is this good news? Arduino IDE 1.X does not support debugging at all. Even the new version will not provide debugging for the small AVR MCUs. With this sketch, you get a tool that allows you to set breakpoints, to single-step, and to inspect and set variables. Hopefully, this will make debugging much more enjoyable and will save you a lot of valuable time.

Do you want to try it? Use the [**Quick-start Guide**](docs/quickstart.md) in order find out whether it works for you. If you like it and want to make part of your workflow, you can buy the (optional) Uno shield at [Tindie](https://www.tindie.com/products/31798/).

The background and ongoing development of dw-link is a topic on my [blog](https://arduino-craft-corner.de/). In particular, the blog posts tagged with [dw-link](https://arduino-craft-corner.de/index.php/tag/dw-link/) will be of interest to you.

This repository contains the following directories:

* [**dw-link**](dw-link/): Contains the Arduino sketch that turns your Arduino board into a hardware debugger
* [**docs**](docs/): Contains the documentation, in particular the [manual](docs/manual.md)
* [**dw-server**](dw-server/): Contains the Python script dw-server.py, which discovers the serial line dw-link is connected to and provides a serial-to-TCP/IP bridge
* [**examples**](examples/): Contains two Arduino sketches and a PlatformIO project
* [**tests**](tests/): Contains some test cases and a Python script for running them semi-automatically
* [**pcb**](pcb/): Contains design data (KiCad) for the optional adapter board
* [**core-mods**](core-mods/): Contains all the modifications to the core files necessary to support the generation of debug-friendly object files
* [**opcodes**](opcodes): Contains a list of legal and illegal opcodes for large and small AVRs as well as C program to generate the lists.

Note that the debugger is a beta release and may contain bugs. If you encounter behavior that you think is wrong, try to be as specific as possible so that I can reproduce the behavior. I have prepared an [issue form](docs/issue_form.md) for that purpose. Current issues, known bugs, and limitations are listed in Section 8 of the [manual](docs/manual.md). 



<sup><sub>The cover picture was designed based on vector graphics by [captainvector at 123RF](https://de.123rf.com/profile_captainvector).</sub></sup>

