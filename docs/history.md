# Revision history

### V 1.1 

Initial version

### V 1.2

- Changed pin mapping. The default is now to use ISP pins on the debugger so that a simple ISP cable with broken out RESET line is sufficient. System LED is pin D7, GND for the system LED is provided at pin D6. In order to use the pin mapping for shields/adapters, one has to tie SNSGND to ground, whereby the pin number of SNSGND depends on the Arduino board dw-link is compiled for (see mapping described in [Section 8.3.3](#section833)).
- Added wording to recommend optimization level -O0 instead of -Og, because otherwise assignments to local variables will not work. Single-stepping works now with -Og after dw-link hides all inserted BREAK instructions. 

### V 1.3

- Removed Arduino Mega boards from the set of boards that can be used as hardware debuggers

### V 1.4

- New error messages
- System LED with fewer modes
- Some screen shots added to PlatformIO description

### V 1.5

- New error message (126)
- default DW speed is now 250 kbps

### V 1.6

- New example: Debugging Uno board as target

### V 1.7

- Changes in 8.7 
- Section 9, Problem 'vMustReplyEmpty': timeout - explanation of what problems I encountered
- Section 5.1-5.3 have been reworked, in particular concerning ATTinyCore 2.0.0 and the new Python script for extending the boards.txt files.

### V 1.8

- New help command for monitor commands in 5.7

### V 1.9

- Additional trouble shooting help when lock bits are set

### V 1.10

- Pointed out in Section 4.2 that when debugging an Uno the first time you try to debug it, you need to erase the chip in order to clear the lock bits.
- Added similar wording under trouble shooting

### V 1.11

* fixed some small inconsistencies

### V 2.0

- Removed „lock bit“ error
- Added explanation that lock bits are automatically removed by erasing the entire chip
- Added extra part how to restore UNO functionality 
- Restructured Introduction
- Removed instructions how to modify board and platform files. Now the board definition files are downloaded from my fork.
- Added section 8.11
- More explanation how to start a debugging session using the Arduino IDE
- Reorganized and simplified as much as possible
- Corrected wrong placement in the table about the connections between UNO and ATtiny85
- new monitor command: lasterror
- deleted monitor commands: eraseflash, serial
- added comment about dark system LED
- changed Section 7 in order to describe the V2.0 design
- have thrown out ATtiny13 since it behaves strangely
- added that disabling debugWIRE is now done automatically 
- added dw-server.py
- added description of Gede
- added description of new hardware version
- added that dw-link is now also an ISP programmer
- simplified recovery for UNO

### V 3.0

* Redesign of monitor commands; most of them now take an argument
* Disabling automatic mode switching (Section 2)
* Lowest frequency is now 4kHz (Section 8.7)
* Number of breakpoints reduced from 33 to 25 because of stability problems (when debugging was on)
* New dw-link probe
* Debugging UNO with an active serial connection to the host
* Added problem that stopping at a function might display the location of the inlined function

### V 4.0

* Integration of Arduino IDE 2
* New fatal error: Wrong MCU type (caused by monitor mcu command)
* Renamed fatal error 3
* The *boards manager URLs* have changed: a suffix `_plus_Debug` has been added to the core name.
* Simplified platformio.ini
* Corrected statement about the meaning of BREAK when the debugger is not active
* `monitor mcu` command listed
* Description of how to use the AutoDW jumper added 
* Added a section on how to restore an UNO
* Added a problem section on when the hardware debugger becomes unresponsive
* Added notes that you cannot debug the UNO, but need to select ATmega328
* Added notes about the target board and potentially using external powering
* Edited the problem description for locked up hardware debugger/serial line
* New fatal error: capacitive load on the reset line
* Introduced subsections in the problem section
* Added *monitor runtimers*  command in table
* Added paragraph in problem section how to use *monitor runtimers*
* New section 3.1 about choosing between external powering and powering using the hardware debugger. 
* Changed some section titles in order to make compatible with the TOC generator
* Removed `monitor oscillator` . This is actually something quite dangerous, because it can brick the chip.
* Added new 'S' switch for monitor breakpoint
* Added note that the debugger might recognize capacitive load on the RESET line.

### V 5.0

- No more powering through a data pin, i.e., VSUP on pin D9!
- The only way automatic power-cycling happens now is when the *dw-link probe shield* is installed and the command `monitor debugwire e` is used.
- The only way to enter dW mode is now the command `monitor debugwire enable`. The only way to leave is the command `monitor debugwire disable` (or using the appropiate debugger and issue a `Burn Bootloader` command).
- Changed and extended monitor commands in order to make it compatible with dw-gdbserver
- Thrown out Sections 5 & 7. Will be handeled in dw-gdbserver setup instructions
- New fatal error 113: *Error verifying flashed page while loading program*
- ATtiny13 support

### V 5.1
- Completely reworked the manual to put it into the MkDocs format.

### V 5.2

- Adapted to new package indices in the quickstart and installation sections
