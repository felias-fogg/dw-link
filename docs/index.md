# An Arduino-based debugWIRE debugger: dw-link

<p align="center">
<img src="https://raw.githubusercontent.com/felias-fogg/dw-link/refs/heads/master/docs/pics/uno-debug2.png" width="40%">
</p>

What can you do when you want to debug your Arduino project on an ATmega328P or on a classic ATtiny? In theory, it should be possible to use *on-chip debugging* via the *debugWIRE* interface, which can be accessed by utilizing a so-called *hardware debugger*. If you do not own such a hardware debugger but would like to try it, then dw-link is the right thing for you. It turns an Arduino UNO into a hardware debugger. It can be used stand-alone (using the symbolic debugger AVR-GDB in a console window), or you can use it as part of the Arduino IDE 2 or other IDEs. Check out the quickstart guides!

These days, Microchip has lowered the prices for its hardware debugger [SNAP](https://www.microchip.com/en-us/development-tool/pg164100) so much that it makes sense to buy one of those instead of using a DIY solution. When employing the GDB server [PyAvrOCD](https://pyavrocd.io) (perhaps implicitly by using the Arduino IDE 2), SNAP is a drop-in replacement for dw-link, and it can do much more!
