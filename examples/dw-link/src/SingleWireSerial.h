/*
  SingleWireSerial.h - A software serial library that uses only
  one wire to connect two systems in half-duplex mode. In addition,
  it uses the input capture feature and output 
  compare match feature of timer 1 in order to support high bit rates.
  It is loosely based on SoftwareSerial, but uses a completely
  different method for reading and writing. 

  Since one usually has only one input capture pin, it does not make
  sense to have more than one instance. 

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

*/

#ifndef SingleWireSerial_h
#define SingleWireSerial_h
#include "SingleWireSerial_config.h"
#include <inttypes.h>
#include <Stream.h>

/******************************************************************************
* Definitions
******************************************************************************/

#ifndef _SS_MAX_RX_BUFF
#define _SS_MAX_RX_BUFF 64 // RX buffer size
#endif

class SingleWireSerial : public Stream
{
protected:
  bool _finishSendingEarly = false;
  static bool _twoWire;
  static bool _waitBeforeSending;
  static bool _buffer_overflow;
  static uint16_t _oneAndAHalfBitDelay;
  static uint16_t _bitDelay;
  static uint16_t _endOfByte;
  static uint8_t _setICfalling, _setICrising, _setCTC;
  static uint8_t _receive_buffer[_SS_MAX_RX_BUFF]; 
  static volatile uint8_t _receive_buffer_tail;
  static volatile uint8_t _receive_buffer_head;


protected:
  static void setRxIntMsk(bool enable);
  

public:
  // public methods
  SingleWireSerial(bool twoWire = false);
  ~SingleWireSerial();
  void begin(long speed);
  void end();
  bool overflow(); 
  int peek();

  virtual size_t write(uint8_t data);
  virtual int read();
  virtual int available();
  virtual void flush();
  operator bool() { return true; }

  // public only for easy access by interrupt handler
  static inline void handle_interrupt() __attribute__((__always_inline__));
};

#endif
