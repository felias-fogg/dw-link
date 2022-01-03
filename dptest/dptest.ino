// sketch for testing dw-probe

#define VERSION "0.9.0"

#define NANOVERSION 3


#if defined(ARDUINO_AVR_UNO)
#define ID      "UNO"
#define VHIGH   2        // switch, low signals that one should use the 5V supply
#define VON     5        // switch, low signals that dw-probe should deliver the supply charge
#define V5      9        // a low level switches the MOSFET for 5 volt on 
#define V33     7        // a low level switches the MOSFET for 3.3 volt on 
#define VSUP    9        // Vcc - direct supply charge (limit it to 20-30 mA!)
#define SNSGND 14        // If low, then we use a shield
#define DWLINE  8        // RESET (needs to be 8 so that we can use it as an input for TIMER1)
#define DSCK    12        // SCK
#define DMOSI   10        // MOSI
#define DMISO   11        // MISO
#define DEBTX   3        // TX line for TXOnlySerial
#define ISPROG  6        // if low, signals that one wants to use the ISP programming feature
// System LED = Arduino pin 13
#define LEDDDR  DDRB     // DDR of system LED
#define LEDPORT PORTB    // port register of system LED
#define LEDPIN  PB5      // pin (=D13)
//-----------------------------------------------------------
#elif defined(ARDUINO_AVR_LEONARDO)
#define ID      "LEONARDO"
#define VHIGH   2        // switch, low signals that one should use the 5V supply
#define VON     5        // switch, low signals that dw-probe should deliver the supply charge
#define V5      9        // a low level switches the MOSFET for 5 volt on 
#define V33     7        // a low level switches the MOSFET for 3.3 volt on 
#define VSUP    9        // Vcc - direct supply charge (limit it to 20-30 mA!)
#define SNSGND 18        // If low, then we use a shield
#define DWLINE  4        // RESET (needs to be 4 (for Mega32U4) so that we can use it as an input for TIMER1)
#define DSCK    12        // SCK
#define DMOSI   10        // MOSI
#define DMISO   11        // MISO
#define DEBTX   3        // TX line for TXOnlySerial
#define ISPROG  6        // if low, signals that one wants to use the ISP programming feature
// System LED = Arduino pin 13
#define LEDDDR  DDRC     // DDR of system LED
#define LEDPORT PORTC    // port register of system LED
#define LEDPIN  PC7      // pin (=D13)
//-----------------------------------------------------------
#elif defined(ARDUINO_AVR_MEGA2560)
#define ID      "MEGA"
#define VHIGH   2        // switch, low signals that one should use the 5V supply
#define VON     5        // switch, low signals that dw-probe should deliver the supply charge
#define V5      9        // a low level switches the MOSFET for 5 volt on 
#define V33     7        // a low level switches the MOSFET for 3.3 volt on 
#define VSUP    9        // Vcc - direct supply charge (limit it to 20-30 mA!)
#define SNSGND 54        // If low, then we use a shield
#define DWLINE 49        // RESET line 
#define DSCK    12        // SCK
#define DMOSI   10        // MOSI
#define DMISO   11        // MISO
#define DEBTX   3        // TX line for TXOnlySerial
#define ISPROG  6        // if low, signals that one wants to use the ISP programming feature
// System LED = Arduino pin 13
#define LEDDDR  DDRB     // DDR of system LED
#define LEDPORT PORTB    // port register of system LED
#define LEDPIN  PB7      // pin (=D13)
//-----------------------------------------------------------
#elif defined(ARDUINO_AVR_NANO)  // on Nano board -- is aligned with Pro Mini and Pro Micro
#define VHIGH   7        // switch, low signals that one should use the 5V supply
#define VON    15        // switch, low signals that dw-probe should deliver the supply charge
#define V33     5        // a low level switches the MOSFET for 3.3 volt on 
#define V5      6        // a low level switches the MOSFET for 5 volt on 
#define VSUP    6        // Vcc - direct supply charge (limit it to 20-30 mA!)
#define SNSGND 11        // If low, then we are on the adapter board
#define DWLINE  8        // RESET (needs to be 8 so that we can use it as an input for TIMER1)
#define DSCK     3        // SCK
#define ISPROG  2        // if low, signals that one wants to use the ISP programming feature
#if NANOVERSION == 3
#define ID      "NANO3"
#define DMOSI   16        // MOSI
#define DMISO   19        // MISO
#define DEBTX  18        // TX line for TXOnlySerial
#else
#define ID      "NANO2"
#define DMOSI   19        // MOSI
#define DMISO   16        // MISO
#define DEBTX  17        // TX line for TXOnlySerial
#endif
// System LED = Arduino pin 13 (builtin LED) (pin TX0 on Pro Micro/Mini)
#define LEDDDR  DDRB     // DDR of system LED
#define LEDPORT PORTB    // port register of system LED
#define LEDPIN  PB5      // Arduino pin 13
//-----------------------------------------------------------
#elif defined(ARDUINO_AVR_PRO)  // on a Pro Mini board
#define ID      "PRO"
#define VHIGH  16        // switch, low signals that one should use the 5V supply
#define VON     2        // switch, low signals tha dw-probe should deliver the supply charge
#define V33    14        // a low level switches the MOSFET for 3.3 volt on 
#define V5     15        // a low level switches the MOSFET for 5 volt on 
#define VSUP   15        // Vcc - direct supply charge (limit it to 20-30 mA!)
#define SNSGND 10        // If low, then we are on the adapter board
#define DWLINE  8        // RESET (needs to be 8 so that we can use it as an input for TIMER1)
#define DSCK    12        // SCK
#define DMOSI    3        // MOSI
#define DMISO    6        // MISO
#define DEBTX   5        // TX line for TXOnlySerial
#define ISPROG   11        // if low, signals that one wants to use the ISP programming feature
// System LED = Arduino pin 13 (builtin LED) (pin D4 on Nano and D15 on Pro Micro)
//#define LEDDDR  DDRC     // DDR of system LED
//#define LEDPORT PORTC    // port register of system LED
//#define LEDPIN  PC7      // not connected to the outside world!
//-----------------------------------------------------------
#elif defined(ARDUINO_AVR_PROMICRO)  // Pro Micro, i.e., that is a Mega 32U4
#define ID      "PRO MICRO"
#define VHIGH  20        // switch, low signals that one should use the 5V supply
#define VON     2        // switch, low signals tha dw-probe should deliver the supply charge
#define V33    18        // a low level switches the MOSFET for 3.3 volt on 
#define V5     19        // a low level switches the MOSFET for 5 volt on 
#define VSUP   19        // Vcc - direct supply charge (limit it to 20-30 mA!)
#define SNSGND 10        // If low, then we are on the adapter board
#define DWLINE  4        // RESET (needs to be 4 (for Mega32U4) so that we can use it as an input for TIMER1)
#define DSCK    14        // SCK
#define DMOSI    3        // MOSI
#define DMISO    6        // MISO
#define DEBTX   5        // TX line for TXOnlySerial
#define ISPROG   16        // if low, signals that one wants to use the ISP programming feature
// System LED = Arduino pin 17 (RXLED) (connected to RXI, which is not connected to anything else)
#define LEDDDR  DDRB     // DDR of system LED
#define LEDPORT PORTB    // port register of system LED
#define LEDPIN  PB0      // Arduino pin 17
//-----------------------------------------------------------
#elif defined(ARDUINO_AVR_MICRO)  // Micro, i.e., that is a Mega 32U4
#define ID      "MICRO"
#define VHIGH   7        // switch, low signals that one should use the 5V supply
#define VON    19        // switch, low signals tha dw-probe should deliver the supply charge
#define V33     5        // a low level switches the MOSFET for 3.3 volt on 
#define V5      6        // a low level switches the MOSFET for 5 volt on 
#define VSUP    6        // Vcc - direct supply charge (limit it to 20-30 mA!)
#define SNSGND 11        // If low, then we are on the adapter board
#define DWLINE  4        // RESET (needs to be 4 (for Mega32U4) so that we can use it as an input for TIMER1)
#define DSCK     3        // SCK
#define DMOSI   20        // MOSI
#define DMISO   23        // MISO
#define DEBTX  22        // TX line for TXOnlySerial
#define ISPROG    2        // if low, signals that one wants to use the ISP programming feature
// System LED = Arduino pin 17 (RXLED) (connected to RXI, which is not connected to anything else)
#define LEDDDR  DDRB     // DDR of system LED
#define LEDPORT PORTB    // port register of system LED
#define LEDPIN  PB0      // Arduino pin 17
//-----------------------------------------------------------#else
#error "Board is not supported yet. dw-probe works only on Uno, Leonardo, Mega, Nano, Pro Mini, Micro, and Pro Micro (yet)" 
#endif

void setup() {
  Serial.begin(115200);
  Serial.println(F("\nTesting dw-probe " VERSION " ..."));
  digitalWrite(ISPROG, HIGH);
  pinMode(ISPROG, OUTPUT);
  digitalWrite(DMOSI, HIGH);
  pinMode(DMOSI, OUTPUT);
  digitalWrite(DSCK, HIGH);
  pinMode(DSCK, OUTPUT);
  pinMode(VON, INPUT_PULLUP);
  pinMode(VHIGH, INPUT_PULLUP);
  pinMode(DMISO, INPUT);
}

void loop() {
  char c;
  if (Serial.available()) {
    c = Serial.read();
    if (c <= ' ') return;
    Serial.println(c);
    switch(c) {
    case 'h':
    case 'H':
      printHelp();
      break;
    case '?':
      printState();
      break;
    case '3':
      pinMode(V5,INPUT);
      pinMode(V33,OUTPUT);
      Serial.println(F("3.3 V supply switched on"));
      break;
    case '5':
      pinMode(V33,INPUT);
      pinMode(V5,OUTPUT);
      Serial.println(F("5 V supply switched on"));
      break;
    case '0':
      pinMode(V5,INPUT);
      pinMode(V33,INPUT);
      Serial.println(F("Supply switched off"));
      break;
    case 'R':
      pinMode(DWLINE, INPUT);
      Serial.println(F("RESET line is high/input"));
      break;
    case 'r':
      pinMode(DWLINE, OUTPUT);
      Serial.println(F("RESET line is low/output"));
      break;
    case 'I':
      digitalWrite(ISPROG, LOW);
      Serial.println(F("ISP is enabled"));
      break;
    case 'i':
      digitalWrite(ISPROG, HIGH);
      Serial.println(F("ISP is disabled"));
      break;
    case 'O':
      digitalWrite(DMOSI, HIGH);
      Serial.println(F("MOSI is HIGH"));
      break;
    case 'o':
      digitalWrite(DMOSI, LOW);
      Serial.println(F("MOSI is LOW"));
      break;
    case 'S':
      digitalWrite(DSCK, HIGH);
      Serial.println(F("SCK is HIGH"));
      break;
    case 's':
      digitalWrite(DSCK, LOW);
      Serial.println(F("SCK is LOW"));
      break;
    default:
      Serial.print(F("Unknown command: '"));
      Serial.print(c);
      Serial.println("'");
      break;
    }
  }
}

void printHelp() {
  Serial.print(F("h/H - print this text\n" \
		 "?   - print state of input lines\n" \
		 "3   - switch 3.3 V supply on\n" \
		 "5   - switch 5.0 V supply on\n" \
		 "0   - switch supply off\n" \
		 "R/r - set RESET HIGH/input or LOW/output\n" \
		 "I/i - enable/disable ISP outputs\n" \
		 "O/o - set MOSI HIGH/LOW\n" \
		 "S/s - set SCK HIGH/LOW\n"));
}

void printState() {
  Serial.print(F("SNSGND: "));
  Serial.print(digitalRead(SNSGND));  
  Serial.print(F(", VON: "));
  Serial.print(digitalRead(VON));
  Serial.print(F(", VHIGH: "));
  Serial.print(digitalRead(VHIGH));
  Serial.print(F(", MISO: "));
  Serial.print(digitalRead(DMISO));
  Serial.print(F(", MOSI: "));
  Serial.print(digitalRead(DMOSI));
  Serial.print(F(", DW: "));
  Serial.println(digitalRead(DWLINE));
}
      
