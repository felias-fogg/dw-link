// tests a dw-link probe
#define VERSION "1.5.0"

// pins
const byte IVSUP = 2;
const byte AUTODWSENSE = 3;
const byte TISP = 4;
const byte SENSEBOARD = 5;
const byte SYSLED = 7;
const byte DWLINE = 8;
const byte TMOSI = 11;
const byte TMISO = 12;
const byte TSCK = 10; // was D13
const byte TARMOSI = A3; // was A3
const byte TARSCK = A2; // was A4
const byte TARRES = A5; //was A2
const byte TARSUP = A4; // was A5

const byte MAXCTRL = 3;

byte ctrls[MAXCTRL] = {  TMOSI, TMISO, TSCK }; 

void setup()
{
  Serial.begin(115200);
  Serial.println(F("\nTesting dw-link-probe " VERSION));
  pinMode(SYSLED, OUTPUT);
  pinMode(TMISO, OUTPUT);
  // initially switch the two SPI lines and the RESET line to GND in order to discharge any stray capacitance
  pinMode(TARMOSI, OUTPUT);
  pinMode(TARSCK, OUTPUT);
  pinMode(DWLINE, OUTPUT);
  delay(100);
  pinMode(DWLINE, INPUT);
  pinMode(TARMOSI,INPUT);
  pinMode(TARSCK,INPUT);
  pinMode(SENSEBOARD, INPUT_PULLUP);
  pinMode(AUTODWSENSE, INPUT_PULLUP);
  printHelp();
}

void loop()
{
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
      printOutputState();
      break;
    case '!':
      printInputState();
      break;
    case 'L':
      digitalWrite(SYSLED,HIGH);
      break;
    case 'l':
      digitalWrite(SYSLED,LOW);
      break;
    case 'I':
      digitalWrite(TMISO,HIGH);
      break;
    case 'i':
      digitalWrite(TMISO,LOW);
      break;
    case 'P':
      activate(IVSUP);
      break;
    case 'p':
      deactivate(IVSUP);
      break;
    case 'D':
      activate(DWLINE);
      break;
    case 'd':
      deactivate(DWLINE);
      break;
    case 'R':
      activate(TARRES);
      break;
    case 'r':
      deactivate(TARRES);
      break;
    case 'S':
      activate(TISP);
      break;
    case 's':
      deactivate(TISP);
      break;
    case 'O':
      activate(TMOSI);
      break;
    case 'o':
      deactivate(TMOSI);
      break;
    case 'C':
      activate(TSCK);
      break;
    case 'c':
      deactivate(TSCK);
      break;
    case 't':
      testSequence();
      break;
    }
  }
}

      
void printHelp()
{
  Serial.print(F("H/h - print this text\n" \
		 "!/? - print state of input/output lines\n" \
		 "L/l - switch LED on/off\n" \
		 "P/p - switch power supply on/off\n" \
		 "D/d - set DW to LOW(active)/HIGH(inactive)\n" \
		 "R/r - set RESET(target) LOW(active)/HIGH(inactive)\n" \
		 "S/s - enable/disable SPI \n" \
		 "O/o - set MOSI LOW(active)/HIGH(inactive)\n" \
		 "I/i - set MISO LOW(active)/HIGH(inactive)\n" \
		 "C/c - set SCK LOW(active)/HIGH(inactive)\n" \
		 "t   - run a test sequence\n"));
}

static inline void activate(byte pin)
{
  pinMode(pin, OUTPUT);
}

static inline void deactivate(byte pin)
{
  pinMode(pin, INPUT);
}

void printInputState()
{
  Serial.print(F("SYSLED (7):    "));
  Serial.println(digitalRead(SYSLED));
  Serial.print(F("MISO (12):     "));
  Serial.println(digitalRead(TMISO));
  Serial.print(F("Power(2):      "));
  Serial.println(getState(IVSUP));
  Serial.print(F("DW(8):         "));
  Serial.println(getState(DWLINE));
  Serial.print(F("RESET(Target): "));
  Serial.println(getState(TARRES));
  Serial.print(F("SPI (4):       "));
  Serial.println(getState(TISP));
  Serial.print(F("MOSI (11):     "));
  Serial.println(getState(TMOSI));
  Serial.print(F("SCK (13):      "));
  Serial.println(getState(TSCK));
}

void printOutputState()
{
  Serial.print(F("Vcc(Target):   "));
  Serial.print(getLevel(TARSUP));
  Serial.print(F(" mV\nMOSI(Target):  "));
  Serial.print(getLevel(TARMOSI));
  Serial.print(F(" mV\nMSCK(Target):  "));
  Serial.print(getLevel(TARSCK));
  Serial.print(F(" mV\nRESET(Target): "));
  Serial.print(getLevel(TARRES));
  Serial.print(F(" mV\nDW(8):         "));
  if (getState(DWLINE) == 'I') Serial.println(digitalRead(DWLINE));
  else Serial.println("?");
  Serial.print(F("AUTODWSENSE(3):"));
  Serial.println(digitalRead(AUTODWSENSE));
}
  


char getState(uint8_t pin)
{
  uint8_t bit = digitalPinToBitMask(pin);
  uint8_t port = digitalPinToPort(pin);
  volatile uint8_t *reg, *out;

  if (port == NOT_A_PIN) return 0xFF;

  reg = portModeRegister(port);
  out = portOutputRegister(port);
  
  //Check for tris (DDxn)
  if( (*reg) & bit ){ //is output
    if ( (*out) & bit )
      return '1'; // is logical '1'
    else 
      return 'A'; // active
  }
  else{ //is input
    //Check state (PORTxn)
    if( (*out) & bit )  //is set
      return 'P'; // inactive with activated pullups
    else //is clear
      return 'I'; // inactive
    }
}

int getLevel(byte pin)
{
  int level = (analogRead(pin)*10)/2;
  return level;
}
		     
int getDisLev(byte pin) {
  int level =  (analogRead(pin)*10)/2;

  if (level < 1000) return 0;
  else if (level > 3000 && level < 3700) return 3;
  else if (level > 4500) return 5;
  else return -1;
}

void testSequence(void) {
  int lev;
  Serial.println(F("Starting test sequence ..."));
  
  Serial.println(F("  Checking SENSEBOARD ..."));  
  if (digitalRead(SENSEBOARD) != LOW) {
    Serial.println(F("     ERROR: SENSEBOARD(5) not tied to ground"));
    return;
  } else {
    Serial.println(F("     OK"));
  }

  // switch off everything
  digitalWrite(SYSLED, LOW);
  deactivate(IVSUP);
  deactivate(TISP);
  deactivate(DWLINE);
  deactivate(TMOSI);
  deactivate(TSCK);
  Serial.println(F("  Checking everything off ..."));
  delay(600);
  if (getDisLev(TARMOSI) == 0 && getDisLev(TARSCK) == 0 && getDisLev(TARRES) == 0 && getDisLev(TARSUP) == 0)
    Serial.println(F("     OK"));
  else {
    Serial.println(F("     ERROR"));
    return;
  }
  
  Serial.println(F("  Checking for shortages on SPI lines ..."));
  for (byte c = 0; c < MAXCTRL; c++)
    pinMode(ctrls[c], INPUT_PULLUP);
  for (byte c = 0; c < MAXCTRL; c++) {
    digitalWrite(ctrls[c], LOW);
    pinMode(ctrls[c], OUTPUT);
    delay(2);
    for (byte d = 0; d < MAXCTRL; d++) {
      if (c != d) {
	if (digitalRead(ctrls[d]) == LOW) {
	  Serial.print(F("     ERROR: apparent shortage between "));
	  Serial.print(ctrls[c]);
	  Serial.print(F(" and "));
	  Serial.print(ctrls[d]);
	  for (byte c = 0; c < MAXCTRL; c++) {
	    digitalWrite(ctrls[c], LOW);
	    pinMode(ctrls[c], INPUT);
	  }
	  return;
	}
      }
    }
    pinMode(ctrls[c], INPUT_PULLUP);
  }
  for (byte c = 0; c < MAXCTRL; c++) {
    digitalWrite(ctrls[c], LOW);
    pinMode(ctrls[c], INPUT);
  }
  Serial.println(F("     OK"));

  
  Serial.println(F("  Checking supply ..."));
  activate(IVSUP);
  delay(600);
  lev = getDisLev(TARSUP);
#if 0
  Serial.println(lev);
  Serial.println(getDisLev(TARMOSI));
  Serial.println(getDisLev(TARSCK));
  Serial.println(getDisLev(TARRES));
  Serial.println(getDisLev(TARSUP));
#endif
  if (getDisLev(TARMOSI) == 0 && getDisLev(TARSCK) == 0 && getDisLev(TARRES) == lev && getDisLev(TARSUP) > 0) {
    if (lev == 3) 
      Serial.println(F("     3.3 V detected"));
    else if (lev == 5)
      Serial.println(F("     5 V detected"));
    Serial.println(F("     OK"));
  } else {
    Serial.println(F("     ERROR"));
    return;
  }
  Serial.println(F("  Checking SPI pull-ups ..."));
  activate(TISP);
  delay(100);
  if (getDisLev(TARMOSI) == lev && getDisLev(TARSCK) == lev && getDisLev(TARRES) == lev && getDisLev(TARSUP) > 0)
    Serial.println(F("     OK"));
  else {
    Serial.println(F("     ERROR"));
    return;
  }
  Serial.println(F("  Checking SCK ..."));
  activate(TSCK);
  delay(100);
  if (getDisLev(TARMOSI) == lev && getDisLev(TARSCK) == 0 && getDisLev(TARRES) == lev && getDisLev(TARSUP) > 0)
    Serial.println(F("     OK"));
  else {
    Serial.println(F("     ERROR"));
    return;
  }
  Serial.println(F("  Checking MOSI ..."));
  deactivate(TSCK);
  activate(TMOSI);
  delay(100);
  if (getDisLev(TARMOSI) == 0 && getDisLev(TARSCK) == lev && getDisLev(TARRES) == lev && getDisLev(TARSUP) > 0)
    Serial.println(F("     OK"));
  else {
    Serial.println(F("     ERROR"));
    return;
  }
  Serial.println(F("  Checking DWLINE output ..."));
  deactivate(TSCK);
  deactivate(TMOSI);
  activate(DWLINE);
  delay(100);
  if (getDisLev(TARMOSI) == lev && getDisLev(TARSCK) == lev && getDisLev(TARRES) == 0 && getDisLev(TARSUP) > 0)
    Serial.println(F("     OK"));
  else {
    Serial.println(F("     ERROR"));
    return;
  }
  Serial.println(F("  Checking DWLINE input ..."));
  deactivate(DWLINE);
  activate(TARRES);
  delay(100);
  if (getDisLev(TARMOSI) == lev && getDisLev(TARSCK) == lev && getDisLev(TARRES) == 0 && getDisLev(TARSUP) > 0 && digitalRead(DWLINE) == 0)
    Serial.println(F("     OK"));
  else {
    Serial.println(F("     ERROR"));
    return;
  }
  deactivate(TARRES);
  deactivate(DWLINE);
  Serial.println(F("  Switching on SYSLED ..."));
  digitalWrite(SYSLED,HIGH);
  Serial.println(F("... Test sequence finished"));
}
  
  
