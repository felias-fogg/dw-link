# An Arduino-based debugWIRE debugger: dw-link

<p align="center">
<img src="https://raw.githubusercontent.com/felias-fogg/dw-link/refs/heads/master/docs/pics/uno-debug2.png" width="40%">
</p>
You want to debug your Arduino project on an ATmega328P or on a classic ATtiny? For this purpose, you need a *hardware debugger*. If you do not own such a hardware debugger but would like to try debugging your Arduino project on the chip nevertheless, then *dw-link* is the right thing for you. It turns an Arduino UNO into a hardware debugger for AVR MCUs which have a debugWIRE debugging interface. Check out the [quickstart guide](https://quickstart.md).

If you are looking for a more durable debugging solution, I recommend [MPLAB SNAP](https://www.microchip.com/en-us/development-tool/pg164100) (or any other EDBG-based Microchip debugger). SNAP is a drop-in replacement for dw-link, and it can do much more than dw-link.
