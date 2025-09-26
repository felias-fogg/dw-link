# An Arduino-based debugWIRE debugger: dw-link

<p align="center">
<img src="https://raw.githubusercontent.com/felias-fogg/dw-link/refs/heads/master/docs/pics/uno-debug2.png" width="40%">
</p>

The Arduino IDE is very simple and makes it easy to get started. After a while, however, one notices that a lot of important features are missing. In particular, neither the old nor the new IDE supports any kind of debugging for the classic AVR chips. So what can you do when you want to debug your Arduino project on small ATmegas (such as the popular ATmega328) or ATtinys? The usual way is to insert print statements and see whether the program does the things it is supposed to do.

However, supposedly, one should be able to do better than that because the MCUs mentioned above support *on-chip debugging* via debugWIRE, which can be accessed by utilizing a so-called hardware debuggers. If you do not want to buy a hardware debugger, such as MPLAB SNAP, PICkit4, Atmel-ICE, Atmel's JTAGICE3, or Atmel's Powerdebugger, then dw-link is the right thing for you. It turns an Arduino UNO into a hardware debugger that acts as a gdbserver. It can be used stand-alone (using the symbolic debugger AVR-GDB in a console window), or you can integrate it into an IDE such as Arduino IDE 2, PlatformIO, or CLion. Here, we will only cover the case of the Arduino IDE 2.

If you just want to try out, the two quickstart giudes on [debugging using a command-line interface](quickstart-AVR-GDB.md) and [debugging using the Arduino IDE 2](quickstart-Arduino-IDE2.md) might be the right entry points for you.

<font color="red">

## Warning

</font>

Please read the [sections about the RESET line requirements](requirements.md#requirements-concerning-the-target-system) before connecting the debugger to a target system. You might very well "brick" your MCU by enabling debugWIRE on a system that does not satisfy these requirements. 
