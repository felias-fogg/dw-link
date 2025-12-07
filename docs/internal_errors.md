If the error number is less than 100, then it is a connection error. Errors above 100 are serious internal debugger errors. 

If you have encountered an internal debugger error, then please try to reproduce the problem and tell me how it happened. 

After a reset or a power cycle of the hardware debugger, everything usually works again. If not, you need to unplug everything and put it together again.


| Error # | Meaning                                                      |
| ------: | ------------------------------------------------------------ |
|       1 | Connection error: Could not communicate by ISP; check wiring |
|       2 | Connection error: Could not activate debugWIRE               |
|       3 | Connection error: MCU type is not supported                  |
|       4 | Connection error: Lock bits or BOOTRST could not be cleared  |
|       5 | Connection error: MCU has PC with stuck-at-one bits          |
|       6 | Connection error: RESET line has a capacitive load           |
|       7 | Connection error: Target not powered or RESET shortened to GND |
|       8 | MCU type does not match                                      |
|       9 | Unknown connection error                                     |
|     101 | No free slot in breakpoint table                             |
|     102 | Packet length too large                                      |
|     103 | Wrong memory type                                            |
|     104 | Packet length is negative                                    |
|     105 | Reset operation failed                                       |
|     106 | Memory address in flash read operation does not point to page start |
|     107 | Could not complete flash read operation                      |
|     108 | Could not complete RAM read operation                        |
|     109 | Memory address in flash write operation does not point to page start |
|     110 | Could not complete flash page erase operation                |
|     111 | Could not load data into the flash buffer for writing        |
|     112 | Error when programming flash page from buffer                |
|     113 | Error verifying flashed page while loading program           |
|     114 | Assignment of hardware breakpoint is inconsistent            |
|     115 | BREAK inserted by debugger at a point where a step or execute operation is required |
|     116 | Trying to read flash word at an uneven address               |
|     117 | Error when single-stepping                                   |
|     118 | A relevant breakpoint has disappeared                        |
|     119 | Input buffer overflow                                        |
|     120 | Wrong fuse                                                   |
|     121 | Breakpoint update while flash programming is active          |
|     122 | Timeout while reading from debugWIRE line                    |
|     123 | Timeout while reading general register                       |
|     124 | Timeout while reading IO register                            |
|     125 | Could not reenable RWW                                       |
|     126 | Failure while reading from EEPROM                            |
|     127 | Bad interrupt                                                |

