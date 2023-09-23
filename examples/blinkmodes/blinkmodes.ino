// This is an example sketch to demonstrate embedded debugging

// It is a modified blink sketch. It has four different modes:
// off, on, slow blinking, fast blinking. You can cycle through these
// modes by pressing a button, which is recognized using an interrupt.
// Parallel to that, we process an ASCII input stream inverting the case of each
// character before sending it out.

// In order to keep external components at a minimum, we use the builtin LED
// and a single button.  The blinking is implemented by using the
// "Timer/Counter0 Compare Match A" interrupt.

#include <ctype.h>

#define LEDPIN LED_BUILTIN
#define BUTTON 2
#define BUTTONGND 4

volatile byte counter = 0;           // the counter for blinking, it counts ms
volatile byte mode = 0;              // the mode of the b linker
volatile unsigned long lastpress = 0;// when was button pressed last?

const byte debounce_ms = 100;        // the debounce time in ms

void setup() {
  Serial.begin(19200);
  Serial.println(F("\nblinkmode V1.0.0"));
  pinMode(LEDPIN, OUTPUT);           // initialize LEDPIN 
  pinMode(BUTTONGND, OUTPUT);        // make neighboring pin GND for the button
  pinMode(BUTTON, OUTPUT);           // initialize button
  TIMSK0 |= _BV(OCIE0A);             // enable Timer0 Compare Match A interrupt
  OCR0A = 0x80;                      // set interrupt time between two millis interrupts
  attachInterrupt(digitalPinToInterrupt(BUTTON), readButton, FALLING);
}

void loop() {
  char c;

  if (Serial.available()) {
    c = Serial.read();
    if (isupper(c)) Serial.write(tolower(c));
    else if (islower(c)) Serial.write(toupper(c));
  }
}

void readButton() {
  if (lastpress + debounce_ms > millis())
    return;
  lastpress = millis();              // remember time of last press for debouncing
  mode = (mode+1) % 4;               // go to next mode
}

ISR(TIMER_COMPA_vect) {              // timer ISR
  counter++;                          // advance counter
  if (mode == 0)
    digitalWrite(LEDPIN, 0);          // in mode 0, switch LED off
  if (mode == 1)
    digitalWrite(LEDPIN, 1);          // in mode 1, switch LED on
  else if ((mode == 2 && counter > 500) || // in mode 2, toggle when going above 500
	   (counter > 100)) {         // otherwise, when mode 3, after going above 100, 
    digitalWrite(LEDPIN,              // toggle LEDPIN
		   !digitalRead(LEDPIN));
    counter = 0;                      // reset counter to zero
  }   
}
