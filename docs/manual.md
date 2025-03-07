# dw-link

# An Arduino-based debugWIRE debugger

**Bernhard Nebel**

**Version 4.0 - January 2025**

<a rel="license" href="http://creativecommons.org/licenses/by/4.0/"><img alt="Creative Commons License" style="border-width:0" src="https://i.creativecommons.org/l/by/4.0/88x31.png" /></a><br />This work is licensed under a <a rel="license" href="http://creativecommons.org/licenses/by/4.0/">Creative Commons Attribution 4.0 International License</a>.

![titlepic](pics/uno-debug2.png)

<div style="page-break-after: always"></div>

- [1. Introduction](#1-introduction)
  * [1.1 Enter the wonderful world of debugging in a few easy steps](#11-enter-the-wonderful-world-of-debugging-in-a-few-easy-steps)
  * [1.2 Other debugging approaches for classic ATtinys and ATmegaX8s](#12-other-debugging-approaches-for-classic-attinys-and-atmegax8s)
  * [Warning](#warning)
- [2. The debugWIRE interface](#2-the-debugwire-interface)
- [3. Hardware requirements](#3-hardware-requirements)
  * [3.1 Power supply](#31-power-supply)
  * [3.2 The hardware debugger](#32-the-hardware-debugger)
  * [3.3 MCUs with debugWIRE interface](#33-mcus-with-debugwire-interface)
  * [3.4 Requirements concerning the target system](#34-requirements-concerning-the-target-system)
  * [3.5 Worst-case scenario](#35-worst-case-scenario)
- [4. Installation of firmware and hardware setup](#4-installation-of-firmware-and-hardware-setup)
  * [4.1 Firmware installation](#41-firmware-installation)
  * [4.2 Setting up the hardware](#42-setting-up-the-hardware)
    + [4.2.1 Debugging an ATtiny85](#421-debugging-an-attiny85)
    + [4.2.2 Debugging an UNO](#422-debugging-an-uno)
    + [4.2.3 Debugging an UNO that needs a serial connection to the host](#423-debugging-an-uno-that-needs-a-serial-connection-to-the-host)
    + [4.2.4 Restoring an UNO to its native state](#424-restoring-an-uno-to-its-native-state)
  * [4.3 States of the hardware debugger](#43-states-of-the-hardware-debugger)
- [5. Arduino IDE 2](#5-arduino-ide-2)
  * [5.1 Installing board manager files](#51-installing-board-manager-files)
  * [5.2. Compiling the sketch](#52-compiling-the-sketch)
  * [5.3. Debugging](#53-debugging)
  * [5.4 Some Pro Tips](#54-some-pro-tips)
- [6. Arduino IDE and avr-gdb](#6-arduino-ide-and-avr-gdb)
  * [6.1 Installing board manager files](#61-installing-board-manager-files)
  * [6.2 Installing avr-gdb](#62-installing-avr-gdb)
  * [6.3 Compiling the sketch](#63-compiling-the-sketch)
  * [6.4 Example session with avr-gdb](#64-example-session-with-avr-gdb)
  * [6.5 Disabling debugWIRE mode explicitly](#65-disabling-debugwire-mode-explicitly)
  * [6.6 GDB commands](#66-gdb-commands)
  * [6.7 A graphical user interface: Gede](#67-a-graphical-user-interface--gede)
- [7 PlatformIO IDE](#7-platformio-ide)
  * [7.1 Installing PlatformIO](#71-installing-platformio)
  * [7.2 Open an existing project](#72-open-an-existing-project)
  * [7.2 Adapting the platformio.ini file](#72-adapting-the-platformioini-file)
  * [7.3 Debugging with PlatformIO](#73-debugging-with-platformio)
  * [7.4 Disabling debugWIRE mode](#74-disabling-debugwire-mode)
  * [7.5 Configuring platformio.ini](#75-configuring-platformioini)
- [8. A real hardware debugger](#8-a-real-hardware-debugger)
  * [8.1 The basic solution](#81-the-basic-solution)
  * [8.2 A simple shield](#82-a-simple-shield)
  * [8.3 Adapter with level-shifters and switchable power supply](#83-adapter-with-level-shifters-and-switchable-power-supply)
- [9. Problems and shortcomings](#9-problems-and-shortcomings)
  * [9.1 Flash memory wear](#91-flash-memory-wear)
  * [9.2 Slow responses when loading or single-stepping](#92-slow-responses-when-loading-or-single-stepping)
  * [9.3 Program execution is very slow when conditional breakpoints are present](#93-program-execution-is-very-slow-when-conditional-breakpoints-are-present)
  * [9.4 Single-stepping and interrupt handling clash](#94-single-stepping-and-interrupt-handling-clash)
  * [9.5 Limited number of breakpoints](#95-limited-number-of-breakpoints)
  * [9.6 Power saving is not operational](#96-power-saving-is-not-operational)
  * [9.7 MCU operations interfering with debugWIRE](#97-mcu-operations-interfering-with-debugwire)
  * [9.8 BREAK instructions in your program](#98-break-instructions-in-your-program)
  * [9.9 Some MCUs have stuck-at-one bits in the program counter](#99-some-mcus-have-stuck-at-one-bits-in-the-program-counter)
  * [9.10 The start of the debugger takes two seconds](#910-the-start-of-the-debugger-takes-two-seconds)
  * [9.11 Code optimization reorganizes code and makes it impossible to stop at a particular source line or to inspect or change values of local variables](#911-code-optimization-reorganizes-code-and-makes-it-impossible-to-stop-at-a-particular-source-line-or-to-inspect-or-change-values-of-local-variables)
- [10. Troubleshooting](#10-troubleshooting)
  * [10.1 Problems while preparing the setup](#101-problems-while-preparing-the-setup)
  * [10.2 Connection problems](#102-connection-problems)
  * [10.3 Problems while debugging](#103-problems-while-debugging)
  * [10.4 Strange behavior of the debugger](#104-strange-behavior-of-the-debugger)
  * [10.5 Problems with with GUI/IDE](#105-problems-with-with-gui-ide)
  * [10.6 Problems after debugging](#106-problems-after-debugging)
  * [10.7 Internal and fatal debugger errors](#107-internal-and-fatal-debugger-errors)
- [Acknowledgements](#acknowledgements)
- [Revision history](#revision-history)

<small><i><a href='http://ecotrust-canada.github.io/markdown-toc/'>Table of contents generated with markdown-toc</a></i></small>

<div style="page-break-after: always"></div>

## 1. Introduction 

The Arduino IDE is very simple and makes it easy to get started. After a while, however, one notices that a lot of important features are missing. In particular, neither the old nor the new IDE supports any kind of debugging for the classic AVR chips. So what can you do when you want to debug your Arduino project on small ATmegas (such as the popular ATmega328) or ATtinys? The usual way is to insert print statements and see whether the program does the things it is supposed to do. However, supposedly one should be able to do better than that because the above-mentioned MCUs support [on-chip debugging](https://en.wikipedia.org/wiki/In-circuit_emulation#On-chip_debugging) via [debugWIRE](https://en.wikipedia.org/wiki/DebugWIRE).

When you want hardware debugging support, you could buy expensive hardware debuggers such as the Atmel-ICE, PICkit4 or 5, or Atmel's Powerdebugger. Meanwhile, you can also buy MPLAB SNAP, which costs only around € 20.  In any case, you must use the proprietary development IDE [Microchip Studio](https://www.microchip.com/en-us/development-tools-tools-and-software/microchip-studio-for-avr-and-sam-devices) (for Windows) or [MPLAB X IDE](https://www.microchip.com/en-us/development-tools-tools-and-software/mplab-x-ide) (for all platforms). There is also [AVaRICE](https://avarice.sourceforge.io/), but I could never get a version that worked on my Mac. 

The question is, of course, whether there are open-source alternatives. Preferably supporting avr-gdb, the [GNU debugger](https://www.gnu.org/software/gdb/) for AVR MCUs.  With *dw-link*, you have such a solution. It turns an Arduino UNO into a hardware debugger that acts as a [gdbserver](https://en.wikipedia.org/wiki/Gdbserver) by implementing the [GDB remote serial protocol](https://www.embecosm.com/appnotes/ean4/embecosm-howto-rsp-server-ean4-issue-2.html). Meanwhile, you can buy an Arduino UNO shield called [dw-link probe](https://www.tindie.com/products/fogg/dw-link-probe-a-debugwire-hardware-debugger/) at Tindie, which simplifies the hardware setup, allows the debugging of 5 and 3.3 Volt systems, and is able to provide up to 200 mA supply current.

### 1.1 Enter the wonderful world of debugging in a few easy steps 

For your first excursion into the wonderful world of debugging, you need an Arduino UNO (or something equivalent) as the hardware debugger (see [Section 3.2](#section32)) and a chip or board that understands debugWIRE (see [Section 3.3](#section33)), i.e., a classic ATtiny or an ATmegaX8. Then you only have to install the firmware for the debugger on the UNO ([Section 4.1](#section41)) and to set up the hardware for a debugging session ([Section 4.2](#section42)).

Finally, you need to install a debugging environment. I will describe three approaches.

1. The first approach, described in Section 5, uses the Arduino IDE 2. You only need to download two board manager files before you can start debugging. 
2.  If you dislike the Arduino IDE 2 or cannot use it for some reason, then you can employ the approach covered in [Section 6](#section6). In addition to installing new board definition files, you must download and install avr-gdb. Debugging will occur in a console window, where you have to start avr-gdb. For people unhappy with command line interfaces, [Section 6.7](#section67) covers how to install and use a graphical user interface (which works only for macOS and Linux, though).  
3. The third method, described in [Section 7](#section7), involves downloading the [PlatformIO](https://platformio.org/) IDE, setting up a project, and starting your first debug session with this IDE. 

There are numerous other possibilities, which you might try out. In the [guide](https://github.com/jdolinay/avr_debug/blob/master/doc/avr_debug.pdf) to debugging with *avr_debug*, there is an extensive description of how to setup [Eclipse](https://www.eclipse.org/) for debugging with avr_debug, which applies to dw-link as well. Another option may be [Emacs](https://www.gnu.org/software/emacs/). 

If you have performed all the above steps, then the setup should look like as in the following picture.

<img src="pics/debugger-setup.png" alt="hardware debugger setup" style="zoom:50%;" />

Your development machine, the *host*, is connected to the UNO acting as a *hardware debugger* over the usual USB connection. The  two devices use the *GDB remote serial protocol* to communicate with each other. The hardware debugger in turn is connected to the *target system*, whereby the *debugWIRE protocol* is used for communication. 

The physical connection between the hardware debugger and the target, as described in [Section 4.2](#section42), is something that might need some enhancements. Instead of six flying wires, you may want to have a more durable connection. This is covered in [Section 8](#section8). Finally, possible problems and troubleshooting are covered in [Section 9](#section9) and [Section 10](#trouble), respectively.

And what do you with your hardware debugger once you have debugged all your programs and they work flawlessly? Since version 2.2.0, you can use dw-link also as an STK500 v1 ISP programmer. If you connect to dw-link with 19200 bps and start avrdude, then dw-link becomes an ISP programmer.

<a name="section12">

### 1.2 Other debugging approaches for classic ATtinys and ATmegaX8s

While dw-link is (unsurprisingly) my preferred open source solution for debugging classic tiny AVRs and ATmegaX8s, there are a number of other possible approaches. 

[Bloom](https://bloom.oscillate.io/) is not a hardware debugger, but it is a pretty extensive implementation of a GDB server for almost all AVR MCUs using the Microchip hardware debuggers. The only drawback is that it is only available under Linux. 

There exists a software simulator called [SIMAVR](https://github.com/buserror/simavr) and there is a [GDB remote stub](https://sourceware.org/gdb/onlinedocs/gdb/Remote-Stub.html) for some ATmegas, called [avr_debug](https://github.com/jdolinay/avr_debug). Both are integrated into [PlatformIO](https://platformio.org/) as debuggers. However, both tools come with a lot of restrictions and using them is not the same as debugging on the hardware where your firmware should finally run. 

Based on RikusW's work on [reverse engineering the debugWIRE protocol](http://www.ruemohr.org/docs/debugwire.html), you can find a few attempts at building debuggers using debugWIRE. First, there is an implementation called [dwire-debug](https://github.com/dcwbrown/dwire-debug) for host systems that uses only the serial line interface to talk with a target using the debugWIRE interface. This program implements GDB's remote serial protocol.  Unfortunately, the particular way of turning a serial interface into a one-wire interface did not work for me on a Mac (most probably, the large latency of my FTDI adapter was the culprit). This approach has been further developed, resulting in an interesting solution for [debugging Arduino UNOs using a CH552 board](https://github.com/DeqingSun/unoDebugTestPackage). In fact, this provided the breakthrough to enable debugging in the Arduino IDE 2 for dw-link. 

Finally, there exists also an Arduino UNO based hardware debugger called [DebugWireDebuggerProgrammer](https://github.com/wholder/DebugWireDebuggerProgrammer). However, it does not provide an interface for GDB's remote serial protocol. On top of that, all these solutions allow only one breakpoint (the hardware breakpoint of debugWIRE).

There exists an implementation similar to dwire-debug in Pascal called [debugwire-gdb-bridge](https://github.com/ccrause/debugwire-gdb-bridge) that appears to be more complete. In particular, it handles multiple breakpoints. However, I was not able to install it. That is probably based on the fact that my knowledge of Pascal is rusty and I have no experience with the Lazarus IDE. 

I took all of the above ideas (and some of the code) and put it together in order to come up with a cheap debugWIRE hardware debugger supporting GDB's remote serial protocol. Actually, it was a bit more than just throwing things together. I developed a [new library for single wire serial communication](https://github.com/felias-fogg/SingleWireSerial) that is [much more reliable and robust](https://hinterm-ziel.de/index.php/2021/10/30/one-line-only/) than the usually employed SoftwareSerial library. Further, I fixed a few loose ends in the existing implementations, sped up communication and flash programming, supported slow MCU clocks (4 kHz), implemented an [interrupt-safe way of single-stepping](https://hinterm-ziel.de/index.php/2022/01/02/thats-one-small-step-for-a-man-one-giant-leap-for-a-debugger-on-single-stepping-and-interrupts/), and spent a few nights debugging the debugger. Along the way, I also made [a number of interesting discoveries](https://hinterm-ziel.de/index.php/2021/12/29/surprise-surprise/). And I tested the debugger on almost all MCUs supported by [ATTinyCore](https://github.com/SpenceKonde/ATTinyCore) and [MiniCore](https://github.com/MCUdude/MiniCore). 

<font color="red">

### Warning

</font>

Please read  [Sections 3.4 & 3.5](#section34) about the RESET line requirements before connecting the debugger to a target system. You might very well "brick" your MCU by enabling debugWIRE on a system that does not satisfy these requirements. 

<a name="section2"></a>

## 2. The debugWIRE interface

The basic idea of debugWIRE is that the RESET line is used as a communication line between the target system (the system you want to debug) and the hardware debugger, which in turn can then communicate with the development machine or host, which runs a debug program such as GDB. The idea of using only a single line that is not used otherwise is very cool because it does not waste any of the other pins for debugging purposes (as does, e.g., the [JTAG interface](https://en.wikipedia.org/wiki/JTAG)). However, using the RESET line as a communication channel means, of course, that one cannot use the RESET line to reset the MCU anymore. Furthermore, one cannot any longer use [ISP programming](https://en.wikipedia.org/wiki/In-system_programming) to upload new firmware to the MCU or change the fuses of the MCU. 

Although dw-link tries to hide all this from you by enabling the debugWire mode when starting a debugging session and disabling it, when terminating the session, it is a good idea to get an idea of what is going on behind the scenes.

With respect to the debugWIRE protocol there are basically three states your MCU could be in:

1. The **normal** **state** in which the DWEN (debugWIRE enable) [fuse](https://microchipdeveloper.com/8avr:avrfuses) is disabled. In this state, you can use ISP programming to change fuses and to upload programs. By enabling the DWEN fuse, one reaches the **transitional** **state**.
2. The **transitional** **state** is the state in which the DWEN fuse is enabled. In this state, you could use ISP programming to disable the DWEN fuse again, to reach the **normal state**. By *power-cycling* (switching the target system off and on again), one reaches the **debugWIRE** **state**.
3. The **debugWIRE** **state** is the state in which you can use the debugger to control the target system. If you want to return to the **normal** **state**, a particular debugWIRE command leads to the **transitional state**, from which one can reach the **normal state** using ordinary ISP programming by disabling the DWEN fuse.

The hardware debugger will take care of bringing you from *normal* state to *debugWIRE* state when you connect to the target by using the `target remote` command or when using the ```monitor dwire +``` command. The system LED will flash in a particular pattern, which signals that you should power-cycle the target. Alternatively, if the target is powered by the hardware debugger, it will power-cycle automatically. The transition from *debugWIRE* state to *normal* state will take place when you terminate GDB. It can also be achieved by the GDB command ```monitor dwire -```. If things seem to have not worked out, you can simply reconnect the target to the hardware debugger and issue `monitor dwire -` again.



<!-- mermaid
    stateDiagram
    normal --\> transitional: set DWEN 
    transitional --\> normal: clear DWEN
    normal --\> debugWIRE: monitor dw +\ntarget remote ...
    transitional --\> debugWIRE: power cycle
    debugWIRE --\> transitional: disable debugWIRE
    debugWIRE --\> normal: monitor dw -\nquit -->

![state-dia](pics/state-diagram.png)

Since Version 4.1.0, automatic switching from normal state to debugWIRE state with `target remote ...` and conversely with the `quit` command is disabled when the target is externally powered. One can also disable it by grounding D3 or setting the jumper `AutoDW` on the dw-link probe to the `off`  position. In this case, the user must enable the transition to the debugWIRE state by `monitor dwire +` at the first connection. If the target is externally powered, the user will be asked to power cycle the target at this point.

When leaving the debugger, the debugWIRE state will not be left. In subsequent debugging sessions, debugWIRE mode is active when a connection is established with the `target remote` command. This means one can have debugging-editing cycle until everything is fixed.  After this cycle has been finished, one can disable the debugWIRE mode using the `monitor dwire -` command. After that, the only sensible operation is to quit the debugger. 

<a name="section3"></a>

## 3. Hardware requirements

There are a few constraints on what kind of board you can use as the base for the hardware debugger and some requirements on how to connect the debugger to the target system. Furthermore, there is only a limited set of AVR MCUs that can be debugged using debugWIRE.

Finally, there is the question of where you get the power from to source your target board.

### 3.1 Power supply

Microchip's official programming and debugging tools require an external power supply for the target board. On the other hand, cheap programmers, such as USBASP, power the target board, which is very convenient but has limitations. For instance, they have only limited power, typically up to 500 mA.

In the context of debugWIRE, powering the target board externally or by the hardware debugger makes a huge difference. In the latter case, the hardware debugger can power-cycle the target board. Otherwise, the user has to do that. The dw-link debugger can deal with both scenarios. When powered externally, you need to enable and disable debugWIRE mode explicitly using the GDB commands `monitor dwire +` and `monitor dwire -`. When using an IDE such as PlatformIO IDE or Arduino IDE 2, there are already commands in the startup scripts that bring the target board into this mode. To get out of it, you must either do that explicitly with the `monitor dw -` command or use the integrated ISP programmer once. 

<a name="section32"></a>

### 3.2 The hardware debugger

As a base for the debugger, in principle one can use any ATmega328 based board. The clock speed  must be 16MHz. Currently, the sketch has been tested on the following boards:

* [Arduino UNO](https://store.arduino.cc/products/arduino-uno-rev3),
* [Arduino Nano](https://store.arduino.cc/products/arduino-nano),
* [Arduino Pro Mini](https://docs.arduino.cc/retired/boards/arduino-pro-mini).

If you intend to use dw-link on a board with an MCU different from ATmega328P, you should be aware that dw-link makes heavy use of the particular hardware features of the ATmega328P and operates close to the limit. I tried it out on the Leonardo and on the Mega256, but was not successful. 

The most basic setup is to use the UNO board and connect the cables as it is shown in the [Fritzing sketch](#Fritzing) further down. If you want to use the debugger more than once, it may pay off to use a prototype shield and put an ISP socket on it. The more luxurious solution is a shield for the UNO as described further down in [Section 8](#section8).

<a name="section33"></a>

### 3.3 MCUs with debugWIRE interface

In general, almost all "classic" ATtiny MCUs and the ATmegaX8 MCU family have the debugWIRE interface. Specifically, the following MCUs that are supported by the Arduino standard core,  by [ATTinyCore](https://github.com/SpenceKonde/ATTinyCore), and/or by [MiniCore](https://github.com/MCUdude/MiniCore) can be debugged using this interface:

* <s>__ATtiny13__</s>
* __ATtiny43U__
* __ATtiny2313(A)__, __ATtiny4313__
* __ATtiny24(A)__, __ATtiny44(A)__, __ATtiny84(A)__
* __ATtiny441__, __ATtiny841__
* __ATtiny25__, __ATtiny45__, __ATtiny85__
* __ATtiny261(A)__, __ATtiny461(A)__, __ATtiny861(A)__
* __ATtiny87__, __ATtiny167__
* __ATtiny828__
* __ATtiny48__, __ATtiny88__
* __ATtiny1634__
* <s>__ATmega48__</s>, __ATmega48A__, __ATmega48PA__, ATmega48PB, 
* <s>__ATmega88__</s>, __ATmega88A__, __ATmega88PA__, Atmega88PB, 
* __ATmega168__, __ATmega168A__, __ATmega168PA__, ATmega168PB, 
* __ATmega328__, __ATmega328P__, __ATmega328PB__

I have tested the debugger on MCUs marked bold. The untested PB types appear to be very difficult to obtain. I excluded the ATtiny13 because it behaved very strangely, and I could not figure out why. The two ATmegas that are stroked out have program counters with some bits stuck at one (see Section 9.9). For this reason, GDB has problems debugging them, and dw-link rejects these MCUs. 

Additionally, there exist a few more exotic MCUs, which also have the debugWIRE interface:

* ATmega8U2, ATmega16U2, ATmega32U2
* ATmega32C1, ATmega64C1, ATmega16M1, ATmega32M1, ATmega64M1
* AT90USB82, AT90USB162
* AT90PWM1, AT90PWM2B, AT90PWM3B
* AT90PWM81, AT90PWM161
* AT90PWM216, AT90PWM316
* <s>ATmega8HVA</s>, <s>ATmega16HVA</s>, <s>ATmega16HVB</s>, <s>ATmega32HVA</s>, <s>ATmega32HVB</s>, <s>ATmega64HVE2</s>

The debugger contains code for supporting all listed MCUs except for the ones stroked out, which are obsolete. I expect the debugger to work on the supported MCUs. However, there are always [surprises](https://arduino-craft-corner.de/index.php/2021/12/29/surprise-surprise/). If you can debug such an MCU using dw-link, please drop me a note.

<a name="section34"></a>

### 3.4 Requirements concerning the target system 

If you want to connect the hardware debugger to a power-hungry target board, consider powering it externally. Power-hungry means > 20 mA when you power the target over a data pin or > 200 mA when using the dw-link probe. The target's capacitive load on the supply line could also create a problem because the hardware debuggers's supply voltage can drop significantly when the target board is switched on. Symptoms of the issues are lockups of the hardware debugger and/or the serial line. In this case, one should switch to external power. You will lose the automatic power-cycling feature and need to power-cycle your board manually. 

Another critical point is the RESET line of the target system. Since this line is used as an [open-drain](https://en.wikipedia.org/wiki/Open_collector#MOSFET), [asynchronous](https://en.wikipedia.org/wiki/Asynchronous_communication) [half-duplex](https://en.wikipedia.org/wiki/Duplex_(telecommunications)#HALF-DUPLEX) [serial communication](https://en.wikipedia.org/wiki/Serial_communication) line, one has to ensure there is no capacitive load on the line when used in debugWIRE mode. While the debugger tries to recognize such situations before any damage is done, it will certainly not catch all problematic configurations.

Further, there should be a pull-up resistor of around 10 kΩ. According to reports of other people, 4.7 kΩ might also work. And the RESET line should, of course,  not be directly connected to Vcc and there should not be any external reset sources on the RESET line. The debugger does not recognize these problems.

If your target system is an Arduino UNO, you have to be aware that there is a capacitor between the RESET pin of the ATmega328 and the DTR pin of the serial chip, which implements the auto-reset feature. This is used by the Arduino IDE to issue a reset pulse to start the bootloader. One can disconnect the capacitor by cutting the solder bridge labeled *RESET EN* on the board (see picture), but then you cannot use the automatic reset feature of the Arduino IDE any longer. 

<img src="pics/cutconn.jpg" alt="Solder bridge on Uno board" style="zoom:50%;" />

A recovery method is to put a bit of soldering  on the bridge or to solder pins to it that can be shortened by a jumper.

UNO  clones, which have a CH340G chip as the serial interface,  do not have such a nice cut-off point. Here, you need to remove the C8 capacitor. 

<img src="pics/remove-c8.png" alt="Solder bridge on Uno board" style="zoom:50%;" />

Other Arduino boards, [such as the Nano, are a bit harder to modify](https://mtech.dk/thomsen/electro/arduino.php). A Pro Mini, on the other hand, can be used without a problem, provided the DTR line of the FTDI connector is not connected. In general, it is a good idea to get hold of a schematic of the board you are going to debug. Then it is easy to find out what is connected to the RESET line, and what needs to be removed. It is probably also a good idea to check the value of the pull-up resistor, if present. 

<a name="worstcase"></a>
### 3.5 Worst-case scenario 

So, what is the worst-case scenario when using debugWIRE? As described in [Section 2](#section2), first, the DWEN fuse is programmed using ISP programming. Then, one has to power-cycle to reach the debugWIRE state, where you can communicate with the target over the RESET line. If this kind of communication fails, you cannot put the target back in a state where ISP programming is possible. And the bootloader will not work either because it had to be erased. Your MCU is *bricked*.   

One way to try to resurrect your MCU is to make the RESET line compliant with the debugWIRE requirements. Then you should be able to connect to the target using the hardware debugger. 

However, I have seen cases where the transition into debugWIRE mode was apparently incomplete and the MCU neither reacted to ISP programming nor did it respond with a 'U' after a break, which it should after having switched to debugWIRE mode. I have not yet nailed down under which circumstances this happens and I have not found anything about that on the web.

The most reliable way to resurrect you MCU is  HV programming, where 12 volt have to be applied to the RESET pin, and a lot of other things have to happen. So you either remove the chip from the board and do the programming offline or you remove any connection from the RESET line to the Vcc rail and other components on the board. Then you can use either an existing high-voltage programmer or you [build one on a breadboard](https://github.com/felias-fogg/RescueAVR).

<a name="section4"></a>


## 4. Installation of firmware and hardware setup

There are only a few steps necessary for installing the dw-link firmware on the hardware debugger, most of which you probably already have done. For the hardware setup, you need a breadboard or a development board with one of the chips that speaks debugWIRE.

<a name="section41"></a>

### 4.1 Firmware installation

Since the firmware of the hardware debugger comes in form of an Arduino sketch, you need to download first of all the [Arduino IDE](https://www.arduino.cc/en/software), if you have not done that already. Note that for some of the later software components (e.g., ATTinyCore), a reasonably recent version of the IDE is required, i.e. 1.8.13+. It is probably best when you upgrade your installation now. As an alternative, you can also use [PlatformIO](https://platformio.org/). 

Second, you need to download the dw-link firmware somewhere, where the IDE is able to find the Arduino sketch. If you use PlatformIO, note that the repository is already prepared to be opened as a PlatformIO project, i.e., it contains a `platformio.ini` file. Either download the dw-link repository or download a release.

Third, you have to connect your future hardware debugger, i.e., the ATmega328 board, to your computer, select the right board in the Arduino IDE, and upload the `dw-link.ino` sketch to the board. Similarly, in PlatformIO, you have to choose the right board and choose the `Upload` menu entry.

Usually, it should not be necessary to change a compile-time constant in dw-link. I will nevertheless document all these constants here. If you want to change one of them, you can do that when using `arduino-cli` by using the `--build-property` option or by changing the value in the source code.

| Name            | Default                | Meaning                                                      |
| --------------- | ---------------------- | ------------------------------------------------------------ |
| __VERSION__     | current version number | Current version number of dw-link; should not be changed, except when one generates a new version |
| **HOSTBPS**     | 115200                 | Communication speed for host interface                       |
| **HIGHSPEEDDW** | 0                      | If 1, the speed limit of debugWIRE communication is initially 300 kbps, otherwise it is 150kbps |
| __STUCKAT1PC__  | 0                      | If this value is set to 1, then dw-link will accept connections to targets that have program counters with stuck-at-one bits; one can then use the debugger, but GDB can get confused at many points, e.g., when single-stepping or when trying to produce a stack backtrace. |

<a name="section42"></a>

### 4.2 Setting up the hardware

Before you can start debugging, you have to set up the hardware. I'll use an ATtiny85 on a breadboard as the example target system and an UNO as the example debugger. However, any MCU listed above would do as a target. You have to adapt the steps where I describe the modification of configuration files in [Section 6](#section6) accordingly, though. One could even use an Arduino UNO, provided the modifications described in [Section 3.3](#section34) are done. 

#### 4.2.1 Debugging an ATtiny85

In order to debug an ATtiny85, we will assume it is completely "naked" and plugged into a breadboard as shown below. 

<a name="Fritzing"></a>

![Fritz-attiny](pics/debug-attiny85-LED-onboard.png)

First of all, notice the capacitor of 10 µF or more between RESET and GND on the UNO board. This will disable the auto-reset of the UNO board. Second, note the yellow LED plugged into pin D7. This is the system LED which is used to visualise the internal state of the debugger (see below).  You can also build an LED with a series resistor soldered on and then use pins D6 and D7, where D6 is used as GND.

As you can see, the Vcc rail of the breadboard is connected to pin D9 of the Arduino UNO so that it will be able to power-cycle the target chip. Furthermore, pin D8 of the Arduino UNO is connected to the RESET pin of the ATtiny (pin 1).   Note the presence of the pull-up resistor of 10kΩ on the ATtiny RESET pin. The remaining connections between Arduino UNO and ATtiny are MOSI (Arduino UNO D11), MISO (Arduino UNO D12), and SCK (Arduino UNO D13), which you need for ISP programming. In addition, there is an LED connected to pin 3 of the ATtiny chip (which is PB4 or pin D4 in Arduino terminology). The pinout of the ATtiny85 is given in the next figure (with the usual "counter-clockwise" numbering of Arduino pins).


![ATtiny85 pinout](https://raw.githubusercontent.com/SpenceKonde/ATTinyCore/v2.0.0-devThis-is-the-head-submit-PRs-against-this/avr/extras/Pinout_x5.jpg)

Here is a table of all the connections so that you can check that you have made all the connections. 

ATtiny pin# | Arduino UNO pin | component
--- | --- | ---
1 (Reset) | D8 | 10k resistor to Vcc 
2 (D3) |  |
3 (D4) |  |220 Ω resistor to (red) target LED (+)
4 (GND) | GND | both LED (-), decoupling cap 100 nF, RESET blocking cap of 10µF (-) 
5 (D0, MOSI) | D11 |
6 (D1, MISO) | D12 |
7 (D2, SCK) | D13 |
8 (Vcc) | D9 | 10k resistor, decoupling cap 100 nF 
&nbsp;|RESET|RESET blocking cap of 10 µF (+)
&nbsp;|D7|220 Ω resistor to (yellow) system LED (+)

<a name="section452"></a>

#### 4.2.2 Debugging an UNO

If you want to debug an UNO board instead of an ATtiny85, everything said above applies. The Fritzing sketch below shows the connections. Here, the series resistor for the system LED is soldered to the LED cathode, so we do not need a breadboard. The hardware debugger needs a USB connection to your host, but the target should not be connected to the host! Otherwise, the automatic power-cycling will not work (but see below). 

Remember to cut the `RESET EN` solder bridge on the target board (see [Section 3.4](#section34))! When you first establish a connection with the UNO as a target, the target will be completely erased (including the boot loader), because the lock bits have to be cleared. The steps to restore your UNO to its original state are described below in [Section 4.2.4](#section424).

![Uno as DUT](pics/Debug-Uno.png)

While the above configuration works, the ATmegas are used slightly outside their specifications. The hardware debugger sources roughly 30 mA to the target through pin D9, which is inside the maximum rating, but the voltage drops to 4.0 V, which is outside the specs for running the ATmega at 16 MHz. So, a more stable configuration is one where the target is sourced from an external power supply.

<a name="section423"></a>

#### 4.2.3 Debugging an UNO that needs a serial connection to the host

Special considerations are necessary if you want to debug an Arduino UNO that needs a serial connection to the host. You may plug a USB cable into the target, but remember to remove the power supply connection from the hardware debugger to the target.

Now, open up a terminal connection to the target, using, e.g., cu, screen, PuTTY, HTerm, or any other program that can establish a serial connection. The disadvantage is that automatic power-cycling does not work any longer. So, when the system LED signals that you should power-cycle (0.1-second flash every second), you must disconnect and reconnect the USB cable to the target.

A more elegant solution is to use a USB-to-serial converter and plug the TX, RX, and GND connections into the appropriate sockets on the target. Or you can use a [USB power blocker](https://spacehuhn.store/products/usb-power-blocker), something you can also find on Amazon under the name [PortaPow USB Power Blocker](https://www.amazon.de/PortaPow-USB-Power-Blocker/dp/B094FYL9QT/). Such a blocker cuts off the power line so that automatic power-cycling can work its magic.

<a name="section424"></a>

#### 4.2.4 Restoring an UNO to its native state

When after a debugging session you want to restore the target so that it behaves again like an ordinary UNO, you need to do the following:

1. You need the target board to exit the debugWIRE mode. This could be done as described in [Section 6.5,](#explicit1) or it will be done automagically using the internal programmer mentioned in the following step.
2. Now you have to flash the bootloader again. As mentioned in Section 1, since version 2.2.0, the hardware debugger can also act as a programmer! This means that you leave the whole hardware setup as it was. 
3. Select `Arduino UNO` as the target board in the `Tools` menu, select `AVR ISP` as the Programmer, and choose `Burn Bootloader` from the `Tools` menu. This will revert the MCU to its normal state (if it still in debugWARE state) and will restore the fuses, the bootloader, and the lock bits to their original state. 
4. Reestablish the `RESET EN` connection by putting a solder blob on the connection or soldering pins to the connections that can be shortened using a jumper as shown in the next picture. It does not look pretty, but it does its job. After that, your UNO is as good as new.

![restored](pics/pins+jumper.JPG)

### 4.3 States of the hardware debugger

We are now good to go and 'only' need to install the additional debugging software on the host. Before we do that, let us have a look, in which states the hardware debugger can be and how it signals that using the system LED.

There are five states the debugger can be in and each is signaled by a different blink pattern of the system LED:

* not connected (LED is off),
* waiting for power-cycling the target (LED flashes every second for 0.1 sec),
* target is connected (LED is on) ,
* ISP programming (LED is blinking slowly every 0.5 sec), or
* error, i.e., it is not possible to connect to the target or there is an internal error (LED blinks furiously every 0.1 sec).

If the hardware debugger is in the error state, one should try to find out the reason by typing the command `monitor lasterror`, studying the [error message table](#fatalerror) at the end of the document, finishing the GDB session, resetting the debugger, and restarting everything. I have made the experience that sometimes it is a good idea to disconnect the USB cable and the connection to the target before starting over.

 If the problem persists, please check the section on [troubleshooting](#trouble).

<a name="section5"></a>

## 5. Arduino IDE 2

Debugging with Arduino IDE 2 is probably the most straightforward approach. You only have to install two board manager files before you can start. 

However, you must keep in mind that you cannot use the `Arduino Uno` selection in the `Tools` menu to debug an UNO board. You have to select the `Atmega328` board from the MiniCore package instead and select `External 16 MHz` as the clock source.

### 5.1 Installing board manager files

We need to install two new cores, which are forks of [MiniCore](https://github.com/MCUdude/MiniCore) and [ATTinyCore](https://github.com/SpenceKonde/ATTinyCore). Open the `Preference` dialog of the Arduino IDE and paste the following two URLs into the list of `Additional boards manager URLs`:

```
https://felias-fogg.github.io/ATTinyCore/package_drazzy.com_ATTinyCore_plus_Debug_index.json
```

```
https://felias-fogg.github.io/MiniCore/package_MCUdude_MiniCore_plus_Debug_index.json
```

Then, you need to start the  `Boards Manager`, which you find under `Tools`-->`Board`. Install MiniCore and ATTinyCore, choosing the most recent version with a `+debug-2.X` postfix. Note that the packages include tools that are incompatible with older OS versions. In particular, 32-bit Linux systems are not supported. If you have such a system, you can use only the older debug-enabled packages and the approach described in Sections 6 and 7.

### 5.2. Compiling the sketch

You must load the sketch into the editor and select a board as usual. Before clicking the `Verify` button in the upper left corner, choose `Optimize for Debugging` in the `Sketch` menu. This is necessary so that the compiler does not optimize the code in a way that makes debugging impossible. Alternatively, you can fine-tune the different compiler optimizations by using the `Debug Compile Flags` attribute in the `Board` section of the `Tools` menu (see [Section 9.11](#section911)). 

### 5.3. Debugging

You start by first verifying the sketch (which will also compile it) and then by clicking on the debug button in the right side bar, as shown below.

![ide0](pics/ide0.png)

After that, one can click the debug button in the upper row to start the debug process, as shown in the following picture.

![ide1](pics/ide1.png) 

The debugger starts, and eventually, execution is stopped in line 8 at an initial internal breakpoint, indicated by the yellow triangle left of line 8 in the following screenshot. It might take a while before we reach that point because the debugger also loads the program. 

Now is a good time to familiarize yourself with the window's layout. The source code is on the right side. Below that is a console window, and to the left are the debug panes. If you want to set a breakpoint, you can do that by clicking to the left of the line numbers. Such breakpoints are displayed as red dots as the one left of line 12.

![ide2](pics/ide2.png)

Pane A contains the debug controls. From left to right:

- *Reset*ting the device
- *Continue* execution or *pause*
- *Step over*: execute one source line
- *Step into*: execute stepping into the function, if in this line one is called
- *Step out*: finish the current function and stop where it had been called
- *Restart*: Same as Reset
- *Stop*: Terminate debugging

Pane B shows the active threads, but there is just one in our case. Pane C displays the call stack starting from the bottom, i.e., the current frame is the topmost. Pane D displays variable values. Unfortunately, global variables are not shown. Pane E can be populated with watch expressions. This can be used to display relevant global variables. Finally, in pane F, the active breakpoints are listed. The panes below are useless in our case.

Some more information about debugging can be found in the [debugging tutorial](https://piolabs.com/blog/insights/debugging-introduction.html) for the PlatformIO IDE. Although it was written for PlatformIO, it also applies to the Arduino IDE 2 to a large extent. 

### 5.4 Some Pro Tips

Global variables are, for some reason, not displayed. However, you can set a watch expression in the Watch pane to display a global variable's value.

If you select the Debug Console, you can type GDB commands (see [Section 6.6](#section66)) in the bottom line. This can be useful for changing the value of global variables, which cannot be accessed otherwise. Or for disabling debugWIRE mode. 

When powering your target board externally, you must disconnect the power line from the hardware debugger to the target. For example, on the dw-link probe, you can set the `power` jumper to the middle position. This setting will disable the automatic transition back to the normal state when the debugger is terminated in order to minimize the number of manual power-cycles the user has to perform. You now have to use  the `monitor dwire -`  command before exiting the debugger. Alternatively, you can choose `Burn Bootloader` from the `Tools` menu with dw-link as a programmer (the programmer should then be `Arduino as ISP`).

<a name="section6"></a>

## 6. Arduino IDE and avr-gdb

A more minimalistic approach might be better if you are uncomfortable using Arduino IDE 2. The GNU Debugger GDB provides console-based interactions and can be easily configured to work with dw-link. You only must install the avr-gdb debugger and the appropriate board manager files. Note that this works only with Arduino IDE versions at least 1.8.13. 

As mentioned above, you must keep in mind that you cannot use the `Arduino Uno` selection in the `Tools` menu to debug an UNO board. You have to select the `Atmega328` board instead and select `External 16 MHz` as the clock source.

### 6.1 Installing board manager files

In order to be able to debug the MCUs mentioned in the Introduction, you need to install 3rd party cores. For the classic ATtiny family, this is [ATTinyCore](https://github.com/SpenceKonde/ATTinyCore/blob/master/Installation.md) and for the ATmegaX8 family (including the Arduino UNO), it is [MiniCore](https://github.com/MCUdude/MiniCore). To be able to generate object files that are debug-friendly, you need to install my fork of the board manager files. You first have to add URLs under the `Additional Boards Manager URLs` in the `Preference` menu:

- ```
  https://felias-fogg.github.io/ATTinyCore/package_drazzy.com_ATTinyCore_plus_Debug_index.json
  ```

- ```
  https://felias-fogg.github.io/MiniCore/package_MCUdude_MiniCore_plus_Debug_index.json
  ```

After that, you can download and install the board using the `Boards Manager`, which you find in the Arduino IDE menu under `Tools`-->`Board`. Currently, choose the versions that have a `+debug-2.X` suffix in their version number! I hope the capability of generating debug-friendly binaries will be incorporated in future versions of these board manager files, in which case you can rely on the regular board manager files by MCUdude and SpenceKonde.

### 6.2 Installing avr-gdb

Unfortunately, the debugger is no longer part of the toolchain integrated into the Arduino IDE. However, the new board manager file will download a version of avr-gdb, and you should be able to copy it to a place in your `PATH`, e.g., `/usr/local/bin`. You can find them in the tool directory of the corresponding packages:

- On a Mac: `~/Library/Arduino15/packages/MiniCore/tools/dw-link-tools/XXX/avr-gdb`
- Under Linux: `~/.arduino15/packages/MiniCore/tools/dw-link-tools/XXX/avr-gdb`
- Under Windows:  `C:\Users\\{username}\AppData\Local\Arduino15\packages\MiniCore\tools\dw-link-tools\XXX\avr-gdb.exe`

If the avr-gdb version is not there or all of the versions are incompatible with your operating system. In this case, you have to download and install it by yourself:

* macOS: Use [**homebrew**](https://brew.sh/index_de) to install it: 

  ```
  brew tap osx-cross/avr && brew install avr-gdb
  ```

* Linux: Use your favorite packet manager to install the package **gdb-avr**, i.e., under Ubuntu/Debian:

  ```
  sudo apt-get install gdb-avr 
  ```

* Windows: You can download the AVR-toolchain from the [Microchip website](https://www.microchip.com/en-us/development-tools-tools-and-software/gcc-compilers-avr-and-arm) or [Zak's Electronic Blog](https://blog.zakkemble.net/avr-gcc-builds/). This includes avr-gdb. You have to copy `avr-gdb.exe` (which you find in the `bin` folder) to some place (e.g., to C:\ProgramFiles\bin) and set the `PATH` variable to point to this folder. Afterward, you can execute the debugger by simply typing `avr-gdb.exe` into a terminal window (e.g. the Windows Powershell).

### 6.3 Compiling the sketch

Now we need to compile a sketch. Let us take `varblink.ino` from the `examples` folder of the dw-link package as our example sketch. First, select the board you want to compile the sketch for under the `Tools` menu. In particular, you should select a `328P` (with a 16MHz clock) from the `MiniCore` when debugging an UNO board. 

Then set additional parameters for the board. Most importantly, you need to select `Debug` for the `Debug Compiler Flags` option. The other possibilities for this option can be chosen when the debugger should create code that more closely mirrors the source code, as described in [Section 9.11](#section911). Now compile the example `varblink.ino` with debugging enabled by requesting to `Export compiled Binary` in the `Sketch` menu. This will generate the file `varblink.ino.elf` in the sketch or build directory, depending on which version of the IDE or CLI you are using.

### 6.4 Example session with avr-gdb

Open a terminal window, and change to the directory where the Arduino IDE/CLI has copied the ELF file to. Assuming that you have connected the hardware debugger to the target board and the host computer, we can start a debugging session.

All the lines starting with either the **>** or the **(gdb)** prompt contain user input and everything after # is a comment. **\<serial port\>** is the serial port you use to communicate with the hardware debugger and **\<bps\>** is the baud rate, i.e., 115200, provided you have not changed the compile-time constant `HOSTBPS`.

```
> avr-gdb -b <bps> varblink.ino.elf 
GNU gdb (GDB) 10.1
Copyright (C) 2020 Free Software Foundation, Inc.
...
Reading symbols from varblink.ino.elf...
(gdb) target remote <serial port>              # connect to the HW debugger  
Remote debugging using <serial port>           # connection made
0x00000000 in __vectors ()                     # we always start at location 0x0000
(gdb) monitor dwire                            # show properties of the dW connection 
Connected to ATtiny85
debugWIRE is now enabled, bps: 125736
(gdb) load                                     # load binary file
Loading section .text, size 0x714 lma 0x0
Loading section .data, size 0x4 lma 0x714
Start address 0x00000000, load size 1816
Transfer rate: 618 bytes/sec, 113 bytes/write.
(gdb) list loop                                # list part of loop and shift focus
6       byte thisByte = 0;
7       void setup() {
8         pinMode(LED, OUTPUT);
9       }
10      
11      void loop() {
12        int i=digitalRead(1)+20;
13        digitalWrite(LED, HIGH);  
14        delay(1000);              
15        digitalWrite(LED, LOW);              
(gdb) break loop                               # set breakpoint at start of loop 
Breakpoint 1 at 0x494: file ..., line 12.
(gdb) br 15                                    # set breakpoint at line 15
Breakpoint 2 at 0x4bc: file ..., line 15.
(gdb) c                                        # start execution at PC=0
Continuing.

Breakpoint 1, loop () at /.../varblink.ino:12
12        int i=digitalRead(1)+20;
(gdb) next                                     # single-step over function              
13        digitalWrite(LED, HIGH);  
(gdb) n                                        # again
14        delay(1000);              
(gdb) print i                                  # print value of 'i'
$1 = 21
(gdb)  print thisByte                          # print value of 'thisByte'
$2 = 0 '\000'
(gdb) set var thisByte = 20                    # set variable thisByte
(gdb) p thisByte                               # print value of 'thisByte' again
$3 = 20 '\024'
(gdb) step                                     # single-step into function
delay (ms=1000) at /.../wiring.c:108
108             uint32_t start = micros();
(gdb) finish                                    # execute until function returns
Run till exit from #0  delay (ms=1000)
    at /.../wiring.c:108

Breakpoint 2, loop () at /.../varblink.ino:15
15        digitalWrite(LED, LOW);              
(gdb) info br                                   # give inormation about breakpoints
Num     Type           Disp Enb Address    What
1       breakpoint     keep y   0x00000494 in loop() 
                                           at /.../varblink.ino:12
        breakpoint already hit 1 time
2       breakpoint     keep y   0x000004bc in loop() 
                                           at /.../varblink.ino:15
        breakpoint already hit 1 time
(gdb) delete 1                                  # delete breakpoint 1
(gdb) quit                                      # quit debugger
A debugging session is active.

	Inferior 1 [Remote target] will be killed.

Quit anyway? (y or n) y
>
```



<a name="explicit1"></a>

### 6.5 Disabling debugWIRE mode explicitly

Exiting GDB should disable debugWIRE mode. However, if something went wrong, you set the [`AutoDW` jumper](#jumper) to `off`, you powered your target board externally, or you killed the debug session, the MCU might still be in debugWIRE mode, the RESET pin cannot be used to reset the chip, and you cannot use general ISP programming. In this case, you can explicitly disable debugWIRE, as shown below. 

```
> avr-gdb
GNU gdb (GDB) 10.1
...

(gdb) set serial baud 115200            # set baud rate 
(gdb) target remote <serial port>       # connect to serial port of debugger    
0x00000000 in __vectors ()
(gdb) monitor dwire -                   # terminate debugWIRE mode 
Connected to ATtiny85
debugWIRE is now disabled
(gdb) quit
>
```
<a name="section66"></a>

### 6.6 GDB commands

In the example session above, we saw several relevant commands already. If you really want to debug using GDB, you need to know a few more commands, though. Let me just give a brief overview of the most relevant commands (anything between square brackets can be omitted, a vertical bar separates alternative forms, and arguments are in italics). For the most common commands, it is enough to just type the first character (shown in boldface). In general, you only have to type as many characters as are necessary to make the command unambiguous. You also find a good reference card and a very extensive manual on the [GDB website](https://sourceware.org/gdb/current/onlinedocs/). I also recommend these [tips on using GDB](https://interrupt.memfault.com/blog/advanced-gdb) by [Jay Carlson](https://jaycarlson.net/). 

command | action
--- | ---
**h**elp [*command*] | get help on a specific command
**s**tep [*number*] | single step statement, descending into functions (step in), *number* times 
**n**ext [*number*] | single step statement without descending into functions (step over)
finish | finish the current function and return from call (step out) 
**c**ontinue [*number*] | continue from current position and stop after *number* breakpoints have been hit. 
**r**un | reset MCU and restart program at address 0x0000
**b**reak *function* \| [*file*:]*number* | set breakpoint at beginning of *function* or at line *number* in *file* 
**i**nfo **b**reakpoints | list all breakpoints
**d**elete [*number* ...] | delete breakpoint(s) *number* or all breakpoints 

In order to display information about the program and variables in it, the following commands are helpful. Further, you may want to change the value of variables.

command | action
--- | ---
**l**ist [*function* \| [*filename*:]*number*] | show source code around current point, of *function*, or around line *number* in *filename* 
**p**rint *expression* | evaluate expression and print 
set var *variable* = *expression* | set the variable to a new value 
display\[/*f*] *expression* | display expression using format *f* each time the program halts
**i**nfo display | print all auto-display commands
**d**elete display [*number* ...] | delete auto-display commands(s) or all auto-display commands 

<a name="controlcommands"></a>In addition to the commands above, you have to know a few more commands that control the execution of avr-gdb.

command | action
--- | ---
set serial baud *number* | set baud rate of serial port to the hardware debugger (same as using the `-b` option when starting avr-gdb); only effective when called before establishing a connection with the `target` command 
target [extended-]remote *serialport* | establish a connection to the hardware debugger via *serialport*, which in turn will set up a connection to the target via debugWIRE; if extended is used, then establish a connection in the *extended remote mode*, i.e., one can restart the program using the `run` command 
file *name*.elf | load the symbol table from the specified ELF file 
load | load the ELF file into flash memory (should be done every time after the `target remote` command; it will only change the parts of the flash memory that needs to be changed)
**q**uit | exit from GDB 

Finally, there are commands that control the settings of the debugger and the MCU, which are particular to dw-link. They all start with the keyword `monitor`. You can abbreviate all keywords to 2 characters if this is unambiguous.

| command                      | action                                                       |
| :--------------------------- | ------------------------------------------------------------ |
| monitor help                 | give a help message on monitor commands                      |
| monitor version              | print version number of firmware                             |
| monitor dwire [+\|-]         | **+** activate debugWIRE; **-** disables debugWIRE; without any argument, it will report MCU type and whether debugWIRE is enabled (*) |
| monitor reset                | resets the MCU (*)                                           |
| monitor load [r\|w]          | When loading, either read before write (r) or write only. The first option is faster and reduces flash wear. So, it is the default. |
| monitor runtimes             | run timers (**+**) or freeze (**-**) (default) when the program is stopped |
| monitor mcu [*mcu-name*]     | If no argument is given, the MCU dw-link is connected to is printed. Otherwise, it is checked whether the given *mcu-name* matches the connected MCU and if not, a fatal error is signaled and debugging is stopped. |
| monitor ckdiv [1\|8]         | **1** unprograms the CKDIV8 fuse, **8** programs it; without an argument, the state of the fuse is reported (*+) |
| monitor breakpoint [h\|s\|S] | set the number of allowed breakpoints to 1, when **h**ardware breakpoint only, or 25 (default), when also **s**oftware breakpoints are permitted; use **S** if *only* software breakpoints should be used; without argument, it reports setting |
| monitor speed [l\|h]         | set the communication speed limit to **l**ow (=150kbps) (default) or to **h**igh (=300kbps); without an argument, the current communication speed and speed limit is printed |
| monitor singlestep [s\|u]    | Sets single stepping to **s**afe (no interrupts) (default) or **u**nsafe (interrupts can happen); without an argument, it reports the state |
| monitor lasterror            | print error number of the last fatal error                   |
| monitor flashcount           | reports on how many flash-page write operations have taken place since the start |
| monitor timeouts             | report number of timeouts (should be 0!)                     |

All of the commands marked with (*) reset the MCU. The ones marked with (+) need to take the MCU out of the debugWIRE mode and afterwards in again. 

<a name="section67"></a>

### 6.7 A graphical user interface: Gede

If you believe that GDB is too much typing, then you are probably the type of programmer who wants a graphical user interface. As it turns out, it is not completely trivial to come up with a solution that is easy to install and easy to work with. Recently, I stumbled over *gede*, which appears to be just the right solution. It has been designed for Linux, but after a few small changes, it also works under macOS. Unfortunately, Windows is not supported. However, you could use [*WSL2*](https://en.wikipedia.org/wiki/Windows_Subsystem_for_Linux) to run Gede, avr-gdb, dw-server.py, and the Arduino IDE. Connecting to a serial port could then be done by using [*usb-ip*](https://github.com/dorssel/usbipd-win). 

To make a long story short, you can download the source from the [Gede repo](https://github.com/jhn98032/gede) and build it according to the instructions in the readme file. It is straightforward.

The `dw-server` directory of the dw-link directory contains a Python script called `dw-server.py`, which you should copy to `/usr/local/bin`. 

Open now a terminal window, `cd` into the folder that contains the ELF file, and type

```
dw-server.py -g
```

The script will try to discover a dw-link adapter connected to a serial line. After that, it starts gede, and then it forwards the serial connection over TCP/IP to gede, which will present you with the following window.

![gede-start](pics/gede-start.png)

`Project dir` and `Program` are specific to your debugging session. The rest should be copied as it is shown. And with clicking on OK, you start a debugging session. Well, the startup takes a while because the debugger always loads the object file into memory.

The GUI looks as shown in the next figure. Johan Henriksson, the author of the GUI, has written up two [short tutorials](https://gede.dexar.se/pmwiki.php?n=Site.Tutorials) about using it.

![gede](pics/gede.png)

<a name="section7"></a>

## 7 PlatformIO IDE

[*PlatformIO*](https://platformio.org/) is an IDE for embedded systems based on PlatformIO Core, which extends Visual Studio Code. It supports many MCUs, particularly almost all AVR MCUs. It is also possible to import Arduino projects, which are converted into ordinary C++ projects. Projects are highly configurable, which means that many parameters can be set for different purposes. However, that makes things a bit more challenging in the beginning. 

The main differences to the Arduino IDE are:

1. You do not select the MCU and its parameters using a dropdown menu, but you have to write/modify the INI-style file `platform.ini`.
2. Libraries are not global by default, but they are usually local to each project. That means that a new library version will not break your project, but you have to update library versions for each project separately.
3. No preprocessor generates function declarations automagically as the Arduino IDE/CLI does for you. You have to add the include statement for the Arduino header file and all function declarations by yourself. In addition, you need to import `Arduino.h` explicitly.
4. There is already a powerful editor integrated into the IDE.
5. Most importantly, the IDE contains ways to configure the debugging interface, which makes it possible to integrate dw-link easily. Note that this is still not possible for the Arduino IDE 2.X!

So, moving from the Arduino IDE to PlatformIO is a significant step! 

### 7.1 Installing PlatformIO

Installing PlatformIO is straight-forward. Download and install Visual Studio Code. Then start it and click on the extension icon on the left, search for the PlatformIO extension, and install it, as is described [here](https://platformio.org/install/ide?install=vscode). Check out the [quick start guide](https://docs.platformio.org/en/latest//integration/ide/vscode.html#quick-start). Now we are all set.

On a Mac, unfortunately, it does not work out of the box, because the gcc-toolchain PlatformIO uses is quite dated, and the included version of avr-gdb is no longer compatible with recent macOS versions. Simply install avr-gdb with homebrew and copy the file (`/usr/local/bin/avr-gdb`) into the toolchain directory (`~/.platformio/packages/toolchain-atmelavr/bin/`).

### 7.2 Open an existing project

Now let us prepare a debugging session with the same Arduino sketch as in the last section. However, this time it is a C++ file (with the extension `cpp`) and it contains the necessary `#include <Arduino.h>`. Startup *Visual Studio Code* and click on the Ant symbol in the left bar, then on Open in the list menu, and finally on **Open Project** in the right panel. 

![PIO](pics/pio00a.png)

After that, you have to navigate through your file structure and find the project that you want to open. Finally, click on **Open pio-varblink**.

![PIO-open](pics/pio00b.png)

### 7.2 Adapting the platformio.ini file

PlatformIO will then open the project and come up with platform.ini in the editor window. There it is obvious that we need to replace the string SERIAL-PORT with the actual port, our hardware debugger is connected to. 

![PIO-ini](pics/pio01.png)

To find out, which port it is, we can ask for the connected serial ports as shown below. Then we can just copy the name of the port by clicking right on  paper icon.

![PIO-device](pics/pio02.png)

After that, the string can be inserted into the `platformio.ini` file.

![PIO-insert-device](pics/pio03.png)

### 7.3 Debugging with PlatformIO

If you click on the debug symbol in the left navigation bar (fourth from the top), PlatformIO enables debugging using the custom specifications in `platform.ini`. You can now start a debug session by clicking the green triangle at the top navigation bar labeled **PIO Debug**. 

![PIO](pics/pio04.png)

On the right, the debug control bar shows up, with symbols for starting execution, step-over, step-in, step-out, reset, and exit. On the left, several window panes give you information about variables, watchpoints, the call stack, breakpoints, peripherals, registers, memory, and disassembly. The program has already been started and stopped at the temporary breakpoint set at the first line of `main`.

![PIOdebugactive](pics/pio05.png)

Before we play around with the debug control buttons, let us set a breakpoint in the user program. First, select the explorer view by clicking on the left top icon, then select the file varblink.cpp.

![PIOexplore](pics/pio06.png)

Now, we can set a breakpoint, e.g. in line 11, by double clicking left to line number. After that, let us start execution by pressing the execute button.

![PIObreakpoint](pics/pio07.png)

After a brief moment. the debugger will then stop at the breakpoint.

![PIObreak](pics/pio08.png)

<a name="explicit2"></a>

### 7.4 Disabling debugWIRE mode

There are two ways of switching off the debugWIRE mode. It happens automatically when you terminate the debugger using the exit button. Alternatively, you should be able to bring back your MCU to the normal state by typing `monitor dwire -` in the debugging terminal window after having started a debugging session in PlatformIO IDE. 

### 7.5 Configuring platformio.ini

This is not the place to tell you all about what can be configured in the `platformio.ini` file.  There is one important point, though. PlatformIO debugging will always choose the *default environment* or, if this is not set, the first environment in the config file. 

You may have noticed the following two lines: 

```
debug_port = /dev/cu.usbmodem211101 ;; <-- specify instead of debug_server with correct serial line
;;debug_server = /usr/local/bin/dw-server.py  
                   -p 3333 ;; <-- specify instead of debug_port
```

If the `debug_port` line is commented out and the `debug_server` line is uncommented, a debug server is used instead of communicating directly over the serial line. This server discovers the serial line the hardware debugger is connected to and then provides a serial-to-TCP/IP bridge. The advantage is that one does not have to determine which serial port the hardware debugger is connected to. This server can be found in the [`dw-server`](../dw-server) folder and needs to be copied to /usr/local/bin or any other location where it can be started.

When creating new projects, you can take this project folder as a blueprint and modify and extend `platformio.ini` according to your needs. You can find an extensive description of how to do that in the [PlatformIO documentation](https://docs.platformio.org/en/stable/projectconf/index.html). A very [readable introduction to debugging](https://piolabs.com/blog/insights/debugging-introduction.html) using PlatformIO has been written by [Valerii Koval](https://www.linkedin.com/in/valeros/). It explains the general ideas and all the many ways how to interact with the PlatformIO GUI. [Part II](https://piolabs.com/blog/insights/debugging-embedded.html) of this introduction covers embedded debugging.

<a name="section8"></a>

## 8. A real hardware debugger

The hardware part of our hardware debugger is very limited so far. You can, of course, use 6 jumper wires to connect dw-link to your target as described in [Section 4.2](#section42). However, if you want to use this tool more than once, then there should be at least something like an ISP cable connection. Otherwise, you might scratch your head which cable goes where every time you start a debugging session.

### 8.1 The basic solution

For most of the wires, we use the same pins on the debugger and the target. So, it makes sense to think about something similar to an ISP cable people use when employing an Arduino UNO as an ISP programmer. Such cables can be easily constructed with some Dupont wires and a bit of heat shrink tube as, for example, demonstrated in [this instructable](https://www.instructables.com/Arduino-ICSP-Programming-Cable/). In contrast to such a programmer cable, it makes sense to also break out the Vcc wire. And you do not want to integrate a capacitor between RESET and GND in such a cable in contrast to what is described in the instructable!

![isp-cable](pics/isp-cable.jpg)

As argued in [my blog post on being cheap](https://hinterm-ziel.de/index.php/2022/01/13/a-debugwire-hardware-debugger-for-less-than-10-e/), with such an ISP cable, we have sort of constructed a hardware debugger for less than 10 €, which can be considered semi-durable. Just add the optional system LED with an attached resistor and a capacitor between RESET and GND.

![el cheapo debugger](pics/debugger-built.jpg)



The relevant pins are therefore as defined in the following table. 

<a name="simplemap"></a>

| Arduino pin | ISP pin | Function                                                 |
| ----------- | ------- | -------------------------------------------------------- |
| D13         | 3       | SCK                                                      |
| D12         | 1       | MISO                                                     |
| D11         | 4       | MOSI                                                     |
| D9          | 2       | VTG                                                      |
| D8          | 5       | RESET                                                    |
| GND         | 6       | GND                                                      |
| D7          |         | System LED+                                              |
| D6          |         | System LED- (if using a LED with a resistor soldered on) |

### 8.2 A simple shield

Taking it one step further, one can take a prototype shield for an UNO, put an ISP socket on it, and connect the socket to the respective shield pins. You probably should also plan to have jumper pins in order to be able to disconnect the target power supply line from the Arduino pin that delivers the supply voltage. And finally, you probably also want to place the system LED on the board. So, it could look like as in the following Fritzing sketch. 

![dw-probe-fritzing](pics/dw-probe-2.0.png)

In reality, that might look like as in the following picture.

![dw-probe-pcb-V2.0](pics/dw-probe-pcb-V2.0.jpg)

### 8.3 Adapter with level-shifters and switchable power supply

The basic adapter is quite limited. It can only supply 20 mA to the target board, it cannot interact with 3.3 V systems, and it has a high load on the SCK line (because the UNO LED is driven by this pin) when the ISP mode is disabled. Thus, it would be great to have a board with the following features: 

* switchable target power supply (supporting power-cycling by the hardware debugger) offering 5-volt and 3.3-volt supply up to 300 mA, 
* a bidirectional (conditional) level-shifter on the debugWIRE/RESET line,
* an optional pull-up resistor of 10 kΩ on this line,
* unidirectional (conditional) level-shifters on the ISP lines, and
* high-impedance status for the two output signals MOSI and SCK when ISP is inactive.

Such a board does not need to be very complex. The electronic design is minimalistic. It uses just three MOS-FETs, one LED, one voltage regulator, and some passive components. We need to (conditionally) level-shift the RESET line in a bidirectional manner and the SPI lines unidirectionally.  One needs to shift the MISO line from 3.3-5 V up to 5 V, and the MOSI and SCK lines from 5 V down to 3.3-5 V. For the former case, no level shifting is done at all, relying on the fact that the input pins of the hardware debugger recognize a logical one already at  3.0 V. For the RESET line, which is open drain, we rely on the same fact. This means that this hardware debugger cannot deal with systems that use a supply voltage of less than 3 V, though.

For downshifting, we use the output pins of the hardware debugger in an open drain configuration and have pull-up resistors connected to the target supply voltage. These have to be particularly strong because some possible target boards, e.g., the Arduino UNO, use the SCK  line for driving an LED with a series resistor of 1kΩ. For this reason,  we use 680Ω pull-up resistors that guarantee that the signal level is above 3V on the SCK line, when we supply the board with 5V. These pull-ups will be disabled when no ISP programming is active, giving the target system full control of the two lines. The schematic looks as follows (thanks to **[gwideman](https://github.com/gwideman)** for the reworked schematic).

![KiCad-Schematic](../pcb/schematic.png)

The pin mapping is a bit different from the basic design described above. The change from the basic mapping is controlled by pin D5, which is tied to GND in order to signal that the more complex pin mapping is used. The additional pins are all in italics. The ones not used on the board are struck out.

| Arduino pin | ISP pin  | Function                                                     |
| ----------- | -------- | ------------------------------------------------------------ |
| <s>D13</s>  | <s>3</s> | <s>SCK</s>                                                   |
| D12         | 1        | MISO                                                         |
| D11         | 4        | MOSI (open drain)                                            |
| *D10*       | *3*      | *SCK (open drain)*                                           |
| <s>D9</s>   | <s>2</s> | <s>VTG</s>                                                   |
| D8          | 5        | RESET                                                        |
| GND         | 6        | GND                                                          |
| D7          |          | System LED+                                                  |
| <s>D6</s>   |          | <s>System LED- (if using an LED with a resistor soldered on)</s> |
| *D5*        |          | *Sense pin: Connected to GND when a board with a level shifter is used* |
| *D4*        |          | *ISP pull-up enable (open drain, active low)*                |
| *D3*        |          | *Input: automatic debugWire switching disable (open drain, active low)* |
| *D2*        |          | *Power enable (open drain, active low)*                      |
|             | *2*      | *VTG:* *Power from Q1 controlled by power jumper*            |



And here is the early breadboard prototype, which worked beautifully. It contains a bug, though. Can you spot it?

![V2-prototype](pics/dw-probe-V3.jpg)

I have turned the modified prototype into an Arduino Shield, which you can buy [at Tindie](https://www.tindie.com/products/31798/) as a kit. With that, the hardware setup is straightforward. Just plug in an ISP cable, and you can start debugging.

![dw-link probe](pics/dw-probe.jpg)

<a name="jumper"></a>

Before you start, you have to configure three jumpers. Then you are all set.

Label | Left | Middle | Right 
--- | --- | --- | --- 
**Supply** | **5 V** are supplied to the target | **extern**: target needs its own supply and power cycling has to be done manually | **3.3 V** are supplied to the target 
**Pullup** | There is **no** pull-up resistor connected to RESET | &nbsp; | A **10 kΩ** pull-up resistor is connected to the RESET line of the target 
**Auto_DW** | Automatic transition from debugWIRE mode to normal mode when leaving the debugger is **off**. |  | Automatic transitions from debugWIRE mode back to normal mode when exiting the debuggere is **on**. This is the default and *recommended* mode. 

<a name="section9"></a>

## 9. Problems and shortcomings

dw-link is still in ***beta*** state. The most obvious errors have been fixed, but there are most probably others. If something does not go according to plan, please try to isolate the reason for the erroneous behavior, i.e., identify a sequence of operations to replicate the error. The most serious errors are *fatal errors*, which stop the debugger from working. With the command `monitor lasterror` you can get information on what the cause is (check the [error table at the end](#fatalerror)).

One perfect way to document a debugger error is to switch on logging and command tracing in the debugger:

```
set trace-commands on
set remote debug 1
set logging on
...
set logging off
```

This can either be done during the interactive debug session or in the `.gdbinit` file in the home directory. The latter is preferable if the problem happens before the session is started using `target remote ...`.

I have prepared an *[issue form](issue_form.md)* for you, where I ask for all the information necessary to replicate the error. 

Apart from bugs, there are, of course, shortcomings that one cannot avoid. I will present some of them in the next subsections.

### 9.1 Flash memory wear

Setting and removing *breakpoints* is one of the main functionality of a debugger. Setting a breakpoint is mainly accomplished by changing an instruction in flash memory to the BREAK instruction. This, however, implies that one has to *reprogram flash memory*. Since flash memory wears out, one should try to minimize the number of flash memory reprogramming operations.

GDB does not pass *breakpoint set* and *breakpoint delete* commands from the user to the hardware debugger, but instead, it sends a list of *breakpoint set* commands before execution starts. After execution stops, it sends *breakpoint delete* commands for all breakpoints. In particular, when thinking about conditional breakpoints, it becomes clear that GDB may send a large number of *breakpoint set* and *breakpoint delete* commands for one breakpoint during one debug session. Although it is guaranteed that flash memory can be reprogrammed at least 10,000 times according to the data sheets, this number can easily be reached even in a few debug sessions, provided there are loops that are often executed and where a conditional breakpoint has been inserted. Fortunately, the situation is not as bad as it looks since there are many ways of getting around the need of reprogramming flash memory.

First, dw-link leaves the breakpoints in memory even when GDB requests to remove them. The breakpoints in flash memory are updated only when GDB requests to continue execution. Well, the same happens before loading program code, detaching, exiting, etc. Assuming that the user does not change breakpoints too often, this will significantly reduce flash reprogramming.  

Second, if there are many breakpoints on the same flash page, then the page is reprogrammed only once instead of reprogramming it for each breakpoint individually.

Third, when one restarts from a location where a breakpoint has been set, GDB temporarily removes this breakpoint, single-steps to the next instruction, reinserts the breakpoint, and only then continues execution. This would lead to two reprogramming operations. However, dw-link does not update flash memory before single-stepping. Instead, if the instruction is a single-word instruction, it loads the original instruction into the MCU's instruction register and executes it there. 

For two-word instructions (i.e., LDS, STS, JUMP, and CALL), things are a bit more complicated. The Microchip documents state that one should refrain from inserting breakpoints at double-word instructions, implying that this would create problems. Indeed, RikusW noted in his [reverse engineering notes about debugWIRE](http://www.ruemohr.org/docs/debugwire.html):
>Seems that its not possible to execute a 32 bit instruction this way.
The Dragon reflash the page to remove the SW BP, SS and then reflash again with the SW BP!!! 

I noticed that this is still the case, i.e., MPLAB-X in connection with ATMEL-ICE still reprograms the page twice for hitting a breakpoint at a two-word instruction. The more sensible solution is to simulate the execution of these instructions, which is at least as fast and saves two reprogramming operations. And this is what dw-link does.

Fourth, each MCU contains one *hardware breakpoint register*, which stops the MCU when the value in the register equals the program counter. Dw-link uses this for the breakpoint introduced most recently. With this heuristic, temporary breakpoints (as the ones GDB generates for single-stepping) will always get priority and more permanent breakpoints set by the user will end up in flash. 

Fifth, when reprogramming of a flash page is requested, dw-link first checks whether the identical contents should be loaded, in which case it does nothing. Further, it checks whether it is possible to achieve the result by just turning some 1's into 0's. Only if these two things are not possible, the flash page is erased and reprogrammed. This helps in particular when reloading a file with the GDB `load` command after only a few things in the program have been changed.  

With all of that in mind, you do not have to worry too much about flash memory wear when debugging. As a general rule, you should not make massive changes to the breakpoints each time the MCU stops executing. Finally, Microchip recommends that chips that have been used for debugging using debugWIRE should not be shipped to customers. Well, I never ship chips to customers anyway.

<a name="paranoid"></a>For the really paranoid,  there is the option that permits only one breakpoint, i.e., the hardware breakpoint: `monitor breakpoint h`. In this case, one either can set one breakpoint or one can single-step, but not both. So, if you want to continue after a break by single-stepping, you first have to delete the breakpoint. By the way, with `monitor breakpoint s`, one switches back to normal mode, in which 25 (including one temporary) breakpoints are allowed.

In addition, the debugger command `monitor flashcount` returns the number of flash page reprogramming commands executed since the debugger was started. This also includes the flash reprogramming commands needed when loading code.

<a name="section92"></a>

### 9.2 Slow responses when loading or single-stepping

Sometimes, in particular, when using a clock speed below 1 MHz, responses from the MCU can be quite sluggish. This shows, e.g., when loading code or single-stepping. The reason is that a lot of communication over the RESET line is going on in these cases and the communication speed is set to the MCU clock frequency divided by 8, which is roughly 16000 bps in case of a 128 kHz MCU clock. If the CKDIV8 fuse is programmed, i.e., the MCU clock uses a prescaler of 8, then we are down to 16 kHz MCU clock and 2000 bps. The [Atmel AVR JTAGICE mkII manual ](https://onlinedocs.microchip.com/pr/GUID-73C92233-8EC5-497C-92C3-D52ED257761E-en-US-1/index.html) states under [known issues](https://onlinedocs.microchip.com/oxy/GUID-73C92233-8EC5-497C-92C3-D52ED257761E-en-US-2/GUID-A686427B-0B7C-465A-BCFF-F093FD6B7A8F.html):

>Setting the CLKDIV8 fuse can cause connection problems when using debugWIRE. For best results, leave this fuse un-programmed during debugging. 

"Leaving the fuse un-programmed" means that you probably have to change the fuse to be un-programmed using a fuse-programmer, because the fuse is programmed by default. In order to simplify life, I added the two commands `monitor ckdiv 8` and `monitor ckdiv 1` to the hardware debugger that allows you to change this fuse. `monitor ckdiv 8` programs the fuse, i.e., the clock is divided by 8, `monitor ckdiv 1` un-programs this fuse. Using the `monitor ckdiv` command without an argument reports the setting of this fuse. Note that after executing this command, the MCU is reset (and the register values shown by the GDB `register info` command are not valid anymore). 

With an optimal setting, i.e., 250 kbps for the debugWIRE line and 230400 bps for the host communication line, loading is done with 500-800 bytes/second. It should be 3-5 KiB/second when the identical file is loaded again (in which case only a comparison with the already loaded file is performed). For the default setting (115200bps to host, 125000bps for debugWIRE), it is probably half the speed.

### 9.3 Program execution is very slow when conditional breakpoints are present

If you use *conditional breakpoints*, the program is slowed down significantly.  The reason is that at such a breakpoint, the program has to be stopped, all registers have to be saved, the current values of the variables have to be inspected, and then the program needs to be started again, whereby registers have to be restored first. For all of these operations, debugWIRE communication takes place. This takes roughly 100 ms per stop, even for simple conditions and an MCU running at 8MHz. So, if you have a loop that iterates 1000 times before the condition is met, it may easily take 2 minutes (instead of a fraction of a second) before execution stops.

<a name="section94"></a>

### 9.4 Single-stepping and interrupt handling clash

In many debuggers, it is impossible to do single-stepping when timer interrupts are active since after each step the program ends up in the interrupt routine. This is not the case with avr-gdb and dw-link. Instead, time is frozen and interrupts cannot be raised while the debugger single-steps. Only when the `continue` command is used, interrupts are serviced and the timers are advanced. One can change this behavior by using the command `monitor singlestep u`. In this case, it can happen that control is transferred to the interrupt vector table while single-stepping.

### 9.5 Limited number of breakpoints

The hardware debugger supports only a limited number of breakpoints. Currently, 25 breakpoints (including one temporary breakpoint for single-stepping) are supported by default. You can reduce this to 1 by issuing the command `monitor breakpoint h` ([see above](#paranoid)). If you set more breakpoints than the maximum number, it will not be possible to start execution. Instead one will get the warning `Cannot insert breakpoint ... Command aborted`. You have to delete or disable some breakpoints before program execution can continue. However, you should not use that many breakpoints in any case. One to five breakpoints are usually enough. 

### 9.6 Power saving is not operational 

When you activate *sleep mode*, the power consumed by the MCU is supposed to go down significantly. If debugWIRE is active, then some timer/counters will never be stopped and for this reason the power reduction is not as high as in normal state.

<a name="section97"></a>

### 9.7 MCU operations interfering with debugWIRE

There are a few situations where MCU operations interfere with the debugWIRE system. The above-mentioned list of [known issues](https://onlinedocs.microchip.com/oxy/GUID-73C92233-8EC5-497C-92C3-D52ED257761E-en-US-2/GUID-A686427B-0B7C-465A-BCFF-F093FD6B7A8F.html) contains the following:

* The PRSPI bit in the power-saving register should not be set
* Breakpoints should not be set at the last address of flash memory
* Do not single step over a SLEEP instruction
* Do not insert breakpoints immediately after an LPM instruction and do not single-step LPM code

Setting the `PRSPI` bit can disable the clock for the debugWIRE line and should be avoided for this reason. The latter three situations may lead to problems stopping at the breakpoint or executing the instructions, respectively.

The list of known issues mentions also the following five potential problems:

* Be aware that the On-chip Debug system is disabled when any lock bits are set
* BOD and WDT resets lead to loss of connection 
* The OSCCAL and CLKPR registers should not be changed during a debug session
* The voltage should not be changed during a debug session
* The CKDIV8 fuse should not be in the programmed state when running off a 128 kHz clock source

The first issue is mitigated by dw-link erasing the chip when lock bits are set. This is not an industrial-strength solution, but it makes life easier because all UNO boards have their lock bits set initially. So, instead of explaining that the bits have to be cleared, it is just done automatically. 

Concerning resets, I did not experience fundamental problems. The only issue was that the target would not stop at the hardware breakpoint after a reset, since the reset will clear this hardware breakpoint. So, if you want to be sure to stop after a reset, use the command `monitor breakpoint S` (capital S), which forces all breakpoints to be software breakpoints. If you use the watchdog timer to issue a software reset, make sure that right after restarting the MCU, the watchdog timer will be disabled, as mentioned in the [AVR-LibC FAQ](https://avrdudes.github.io/avr-libc/avr-libc-user-manual-2.2.0/FAQ.html#faq_softreset). Otherwise, you run into a WDT-restart loop.

Changing the clock frequency is also not a problem since, at each stop, the debugger re-synchronizes with the target. Further, changing the supply voltage can be done if you have level-shifting hardware in place. It is still not something that is recommended. 

Finally, debugging at very low clock frequencies (32 kHz/8 = 4 kHz) is not impossible, but communication is extremely slow. I have implemented that mainly because of curiosity and to help you recover and switch to a higher frequency.

### 9.8 BREAK instructions in your program

It is possible to put the BREAK instruction, which is used to implement breakpoints, in ones program by using the inline assembly statement `asm("break")`. This makes no sense since, without the debugger, the MCU will treat this instruction as a NOP. 

Such a BREAK instruction may also be in the program because a previous debugging session was not terminated in a clean way. If such a BREAK is detected, one should issue the `load` command again.

When running under the debugger, the program will be stopped in the same way as if there is a software breakpoint set by the user. However, one cannot continue execution from this point with the `step`, `next`, or `continue` command. Instead, the debugger gets an "illegal instruction" signal. So, one either needs to reload the program code, set the PC to a different value, or restart the debugging session.

<a name="section99"></a>

### 9.9 Some MCUs have stuck-at-one bits in the program counter

Some debugWIRE MCUs appear to have program counters in which some unused bits are stuck at one. ATmega48s and ATmega88s (without the A-suffix), which I have sitting on my bench,  have their PC bits 11 and 12 or only PC bit 12 always stuck at one. In other words, the PC has at least the value 0x1800 or 0x1000, respectively (note that the AVR program counter addresses words, not bytes!). The hardware debugger can deal with it, but GDB gets confused when trying to perform a stack backtrace. It gets also confused when trying to step over a function call or tries to finalize a function call. For these reasons, debugging these MCUs does not make much sense and dw-link rejects these MCUs with an error message when one tries to connect to one of those (see also [this blog entry](https://hinterm-ziel.de/index.php/2021/12/29/surprise-surprise/)). 

The only reasonable way to deal with this problem is to use a different MCU, one with an A, PA, or PB suffix. If you really need to debug this particular MCU and are aware of the problems and limitations, you can recompile the sketch with the compile-time constant `STUCKAT1PC` set to 1.

### 9.10 The start of the debugger takes two seconds

The reason is that when the host establishes a connection to the debugger, the debugger is reset and the bootloader waits two seconds. You can avoid that by disabling the auto-reset feature putting a capacitor of 10 µF or more between RESET and GND. The dw-link probe shield also does that for you.

<a name="section911"></a>

### 9.11 Code optimization reorganizes code and makes it impossible to stop at a particular source line or to inspect or change values of local variables

The standard setting of the Arduino IDE and CLI is to optimize for space, which is accomplished using the compiler option **-Os**. In this case, it may be difficult to stop at some source lines, and single-stepping may give strange results. When you choose `Debug` as the value for the option `Debug Compile Flags`, then the compiler optimizes the code in a debugger-friendly way (using the compiler option **-Og**). And this is actually what the GDB people recommend. The same effect can be accomplished by selecting the entry `Optimize for Debugging` in the `Sketch` menu in the Arduino IDE 2.

I have encountered situations [when it was impossible to get the right information about C++ objects](https://arduino-craft-corner.de/index.php/2021/12/15/link-time-optimization-and-debugging-of-object-oriented-programs-on-avr-mcus/). This can be avoided by disabling *link-time optimization* (LTO). Choose `Debug (no LTO)` in this case. Finally, if there are still discrepancies between what you expect and what the debugger delivers, you can try `Debug (no LTO, no comp. optim.)`, which effectively switches off any optimization (corresponding to **-O0 -fno-lto**).

In PlatformIO, you can set the options for generating the debug binary in the `platform.ini` file.

<a name="trouble"></a>

## 10. Troubleshooting

### 10.1 Problems while preparing the setup

**Problem: It is impossible to upload the dw-link firmware to the UNO board**

Maybe, the dw-link probe shield or the auto-reset disabling capacitor is still plugged into the UNO board? Remove, and try gain.

### 10.2 Connection problems

**Problem: It is impossible to connect to the hardware debugger via the serial line**

Sometimes, one can no longer connect to the hardware debugger. When calling `dw-server`, it reports that "no dw-link adapter is found." This state persists even after the hardware debugger is reset or the USB cable is disconnected and reconnected.

If this happens when the hardware debugger powers the target, this is a sign that the capacitive load of the target may be too high. So, one cure is here to power the target externally and do the power-cycling manually. In this situation, it is also a good idea to disable the automatic return to the normal state when leaving the debugger. Set the `AutoDW` jumper to the `off` position or connect D3 to GND. 

Remember to disable debugWIRE mode in the end by issuing the command `monitor dwire -` before leaving the debugger.

**Problem: When connecting to the target using the *target remote* command, it takes a long time and then you get the message *Remote replied unexpectedly to 'vMustReplyEmpty': timeout***

The serial connection to the hardware debugger could not be established. The most likely reason for that is that there is a mismatch of the bit rates. The Arduino uses by default 115200 baud, but you can recompile dw-link with a changed value of `HOSTBPS`, e.g., using 230400. If GDB is told something differently, either as the argument to the `-b` option when starting avr-gdb or as an argument to the GDB command `set serial baud ...`, you should change that. If you did not specify the bitrate at all, GDB uses its default speed of 9600, which will not work!

My experience is that 230400 bps works only with UNO boards. The Arduino Nano cannot communicate at that speed.

A further (unlikely) reason for a failure in connecting to the host might be that a different communication format was chosen (parity, two stop bits, ...). 

**Problem: When connecting to the target using the *target remote* command, you do not get an error message, but the system LED is still off and there is apparently no connection**

This happens when you select the `AutoDW-off` jumper option on the dw-link board. In this case, you have to initiate the connection with the `monitor dwire +` command. 

When you are done with debugging, you should switch back to normal mode with `monitor dwire -` just before leaving the debugger. Usually, you want the jumper in the `on` position, except when you power the target from an external source.

**Problem: In response to the `monitor dwire +` command or when initally connecting, you get the error message *Cannot connect: ...***

Depending on the concrete error message, the problem fix varies.

- *Cannot connect: Could not communicate by ISP; check wiring*: The debugger cannot establish an ISP connection. Check wiring. Maybe you forgot to power the target board? I did that more than once. If this is not the reason, disconnect everything and put it together again. This helps sometimes. Finally, this error could be caused by bricking your MCU having too much capacitive load or a pull-up resistor that is too weak on the RESET line.  
- *Cannot connect: Could not activate debugWIRE*: An ISP connection was established, but it was not possible to activate debugWIRE. Most probably the MCU is now in a limbo state and can only be resurrected by a HV programmer. The reason is most probably too much capacitive load on the RESET line or a weak pullup resistor on this line.
- *Cannot connect: Unsupported MCU*: This MCU is not supported by dw-link. It most probably has no debugWIRE on board. 
- *Cannot connect: Lock bits could not be cleared:* This should not happen at all because it is always possible to clear the lock bits by erasing the entire chip.
- *Cannot connect: PC with stuck-at-one bits*: dw-link tried to connect to an MCU with stuck-at-one bits in the program counter (see [Section 9.9](#section99)). These MCUs cannot be debugged with GDB. 
- *Cannot connect: Reset line has a capacitive load*: The message says it all.
- *MCU type does not match*: The chip connected to the hardware debugger is different from what you announced to the IDE.
- *Cannot connect for unknown reasons:* This error message should not be shown at all. If it does, please tell me!

**Problem: You receive the message *Protocol error with Rcmd*** 

This is a generic GDB error message that indicates that the last `monitor` command you typed could not be successfully executed. Usually, also a more specific error message is displayed, e.g., *debugWIRE could NOT be disabled*. These messages are suppressed in some GUIs, though. 

### 10.3 Problems while debugging

<a name="lost"></a>

**Problem: You get the message *Connection to target lost*, the program receives a `SIGHUP` signal when you try to start execution, and/or the system LED is off**

The target is not responsive any longer. Possible reasons for such a loss of connectivity could be that the RESET line of the target system does not satisfy the necessary electrical requirements (see [Section 3.4](#section34)). Other reasons might be that the program disturbed the communication by changing, e.g., the MCU clock frequency (see [Section 9.7](#section97)). Try to identify the reason, eliminate it, and then restart the debug session.  Most probably, there are still BREAK instructions in flash memory, so the `load` command should be used to reload the program.

**Problem: When stopping the program with Ctrl-C (or with the stop button), you get the message *Cannot remove breakpoints because program is no longer writable.***

The reason is most probably that the communication connection to the target system has been lost ([see above](#lost)).

**Problem: When trying to start execution with the `run` command, GDB stops with an internal error**

This happens with avr-gdb versions older than version 10.1. You can instead use `monitor reset` and `continue`. 

**Problem: The debugger does not start execution when you request *single-stepping* or *execution* and you get the warning *Cannot insert breakpoint ... Command aborted*** 

You use more than the allowed number of breakpoints, i.e., usually 25 (including one for a temporary breakpoint for single-stepping). If you have executed the `monitor breakpoint h` command, this number is reduced to 1. In this case, you can either set a breakpoint or you can single-step, but not both! In any case, you need to reduce the number of breakpoints before you can continue.

**Problem: When single stepping with `next` or `step` , you receive the message *Warning: Cannot insert breakpoint 0* and the program is stopped at a strange location**

The problem is similar to the one above: You used too many breakpoints and there is no temporary breakpoint left for GDB. The program is probably stopped somewhere you have not anticipated. You may be able to recover by deleting one or more breakpoints, setting a breakpoint close to where you wanted to step, and then using the `continue` command. If this is not possible, restart and use fewer breakpoints.

**Problem: The debugger does not start execution when you request *single-stepping* or *execution*, you get the message *illegal instruction*, and the program receives a `SIGILL` signal**

The debugger checks whether the first instruction it has to execute is a legal instruction according to the Microchip specification. Additionally, a BREAK instruction (which has not been inserted by the debugger) is considered as illegal since it would halt the MCU. Such a BREAK instruction might have been inserted as part of the program code or may be a leftover from a previous debugging session that has not been terminated in a clean way.

Check the instruction by using the command `x/i $pc`. If the BREAK instruction is a leftover from a previous debug session, you can remove it using the `load` command. Note that the program counter is set to 0x0000 and you should use the `monitor reset` command to reset your MCU before restarting.

If you simply want to continue, you can set the PC to another value, e.g., one that is higher by two or four. Do that by using the command `set $pc=...`. 

### 10.4 Strange behavior of the debugger

**Problem: After changing optimization options, the binary is still too large/very small**

You switched the optimization option from **-Og -fno-lto** back to normal and you recompiled, but your program still looks very big. The reason for that can be that the Arduino IDE/CLI does not always recompile the core, but reuses the compiled and linked archive. In the Arduino IDE 1, you can force a recompile of the core by exiting the IDE. In IDE 2, this is no longer an option. You need to look at where the files are compiled are stored and delete them manually.

**Problem: The debugger responses are very sluggish**   

One reason for that could be that the target is run with a clock less than 1 MHz, e.g. at 128 kHz. Since the debugWIRE communication speed is MCU usually clock/8, the debugWIRE communication speed could be 16kbps. If the CKDIV8 fuse is programmed, it could even be only 2kbps. Unprogram CKDIV8 and if possible choose a higher clock frequency  (see [Section 9.2](#section92)). 

**Problem: While single-stepping, time seems to be frozen, i.e., the timers do not advance and no timer interrupt is raised**

This is a feature, not a bug.  It allows you to single-step through the code without being distracted by interrupts that transfer the control to the interrupt service routine. Time passes and interrupts are raised only when you use the `continue` command (or when the `next` command skips over a function call). You can change this behavior by using the command `monitor singlestep u`, which enables the timers and interrupts while single-stepping. In this case, however, it may happen that during single-stepping control is transferred into an interrupt routine.

**Problem: PWM (analogWrite) does not seem to work when the program is stopped**

The reason is that all timers are usually stopped when the program is in a stopped state. However, you can change this behavior using the GDB command `monitor runtimers +`. In this case, the timers are run even when the program is stopped, which means that PWM (aka `analogWrite`) is also still active.

**Problem: When single stepping with `next` or `step` , the program ends up at the start of flash memory, e.g., 0x0030**

This should only happen when you have used the command `monitor singlestep u` before, which enables interrupts while single-stepping. In this case, an interrupt might have been raised which has transferred control to the interrupt vector table at the beginning of flash memory. If you want to continue debugging, set a breakpoint at the line you planned to stop with the single-step command and use the `continue` command. If you want to avoid this behavior in the future, issue the debugger command `monitor singlestep s`. 

**Problem: After requesting to stop at a function, the debugger displays a completely different file, where the execution will stop**

This is a GDB problem. It can happen when a function call is inlined at the beginning of the function one intends to stop at. While the place where execution will stop looks crazy (e.g., HardwareSerial.h at line 121), the execution stops indeed at the beginning of the specified function (in this case at the beginning of setup).

**Problem: The debugger does not stop at the line a breakpoint was set**

Not all source lines generate machine code so that it is sometimes impossible to stop at a given line. The debugger will then try to stop at the next possible line. This effect can get worse with different compiler optimization levels. For debugging, **-Og** is the recommended optimization option, which applies optimizations in a debug-friendly way. This is also the default for PlatformIO. In the Arduino IDE, you have to select the `Debug` option. You can also disable all possible optimizations (choose `Debug (no comp. optim.)` in the Arduino IDE).

**Problem: The debugger does things that appear to be strange**

The debugger starts execution, but it never stops at a breakpoint it should stop, single-stepping does not lead to the expected results, etc. I have seen three possible reasons for that (apart from a programming error that you are hunting).

Often, I had forgotten to load the binary code into flash. Remember to use the `load` command ***every time*** after you have started a debugging session. Otherwise it may be the case that the MCU flash memory contains old code! Note that after the `load` command the program counter is set to zero. However, the MCU and its registers have not been reset. You should probably force a hardware reset by using the command `monitor reset`. Alternatively, when you initiated your session with `target extended-remote ...`, you can use the `run` command that resets the MCU and starts at address zero. 

Second, you may have specified a board/MCU different from your actual target. This happens quite easily with PlatformIO when you work with different targets. In this case, some things appear to work, but others do not work at all. 

Another possible reason for strange behavior is the chosen compiler optimization level. If you have not chosen **-Og** (or **-O0**), then single-stepping may not work as expected and/or you may not be able to assign values to local variables. If objects are not printed the right way, then you may consider disabling LTO (by using the compiler option **-fno-lto**). Have a look into the [Section about compiler optimization flags](#section911).

So, before blaming the debugger, check for the three possible causes.

**Problem: You have set the value of a local variable using the `set var <var>=<value>` command, but the value is still unchanged when you inspect the variable using the `print` command**

This appears to happen even when the optimization level is set to **-Og**, but not when you use **-O0**. So, if it is important for you to change the value of local variables from the debugger, you should use the latter optimization level (see the preceding problem).



### 10.5 Problems with with GUI/IDE

**Problem: When starting the debug session in PlatformIO, you get the message *pioinit:XX: Error in sourced command file***

Something in the `platformio.ini` file is not quite right. Sometimes an additional line of information is given that identifies the problem. If you see also see the message `"monitor" command not supported by this target` then the dw-link adapter could not be found.

One other common problem is that the debug environment is not the first environment or the default environment. In this case, the wrong environment is used to configure the debug session and probably some environment variables are not set at all or set to the wrong values. So, you need to edit the `platformio.ini` file accordingly.



### 10.6 Problems after debugging

**Problem: After debugging, the chip is unresponsive, i.e., does not respond anymore to ISP programming or bootloader upload**

There are many possible causes:

* The DWEN fuse is still programmed, i.e., the MCU is still in debugWIRE mode. In this case, it may help to enter and leave the debugger again, provided that there are not any [problems with the RESET line](#worstcase). It may also be helpful to issue the command `monitor dwire -`. 
* Another fuse has been programmed by accident. In particular, there are the `monitor` commands that change the clock source. If an external clock or an XTAL has been chosen, then you can recover the chip only by providing such an external clock or XTAL and then use either ISP programming or connect again to dw-link. 
* As mentioned in Section 3.4, it apparently happens that the MCU is stuck halfway when transitioning to debugWIRE state. Then only HV programming can resurrect the chip.

If nothing helps, then [high-voltage programming](#worstcase) might still be a last resort.



### 10.7 Internal and fatal debugger errors

<a name="fatalerror"></a>

**Problem: The system LED blinks furiously and/or the program receives an `ABORT` signal when trying to start execution**

In this case some serious internal error had happened. You have to stop the current debug session and restart. 

The reason for such an error could be that the connection to the target could not be established or that there was an internal debugger error. It may be that the corresponding error message has already been displayed. You can find out what kind of error happened by typing the following command:

```
monitor lasterror
```

If the error number is less than 100, then it is a connection error. Errors above 100 are serious internal debugger errors (see below).

If you have encountered an internal debugger error, then please try to reproduce the problem and tell me how it happened. Please try to distill a minimal example leading to the problem and fill out the [*issue form*](issue_form.md). By the way: `monitor dwire -` can still be executed, provided there is still a functioning connection to the target. So you should still be able to disable debugWIRE on the target MCU even if a fatal error has happened. 

After a reset or a power cycle of the hardware debugger, usually, everything works again. If not, take everything apart and put it together again.


Error #  | Meaning
--:|---
1 | Connection error: Could not communicate by ISP; check wiring 
2 | Connection error: Could not activate debugWIRE 
3 | Connection error: MCU type is not supported
4 | Connection error: Lock bits or BOOTRST could not be cleared 
5 | Connection error: MCU has PC with stuck-at-one bits 
6 | Connection error: RESET line has a capacitive load 
7 | Connection error: Target not powered or RESET shortened to GND
8 | MCU type does not match 
9 | Unknown connection error 
101 | No free slot in breakpoint table
102 | Packet length too large
103 | Wrong memory type
104 | Packet length is negative
105 | Reset operation failed
106 | Memory address in flash read operation does not point to page start
107 | Could not complete flash read operation
108 | Could not complete RAM read operation
109 | Memory address in flash write operation does not point to page start
110 | Could not complete flash page erase operation
111 | Could not load data into the flash buffer for writing
112 | Error when programming flash page from buffer
113 | Assignment of hardware breakpoint is inconsistent
114 | BREAK inserted by debugger at a point where a step or execute operation is required
115 | Trying to read flash word at an uneven address
116 | Error when single-stepping
117 | A relevant breakpoint has disappeared
118 | Input buffer overflow
119 | Wrong fuse 
120 | Breakpoint update while flash programming is active 
121 | Timeout while reading from debugWIRE line 
122 | Timeout while reading general register 
123 | Timeout while reading IO register 
124 | Could not reenable RWW 
125 | Failure while reading from EEPROM 
126 | Bad interrupt 



## Acknowledgements

First, I would like to thank everyone who made the work described here possible by making their work public. Most I mentioned already in [Section 1.2](). In addition, I would like to thank everybody who contributed to dw-link.

The cover picture was designed based on vector graphics by [captainvector at 123RF](https://de.123rf.com/profile_captainvector).



## Revision history

**V 1.1** 

Initial version

**V 1.2**

- Changed pin mapping. The default is now to use ISP pins on the debugger so that a simple ISP cable with broken out RESET line is sufficient. System LED is pin D7, GND for the system LED is provided at pin D6. In order to use the pin mapping for shields/adapters, one has to tie SNSGND to ground, whereby the pin number of SNSGND depends on the Arduino board dw-link is compiled for (see mapping described in [Section 8.3.3](#section833)).
- Added wording to recommend optimization level -O0 instead of -Og, because otherwise assignments to local variables will not work. Single-stepping works now with -Og after dw-link hides all inserted BREAK instructions. 

**V 1.3**

- Removed Arduino Mega boards from the set of boards that can be used as hardware debuggers

**V 1.4**

- New error messages
- System LED with fewer modes
- Some screen shots added to PlatformIO description

**V 1.5**

- New error message (126)
- default DW speed is now 250 kbps

**V 1.6**

- New example: Debugging Uno board as target

**V 1.7**

- Changes in 8.7 
- Section 9, Problem 'vMustReplyEmpty': timeout - explanation of what problems I encountered
- Section 5.1-5.3 have been reworked, in particular concerning ATTinyCore 2.0.0 and the new Python script for extending the boards.txt files.

**V 1.8**

- New help command for monitor commands in 5.7

**V 1.9**

- Additional trouble shooting help when lockouts are set

**V 1.10**

- Pointed out in Section 4.2 that when debugging an Uno the first time you try to debug it, you need to erase the chip in order to clear the lock bits.
- Added similar wording under trouble shooting

**V 1.11**

* fixed some small inconsistencies

**V 2.0**

- Removed „lock bit“ error
- Added explanation that lock bits are automatically removed by erasing the entire chip
- Added extra part how to restore UNO functionality 
- Restructured Introduction
- Removed instructions how to modify board and platform files. Now the board definition files are downloaded from my fork.
- Added section 8.11
- More explanation how to start a debugging session using the Arduino IDE
- Reorganized and simplified as much as possible
- Corrected wrong placement in the table about the connections between UNO and ATtiny85
- new monitor command: lasterror
- deleted monitor commands: eraseflash, serial
- added comment about dark system LED
- changed Section 7 in order to describe the V2.0 design
- have thrown out ATtiny13 since it behaves strangely
- added that disabling debugWIRE is now done automatically 
- added dw-server.py
- added description of Gede
- added description of new hardware version
- added that dw-link is now also an ISP programmer
- simplified recovery for UNO

**V 3.0**

* Redesign of monitor commands; most of them now take an argument
* Disabling automatic mode switching (Section 2)
* Lowest frequency is now 4kHz (Section 8.7)
* Number of breakpoints reduced from 33 to 25 because of stability problems (when debugging was on)
* New dw-link probe
* Debugging UNO with an active serial connection to the host
* Added problem that stopping at a function might display the location of the inlined function

**V 4.0**

* Integration of Arduino IDE 2
* New fatal error: Wrong MCU type (caused by monitor mcu command)
* Renamed fatal error 3
* The *boards manager URLs* have changed: a suffix `_plus_Debug` has been added to the core name.
* Simplified platoformio.ini
* Corrected statement about the meaning of BREAK when the debugger is not active
* `monitor mcu` command listed
* Description of how to use the AutoDW jumper added 
* Added a section on how to restore an UNO
* Added a problem section on when the hardware debugger becomes unresponsive
* Added notes that you cannot debug the UNO, but need to select ATmega328
* Added notes about the target board and potentially using external powering
* Edited the problem description for locked up hardware debugger/serial line
* New fatal error: capacitive load on the reset line
* Introduced subsections in the problem section
* Added *monitor runtimers*  command in table
* Added paragraph in problem section how to use *monitor runtimers*
* New section 3.1 about choosing between external powering and powering using the hardware debugger. 
* Changed some section titles in order to make compatible with the TOC generator
* Removed `monitor oscillator` . This is actually something quite dangerous, because it can brick the chip.
* Added new 'S' switch for monitor breakpoint
* Added note that the debugger might recognize capacitive load on the RESET line.
