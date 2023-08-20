# dw-link 

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![License: CCBY4.0](https://img.shields.io/badge/License-CCBY4.0-blue.svg)](https://creativecommons.org/licenses/by/4.0/)
[![Commits since latest](https://img.shields.io/github/commits-since/felias-fogg/dw-link/latest?include_prereleases)](https://github.com/felias-fogg/dw-link/commits/master)
![Hit Counter](https://visitor-badge.laobi.icu/badge?page_id=felias-fogg_dw-link)
[![Build Status](https://github.com/felias-fogg/dw-link/workflows/Build/badge.svg)](https://github.com/felias-fogg/dw-link/actions)

This is an Arduino sketch that turns your Arduino ATmega328  board into a hardware debugger for the classic ATtinys and the ATmegaX8s, such as the ATmega328. The debugger can communicate using the [debugWIRE](https://en.wikipedia.org/wiki/DebugWIRE) protocol, which gives access to the [on-chip debugging interface](https://en.wikipedia.org/wiki/In-circuit_emulation#On-chip_debugging) of the mentioned MCUs. Over its USB port, the hardware debugger communicates with an instance of the [GNU debugger](https://en.wikipedia.org/wiki/GNU_Debugger) `avr-gdb` using the GDB remote serial protocol. This means that you can use the hardware debugger to debug your program on your development machine using `avr-gdb` or any IDE that integrates `avr-gdb`, e.g., [PlatformIO](https://platformio.org/) or [Eclipse](https://www.eclipse.org/),  while your program is running on the target hardware (e.g. an ATtiny).  And it is all platform independent, i.e., you can use it under macOS, Linux, or Windows.

Why is this good news? The current version of the Arduino IDE does not support debugging at all. Even the new version will not provide any debugging tools for the small AVR MCUs. So, the only way to debug programs is to use additional print statements and recompile, which is very cumbersome. With this sketch, you are provided with a tool that allows you to set breakpoints, to single-step, and to inspect and change variables. Hopefully, this will make debugging much more enjoyable and will save you a lot of valuable time.

If you are interested in the background and ongoing development of dw-link, visit my [blog](https://hinterm-ziel.de/). In particular, the blogs tagged with [dw-link](https://hinterm-ziel.de/index.php/tag/dw-link/) will be interesting for you.

This repository contains the following directories:

* [**dw-link**](dw-link/): Contains the Arduino sketch that turns your Arduino board into a hardware debugger
* [**docs**](docs/): Contains the documentation, in particular the [manual](docs/manual.md)
* [**gui**](bin/): The macOS and Linux binaries of the *[gede](https://gede.dexar.se/pmwiki.php)* GUI and the dw-server.py script that interfaces between dw-link and gede
* [**examples**](examples/): Contains two tiny Arduino sketches and a PlatformIO project
* [**tests**](tests/): Contains some test cases and a Python script for running them semi-automatically
* [**pcb**](pcb/): Contains design data (Eagle and Gerber) for the (optional) adapter boards 
* [**core-mods**](core-mods/): Contains all the modifications to the core files necessary to support the generation of debug-friendly object files

Note that the debugger is a beta release and may contain bugs. If you encounter behavior that you think is wrong, try to be as specific as possible so that I can reproduce the behavior. For that I need a description of the problem, the source code that leads to the behavior, the way one reproduces the problem, and the type of target chip you used. I have prepared an [issue form](docs/issue_form.md) for that purpose. Current issues, known bugs, and limitations are listed in Section 8 of the [manual](docs/manual.md).
