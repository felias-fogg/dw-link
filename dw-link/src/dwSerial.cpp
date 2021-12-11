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
void dwSerial::sendBreak()
{
  enable(false);
  //  PORTD &= ~_BV(ICBIT); // TEST -- needs to be removed!
  ICDDR |= _BV(ICBIT); // switch pin to output (which is always low)
  _delay_ms(100);
  ICDDR &= ~_BV(ICBIT); // and switch it back to being an input
  enable(true);
}


size_t dwSerial::sendCmd(const uint8_t *loc, uint8_t len) {
  return write(loc, len);
} 

size_t dwSerial::write(const uint8_t *loc, uint8_t len)
{
  for (byte i=0; i < len; i++)
    SingleWireSerial::write(loc[i]);
  return len;
}


void dwSerial::enable(bool active)
{
  setRxIntMsk(active);
}


unsigned long dwSerial::calibrate(void)
{
  unsigned long timeout = 100000UL; // long means roughly 80 ms
  unsigned long bps, eightbits = 0;
  unsigned int start;
  byte edges = 1;
  byte saveSREG;

  saveSREG = SREG;
  cli();
  enable(false);
  TCCRA = 0;
  TCCRB = _BV(ICNC) | _BV(CS0); // noise cancellation, falling edge, prescaler = 1
  TCCRC =  0;
  TIFR |= _BV(ICF);

  while ((TIFR & _BV(ICF)) == 0 && timeout) timeout--;
  if (timeout == 0) {
    SREG = saveSREG;
    return 0;
  }
  start = ICR;
  TIFR |= _BV(ICF);
  timeout = 10000;
  while (edges < 5) {
    while ((TIFR & _BV(ICF)) == 0 && timeout) timeout--;
    if (timeout == 0) {
      SREG = saveSREG;
      return 0;
    }
    TIFR |= _BV(ICF);
    timeout = 10000;
    eightbits += (ICR - start);
    start = ICR;
    edges++;
  }
  SREG = saveSREG;
  bps = (F_CPU*8/eightbits);
  enable(true);
  return bps;
}

