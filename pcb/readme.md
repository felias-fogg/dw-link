# The *dw-link probe*: Hardware for dw-link

<a rel="license" href="http://creativecommons.org/licenses/by/4.0/"><img alt="Creative Commons License" style="border-width:0" src="https://i.creativecommons.org/l/by/4.0/88x31.png" /></a><br />This work is licensed under a <a rel="license" href="http://creativecommons.org/licenses/by/4.0/">Creative Commons Attribution 4.0 International License</a>.

When you want to debug your projects using dw-link more than once, it is a good idea to have a hardware solution. For instance, an Arduino shield. As described in the manual, you can just assemble a solution on a prototype board, which works for 5 V projects with less than 20 mA of supply current. 

The current design of the *dw-link probe* is more ambitious, but far from an industrial-strength hardware debugging tool as, e.g., Atmel ICE. The limitations for the target boards are:

* 3.3 to 5 V supply voltage (but not lower),
* reasonable loads on the SPI lines, e.g., an LED and a 1 kΩ series resistor (but no stronger resistor),
* no capacitive load on the RESET line of the target,
* no loads higher than a 10 kΩ resistance on the RESET line,
* minimum target MCU clock frequency of 4 kHz.

If these constraints are met, the dw-link probe should work.  Otherwise, one might encounter strange behavior and/or error messages. 

The current version (V3.1) of the dw-link probe (an Arduino UNO shield) uses mainly THT components, which should make it easy to assemble it using [these instructions](assembly.md). The three SMD components can also be hand-soldered to the PCB since the solder pads are extra large. 

