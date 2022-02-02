// This is a modified blink sketch. It has four different modes:
// off, on, slow blinking, fast blinking. You can cycle through these
// modes by pressing a button. The blinking is implemented by using
// the "Timer/Counter0 Compare Match A" interrupt.

#define LEDPIN LED_BUILTIN
#define BUTTON 12

byte counter = 0;
volatile byte mode = 0;

void setup() {
  pinMode(LEDPIN, OUTPUT);           // initialize LEDPIN 
  pinMode(BUTTON, OUTPUT);           // initialize button 
  TIMSK0 |= _BV(OCIE0A);             // enable Timer0 Compare Match A interrupt
  OCR0A = 0x80;                      // set interrupt time 
}

void loop() {
  if (digitalRead(BUTTON) == 1) {    // button pressed
    mode = (mode+1) % 4;             // go to next mode
    delay(20);                       // debounce
    while (digitalRead(BUTTON) == 1);// wait for release
    delay(20);                       // debounce
  }
}

ISR(TIMER0_COMPA_vect) {              // timer ISR
  counter++;                          // advance counter
  if (mode == 0)
    digitalWrite(LEDPIN, 0);          // in mode 0, switch LED off
  else if (mode == 1)
    digitalWrite(LEDPIN, 1);          // in mode 1, switch LED on
  else if ((mode == 2 && counter > 250) || // in mode 2, toggle when going above 250
	   (counter > 100)) {         // otherwise, when mode 3, after going above 100, 
    digitalWrite(LEDPIN,              // toggle LEDPIN
		   !digitalRead(LEDPIN));
    counter = 0;                      // reset counter to zero
  }   
}
