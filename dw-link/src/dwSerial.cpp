/*
 * dwSerial.cpp -- debugWIRE serial interface (based on SingleWireSerial)
 */

// 
// Includes
// 
#include <Arduino.h>
#include <util/delay.h>
#include "dwSerial.h"


//
// Constructor
//
dwSerial::dwSerial(void)  
{
}

//
// Public methods
//
void dwSerial::sendBreak(void)
{
  enable(false);
  //  PORTD &= ~_BV(ICBIT); // TEST -- needs to be removed!
  ICDDR |= _BV(ICBIT); // switch pin to output (which is always low)
  _delay_ms(400); // enough for 30 bps
  ICDDR &= ~_BV(ICBIT); // and switch it back to being an input
  enable(true);
}



size_t dwSerial::sendCmd(const uint8_t *loc, uint8_t len, bool fastReturn)
{
  Serial.flush(); // wait until everything has been written to the regular serial line in order to avoid interrupts
  for (byte i=0; i < len; i++) {
    if (i == len-1 && fastReturn) SingleWireSerial::_finishSendingEarly = true;
    SingleWireSerial::write(loc[i]);
  }
  SingleWireSerial::_finishSendingEarly = false;
  return len;
}

void dwSerial::enable(bool active)
{
  setRxIntMsk(active);
}


unsigned long dwSerial::calibrate()
{
  unsigned long timeout = 900000UL; // means roughly 800 ms
  unsigned long bps, eightbits = 0;
  byte edges;
  byte saveSREG;

  DDRC = 1;
  PORTC |= 1;
  PORTC &= ~1;
  saveSREG = SREG;
  cli();
  enable(false);
  TCCRA = 0;
  TCCRB = _BV(ICNC) | _BV(CS0); // noise cancellation, falling edge, prescaler = 1
  TCCRC =  0;
  TIFR |= _BV(ICF);
  TIFR |= _BV(TOV);
  
  PORTC |= 1;
  PORTC &= ~1;
  while ((TIFR & _BV(ICF)) == 0  && timeout) { // wait for first falling edge
    timeout--;
  }
  PORTC |= 1;
  PORTC &= ~1;
  TCNT = 0; // reset timer so that we do not have to worry about TOV
            // we probably start 12 cycles late because of the reset
  if (timeout == 0) {
    SREG = saveSREG;
    return 0;
  }
  edges = 1;
  TIFR |= _BV(ICF);  // clear input capture flag
  TIFR |= _BV(TOV);  // clear timer overflow flag
  timeout = 100000UL; // roughly 80 msec
  while (edges < 5) {
    while ((TIFR & _BV(ICF)) == 0 && (TIFR & _BV(TOV)) == 0 && timeout)
      timeout--;
    if (timeout == 0) { // timeout waiting for falling edge 
      SREG = saveSREG;
      return 0;
    }
    if (TIFR & _BV(ICF)) { // falling edge detected
      TIFR |= _BV(ICF);  // detected falling edge, clear flag
      timeout = 100000UL; // reset timeout again to roughly 80 msec
      edges++; // count the edge and continue
      if (ICR > 0xF000 && edges == 5) { // last edge found before a potential overflow!
	break; // ignore a potential overflow
      }
    }
    if (TIFR & _BV(TOV)) { // counter overflow
      eightbits += 0x10000; // add to eightbit time
      TIFR |= _BV(TOV); // clear timer overflow flag
      continue;
    }
  }
  eightbits = eightbits + ICR + 12; // 12 because of the late state of the counter
  SREG = saveSREG;
  bps = (F_CPU*8/eightbits);
  enable(true);
  return bps;
}

