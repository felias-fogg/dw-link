#define LED SCK // use always the SCK pin so one can see something flashing.

volatile byte cycle = 0; // cycle counter
volatile unsigned long privmillis = 0; 

#ifdef TIM0_COMPA_vect
ISR(TIM0_COMPA_vect)
#else
ISR(TIMER0_COMPA_vect)
#endif
{
  if (cycle < 5) 
    privmillis++;
  else
    privmillis += 5; // time is 5 times faster!
}

unsigned long mymillis(void) {
  byte savesreg = SREG;
  unsigned long now;
  cli();
  now = privmillis;
  SREG = savesreg;
  return now;
}

void mydelay(unsigned long wait) {
  unsigned long start = mymillis();

  while (mymillis() - start < wait);
}

void setup() {
  pinMode(LED, OUTPUT);   // initialize digital pin LED as an output.
  OCR0A = 0x80;           // prepare for having a COMPA interrupt
#ifdef TIMSK
  TIMSK |= _BV(OCIE0A);   // enable COMPA interrupt on Timer0
#else
  TIMSK0 |= _BV(OCIE0A);  // enable COMPA interrupt on Timer0
#endif
#ifdef __AVR_ATtiny13__
  TCCR0B = 0x03;         // set prescaler 64 on ATtiny13, since the ATtony13 does not initialize TCCR0B by itself
#endif
}

// the loop function runs over and over again forever
void loop() {
  digitalWrite(LED, HIGH);       // turn the LED on (HIGH is the voltage level)
  mydelay(1000);                 // wait for approximately one second or 1/5 of a second 
  digitalWrite(LED, LOW);        // turn the LED off by making the voltage LOW
  delay(1000);                   // wait for a second
  if (++cycle >= 10) cycle = 0;  // cyclic counter 
}
