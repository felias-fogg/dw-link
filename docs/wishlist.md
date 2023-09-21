# Wish list for dw-link

* write/design setup for UNO debugging with connected USB cable

* write a short debug description for PIO

* produce short youtube video to promote dw-link probe

* use "blink modes" in order to highlight embedded programming, perhaps in a video?

* try to use seer GUI again

* clean up Gede interface

  * no multiple main breaks oints
  * reloading files in GUI when reloading files

* Implement SCK500v2 protocol

* try integration of tool into IDE 2.0 again

* debug tiny13 problem, perhaps by reverting back to the version in
  2022, when it seemed to work

* reorganize BP management: have a list of stored and a list of new BPs, which would save us 3 bytes per BP, i.e., we could easily go from 25 to 30 BPs -- but be careful!

* maybe have a variable length break, dependent on the speed of ISP?

* implement semi-hosting

* integrate into the Arduino VSC plugin

* design case for debugger and print it

* a mode where reads & writes are double-checked: "monitor
  verify/noverify". Could be on or off by default.

* perhaps make conditional/repeating breakpoints faster: less register saving/restoring (would give you perhaps 10 ms out of 40 ms), shorter pauses by GDB (but where to control this?)

  



#### List of tasks done:

* rewrite compile-time conditionals so that they do not show up as errors in PIO

* use DEBTX pin (if TXODEBUG is not defined) as a sensing pin to disable automatic DWEN programming, i.e., you have to use mo dw +/- by yourself.

* optimize ispTransfer so that higher ISP rates are possible (currently, 50 kHz is max)

* important (3.1.0): When trying to debug the boarduino at 5V with the probe shield, the feedback
  is: `Wrong Wiring`. It works flawlessly with a lot of other boards (ATtiny,
  UNO, ...), with other shields, with 5V, you name it --> turned out to be that the boarduino had a particularly strong pull-down resistor. That is, the cure is to use 1K or stringer pull-ups on the SPI lines!

* `monitor clock` command that displays the current clock setting --
  well, redesigned all monitor commands! 

* integrate programmer into code so that one also could do ISP programming

* design new adapter board
* test prototype of such  a board

* Adding response in dw-link to one special byte in order to be able to identify the debug serial port
* writing Python function for identifying debug port

* Erasing MCU automatically when lock bits are set
* write hook to set UPLOAD\_PORT and DEBUG\_PORT in extra\_script. 
* Explaining how to recover UNO (soldering + Bootloader burning) in Manual
* remove part about lock bits in Manual
* remove automatic speed recognition
* delete the fancy control stuff in dw-link
* rewrite part in manual about adapter boards
* rewrite part in Manual about PlatformIO



