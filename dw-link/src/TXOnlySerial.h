/*
TXOnlySerial.h (derived from SoftSerial.h) - 
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

#ifndef TXOnlySerial_h
#define TXOnlySerial_h

#include <inttypes.h>
#include <Stream.h>

/******************************************************************************
* Definitions
******************************************************************************/

class TXOnlySerial : public Stream
{
private:
  // per object data
  uint8_t _transmitBitMask;
  volatile uint8_t *_transmitPortRegister;

  // Expressed as 4-cycle delays (must never be 0!)
  uint16_t _tx_delay;

  uint16_t _inverse_logic:1;


  // private methods
  void setTX(uint8_t transmitPin);

  // Return num - sub, or 1 if the result would be < 1
  static uint16_t subtract_cap(uint16_t num, uint16_t sub);

  // private static method for timing
  static inline void tunedDelay(uint16_t delay);

public:
  // public methods
  TXOnlySerial(uint8_t transmitPin, bool inverse_logic = false);
  ~TXOnlySerial();
  void begin(long speed);
  void end();

  int peek();
  virtual int read();
  virtual int available();
  virtual size_t write(uint8_t byte);
  virtual void flush();
  operator bool() { return true; }
  
  using Print::write;

};

#endif
