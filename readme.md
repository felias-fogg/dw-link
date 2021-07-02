# debugWIRE-probe 

This is an Arduino sketch that turns your Arduino Uno or Nano into a hardware debugger that uses the [debugWIRE](https://en.wikipedia.org/wiki/DebugWIRE) protocol, which enables you to debug the classic ATtinys and some small ATmegas. It acts as a [gdbserver](https://en.wikipedia.org/wiki/Gdbserver), so you can use it to debug your programs running on the target hardware (e.g. an ATtiny) using avr-gdb or any IDE that integrates avr-gdb, e.g., [PlatformIO](https://platformio.org/) or [Eclipse](https://www.eclipse.org/), on the development machine. And it is all platform independent, i.e., you can use it under macOS, Linux, or Windows.

Why is this good news? The (current version of the) Arduino IDE does not support debugging at all. Even the new version will not provide any debugging tools for the small AVR MCUs. So, the only way to debug programs is to use additional print statements and recompile, which is very cumbersome. With this sketch, you are provided with a hardware debugging tool that allows you to set breakpoints, to single-step, and to inspect and change variables. Hopefully, this will make debugging much more enjoyable and save you a lot of valuable time.

This repository contains the following directories:

* **dw-probe**: Contains the Arduino sketch that turns your Uno into a hardware debugger
* **examples**: Contains a tiny Arduino sketch and a PlatformIO project
* **docs**: Contains the documentation, in particular the [manual](docs/manual.md)

Note that the debugger is an alpha release and may contain bugs. If you encounter behavior that you think is wrong, try to be as specific as possible so that I can reproduce the behavior. For that I need a description of the problem, the source code that leads to the behavior, the way one reproduces the problem, and the type of target chip you used. 

Note that currently (Version 0.9.1), only MCU without bootloader memory are supported. So, ATmega328, ATmega168, ATmega88, and ATtiny828 are not supported yet. *In addition, do not attempt to debug target systems running faster than 8 MHz.*