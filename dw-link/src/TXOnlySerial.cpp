/*
TXOnlySerial.cpp (derived from SoftSerial.cpp) - 
Multi-instance software serial TX only library for Arduino/Wiring
-- Tuning, circular buffer, derivation from class Print/Stream,
   multi-instance support, porting to 8MHz processors,
   various optimizations, PROGMEM delay tables, inverse logic and 
   direct port writing by Mikal Hart (http://www.arduiniana.org)
-- 20MHz processor support by Garrett Mace (http://www.macetech.com)
-- ATmega1280/2560 support by Brett Hagman (http://www.roguerobotics.com/)

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

Latest version could be found here: https://github.com/felias-fogg
*/

// 
// Includes
// 
#include <avr/pgmspace.h>
#include <Arduino.h>
#include "TXOnlySerial.h"
#include <util/delay_basic.h>


//
// Private methods
//

/* static */ 
inline void TXOnlySerial::tunedDelay(uint16_t delay) { 
  _delay_loop_2(delay);
}

//
// Constructor
//
TXOnlySerial::TXOnlySerial(uint8_t transmitPin, bool inverse_logic /* = false */) : 
  _tx_delay(0),
  _inverse_logic(inverse_logic)
{
  setTX(transmitPin);
}

//
// Destructor
//
TXOnlySerial::~TXOnlySerial()
{
  end();
}

void TXOnlySerial::setTX(uint8_t tx)
{
  // First write, then set output. If we do this the other way around,
  // the pin would be output low for a short while before switching to
  // output hihg. Now, it is input with pullup for a short while, which
  // is fine. With inverse logic, either order is fine.
  digitalWrite(tx, _inverse_logic ? LOW : HIGH);
  pinMode(tx, OUTPUT);
  _transmitBitMask = digitalPinToBitMask(tx);
  uint8_t port = digitalPinToPort(tx);
  _transmitPortRegister = portOutputRegister(port);
}

uint16_t TXOnlySerial::subtract_cap(uint16_t num, uint16_t sub) {
  if (num > sub)
    return num - sub;
  else
    return 1;
}

//
// Public methods
//

void TXOnlySerial::begin(long speed)
{
  _tx_delay = subtract_cap((F_CPU / speed) / 4, 15 / 4);
}

void TXOnlySerial::end()
{
}


size_t TXOnlySerial::write(uint8_t b)
{
  if (_tx_delay == 0) {
    setWriteError();
    return 0;
  }

  // By declaring these as local variables, the compiler will put them
  // in registers _before_ disabling interrupts and entering the
  // critical timing sections below, which makes it a lot easier to
  // verify the cycle timings
  volatile uint8_t *reg = _transmitPortRegister;
  uint8_t reg_mask = _transmitBitMask;
  uint8_t inv_mask = ~_transmitBitMask;
  uint8_t oldSREG = SREG;
  bool inv = _inverse_logic;
  uint16_t delay = _tx_delay;

  if (inv)
    b = ~b;

  cli();  // turn off interrupts for a clean txmit

  // Write the start bit
  if (inv)
    *reg |= reg_mask;
  else
    *reg &= inv_mask;

  tunedDelay(delay);

  // Write each of the 8 bits
  for (uint8_t i = 8; i > 0; --i)
  {
    if (b & 1) // choose bit
      *reg |= reg_mask; // send 1
    else
      *reg &= inv_mask; // send 0

    tunedDelay(delay);
    b >>= 1;
  }

  // restore pin to natural state
  if (inv)
    *reg &= inv_mask;
  else
    *reg |= reg_mask;

  SREG = oldSREG; // turn interrupts back on
  tunedDelay(_tx_delay);
  
  return 1;
}

void TXOnlySerial::flush()
{
  // There is no tx buffering, simply return
}

int TXOnlySerial::available()
{
  return 0; // we do not read anything
}

int TXOnlySerial::peek()
{
  return 0; // nothing to peek
}

int TXOnlySerial::read()
{
  return 0; // nothing to read
}
