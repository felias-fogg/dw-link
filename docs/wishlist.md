# Wish list for dw-link

##### List of tasks to work on:

* use "blinkmodes.ino" in order to highlight embedded programming, perhaps in a video?

* Implement STK500v2 protocol

* debug tiny13 problem, perhaps by reverting back to the version in
  2022, when it seemed to work

* debug sleep-crash when single- stepping verhindern

* maybe have a variable length break, dependent on the speed of ISP?

* implement semi-hosting

* design case for debugger and print it

* perhaps make conditional/repeating breakpoints faster: less register saving/restoring (would give you perhaps 10 ms out of 40 ms), shorter pauses by GDB (but where to control this?)

* range-stepping implementieren. Sollte wohl nicht so schwierig sein.

  

##### List of tasks done:

* Create Python script dw-gdbserver.py that implements a dw server on
  top of pymcuprog

* Build a V5 version: unified monitor commands and tight integration
  into the script; power-cycling is now only initiated by monitor debugwire enable

* a mode where writes are double-checked: "monitor verify/noverify". Could be on or off by default.

* produce short youtube video to promote dw-link probe

- Write a step-by-step explanation on how to restore an UNO after debugging

- try integration of tool into IDE 2.0 again

- clean up Gede interface

* no multiple main breaks oints

* reloading files in GUI when reloading files

- write a short debug description for PIO

* write/design setup for UNO debugging with connected USB cable

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



**List of tasks abandoned**:

- try to use seer GUI again

- integrate into the Arduino VSC plugin

* reorganize BP management: have a list of stored and a list of new
  BPs, which would save us 3 bytes per BP, i.e., we could easily go from 25 to 30 BPs -- but be careful!



