# An Arduino-based debugWIRE debugger: dw-link

<p align="center">
<img src="https://raw.githubusercontent.com/felias-fogg/dw-link/refs/heads/master/docs/pics/uno-debug2.png" width="40%">
</p>

Unfortunately, neither the old nor the new Arduino IDE supports any kind of debugging for the *classic AVR chips*. So what can you do when you want to debug your Arduino project on small ATmegas (such as the popular ATmega328) or ATtinys? In theory, it should be possible to use *on-chip debugging* via the *debugWIRE* interface, which can be accessed by utilizing a so-called *hardware debugger*. You do not own such a hardware debugger, but you would like to try it out? Then dw-link is the right thing for you. It turns an Arduino UNO into a hardware debugger. It can be used stand-alone (using the symbolic debugger AVR-GDB in a console window), or you can use it as part of the Arduino IDE 2 or other IDEs. 

If you want to give it a try, the two quickstart guides on [debugging using a command-line interface](quickstart-AVR-GDB.md) and [debugging using the Arduino IDE 2](quickstart-Arduino-IDE2.md) might be the right entry points for you.

!!! Warning
    Please read the [sections about the RESET line requirements](requirements.md#requirements-concerning-the-target-system) before connecting the debugger to a target system. You might very well "brick" your MCU by enabling debugWIRE on a system that does not satisfy these requirements. 
