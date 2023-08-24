# Wish list for dw-link


* design new adapter board
* test prototype of such  a board
* design case for debugger and print it

* a mode where reads & writes are double-checked: "monitor
  verify/noverify". Could be on or off by default.
* perhaps make conditional/repeating breakpoints faster: less register saving/restoring (would give you perhaps 10 ms out of 40 ms), shorter pauses by GDB (but where to control this?)
* integrate programmer into code so that one also could do ISP programming



List of tasks done:

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



