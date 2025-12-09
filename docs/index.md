# An Arduino-based debugWIRE debugger: dw-link

<p align="center">
<img src="https://raw.githubusercontent.com/felias-fogg/dw-link/refs/heads/master/docs/pics/uno-debug2.png" width="40%">
</p>

What can you do when you want to debug your Arduino project on an ATmega328P or on a classic ATtiny? In theory, it should be possible to use *on-chip debugging* via the *debugWIRE* interface, which can be accessed by utilizing a so-called *hardware debugger*. If you do not own such a hardware debugger but would like to try it, then dw-link is the right thing for you. It turns an Arduino UNO into a hardware debugger. It can be used stand-alone (using the symbolic debugger AVR-GDB in a console window), or you can use it as part of the Arduino IDE 2 or other IDEs. 

If you want to give it a try, the two quickstart guides on [debugging using a command-line interface](quickstart-AVR-GDB.md) and [debugging using the Arduino IDE 2](quickstart-Arduino-IDE2.md) might be the right entry points for you.

These days, Microchip has lowered the prices for its hardware debuggers, e.g., [SNAP](https://www.microchip.com/en-us/development-tool/pg164100), so much that it makes sense to buy one of those. Acquire one that uses the EDBG protocol, because then it can be used as a drop-in replacement for dw-link for those of you who already use [PyAvrOCD](https://pyavrocd.io) as an interface between the target chip and the host.
