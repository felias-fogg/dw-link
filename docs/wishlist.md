# Wish list for dw-link

* do power-cycling non-blocking, i.e., respond to packets of the GDB in order not to timeout

* optimize ispTransfer so that higher ISP rates are possible (currently, 50 kHz is max)

* use the DEBTX (if not defined) as a sensing pin to disable automatic DWEN programming, i.e., you have to use mo dw +/- by yourself.

* implement semi-hosting

* integrate into the Arduino VSC plugin

* design case for debugger and print it

* a mode where reads & writes are double-checked: "monitor
  verify/noverify". Could be on or off by default.

* perhaps make conditional/repeating breakpoints faster: less register saving/restoring (would give you perhaps 10 ms out of 40 ms), shorter pauses by GDB (but where to control this?)

  



#### List of tasks done:

* Important (3.1.0): When trying to debug the boarduino at 5V with the probe shield, the feedback
  is: `Wrong Wiring`. It works flawlessly with a lot of other boards (ATtiny,
  UNO, ...), with other shields, with 5V, you name it --> turned out to be that the boarduino had a particularly strong pull-down resistor. That is, the cure is to use 1K pull-ups on the SPI lines!

* `monitor clock` command that displays the current clock setting --
  well, redesigned all monitor commands! 

* integrate programmer into code so that one also could do ISP programming

* design new adapter board
* test prototype of such  a board

* Adding response in dw-link to one special byte in order to be able to identify the debug serial port
* writing Python function for identifying debug port

* Erasing MCU when lock bits are set
* write hook to set UPLOAD\_PORT and DEBUG\_PORT in extra\_script. Well, we do nt set the vars, but set the bytes inline
* Explaining how to recover UNO (SOldering + Bootloader burning) in Manual
* remove part about lock bits in Manual
* remove automatic speed recognition
* delete the fancy control stuff in dw-link
* rewrite part in manual about adapter boards
* rewrite part in Manual about PlatformIO



