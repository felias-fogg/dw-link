# An Arduino-based debugWIRE hardware debugger: *dw-probe*

The Arduino IDE is very simple and makes it easy to get started. After a while, however, one notices that a lot of important features are missing. In particular, the IDE does not support any kind of debugging. So what can you do, when you want to debug your Arduino project? The usual way is to insert print statements and see whether the program does the things it is supposed to do.  

When you want real debugging support, you could buy expensive hardware-debuggers such as the Atmel-ICE, but then you have to use the development IDE [Microchip Studio](https://www.microchip.com/en-us/development-tools-tools-and-software/microchip-studio-for-avr-and-sam-devices) (for Windows) or [MPLAB X IDE](https://www.microchip.com/en-us/development-tools-tools-and-software/mplab-x-ide) (for all platforms).
If you want to use open source software, you will notice that there is AVaRICE. However, it does not appear to work under macOS or Windows. 

So, what are the alternatives if you want to develop programs for AVR ATtinys and small ATmegas and you are in need of a debugging tool and either want to develop on a Mac or do not want to spend more than € 100 (or both)? There exists a software simulator called [SIMAVR](https://github.com/buserror/simavr) and there is a [remote stub](https://sourceware.org/gdb/onlinedocs/gdb/Remote-Stub.html) for some ATmegas, called [AVR8-STUB](https://github.com/jdolinay/avr_debug). Both are integrated into PlatformIO as debuggers. However, using them is not the same as debugging on the hardware where your firmware should finally run. There exists a gdbserver implementation, called [dwire-debug](https://github.com/dcwbrown/dwire-debug), for host systems that uses just the serial interface of the host to talk with a target using the debugWIRE interface. However, only one breakpoint (the hardware breakpoint on the target system) is supported and the particular way of turning a serial interface into a one-wire interface does not seem to work under macOS, as far as I can tell (after some time of experimentation). Finally, there exists an Arduino based hardware debugger called [DebugWireDebuggerProgrammer](https://github.com/wholder/DebugWireDebuggerProgrammer). Unfortunately, it does not provide a gdbserver interface.

So, I took all of the above ideas and put them together in order to come up with a cheap hardware debugger supporting the gdbserver interface that is able to debug the classic ATtinys and some of the smaller ATmegas (such as the popular ATmega328) using the GNU debugger gdb. You just need an ATmega328 based board to get started (e.g., UNO, Nano, or Pro Mini). Actually, ATmega32U4 systems such as Leonardo, Pro Micro, and Micro also work. And you can even use an Arduino Mega (it is a bit of an overkill, though). 

If you want to debug 3.3 volt systems, if your target system is very power hungry, and/or you want to use an ISP cable, then you may want to use an additional adapter PCB that provides you with all these features (see [Section 4](#section4) below). 

### Warning

Read [Sections 2.3 & 2.4](#section23) about the requirements on the RESET line carefully before trying to debug a target system. You might very well "brick" your MCU by enabling debugWIRE on a system which does not satisfy these requirements.


<a name="section1"></a>
## 1. The debugWIRE interface

The basic idea of *debugWIRE* is that one uses the RESET line as a communication line between the target system (the system you want to debug) and the development machine, which runs a debug program such as GDB. This idea is very cool because it does not waste any of the other pins for debugging purposes (as does e.g. the [JTAG interface](https://en.wikipedia.org/wiki/JTAG)). However, using the RESET line as a communication channel means, of course, that one cannot use the RESET line to reset the MCU anymore. Furthermore, one cannot any longer use [ISP programming](https://en.wikipedia.org/wiki/In-system_programming) to upload new firmware to the MCU or change the fuses of the MCU.

Do not get nervous when your MCU does not react any longer as you expect it, but try to understand in which state the MCU is. With respect to the debugWIRE protocol there are basically three states your MCU could be in:

1. The **normal** state in which the DWEN (debugWIRE enable) [fuse](https://microchipdeveloper.com/8avr:avrfuses) is disabled. In this state, you can use ISP programming to change fuses and to upload programs. By enabling the DWEN fuse, one reaches the **transitionary** state.
2. The **transitionary** state, in which the DWEN fuse is enabled. In this state, you could use ISP programming to disable the DWEN fuse again, in order to reach the **normal state**. By *power-cycling* (switching the target system off and on again), one reaches the **debugWIRE** state.
3. The **debugWIRE** state is the state in which you can use the debugger to control the target system. If you want to return to the **normal** state, a particular debugWIRE command leads to a transition to the **transitionary** state, from which one can reach the normal state using ISP programming. 

The hardware debugger will take care of bringing you from state 1 to state 3 when you connect to the target by using the `target` command or when using the ```monitor dwconnect``` command. The system LED will flash in a particular pattern, which signals that you should power-cycle the target. Alternatively, if the target is powered by the hardware debugger, it will power-cycle automatically. The transition from state 3 to state 1 can be achieved by the gdb command ```monitor dwoff```.

Having said all that, I have to admit that I encountered strange situations that I did not understand, and which led to bricking MCUs. Sometimes MCUs started up with a much lower communication speed than is standard and only after a DebugWIRE RESET command they offered the full speed. I have taken this into account now. Further, some MCUs claimed to have been transitioned into the debugWIRE state, but actually did not. In any case, there is no guarantee that the hardware debugger will not brick your MCU. Usually, it can be [resurrected by using a high-voltage programmer](#worstcase), though.

<a name="section2"></a>
## 2. Hardware requirements

There are a few constraints on what kind of board you can use as the base for the hardware debugger and some requirements on how to connect the debugger to the target system. Furthermore, there is only a limited set of AVR MCUs that can be debugged using  debugWIRE.

### 2.1 The debugger
As mentioned above, as a base for the debugger, in principle one can use any ATmega328, ATmega32U4, ATmega1284, or ATmega2560 based board. The clock speed  must be 16MHz. Currently, the following boards are supported:

* [Arduino Uno](https://store.arduino.cc/products/arduino-uno-rev3)
* [Arduino Leonardo](https://store.arduino.cc/products/arduino-leonardo-with-headers)
* [Arduino Mega](https://store.arduino.cc/products/arduino-mega-2560-rev3)
* [Arduino Nano](https://store.arduino.cc/products/arduino-nano)
* [Arduino Pro Mini](https://docs.arduino.cc/retired/boards/arduino-pro-mini)
* [Arduino Micro](https://store.arduino.cc/products/arduino-micro)
* [Arduino Pro Micro](https://www.sparkfun.com/products/12640)

If you intend to use a different board, this is possible without too much hassle. You simply need to set up your own pin mapping (see [Sections 4.2 & 4.3](#section42)) in the source code. 

The most basic solution is to use the UNO board and connect the cables as it is shown in the [Fritzing sketch](#Fritzing) further down. If you want to use the debugger more than once, it may payoff to use a prototype shield and put an ISP socket on it. The more luxurious solution is an adapter board for Nano sized Arduino boards or a shield for the UNO sized Arduino boards as described further down in [Section 4](#section4).




### 2.2 MCUs with debugWIRE interface

In general, almost all "older" ATtiny MCUs and the ATmegaX8 MCUs have the debugWIRE interface. Specifically, the following MCUs can be debugged using this interface:

* __ATtiny13__
* ATtiny43U
* __ATtiny2313__, __ATtiny4313__
* __ATtiny24__, __ATtiny44__, __ATtiny84__
* ATtiny441, ATtiny841
* __ATtiny25__, __ATtiny45__, __ATtiny85__
* __ATtiny261__, __ATtiny461__, __ATtiny861__
* ATtiny87, __ATtiny167__
* ATtiny828
* ATtiny48, ATtiny88
* __ATtiny1634__
* __ATmega48A__, __ATmega48PA__, __ATmega88A__, __ATmega88PA__, __ATmega168A__, __ATmega168PA__, __ATmega328__, __ATmega328P__
* ATmega8U2, ATmega16U2, ATmega32U2

I have tested the debugger on the MCUs marked bold. It will probably also work on the untested MCUs. However, there are always surprises. For example, the ATmegaX8s with 16 KiB flash or less require a particular way of changing fuses, the ATtiny1634 had a special way of erasing flash pages, the ATtiny4313 used a different address for the DWDR register than the 2313, and the ATmega328 claims to be an ATmega328P when debugWIRE is activated. 

<a name="section23"></a>
### 2.3 Requirements concerning the RESET line of the target system 

Since the RESET line of the target system is used as an asynchronous half-duplex serial communication line, one has to make sure that there is no capacitive load on the line when it is used in debugWIRE mode. Further, there should be a pull-up resistor of around 10 kΩ. According to reports of other people, 4.7 kΩ might also work.

If your target system is an Arduino UNO, you have to be aware that there is a capacitor between the RESET pin of the ATmega328 and the DTR pin of the serial chip, which implements the auto-reset feature. This is used by the Arduino IDE to issue a reset pulse in order to start the bootloader. One can disconnect the capacitor by cutting a solder bridge labeled *RESET EN* on the board (see picture), but then you cannot use the automatic reset feature of the Arduino IDE any longer. 

![Solder bridge on Uno board](pics/cut.jpg)
<center>Solder bridge</center> 

A recovery method may be to either put a bit of soldering  on the bridge or better to solder two pins on the board and use a jumper. Alternatively, you could always manually reset the UNO before the Arduino IDE attempts to upload a sketch. The trick is to release the reset button just when the compilation process has finished. 

Other Arduino boards, [such as the Nano, are a bit harder to modify](https://mtech.dk/thomsen/electro/arduino.php), while a Pro Mini, for example, can be used without a problem, provided the DTR line of the FTDI connector is not connected. In general, it is a good idea to get hold of a schematic of the board you are going to debug. Then it is easy to find out what is connected to the RESET line, and what needs to be removed. It is probably also a good idea to check the value of the pull-up resistor, if present. 

<a name="worstcase"></a>
### 2.4 Worst-case scenario 

So, what is the worst-case scenario? As described in [Section 1](#section1), first the DWEN fuse is programmed using ISP programming. Then one has to power-cycle in order to reach the debugWIRE state, in which you can communicate with the target over the RESET line. If this kind of communication fails, you cannot put the target back in a state, in which ISP programming is possible. Your MCU is *bricked*. There are two ways out here. First you can try to make the RESET line compliant with the debugWIRE requirements. Then you should be able to connect to the target using the hardware debugger. Second, you can use high-voltage programming, where 12 volt have to applied to the RESET pin. So you either remove the chip from the board and do the programming offline or you remove any connection from the RESET line to the Vcc rail and other components on the board. Then you can use either an existing high-voltage programmer or you [build one on a breadboard](https://github.com/felias-fogg/RescueAVR).


## 3. Installation and example sessions

Assuming that you have already downloaded and installed the [Arduino](https://www.arduino.cc/) IDE, as a first step, you have to download this repository and install it in a directory where the Arduino IDE recognizes Arduino sketches. Then you have to upload the ```dw-probe.ino``` sketch to the Arduino board you intend to turn into a hardware debugger. 

I will describe two options of using the hardware debugger. The first one is the easiest one, which simply adds a ```platform.local.txt``` file to the Arduino configuration and requires you to download ```avr-gdb```. With that you can start to debug. The second option involves downloading the [PlatformIO](https://platformio.org/) IDE, setting up a project, and starting your first debug session with this IDE. There are numerous other possibilities. In the [guide](https://github.com/jdolinay/avr_debug/blob/master/doc/avr_debug.pdf) to debugging with *avr_debug*, there is an extensive description of how to setup [Eclipse](https://www.eclipse.org/) for debugging with *avr_debug*, which should apply to *debugWIRE-probe* as well. Another option may be [Emacs](https://www.gnu.org/software/emacs/).

However, before you can start to debug, you have to setup the hardware. I'll use an ATtiny85 on a breadboard as the example target system and an UNO as the example debugger. However, any MCU listed above would do. You have to adapt the steps where I describe the modification of configuration files accordingly, though. 

<a name="section31"></a>
### 3.1 Setting up the hardware

<a name="Fritzing"></a>
![ATtiny85 on a breadboard](pics/debug-attiny85_Steckplatine.png)


As you can see, the Vcc rail is connected to pin D9 of the Arduino so that the Uno will be able to power-cycle the target chip. Furthermore, pin D8 of the Arduino is connected to the RESET pin of the ATtiny (pin 1). Note the presence of the pullup resistor of 10kΩ on the ATtiny RESET pin. The remaining connections between Arduino and ATtiny are MOSI, MISO and SCK, which you need for ISP programming. In addition, there is a LED connected to pin 3 of the ATtiny chip (which is PB4 or pin D4 in Arduino terminology). The pinout of the ATtiny85 is given in the next figure (with the usual "counter-clockwise" numbering of Arduino pins).


![ATtiny85 pinout](https://raw.githubusercontent.com/SpenceKonde/ATTinyCore/master/avr/extras/ATtiny_x5.png)

We are now good to go and 'only' need to install the additional debugging software. Before we do that, let us have a look, in which states the debugger can be and how it signals that using the system LED (the Arduino builtin LED on Arduino pin D13).

#### 3.1.1 States of the hardware debugger

There are basically 5 states, the debugger can be in and each is signaled by a different blink pattern of the system LED:

* not connected (LED is off)
* waiting for power-cycling the target (LED flashes every second for 100 ms)
* target is connected (LED is on)
* target is running (LED blinks 0.5 sec on / 0.5 sec off)
* error state, i.e., not possible to connect to target, connection has been lost, or other internal error (LED blinks furiously every 100 ms)

If the hardware debugger is in the error state, one should reset the debugger, finish the gdb session, and restart it.

### 3.2 Arduino IDE and avr-gdb

Assuming that you are working with the Arduino IDE or Arduino CLI, the simplest way of starting to debug your code is to use the GNU debugger. You only have to download the debugger and make a few changes to some of the configuration files.

#### 3.2.1 Installing ATTinyCore

Since ATtinys are not supported by default, one needs to download and install [ATTinyCore](https://github.com/SpenceKonde/ATTinyCore/blob/master/Installation.md). After you have done that, you need to create a ```platform.local.txt``` file in the right directory and might want to add some lines to the ```boards.txt``` file. 

#### 3.2.2 Modifications of platform.local.txt

When you have chosen the **Boards Manager Installation**, then you will find the ATTinyCore configuration files under the following directories:

* macOS: ~/Library/Arduino15/packages/ATTinyCore/hardware/avr/1.5.2
* Linux: ~/.arduino15/packages/ATTinyCore/hardware/avr/1.5.2
* Windows: C:\Users\\*USERNAME*\AppData\Local\Arduino15\packages\ATTinyCore\hardware\avr\1.5.2

If you have chosen **Manual Installation**, then you know where to look. In the directory with the ```platform.txt``` file create ```platform.local.txt``` with the following contents:

```
recipe.hooks.savehex.postsavehex.1.pattern.macosx=cp "{build.path}/{build.project_name}.elf" "{sketch_path}"
recipe.hooks.savehex.postsavehex.2.pattern.macosx=cp "{build.path}/{build.project_name}.lst" "{sketch_path}"
recipe.hooks.savehex.postsavehex.1.pattern.linux=cp "{build.path}/{build.project_name}.elf" "{sketch_path}"
recipe.hooks.savehex.postsavehex.2.pattern.linux=cp "{build.path}/{build.project_name}.lst" "{sketch_path}"
recipe.hooks.savehex.postsavehex.1.pattern.windows=cmd /C copy "{build.path}\{build.project_name}.elf" "{sketch_path}"
recipe.hooks.savehex.postsavehex.2.pattern.windows=cmd /C copy "{build.path}\{build.project_name}.lst" "{sketch_path}"

compiler.c.extra_flags=-Og
compiler.cpp.extra_flags=-Og
```

The first four lines make sure that you receive an **ELF** and a **LST** file in your sketch directory when you select ```Export compiled Binary``` under the menu ```Sketch```. The former type of file contains machine-readable symbol and line number information and is needed when you want to debug a program using avr-gdb. The latter type is informative when you want to know how your program maps to assembler statements. Note that **LST** files are only generated with the ATTinyCore. So, if you want to debug another AVR MCU, you have to either delete the number 2 hooks or copy the *presave hooks* from the ATTinyCore ```platform.txt``` together with the appropriate batch/shell scripts. 

The last two lines enforce that the compiler optimizations are geared towards being debugging friendly. Strictly speaking, this compiler option is not necessary. However, without it, the execution of the program may not follow step by step the way you had specified it in the program. Please also note that the code size of the program may grow.



If you do not want to have '-Og' as the standard option, you can rename ```platform.local.txt``` to something else, when you are not debugging. Another alternative can be to modify the `boards.txt` file in order to set the debug flags.

If you are using `arduino-cli`, you can specify additional options to the `compile` command and do not have to bother with modifying the `boards.txt` file or renaming ```platform.local.txt```. Instead of putting the last two lines above into the ```platform.local.txt``` file, just add the following to your command line:

```
--build-property compiler.c.extra_flags=-Og --build-property compiler.cpp.extra_flags=-Og 
```

#### 3.2.3 Optional modification of boards.txt

Instead of setting the compiler flag globally for the platform, you can modify the ```boards.txt``` file (residing in the same directory as the `platform.txt`file) and introduce for each type of MCU a new menu entry ```debug``` that when enabled adds the build option ```-Og```. For the ATTinyCore platform, you could simply add another menu entry in `boards.txt` under the first couple of lines as follows:

`
menu.debug=Debug Compile Flag
`

If you now want to be able modify the debug flag for the ATtinyX5, scroll down to the line 

`
attinyx5.build.extra_flags={build.millis} -DNEOPIXELPORT=PORTB {build.pllsettings}
`

Now add `{build.debug}` to the end of this line. Before the line, you have to insert the following four lines:

```
attinyx5.menu.debug.disabled=Disabled
attinyx5.menu.debug.disabled.build.debug=
attinyx5.menu.debug.enabled=Enabled
attinyx5.menu.debug.enabled.build.debug=-Og
```

Now you have to restart the Arduino IDE. If you select `ATtiny25/45/85 (No bootloader)` from the menu of possible MCUs, then you will notice that there is a new menu option `Debug`. This works, of course, also for other board definitions, e.g., for the Arduino Uno. 

#### 3.2.4 Installing avr-gdb

Unfortunately, the debugger is not any longer part of the toolchain integrated into the Arduino IDE. This means, you have to download it and install it by yourself:

* macOS: Use [**homebrew**](https://brew.sh/index_de) to install it. 

* Linux: Just install avr-gdb with you packet manager.

* Windows: You can download the AVR-toolchain from the [Microchip website](https://www.microchip.com/en-us/development-tools-tools-and-software/gcc-compilers-avr-and-arm). This includes avr-gdb.

#### 3.2.5 Example session with avr-gdb

Now we are ready to start a debug session. So compile the example `tiny85blink.ino` with debugging enabled, require the binary files to be exported, which gives you the file `tiny85blink.ino.elf` in the sketch directory. Then connect your UNO to your computer and start avr-gdb. All the lines starting with either the **>** or the **(gdb)** prompt contain user input and everything after # is a comment. **\<serial port\>** is the serial port you use to communicate with the UNO.

```
> avr-gdb
GNU gdb (GDB) 10.2
Copyright (C) 2021 Free Software Foundation, Inc.
...

(gdb) file tiny85blink.ino.elf          # load symbol table from executable file
Reading symbols from tiny85blink.ino.elf...
(gdb) set serial baud 115200            # set baud rate 
(gdb) target remote <serial port>       # connect to the serial port of debugger    
Remote debugging using <serial port>
0x00000000 in __vectors ()
(gdb) monitor dwconnect                 # show the debugWIRE connection
Connected to ATtiny85
debugWIRE is now enabled, bps: 63713    # now we are connected to the MCU
(gdb) load                              # load binary file
Loading section .text, size 0x1e2 lma 0x0
Start address 0x00000000, load size 482
Transfer rate: 621 bytes/sec, 160 bytes/write.
(gdb) break loop                        # set breakpoint at start of loop
Breakpoint 1 at 0x1ae: file /.../tiny85blink/tiny85blink.ino, line 14.
(gdb) list loop                         # list part of loop and shift focus
9	}
10	
11	
12	void loop() {
13	  int i=10;
14	  digitalWrite(LED, HIGH);   
15	  i++;
16	  delay(1000);                       
17	  i++;
18	  digitalWrite(LED, LOW);    
(gdb) br 18                             # set breakpoint at line 18
Breakpoint 2 at 0x1bc: file /.../tiny85blink/tiny85blink.ino, line 18.
(gdb) continue                          # start execution (at PC=0)
Continuing.

Breakpoint 1, loop ()
    at /.../tiny85blink/tiny85blink.ino:14
39	  digitalWrite(LED, HIGH);   
(gdb) set var thisByte=20               # set variable thisByte
(gdb)  print i                          # print value of variable i
$1 = 10
(gdb) next                              # make one step (not stepping into functions)
16	  delay(1000);                       
(gdb) step
delay (ms=1000)
    at /.../tiny/wiring.c:518
518	    uint16_t start = (uint16_t)micros();
(gdb) finish                            # finish current function and return
Run till exit from #0  delay (ms=1000)
    at /.../tiny/wiring.c:518

Breakpoint 2, loop ()                   # reached second breakpoint when returning
    at /.../tiny85blink/tiny85blink.ino:43
18	  digitalWrite(LED, LOW);    
(gdb) info breakpoints                  # show all breakpoints
Num     Type           Disp Enb Address    What
1       breakpoint     keep y   0x000001ae in loop() 
                                           at /.../tiny85blink/tiny85blink.ino:14
	breakpoint already hit 1 time
2       breakpoint     keep y   0x000001c0 in loop() 
                                           at /.../tiny85blink/tiny85blink.ino:18
	breakpoint already hit 1 time
(gdb) delete 1                          # remove breakpoint 1
(gdb) detach                            # detach from remote target
Detaching from program: /.../tiny85blink/tiny85blink.ino.elf, ... 
[Inferior 1 (Remote target) detached]
(gdb) quit                              # exit from avr-gdb                

```


#### 3.2.6 Switch off debugWIRE mode 

Note that the ATtiny MCU is still in debugWIRE mode and the RESET pin cannot be used to reset the chip. If you want to bring the MCU back to the normal state, you need to call avr-gdb again.

```
> avr-gdb
GNU gdb (GDB) 10.2
Copyright (C) 2021 Free Software Foundation, Inc.
...

(gdb) set serial baud 115200            # set baud rate 
(gdb) target remote <serial port>       # connect to serial port of debugger    
Remote debugging using <serial port>
0x00000000 in __vectors ()
(gdb) monitor dwoff                     # terminate debugWIRE mode 
Connected to ATtiny85
debugWIRE is now disabled
(gdb) quit
>
```        
Of course, you could have done that before leaving the debug session in the previous section.

#### 3.2.7 GDB commands

In the example session above, we saw a number of relevant commands already. If you really want to debug using gdb, you need to know a few more commands, though. Let me just give a brief overview of the most interesting commands (anything between square brackets can be omitted, arguments are in italics).

command | action
--- | ---
h[elp] | get help on gdb commands
h[elp] *command* | get help on a specific command
s[tep] | single step, descending into functions (step in)
n[ext] | single step without descending into functions (step over)
fin[ish] | finish current function and return from call (step out)
c[ontinue] | continue (or start) from current position
ba[cktrace] | show call stack
up | go one stack frame up (in order to display variables)
down | go one stack frame down (only possible after up)
l[ist] | show source code around current point
l[list] *function* | show source code around start of code for *function*
p[rint] *name* | show value of variable called *name* 
p[rint] **name* | show value of what *name* points to
b[reak] *function* | set breakpoint at beginning of *function*
b[reak] *number* | set breakpoint at source code line *number* in the current file
i[nfo] b[reakpoints] | list all breakpoints
dis[able] *number* | disable breakpoint *number*
en[able] *number* | enable breakpoint *number*
d[elete] *number* | delete breakpoint *number*
d[elete] | delete all breakpoints
cond[ition] *number* *expression* | stop at breakpoinit *number* only if *expression* is true
cond[ition] *number* | make breakpoint *number* unconditional
comm[ands] *number* | Add a list of commands to be executed when the breakpoint is hit

In addition to the commands above, you have to know a few more commands that control the execution of avr-gdb.

command | action
--- | ---
set se[rial] b[aud] *number* | set baud rate of the serial line to the gdbserver
tar[get] rem[ote] *serial line* | specify the serial line to the gdbserver (use only after baud rate has been set)
fil[e] *name*.elf | load the symbol table from the specified ELF file
lo[ad] | load the ELF file into flash memory
mo[nitor] dwc[onnect] | establishes the debugWIRE link to the target
mo[nitor] dwo[ff] | disable debugWIRE mode in the target
mo[nitor] re[set] | resets the MCU
mo[nitor] fla[shcount] | reports on how many flash-page write operation have taken place since start  
mo[nitor] ck8[prescaler] | program the CKDIV8 fuse (i.e., set MCU clock to 1MHz if running on internal oscillator)
mo[nitor] ck1[prescaler] | un-program the CKDIV8 fuse (i.e., set MCU to 8MHz if running on internal oscillator)
mo[nitor] hw[bp] | set number of allowed breakpoints to 1 (i.e., only HW BP) 
mo[nitor] sw[bp] | set number of allowed user breakpoints to 32 (+1 system breakpoint), which is the default
tu[i] e[nable] | enable text window user interface
tu[i] d[isable] | disable text window user interface

The two last commands are particularly interesting because you get a nicer user interface, which can show the source code, disassembled code and the registers (see [manual](https://sourceware.org/gdb/onlinedocs/gdb/TUI-Commands.html#TUI-Commands)).

### 3.3 PlatformIO

[platformIO](https://platformio.org/) is an IDE aimed at embedded systems and it is based on [Visual Studio Code](https://code.visualstudio.com/). It supports many MCUs, in particular all AVR MCUs. And it is possible to import Arduino projects, which are then turned into ordinary C++ projects. Projects are highly configurable, that is a lot of parameters can be set for different purposes. However, that makes things in the beginning a bit more difficult. 

#### 3.3.1 Installing PlatformIO

Installing PlatformIO is straight forward. Download and install Visual Studio Code. Then start it and click on the extension icon on the left, search for the PlatformIO extension and install it, as is described [here](https://platformio.org/install/ide?install=vscode).
Check out the [quick start guide](https://docs.platformio.org/en/latest//integration/ide/vscode.html#quick-start). Now we are all set.

#### 3.3.2 Import an Arduino project into PlatformIO

Now let us prepare a debugging session with the same project we had before. Startup Visual Studio Code and click on the home symbol in the lower navigation bar. Now PlatformIO offers you to create a new project, import an Arduino project, open a project, or take some project examples. Choose **Import Arduino Project** and PlatformIO will ask you which platform you want to use. Type in **attiny85** and choose **ATtiny85 generic**. After that, you can navigate to the directory containing the Arduino project and PlatformIO will import it. 


#### 3.3.3 Debugging with PlatformIO


If you now click on the debug symbol in the left navigation bar (fourth from the top), PlatformIO enables debugging using **simavr**, the default debugger for this chip. You can now start a debug session by clicking the green triangle at the top navigation bar labeled **PIO Debug**. On the right, the debug control bar shows up, with symbols for starting execution, step-over, step-in, step-out, reset, and exit. On the left there are a number of window panes giving you information about variables, watchpoints, the call stack, breakpoints, peripherals, registers, memory, and disassembly. Try to play around with it!

But, of course, this is not the real thing. No LED is blinking. So close the window and copy the following two files from `examples/pio-config` to the project directory of your new PlatformIO project:

* `platformio.ini`
* `extra_script.py`

In the file `platformio.ini`, you need to put in the serial port your Arduino debugger uses. In general, you can use the `platformio.ini` configuration as a blueprint for other projects, where you want to use the hardware debugger. Note one important point, though. PlatformIO debugging will always choose the *default environment* or, if this is not set, the first environment in the config file. 

After having copied the two files into the project directory and reopened the project window, you should be able to debug your project as described above. Only now you are debugging the program on the target system, i.e., the LED really blinks!

A very [readable introduction to debugging](https://piolabs.com/blog/insights/debugging-introduction.html) using PlatformIO has been written by [Valerii Koval](https://www.linkedin.com/in/valeros/). It explains the general ideas and all the many ways how to interact with the PlatformIO GUI.

#### 3.3.4 Switch off debugWIRE mode

There are two ways of switching off the debugWIRE mode. If you click on the ant symbol (last symbol in the left navigation bar), there should be the option *debug* environment. When you then click on *Custom*, you should see you the option *DebugWIRE Disable*. Clicking on it will set the MCU back to its normal state, where the RESET line can be used for resets and ISP programming is possible.  Alternatively, you should be able to bring back you MCU to the normal state by typing `monitor dwoff` in the debugging terminal window of the PlatformIO IDE.

<a name="section4"></a>
## 4. Adapter boards

While one can use the setting as described in [Section 3.1](#section31), it would be much nicer if you just could plug an ISP cable on the one side into the debugger and on the other side into the target system. Further, one would also like to have level shifters in order to be able to debug also 3.3 volt systems. 

I have developed a shield for the Arduino Uno, Leonardo, and Mega and a base board for the Arduino Nano V2, Nano V3, Pro Mini, Pro Micro, and Micro. These boards together with the Arduino result in a powerful debugWIRE hardware debugger. It can supply up to 200 mA regardless of whether 5 or 3.3 volts are needed, and it is able to power-cycle this supply line. In addition, there is the possibility to add a 10 kΩ pull-up to the RESET line. You only have to set three DIP switches, then to plug in a USB cable on one side and an ISP cable on the other side,  and off you go.  

When you use an Arduino Nano, you should be aware that  there are apparently two different versions around, namely version 2 and version 3. The former one has the A0 pin close to the 5V pin, while version 3 boards have the A0 pin close to the REF pin. If you use a Nano on the adapter board, you need to set the compile time constant `NANOVERSION`, either by changing the value in the source or by defining the value when compiling. The default value is 3 (which is the current version).

Of course, it is possible to use any of the Arduino boards without an adapter board, e.g., on a breadboard. In this case, one should leave the **SNSGND** pin (see below) open, signalling that the adapter is not present. This implies that all the switch lines are not sensed and the control lines are not used. Instead, the **VSUP** line can be used to directly supply current to the target, provided the target does not require more than 20 mA.

### 4.1 DIP switch configuration

There a three different DIP switches labelled **Vcc**, **5V**, and **10k**. 

Label | On | Off
--- | --- | ---
**Von** | The debugger supplies power to the target (should be less than 200 mA) and the line is power-cycled when debugWIRE mode needs to be enabled | The debugger does not supply power to the target, which means that power has to be supplied externally and power-cycling needs to be done manually
**5V** | The debugger provides 5 V (if Vcc is on) | The debugger provides 3.3 V (if Vcc is on)
**10k** | A 10kΩ pull-up resistor is connected to the RESET line of the target | 

<a name="section42"></a>
### 4.2 Functional pins of the adapter board 

In order to be able to accommodate different boards on the adapters, the pin mapping of the adapter boards is somewhat complex. First of all, let me list the functional pins.

Functional pin | Direction | Explanation
--- | --- | ---
DEBTX | Output | Serial line for debugging output (when debugging the debugger) 
DWLINE | Input/Output | debugWIRE communication line to be connected to RESET on the target board
GND | Supply | Ground
ISP | Output | If low, then ISP programming is enabled
MISO | Input | SPI signal "Master In, Slave Out"
MOSI | Output | SPI signal "Master Out, Slave In"
SCK | Output | SPI signal "Master clock"
SNSGND | Input | If low, signals that the debugger board sits on the adapter board
V33 | Output | Control line to the MOSFET to switch on 3.3 volt supply for target
V5  | Output | Control line to switch on the 5 volt line
Vcc | Supply | Voltage supply from the board (5 V) that can be used to power the target
VHIGH | Input from switch | If low, then choose 5 V supply for target, otherwise 3.3 V 
VON | Input from switch | If low, then supply target (and use power-cycling)
VSUP | Output | Used as a target supply line driven directly by an ATmega pin, which is only active if the debugger board does not sit on the adapter board, i.e., if SNSGND=open


### 4.3 Pin mapping

If you plug in your Arduino into the adapter board or use the shield, you do not have to bother about pin assignments. The only important thing is to set the DIP switches and plug in the USB and ISP cable. If you want to use the Arduino without such a board, you need, of course, to know which pins of the debugger to connect to the target.

In the table below, the mapping between functional pins of the debugger and the Arduino pins is given. For a standalone setting, only the pins marked in the last column are required. The **DWLINE** pin is the debugWIRE line, which needds to be connected to the traget. MOSI, MISO, and SCK are the usual signals for ISP programming by SPI.

The **VSUP** pin should only be used if the current requirement by the target in not more than 20 mA. Otherwise you need to power the system by an external power source or use the Vcc pin. Note that in a standalone setting, there is no level-shifting done, so you only should debug 5 V systems.

Pin | Nano V2 | Nano V3 | Micro | Pro Micro | Pro Mini | UNO | Leo-nardo | Mega | Stand alone
--- | --- | --- | --- | --- | --- | --- | --- | --- | ---
DEB-TX |A3= D17|A4= D18| A4= D22 | D5 | D5 |D3 |D3|D3|
DW-LINE | D8 | D8 | D4 | D4 |D8| D8 |D4|D49| **+**
GND | GND | GND | GND | GND | GND | GND | GND | GND | **+**
ISP | D2 | D2 | D2 | D16 | D11 |D6|D6|D6|
MISO | A2= D16 | A5= D19 | A5= D23 |D6 | D6 |D11|D11|D11| **+**
MOSI | A5= D19 | A2= D16 | A2= D20 | D3 | D3 |D10|D10|D10| **+**
SCK | D3 | D3 | D3 | D14 | D12 |D12|D12|D12| **+**
SNS-GND | D11 | D11 | D11 | D10 | D10 |A0= D14|A0= D18|A0= D54|
V33 | D5 | D5 | D5 | A0= D18 | A0= D14 |D7|D7|D7| 
V5 | D6 | D6 | D6 | A1= D19 | A1= D15 |D9|D9|D9| 
Vcc | 5V | 5V | 5V | Vcc | Vcc |5V|5V|5V| (+)
VHIGH | D7 | D7 | D7 | A2= D20 | A2= D16 |D2|D2|D2| 
VON | A1= D15 | A1= D15 | A1= D19 | D2 | D2 |D5|D5|D5| 
VSUP | D6 | D6 | D6 | A1= D19 | A1= D15 |D9|D9|D9 | **+**

### 4.3 System LED

As the system LED, we use the builtin LED on pin13 for most boards. On the Pro Micro, the RXI LED is used. On the Pro Mini, the builtin LED cannot be used since Arduino pin 13 conflicts with the input capture pin of the Micro board. So, if you use the Pro Mini, you do not have a system LED that signals the internal state.  


## 5. Problems and shortcomings

dw-probe is still in ***alpha*** state. The most obvious errors have been fixed, but there are most probably others. If something does not go according to plan, please try to isolate the reason for the erroneous behaviour, i.e., identify a sequence of operations to replicate the error. One perfect way to document a debugger error is to switch on logging and command tracing in the debugger:

```
set trace-commands on
set logging on
```

I have prepared an *[issue form](issue_form.md)* for you, where I ask for all the information necessary to replicate the error. 

Apart from bugs, there are, of course, shortcomings that one cannot avoid. I will present some of them in the next subsections.

### 5.1 Flash memory wear

Setting and removing *breakpoints* is one of the main functionality of a debugger. Setting a breakpoint is mainly accomplished by changing an instruction in flash memory to the BREAK instruction. This, however, implies that one has to *reprogram flash memory*. Since flash memory wears out, one should try to minimize the number of flash memory reprogramming operations.

One now has to understand that gdb does not pass *breakpoint set* and *breakpoint delete* commands from the user to the gdbserver, but instead it sends a list of *breakpoint set* commands before execution starts. After execution stops, it sends *breakpoint delete* commands for all breakpoints. In particular, when thinking about conditional breakpoints, it becomes clear that gdb may send a large number of *breakpoint set* and *breakpoint delete* commands for one breakpoint during one debug session. Although it is guaranteed that flash memory can be reprogrammed at least 10,000 times according to the data sheets, this number can easily be reached even in a few debug sessions, provided there are loops which are often executed and where a conditional breakpoint has been inserted. Fortunately, the situation is not as bad as it looks since there are a number of ways of getting around the need of reprogramming flash memory.

First of all, *dw-probe* leaves the breakpoint in memory, even when gdb requests to remove them. Only when gdb requests to continue execution, the breakpoints in flash memory are updated. Well, the same happens before loading program code, detaching, exiting, etc. Assuming that the user does not change breakpoints too often, this will reduce flash reprogramming significantly.  

Second, if there are many breakpoints on the same flash page, then the page is reprogrammed only once instead of reprogramming it for each breakpoint individually.

Third, when one restarts from a location where a breakpoint has been set, gdb removes this breakpoint temporarily, single steps to the next instruction, reinserts the breakpoint, and only then continues execution. This would lead to two reprogramming operations. However, *dw-probe* does not update flash memory before single-stepping. Instead, it checks whether the current location contains a BREAK instruction. If this is not the case, *dw-probe* issues a single-step command. Otherwise, it loads the original instruction into the *instruction register* of the MCU and executes it there. This appears to work even it is a two-word instruction, i.e., the second word is fetched from flash memory, provided the PC was set to the location of the original instruction. 

Fourth, each MCU contains one *hardware breakpoint register*, which stops the MCU when the value in the register equals the program counter. This is used for the most recent breakpoint introduced. With this heuristic, temporary breakpoints (as the ones gdb generates for single-stepping) will always get priority and more permanent breakpoints set by the user will end up in flash. 

Fifth, when reprogramming of a flash page is requested, *dw-probe* first checks whether the identical contents should be loaded. Further, it checks whether it is possible to achieve the result by just turning some 1's into 0's. Only if these two things are not possible, the flash page is erased and reprogrammed. This helps in particular when reloading a file with the gdb `load` command after only a few things in the program have been changed.  

With all of that in mind, you do not have to worry too much about flash memory wear when debugging. As a general rule, you should not make massive changes of the breakpoints each time the MCU stops executing. Finally, Microchip recommends that chips that have been used for debugging using debugWIRE should not been shipped to customers. Well, I never ship chips to customers anyway.

<a name="paranoid"></a>
For the really paranoid,  there is the option that permits only one breakpoint, i.e., the hardware breakpoint: `monitor hwbp`. In this case, one either can set one breakpoint or on can single-step, but not both. So, if you want to continue after a break by single-stepping, you first have to delete the breakpoint. By the way, with `monitor swbp`, one switches back to normal mode, in which 32 (+1 temporary) breakpoints are allowed.

<a name="section52"></a>
### 5.2 Slow responses when loading or single-stepping

Sometimes, in particular when using 1MHz clock speed, the responses from the MCU are quite sluggish. This happens, e.g., when loading code or single-stepping. The reason is that a lot of communication over the RESET line is going on in these cases and the communication speed is set to the MCU clock frequency divided by 128, which is roughly 8000 Baud in case of a 1MHz MCU clock. In theory it is possible to choose higher speeds. However, I was not able to establish a reliable connection in this case. So, the only workaround is setting the MCU clock frequency to 8MHz. Indeed, the [Atmel AVR JTAGICE mkII manual ](https://onlinedocs.microchip.com/pr/GUID-73C92233-8EC5-497C-92C3-D52ED257761E-en-US-1/index.html) states under [known issues](https://onlinedocs.microchip.com/pr/GUID-73C92233-8EC5-497C-92C3-D52ED257761E-en-US-1/index.html?GUID-A686427B-0B7C-465A-BCFF-F093FD6B7A8F):

>Setting the CLKDIV8 fuse can cause connection problems when using debugWIRE. For best results, leave this fuse un-programmed during debugging. 

"Leaving the fuse un-programmed" means that you probably have to change the fuse to be un-programmed using a fuse-programmer, because the fuse is programmed by default. In order to simplify life, I added the two commands `monitor ck8prescaler` and `monitor ck1prescaler` to the hardware debugger that allows you to change this fuse. `monitor ck8prescaler` programs the fuse, i.e., the clock is divided by 8, `monitor ck1prescaler` un-programs this fuse. 

### 5.3 Limited number of breakpoints

The hardware debugger supports only a limited number of breakpoints. Currently, 32 breakpoints (+1 temporary breakpoint for single-stepping) are supported by default. You can reduce this to 1 by issuing the command `monitor hwbp` ([see above](#paranoid)). If you set more than the maximum number, it will not be possible to start execution. Instead one will get the warning `Cannot insert breakpoint ... Command aborted`. You have to delete or disable some breakpoints before program execution can continue. However, you should not use that many breakpoints in any case. One to five breakpoints are usually enough. 

### 5.4 Power saving is not operational 

When you activate *sleep mode*, the power consumed by the MCU is supposed to go down significantly. If debugWIRE is active, this is not the case. 

<a name="section55"></a>
### 5.5 MCU operations interfering with debugWIRE

There are a few more situations, which might lead to problems. The above mentioned list of [known issues](https://onlinedocs.microchip.com/pr/GUID-73C92233-8EC5-497C-92C3-D52ED257761E-en-US-1/index.html?GUID-A686427B-0B7C-465A-BCFF-F093FD6B7A8F) mentions the following:

* BOD and WDT resets lead to loss of connection 
* Breakpoints should not be set at the last address of flash memory
* The voltage should not be changed during a debug session
* The OSCCAL and CLKPR registers should not be changed during a debug session
* The PRSPI bit in the power-saving register should not be set
* The CKDIV8 fuse should not be in the programmed state when running off a 128 kHz clock source
* Do not single step over a SLEEP instruction
* Do not insert breakpoints immediately after an LPM instruction

If you do one of these things, either you might lose the connection to the target or, in the last two cases, the instruction might do something wrong. If you loose connection to the target, then it is very likely that there are still BREAK instructions in flash memory. So, after reconnecting, you need to issue the `load` command in order to get a clean copy of your binary into flash memory.

### 5.6 BREAK instructions in your program

It is possible to put the BREAK instruction, which is used to implement breakpoints, in ones program by using the inline assembly statement asm("break"). This does not make any sense since without the debugger, the MCU will stop at this point and will not do anything anymore. Such a BREAK instruction may also be in the program because a previous debugging session was not terminated in a clean way. If such a BREAK is detected, one may want to issue the `load` command again.

When running under the debugger, the program will be stopped in the same way as if there is a software breakpoint set by the user. However, one cannot continue execution from this point with the `step`, `next`, or `continue` command. Instead, the debugger gets an "illegal instruction" signal. So, one either needs to reload the program code or, set the PC to a different value, or restart the debugging session.

### 5.7 The start of the debugger takes a couple of seconds

The reason is that when `avr-gdb` connects to the hardware debugger, it resets the hardware debugger. If it is a plain UNO or equivalent, then it will spend a couple of seconds in the bootloader waiting for an upload of data before it starts the user program. If you want to have a faster startup, get rid of the bootloader by, e.g., flashing `dw-probe.ino` with an ISP programmer into the hardware debugger.

### 5.8 Program execution is very slow when conditional breakpoints are present

If you use *conditional breakpoints*, the program is slowed down significantly.  The reason is that at such a breakpoint, the program has to be stopped, all registers have to be saved, the current values of the variables have to be inspected, and then the program needs to be started again, whereby registers have to be restored first. For all of these operations, debugWIRE communication takes place. This takes roughly 100 ms per stop, even for simple conditions and an MCU running at 8MHz. So, if you have a loop that iterates 1000 times before the condition is met, it may easily take 2 minutes before execution stops.

## 6 Trouble shooting

#### Problem: When starting the debug session in PlatformIO, you get the message *pioinit:XX: Error in sourced command file*

Something in the `platformio.ini` file is not quite right. Perhaps a missing declaration of the `debug_port`. Sometimes an additional line of information is given that identifies the problem.

One common problem is that the debug environment is not the first environment or the default environment. In this case, the wrong environment is used to configure the debug session and probably some environment variables are not set at all or set to the wrong values. So, you need to edit the `platformio.ini` file accordingly.

#### Problem: You get the message *Protocol error with Rcmd* 
This is a generic gdb error message that indicates that the last `monitor` command you typed could not be successfully executed. Usually, also a more specific error message is displayed, e.g., *debugWIRE could NOT be disabled*. These messages are suppressed in some GUIs, though. So, if you get the generic error message and want to know the deeper reason for it, it may pay off to start `avr-gdb` in a shell and type the command that led to the error message. This may help in particular to find out why a connection cannot be established.

#### Problem: You get the message *debugWIRE could NOT be disabled*
This means that the sequence of first disabling debugWIRE by a debugWIRE command and then using ISP programming to clear the DWEN fuse was not successful. Either there is a wiring problem or the chip is bricked.  

#### Problem: You get the message *Cannot connect: Check wiring* 
The debugger is not able to connect to the target system using ISP commands. Most probably the wiring is wrong. 

#### Problem: You get the message *Cannot connect: Unsupported MCU type* 
The target system is (very probably) debugWIRE incompatible. If you believe that this message is in error, please drop me a line.

#### Problem: You get the message *Cannot connect: Lock bits are set* 
Some of the lock bits are set and for this reason it is not possible to debug the MCU. You need to erase the entire device using ISP programming before you can continue.

#### Problem: In response to the `monitor dwconnect` you get the *power-cycle* message a couple of times, then the debugger stops and blinks furiously

When the *power-cycle* message is displayed, you have to power-cycle the target system. If you do not do that, after a while the debugger will go into the error state (LED blinking) and stop. If you have power-cycled the target system (or the debugger has done that for you automatically), but you still get the message, it could be possible that the RESET line of the target system has no pullup, a pullup that is too strong, or a  capacitive load (see [Section 2.3](#section23)). Be aware: the target system is most probably now in debugWIRE mode and the **only** ways to bring it back to normal mode is to solve the problems with the RESET line, e.g., by removing the capacitor, or you use high-voltage programming to resurrect the MCU (see [Section 2.3](#section23)).

#### Problem: The debugger responses are very sluggish   

One reason for that could be that the target is run with a clock of 1 MHz. Since the debugWIRE communication speed is MCU clock/128, the communication speed is roughly 8000 Baud in this case. It is recommended to run the MCU at 8 MHz for this reason (see [Section 5.2](#section52)).

<a name="lost"></a>
#### Problem: When stopping the program with Ctrl-C (or with the stop button), you get the message *Cannot remove breakpoints because program is no longer writable.*

The reason is most probably that the communication connection to the target system has been lost. The reason might be that the electrical conditions on the RESET line of the target system do not meet the requirements (see [Section 2.3](#section23)). Other reasons for loosing the connection are listed in [Section 5.5](#section55). You have to restart the debug session (and should try to identify the reason why the debugger lost connection before). Most probably, there are still BREAK instructions in flash memory, so the `load` command should be used to reload the program.

#### Problem: You get the message *Connection to target lost* and a `SIGHUP` signal when trying to start execution

The target is not responsive any longer. Possible reasons for such a loss of connectivity are listed [above](#lost). It could be that the RESET line of the target system does not satisfy the necessary electrical requirements (see [Section 2.3](#section23)). Other reasons might be that the program disturbed the communication by changing, e.g., the MCU clock frequency (see [Section 5.5](#section55)). Try to identify the reason, eliminate it and then restart the debug session.

#### Problem: The debugger does not start execution when you request *single-stepping* or *execution* and you get the warning *Cannot insert breakpoint ... Command aborted* 

You use more than the allowed number of breakpoints, i.e., usually 32 (+1 for a temporary breakpoint for single-stepping). If you have used the `monitor hwbp` command, this number is reduced to 1. In this case, you can either set a breakpoint or you can single-step, but not both! In any case, you need to reduce the number of breakpoints before you can continue.

#### Problem: The debugger does not start execution when you request *single-stepping* or *execution* and you get the message *illegal instruction* and a `SIGILL` signal

The debugger checks whether the first instruction it has to execute is a legal instruction according to the Microchip specification. Additionally, a BREAK instruction (which has not been inserted by the debugger) is considered as illegal since it would halt the MCU. Such a BREAK instruction might have been inserted as part of the program code or may be a leftover from a previous debugging session that has not been terminated in a clean way.

Check the instruction by using the command `x/i $pc`. You can continue by setting the PC to another value, e.g., one that is higher by two or four, using the command `set $pc=...`.

#### Problem: You get the message *\*\*\*\*Fatal internal debugger error: n* and an `ABORT` signal when trying to start execution

In this case some serious internal error happened. You have to stop the current debug session, reset the debugger and restart. Please try to reproduce the problem and tell me how it happened. Please try to distill a minimal example leading to the problem and fill out the [*issue form*](issue_form.md). BTW: `monitor dwoff` can still be executed in order to disable the debugWIRE on your MCU even if a fatal error has happened. 

#### Problem: The debugger does not stop at the line the breakpoint was set

Not all source lines generate machine code so that it is sometimes impossible to stop at a given line. The debugger will then try to stop at the next possible line. This effect can get worse with different compiler optimization levels. For debugging, `-Og` is recommended, which does a number of optimizations but tries to give a good debugging experience at the same time. This is also the default for PlatformIO and the Arduino IDE. You can change that to `-O0` which does no optimization at all, but will need more flash memory. 

#### Problem: In PlatformIO, the global variables are not displayed

I have no idea, why that is the case. If you want to see the value of a global variable, you can set a `watchpoint`.

#### Problem: The disassembly cannot be displayed

Older versions of avr-gdb had a problem with dissassembly: [https://sourceware.org/bugzilla/show_bug.cgi?id=13519](https://sourceware.org/bugzilla/show_bug.cgi?id=13519). In the current version the problem has been fixed, though. So, you might want to get hold of a current version.


