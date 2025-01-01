# Wish list for dw-link

##### List of unsolved issues:

- Write a section on how to restore an UNO after debugging

* When trying to debug the UNO board clone with the problematic Atmega16U2 that I have, a lot of strange things happen:
  * With the real UNO board, we cannot even execute the target remote command.
    * dw-link, seems to hang, the LA does not show any activity on the serial lines
    * the get remote debug reports that the packages are sent but nothing is replied
    * even when connecting to another board and resetting, this state continues
    * only disconnecting the USB line and reconnecting resets the connection between the UNO and the host
  * Works much better with the SEEEduino 4.2, however here it can happen:
    * the target MCU is only halfway into dW mode, i.e., neither ISP works nor do we get an 'U' after a break.
  * With the AZdelivery board
    * I have seen problem in uploading the firmware
    * I also saw the same problem as with the real UNO and I also saw a restart for no good reason.
    * So, this is probably not a good start at all.
  * The UNO compatible board by AZdelivery did not work at all.
  * In summary, I do not recommend to use AZdelivery UNO compatible boards
* Perhaps, this is just a problem with this board. However, it would still be nice to know why these things can happen at all. I currently do not have the slightest idea.

##### List of tasks to work on:

* produce short youtube video to promote dw-link probe

* use "blinkmodes.ino" in order to highlight embedded programming, perhaps in a video?

* Implement STK500v2 protocol

* debug tiny13 problem, perhaps by reverting back to the version in
  2022, when it seemed to work

* reorganize BP management: have a list of stored and a list of new BPs, which would save us 3 bytes per BP, i.e., we could easily go from 25 to 30 BPs -- but be careful!

* maybe have a variable length break, dependent on the speed of ISP?

* implement semi-hosting

* design case for debugger and print it

* a mode where reads & writes are double-checked: "monitor
  verify/noverify". Could be on or off by default.

* perhaps make conditional/repeating breakpoints faster: less register saving/restoring (would give you perhaps 10 ms out of 40 ms), shorter pauses by GDB (but where to control this?)

  

##### List of tasks done:

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



