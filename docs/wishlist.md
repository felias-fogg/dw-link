# Wish list for dw-link


* Adding response in dw-link to one special byte in order to be able to identify the debug serial port
* writing Python function for identifying debug port
* write hook to set UPLOAD\_PORT and DEBUG\_PORT in extra\_script
* rewrite part in Manual about PlatformIO
* design new adapter board
* test prototype of such  a board
* design case for debugger and print it



* a mode where reads & writes are double-checked: "monitor
  verify/noverify". Could be on or off by default.
* perhaps make conditional/repeating breakpoints faster: less register saving/restoring (would give you perhaps 10 ms out of 40 ms), shorter pauses by GDB (but where to control this?)



List of tasks done:

* Erasing MCU when lock bits are set
* Explaining how to recover UNO (SOldering + Bootloader burning) in Manual
* remove part about lock bits in Manual
* remove automatic speed recognition
* delete the fancy control stuff in dw-link
* rewrite part in manual about adapter boards



