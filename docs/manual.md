# dw-gdbserver

This is an Arduino sketch that turns your Arduino Uno or Nano into a hardware debugger that uses the [debugWIRE](https://en.wikipedia.org/wiki/DebugWIRE) protocol, which enables you to debug most of the ATtinys and some small ATmegas. It acts as a [gdbserver](https://en.wikipedia.org/wiki/Gdbserver), so you can use it to debug your programs running on the target hardware (e.g. an ATtiny) using avr-gdb or any IDE that integrates avr-gdb, e.g., [PlatformIO](https://platformio.org/) or [Eclipse](https://www.eclipse.org/) on the development machine.

Why is this good news? Well, the (current version of the) Arduino IDE does not support debugging at all. Even the new version will not provide any debugging tools for the small AVR MCUs. 
So, the only way to debug programs is to use additional print statements and recompile, which is very cumbersome.

You could buy expensive hardware-debuggers such as the Atmel-ICE, but then you have to use the development IDE [Microchip Studio](https://www.microchip.com/en-us/development-tools-tools-and-software/microchip-studio-for-avr-and-sam-devices), which only runs under Windows (and is not easy to work with). So, what are the alternatives if you want to develop programs for AVR ATtinys and small ATmegas and you are in need of a debugging tool?

Meanwhile, there exist a software simulator called [SIMAVR](https://github.com/buserror/simavr) and a [remote stub](https://sourceware.org/gdb/onlinedocs/gdb/Remote-Stub.html) for some ATmegas, called [AVR8-STUB](https://github.com/jdolinay/avr_debug). Both are integrated into PlatformIO as debuggers. However, using them is not the same as debugging on the hardware where your firmware should finally run. There exists a gdbserver implementation, called [dwire-debug](https://github.com/dcwbrown/dwire-debug), for host systems that uses just the serial interface of the host to talk with a target using the debugWIRE interface. However, only one breakpoint (the hardware breakpoint on the target system) is supported and the particular way of turning a serial interface into a one-wire interface does not seem to work under macOS, as far as I can tell (after some time of experimentation). Finally, there exists an Arduino Uno based hardware debugger called [DebugWireDebuggerProgrammer](https://github.com/wholder/DebugWireDebuggerProgrammer). Unfortunately, it does not provide a gdbserver interface.

So, I took all of the above ideas and put them together in order to come up with a cheap hardware debugger for the older ATtinys and some smaller ATmegas (such as the popular ATmega328) with a gdbserver interface. 

## The debugWIRE Interface

The basic idea of *debugWIRE* is that one uses the RESET line as a communication line between the target system (the system we want to debug) and the development machine, which runs a debug program such as GDB. This idea is very clever because it does not waste any of the other pins for debugging purposes (as does e.g. the [JTAG interface](https://en.wikipedia.org/wiki/JTAG)). However, using the RESET line as a communication channel means, of course, that one cannot use the RESET line to reset the MCU anymore. Furthermore, one cannot any longer use [ISP programming](https://en.wikipedia.org/wiki/In-system_programming) to upload new firmware to the MCU. 

Do not get nervous when your MCU does not react any longer as you expect it, but try to understand, in which state the MCU is. With respect to the debugWIRE protocol there are three states your MCU could be in:

1. The **normal** state in which the DWEN (debugWIRE enable) [fuse](https://microchipdeveloper.com/8avr:avrfuses) is disabled. In this state, you can use ISP programming to change fuses and to upload programs. By enabling the DWEN fuse, one reaches the **transitionary** state.
2. The **transitionary** state, in which the DWEN fuse is enabled. In this state, you could use ISP programming to disable the DWEN fuse again, in order to reach the **normal state**. By *power-cycling* (switching the target system off and on again), one reaches the **DebugWIRE** state.
3. The **debugWIRE** state is the state in which you can use the debugger to control the target system. If you want to return to the **normal** state, a particular debugWIRE command leads to a transition to the **transitionary** state, from which one can reach the normal state.

The hardware debugger will take care of bringing you from state 1 to state 3 in order start debugging by either power-cycling itself or asking you to do it. This is accomplished with the gdb command ```monitor init```. The transition from state 3 to state 1 can be achieved by the command ```monitor stop```.


## Hardware Requirements

### The debugger
As mentioned above, as a base for the debugger you can use any ATmega328 based board (or anything compatible with more memory). You should run it with 16MHz, otherwise you may not be able to debug targets that use 16MHz. The basic solution is just to use this board and connect the cables (as it is shown in the example sketch for an ATtiny85 as the target system).

If you want to use the debugger more than once, it may payoff to configure an ISP cable with the RESET line broken out, similar to what has been described by dmjlambert in [his instructables](https://www.instructables.com/Arduino-ICSP-Programming-Cable/). Going one step further, one could break out the Vcc line on ICSP pin 2 as well and 

* connect it to Arduino pin 9 in order to be able to power-cycle the target system automatically when necessary; note that in this case he target system should draw no more than 20 mA;
* leave it open when the target system has an external power source;
* or connect it to Vcc of the Arduino board.

In the latter two cases, you need to power-cycle the target system manually.

How much more luxuriously can it get? One could have level shifters so that also 3.3 volt systems can be debugged. In addition, one could provide more power to source the target system. Well, this could all be done on a small adapter PCB for an Arduino Nano, which is actually in the planning stage.


### MCUs with debugWIRE interface

In general, most ATtiny MCUs except for the most recent ones and the ATmegaXX8 series have the debugWIRE interface. Specifically, the following MCUs can be debugged using this interface:

* ATtiny13
* ATtiny2313, ATTiny4313
* ATtiny24, ATtiny44, ATtiny84
* ATtiny441, ATtiny841
* ATtiny25, ATtiny45, ATtiny85
* ATtiny261, ATtiny461, ATtiny861
* ATtiny87, ATtiny167
* ATtiny1634
* ATmega48A, ATmega48PA, ATmega88A, ATmega88PA, ATmega168A, ATmega168PA, ATmega328, ATmega328P
* ATmega8U2, ATmega16U2, ATmega32U2

If you happen to know MCUs not covered here, drop me a note. I will then make it possible to also debug them.


### Requirements concerning the RESET line of the target system 

Since the RESET line of the target system is used as an asynchronous serial communication line, one has to make sure that there is no capacitive load on the line when it is used in debugWIRE mode. On an Arduino Uno and similar boards, there is unfortunately a capacitor between the RESET pin of the ATmega328 and the DTR pin of the serial chip. This is used by the Arduino IDE to issue a reset pulse in order to start the bootloader. One can disconnect the capacitor by cutting a solder bridge labeled *RESET EN* on the board (see picture), but then you cannot use the board any longer with the Arduino IDE. A recovery method may be to either put a bit of soldering  or solder two pins on the board and use a jumper.

![Solder bridge on Uno board](pics/solderbridge.jpg)

Solder bridge (picture copied from [https://sites.google.com/site/wayneholder/debugwire3](https://sites.google.com/site/wayneholder/debugwire3))

Further, one needs a 10 kΩ pullup resistor on the RESET line. According to reports of other people, 4.7 kΩ might also work. Higher values than 10 kΩ are not advisable, tough, because the signal quality might suffer. The Arduino Uno has already such a pullup, so one does not need another one. 

Other Arduino boards, such as the Nano, are a bit harder to modify, while a Pro Mini, for example, can be used without a problem, when the DTR line of the FTDI connector is not used. In general, I suggest to use debugWIRE on boards you have complete control over the hardware, e.g., the boards you have designed yourself.

## Installation and Example Sessions

I will describe two options of using the hardware debugger. The first one is the easiest one, which simply adds a ```platform.local.txt``` file to the Arduino configuration and requires you to download ```avr-gdb```. With that you can start to debug. The second option involves downloading the platformIO IDE and starting your first debug session with a GUI. However, before you can start to debug, you have to setup the hardware. I'll use an ATtiny85 on a breadboard as the target system.

### Setting up the hardware

![ATtiny84 on a breadboard](pics/debug-attiny85_Steckplatine.png)


As you can see, the Vcc pin of the ATtiny85 (which is pin 8) is connected to pin 9 of the Arduino, so that the Uno will be able to power-cycle the target chip. Furthermore, pin 8 of the Arduino is connected to the RESET pin of the ATtiny (pin 1). Note the presence of the pullup resistor of 10kΩ on the ATtiny RESET pin. The remaining connections between Arduino and ATtiny are MOSI, MISO and SCK, which you need for ISP programming. Finally, there is a LED connected to pin 3 of the ATtiny (which is PB4 or digital pin 4 in Arduino terminology). The pinout of the ATtiny85 is given in the next figure. 

![ATtiny85 pinout](https://raw.githubusercontent.com/SpenceKonde/ATTinyCore/master/avr/extras/ATtiny_x5.png)

We are now good to go and 'only' need to install the additional debugging software.

### Arduino IDE + avr-gdb

Since the ATtiny is not supported by default, one needs to download and install the ATtinyCore by 

### PlatformIO 

#### Producing an object code file with debug information using the Arduino IDE

#### A debug session with avr-gdb

#### Installing platformIO

#### Debugging using the platformIO GUI

## Some Considerations about Using Software Breakpoints

## Shortcomings and Problems

- low communication speed when using 1MHz system clock
- no power-down or sleep
- .gdbinit for platformIO
- - Support for ATtiny828, ATtiny43, ATtiny24/48/88


