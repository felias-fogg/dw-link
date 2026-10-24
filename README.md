# dw-link 
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![License: CCBY4.0](https://img.shields.io/badge/License-CCBY4.0-blue.svg)](https://creativecommons.org/licenses/by/4.0/)
[![Commits since latest](https://img.shields.io/github/commits-since/felias-fogg/dw-link/latest?include_prereleases)](https://github.com/felias-fogg/dw-link/commits/master)
![Hit Counter](https://visitor-badge.laobi.icu/badge?page_id=felias-fogg_dw-link)
[![Build Status](https://github.com/felias-fogg/dw-link/workflows/Build/badge.svg)](https://github.com/felias-fogg/dw-link/actions)

![cover](docs/pics/uno-debug2.png)

This Arduino sketch turns your Arduino UNO into a hardware debugger for the classic ATtinys and the ATmegaX8s, such as the ATmega328. It gives you a chance to find out how it feels to debug your Arduino sketch running on an Arduino Uno or an ATtiny board at the source level. Do you want to give it a try? Use the [**Quick-start Guide**](https://felias-fogg.github.io/dw-link/quickstart-Arduino-IDE2/) to see if it works for you. 

From version 7 on, the protocol has been changed to move all of the debug logic into [PyAvrOCD](https://pyavrocd.io). If you want to connect GDB to dw-link using only the serial line. You need to fall back to version 6.1.1.

This repository contains the following directories:

* [**dw-link**](dw-link/): Contains the Arduino sketch that turns your Arduino board into a hardware debugger,
* [**dw-uploader**](dw-uploader/): Contains a Python script to flash the debugging firmware onto an Arduino UNO, 
* [**docs**](docs/): Contains the documentation file for the [manual](https://felias-fogg.github.io/dw-link/),
* [**pcb**](pcb/): Contains design data (KiCad) for the optional dw-link probe,
* [**testprobe**](testprobe/): Contains a sketch to be used to run tests on a freshly assembled dw-link probe.

<sup><sub>The cover picture was designed based on vector graphics by [captainvector at 123RF](https://de.123rf.com/profile_captainvector).</sub></sup>

