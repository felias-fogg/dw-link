# An Arduino-based debugWIRE debug probe: dw-link

![](pics/uno-debug2.png)

You want to debug your Arduino project on an ATmega328P or on a classic ATtiny? For this purpose, you need a *hardware debugger*. If you do not own such a hardware debugger but would like to try debugging your Arduino project on the chip nevertheless, then *dw-link* is the right thing for you. It turns an Arduino UNO into a hardware debugger for AVR MCUs which have a debugWIRE debugging interface. [PyAvrOCD](https://pyavrocd.io) then provides an interface either to avr-gdb or to IDEs like Arduino IDE 2 or PlatformIO. Check out the [quickstart guide](https://quickstart.md).

If you are looking for a more durable debugging solution, I recommend [MPLAB SNAP](https://www.microchip.com/en-us/development-tool/pg164100) (or any other EDBG-based Microchip debugger). SNAP is a drop-in replacement for dw-link, and it can do much more than dw-link.

If you used earlier versions of dw-link, note that with version 7, quite a lot has changed. The debug probe does not speak GDB's *remote serial protocol* any longer, but uses a variant of the mkII protocol to communicate with the GDB server PyAvrOCD. In addition, the packaged programmer no longer uses the STK500v1 protocol, but it is now the ISP programming protocol of the JTAGICE mkII (`jtag2isp` in avrdude's terminology). The benefit is that it all is more robust now and the debugging logic is concentrated at only one point, namely in PyAvrOCD.
