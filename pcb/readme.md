# dw-link probe: the hardware for dw-link

<a rel="license" href="http://creativecommons.org/licenses/by/4.0/"><img alt="Creative Commons License" style="border-width:0" src="https://i.creativecommons.org/l/by/4.0/88x31.png" /></a><br />This work is licensed under a <a rel="license" href="http://creativecommons.org/licenses/by/4.0/">Creative Commons Attribution 4.0 International License</a>.

When you want to debug  your projects using dw-link more than once, it is a good idea to have a hardware solution. For instance, an Arduino shield. As described in the manual, you can just assemble a solution on a prototype board, which works for 5 V projects with less than 20 mA of supply current. 

The current design of the *dw-link probe* is more ambitious, but far from an industrial-strength hardware debugging tool as, e.g., Atmel ICE. The limitations for the target boards are:

* 3.3 to 5 V supply voltage (but not lower),
* reasonable loads on the SPI lines, e.g., an LED and a 1 kΩ series resistor (but no stronger resistor),
* no capactive load on the RESET line of the target,
* no loads higher than a 10 kΩ resistance on the RESET line,
* minimum MCU clock frequency of 16 kHz.

If these constraints are met, the dw-link probe should work.  Otherwise, one might encounter strange behavior and/or error messages.

The current version (V3.0) of the dw-link probe (an Arduino UNO shield) uses mainly THT components, which should make it easy to assemble it. The two SMD components, two MOS-FETs, can also be hand soldered to the PCB since the solder pads are extra large. 

**Bill of materials**

| ID                           | Value                              | Number |
| ---------------------------- | ---------------------------------- | ------ |
| PCB                          | dw-link probe V3.0                 | 1      |
| Q1, Q2                       | IRLML6402                          | 2      |
| R1                           | 1 kΩ (brown, black, red)           | 1      |
| R2, R5, R6                   | 10 kΩ (brown, black, orange)       | 3      |
| R3, R4                       | 220 Ω (red, red, brown)            | 2      |
| R7, R8                       | 680 Ω (blue, grey brown)           | 2      |
| R9, R10                      | 10 MΩ (brown, black, blue)         | 2      |
| C1, C3                       | 10 µF (polarity must be matched)   | 2      |
| C2                           | 100 nF                             | 1      |
| D1                           | LED 3mm (polarity must be matched) | 1      |
| U1                           | LD1117V33                          | 1      |
| SW1                          | Tactile push button                | 1      |
| JP1                          | ISP pin header (2x6)               | 1      |
| PULLUP1, AUTO_DW1            | Pin header 1x3                     | 2      |
| Supply1                      | Pin header 1x4                     | 1      |
| Jumper for the above headers | Jumper                             | 3      |
| IOH1, IOL1                   | Pin Header 1x8                     | 2      |
| POWER1, AD1                  | Pin Header 1x6                     | 2      |

When assembling the kit, just follow what is specified in the table. Start from the top and go down. In case, you do not know  the color codes for resistors by heart, try this [online calculator](https://www.allaboutcircuits.com/tools/resistor-color-code-calculator/). For the capacitors C1 and C3 and the LED, watch out for the polarity, which should be matched. When soldering the connectors to the board, it is a good idea to do that when the plugs are inserted into the sockets of the Arduino UNO. 

If you never have soldered SMD parts before: It is not rocket science. You need to pre-solder one pad, take the SMD part with a pair of tweezers, and solder the part to the pre-soldered pad. Afterwards solder the remaining legs to the respective pads. There are numerous tutorials out there. I think, the [HowTo by JRE](https://josepheoff.github.io/posts/howtosolder-11soldersmdpassive) is very detailed and easy to follow. 

In order to test the basic functionality of the freshly assembled board, you'll find the sketch `testprobe.ino` in the repository.

Note that the board disables the auto-reset capability of the UNO board. That is, when the shield is plugged in, you cannot upload any sketch to the UNO. Unplug the shield and try again! 
