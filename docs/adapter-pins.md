# Pin mapping of the adapter board

dw-probe | Micro | Nano V2 | Nano V3 | Pro Micro | Pro Mini | # lft | # rgt | Pro Mini | Pro Micro | Nano V2+3 | Micro| dw-probe
--- | --- | --- |  --- | --- | --- | --- | --- | --- | --- | --- | --- | ---
NC | D13 | D13 | D13 | TXO | TXO | 1 | 34 | Raw | Raw | D12 | D12 | NC
(**DW-LINE**) | 3.3 V | 3.3 V | 3.3 V | *RXI* | RXI | 2 | 33 | *GND* | *GND* | *D11* | *D11* | **GND/ SNS** 
NC | REF | REF | REF | GND | RST | 3 | 32 | RST | RST | D10 | D10 | **RST** 
NC | A0= D18 | A7 | A0= D14 | *GND* | *GND* | 4 | 31 | *Vcc* | *Vcc* | <del>D9</del> | <del>D9</del> | **Vcc**
**VON** | A1= *D19* | <del>A6</del> | A1= *D15* | *D2* | *D2* | 5 | 30 | <del>A3= D17</del> | <del>A3= D17</del> | *D8* | <del>D8</del> | **DW- LINE**
TMOSI | A2= D20| A5= D19 | A2= D16 | D3 | D3 | 6 | 29 | A2= D16 | A2= D20 | D7 | D7 | VHIGH
**DW- LINE** | <del>A3= D21</del>| <del>A4= D18</del> | <del>A3= D17</del> | *D4* | <del>D4</del> | 7 | 28 | A1= D15 | A1= D19 | D6 | D6 | V5/ VSUP
DEB- TX | A4= D22| A3= D17 | A4= D18 | D5 | D5 | 8 | 27 | A0= D14 | A0= D18 | D5 | D5 | V33
TMISO | A5= D23| A2= D16 | A5= D19 | D6 | D6 | 9 | 26 | <del>D13</del> | <del>D15</del> | <del>D4</del> | *D4* | (**DW-LINE**) 
**VON** |NC| A1= *D15* | <del>A6</del> | <del>D7</del> | <del>D7</del> | 10 | 25 | D12 | D14 | D3 | D3 | TSCK
**DW- LINE** |NC| <del>A0= D14</del> | <del>A7</del> | <del>D8</del> | *D8* | 11 | 24 | D11 | D16 | D2 | D2 | TISP 
**Vcc** | *5V* | *5V* | *5V* | <del>D9</del> | <del>D9</del> | 12 | 23 | *D10* | *D10* | *GND* | *GND* | **GND/ SNS**
NC | RST | RST | RST |    |    | 13 | 22 |    |    | RST | RST | **RST** 
NC | GND | GND | GND |    |    | 14 | 21 |    |    | RXO | RXO | **DW-LINE** 
NC | Vin | Vin | Vin |    |    | 15 | 20 |    |    | TXI | TXI | NC 
NC | TMISO | | | | | 16 | 19 | | | | SS |NC
NC | TSCK  | | | | | 17 | 18| | | | TMOSI|NC

The bold pin names signal that they occur on different pins and have to be connected. For example, **VON** needs to be at board pin 5 for all boards except the Nano V2 and on board pin 1 for the Nano V2. In case, you wonder what the pin names mean:

Pin name | Direction | Explanation
--- | --- | ---
DEBTX | Output | Serial line for debugging output (when debugging the debugger) 
DWLINE | Input/Output | debugWIRE communication line to be connected to RESET on the target board
GND | Supply | Ground
TMISO | Input | SPI signal "Master In, Slave Out"
TMOSI | Output | SPI signal "Master Out, Slave In"
TISP | Output | If low, enables output of ISP buffers to target
TSCK | Output | SPI signal "Master clock"
SNS | Input | If low, signals that the debugger board sits on the adapter board
V33 | Output | Control line to the MOSFET to switch on 3.3 volt supply for target
V5  | Output | Control line to switch on the 5 volt line
Vcc | Supply | Voltage supply from the board (5 V) that can be used to power the target
VHIGH | Input | If low, then choose 5 V supply for target, otherwise 3.3 V 
VON | Input | If low, then supply target (and use power-cycling)
VSUP | Output | Used as a target supply line driven directly by an ATmega pin, which is only active if the debugger board does not sit on the adapter board, i.e., if SNS=open
RST | Control | Common reset line, can be connected via a 10µF cap to ground to prohibit autoresest 

​          

Note that the 32U4 based boards do not seem to work with the dw-link -- and it is not clear how proceed. So, for now,  we stick to the non-USB-MCU based boards.
