# dw-link 
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![License: CCBY4.0](https://img.shields.io/badge/License-CCBY4.0-blue.svg)](https://creativecommons.org/licenses/by/4.0/)
[![Commits since latest](https://img.shields.io/github/commits-since/felias-fogg/dw-link/latest?include_prereleases)](https://github.com/felias-fogg/dw-link/commits/master)
![Hit Counter](https://visitor-badge.laobi.icu/badge?page_id=felias-fogg_dw-link)
[![Build Status](https://github.com/felias-fogg/dw-link/workflows/Build/badge.svg)](https://github.com/felias-fogg/dw-link/actions)



### Now supporting Arduino IDE 2!

![cover](docs/pics/uno-debug2.png)

This Arduino sketch turns your Arduino UNO into a hardware debugger for the classic ATtinys and the ATmegaX8s, such as the ATmega328. It is particularly useful if you would like to find out how it feels to symbolically debug your Arduino sketch on an Arduino Uno or an ATtiny board. Do you want to give it a try? Use the [**Quick-start Guide**](https://felias-fogg.github.io/dw-link/quickstart-Arduino-IDE2/) to see if it works for you. 

Since version 2.2.0, you can use dw-link as a (STK500 v1) programmer as well. Since version 4.0.0, you can use this debugger in the **Arduino IDE 2** by downloading additional board manager files. Version 5.0.0 aims to make the user interface similar to what is offered by the Python packages [dw-gdbserver](https://github.com/felias-fogg/dw-gdbserver) and [PyAvrOCD](https://github.com/felias-fogg/PyAvrOCD), which interface to Microchip's hardware debuggers. Since Microchip's MPLAB SNAP debugger has become incredibly cheap, this may actually be an alternative to this DIY debugger. You can use SNAP as a drop-in replacement for dw-link.

This repository contains the following directories:

* [**dw-link**](dw-link/): Contains the Arduino sketch that turns your Arduino board into a hardware debugger
* [**docs**](docs/): Contains the documentation file for the [manual](https://felias-fogg.github.io/dw-link/)
* [**examples**](examples/): Contains two Arduino sketches and a PlatformIO project
* [**tests**](tests/): Contains some test cases and a Python script for running them semi-automatically
* [**pcb**](pcb/): Contains design data (KiCad) for the optional dw-link probe
* [**testprobe**](testprobe/): Contains a sketch to be used to run tests on freshly assembled dw-link probe
* [**opcodes**](opcodes): Contains a list of legal and illegal opcodes for large and small AVRs as well as a C program to generate the lists.

Note that the debugger may contain bugs. If you encounter behavior that you think is wrong, report it as an [issue](https://github.com/felias-fogg/dw-link/issues). Try to be as specific as possible so that I can reproduce the behavior. Current issues, known bugs, and limitations are listed the [problem section](https://felias-fogg.github.io/dw-link/problems/) of the [manual](https://felias-fogg.github.io/dw-link/). 

<sup><sub>The cover picture was designed based on vector graphics by [captainvector at 123RF](https://de.123rf.com/profile_captainvector).</sub></sup>

