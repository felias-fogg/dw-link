// This is an example sketch to demonstrate embedded debugging
// It is a modified blink sketch. It has three different modes:
// off, slow blinking, fast blinking. you can cycle through these
// modes by pressing a button. In order to keep external components
// at minimum, we use the builtin LED and a single button.
// The blinking is implemented by using the "Timer/Counter0 Compare Match A"
// interrupt.

#define LEDPIN LED_BUILTIN
#define BUTTON 12

byte counter = 0;
volatile byte mode = 0;

void setup() {
  pinMode(LEDPIN, OUTPUT);       // configure LEDPIN as OUTPUT
  //pinMode(BUTTON, INPUT_PULLUP); // enable pull-up resistor for button 
  TIMSK0 |= _BV(OCIE0A);         // enable Timer0 Compare Match A interrupt
  OCR0A = 0x80;                  // set interrupt time between two millis interrupts
}

void loop() {
  if (digitalRead(BUTTON) == 0) { // button pressed
    mode = (mode+1) % 3;          // go to next mode
    delay(20);                    // debounce
    while (digitalRead(BUTTON) == 0); // wait for release
    delay(20);                    // debounce
  }
}

ISR(TIMER0_COMPA_vect) {          // timer ISR
  counter++;                      // advance counter
  if (mode == 0)
    digitalWrite(LEDPIN, 0);      // in mode 0, switch LED off
  else if ((mode == 1 && counter > 250) ||
	   (counter > 100)) { // otherwise (when mode 2), after going above 100, toggle
      digitalWrite(LEDPIN,
		   !digitalRead(LEDPIN));
      counter = 0;                // reset counter to zero
  }
}
