# Background

Before we start the excursion into the world of debugging, some background on the [debugWIRE interface](#the-debugwire-interface) is provided, sketching the physical properties of the debugWIRE protocol. Then we survey [other open-source approaches to debugging classic AVR MCUs](#other-debugging-approaches-for-classic-attinys-and-atmegax8s).


With that out of the way, we have a look at what we need on the hardware side.  You need an Arduino UNO (or something equivalent) as the [hardware debugger](requirements.md#the-hardware-debugger) and a [chip or board that understands debugWIRE](requirements.md#mcus-with-debugwire-interface), i.e., a classic ATtiny or an ATmegaX8. Then, you have to [install the debugger firmware on the UNO](installation.md#firmware-installation) and  [install a software debugging environment](installation.md#setting-up-the-debugging-software). If that is all done, you need to
[set up the hardware for a debugging session](setup.md#setting-up-the-hardware).

If you have performed all the above steps, then the setup should look like as in the following picture.

<p align="center">
<img src="https://raw.githubusercontent.com/felias-fogg/dw-link/refs/heads/master/docs/pics/debugger-setup.png" alt="hardware debugger setup" width="70%" />
</p>

Your development machine, the *host*, is connected to the UNO acting as a *hardware debugger* over the usual USB connection. The  two devices use the *GDB remote serial protocol* to communicate with each other. The hardware debugger in turn is connected to the *target system*, whereby the *debugWIRE protocol* is used for communication. 

The physical connection between the hardware debugger and the target, as described in the [section about the hardware setup](setup.md#setting-up-the-hardware), is something that might need some enhancements. Instead of six jumper wires, you may want to have a more durable connection. This is covered in the part about [*a better hardware debugger*](better.md). Finally, [possible problems](problems.md) and [troubleshooting](troubleshooting.md) is covered.

And what do you with your hardware debugger once you have debugged all your programs and they work flawlessly? Since version 2.2.0, you can use dw-link also as an STK500 v1 ISP programmer. If you connect to dw-link with 19200 bps and start avrdude, then dw-link becomes an ISP programmer.

## The debugWIRE interface

The basic idea of debugWIRE is that the RESET line is used as a communication line between the target system (the system you want to debug) and the hardware debugger, which in turn can then communicate with the development machine or host, which runs a debug program such as GDB. The idea of using only a single line that is not used otherwise is very cool because it does not waste any of the other pins for debugging purposes (as does, e.g., the [JTAG interface](https://en.wikipedia.org/wiki/JTAG)). However, using the RESET line as a communication channel means, of course, that one cannot use the RESET line to reset the MCU anymore. Furthermore, one cannot any longer use [ISP programming](https://en.wikipedia.org/wiki/In-system_programming) to upload new firmware to the MCU or change the fuses of the MCU. 

With respect to the debugWIRE protocol there are basically three states your MCU could be in:

1. The **normal** **state** in which the DWEN (debugWIRE enable) [fuse](https://microchipdeveloper.com/8avr:avrfuses) is disabled. In this state, you can use ISP programming to change fuses and to upload programs. By enabling the DWEN fuse, one reaches the **transitional** **state**.
2. The **transitional** **state** is the state in which the DWEN fuse is enabled. In this state, you could use ISP programming to disable the DWEN fuse again, to reach the **normal state**. By *power-cycling* (switching the target system off and on again), one reaches the **debugWIRE** **state**.
3. The **debugWIRE** **state** is the state in which you can use the debugger to control the target system. If you want to return to the **normal** **state**, a particular debugWIRE command leads to the **transitional state**, from which one can reach the **normal state** using ordinary ISP programming by disabling the DWEN fuse.





![state-dia](pics/statediag.png)

The hardware debugger will take care of bringing you from *normal* state to *debugWIRE* state when you type  the command ```monitor debugwire enable``` . In fact, when using the Arduino IDE 2, this will be done in the background for you. After the hardware debugger has enabled the DWEN fuse, the system LED will flash in a particular pattern, which signals that you should power-cycle the target. Further, in the GDB debugger, a message will be shown that asks you to power-cycle the target system. If the hardware debugger powers the target, it will power-cycle automatically. This transition is only necessary once. The next time, when you start a debugging session, the target system is already in the debugWIRE state and nothing needs to be done. 

When you are done with debugging and you want to get the target system back into the normal state, you must type the command ```monitor debugwire disable``` just before exiting the debugger.



## Other debugging approaches for classic ATtinys and ATmegaX8s

While dw-link is (unsurprisingly) one of my preferred open source solution for debugging classic tiny AVRs and ATmegaX8s, there are a number of other possible solutions. 

[Bloom](https://bloom.oscillate.io/) is not a hardware debugger, but it is a pretty extensive implementation of a gdbserver for almost all AVR MCUs using the Microchip hardware debuggers. The only drawback is that it runs only under Linux. Similarly, [avarice](https://github.com/avrdudes/avarice) is another such gdbserver, which covers even more hardware debuggers. Recently, I added another gdbserver to the mix, written for debugWIRE only: [dw-gdbserver](https://github.com/felias-fogg/dw-gdbserver). This has been extended to cover also JTAG targets and will probably be work with UPDI targets in the not so distant future. Best of all, it provides a pass-through service for dw-link.

There exists a software simulator called [SIMAVR](https://github.com/buserror/simavr), and there is a [GDB remote stub](https://sourceware.org/gdb/onlinedocs/gdb/Remote-Stub.html) for some ATmegas, called [avr_debug](https://github.com/jdolinay/avr_debug). Both are integrated into [PlatformIO](https://platformio.org/) as debuggers. However, both tools come with a lot of restrictions, and using them is not the same as debugging on the hardware where your firmware should finally run. 

Based on RikusW's work on [reverse engineering the debugWIRE protocol](http://www.ruemohr.org/docs/debugwire.html), you can find a few attempts at building debuggers using debugWIRE. First, there is an implementation called [dwire-debug](https://github.com/dcwbrown/dwire-debug) for host systems that uses only the serial line interface to talk with a target using the debugWIRE interface. This program implements GDB's remote serial protocol.  Unfortunately, the particular way of turning a serial interface into a one-wire interface did not work for me on a Mac. This approach has been further developed, resulting in an interesting solution for [debugging Arduino UNOs using a CH552 board](https://github.com/DeqingSun/unoDebugTestPackage). 

Then there is also an Arduino UNO-based hardware debugger called [DebugWireDebuggerProgrammer](https://github.com/wholder/DebugWireDebuggerProgrammer). However, it does not provide an interface for GDB's remote serial protocol. On top of that, all these solutions allow only one breakpoint (the hardware breakpoint of debugWIRE).

There exists an implementation similar to dwire-debug in Pascal called [debugwire-gdb-bridge](https://github.com/ccrause/debugwire-gdb-bridge) that appears to be more complete. In particular, it handles multiple breakpoints. However, I was not able to install it. That is probably based on the fact that my knowledge of Pascal is rusty and I have no experience with the Lazarus IDE. 

I took all of the above ideas (and some of the code) and put them together in order to come up with a cheap debugWIRE hardware debugger supporting GDB's remote serial protocol. Actually, it was a bit more than just throwing things together. I developed a [new library for single-wire serial communication](https://github.com/felias-fogg/SingleWireSerial) that is [much more reliable and robust](https://hinterm-ziel.de/index.php/2021/10/30/one-line-only/) than the usually employed SoftwareSerial library. Further, I fixed a few loose ends in the existing implementations, sped up communication and flash programming, supported slow MCU clocks, implemented an [interrupt-safe way of single-stepping](https://hinterm-ziel.de/index.php/2022/01/02/thats-one-small-step-for-a-man-one-giant-leap-for-a-debugger-on-single-stepping-and-interrupts/), and spent a few nights debugging the debugger.  And I tested the debugger on almost all MCUs supported by [ATTinyCore](https://github.com/SpenceKonde/ATTinyCore), [MicroCore](https://github.com/MCUdude/MicroCore), and [MiniCore](https://github.com/MCUdude/MiniCore). Along the way, I also made [a number of interesting discoveries](https://hinterm-ziel.de/index.php/2021/12/29/surprise-surprise/).

