/*
  SingleWireSerial.cpp -  A software serial library that uses only
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

// When _DEBUG  == 1, then some bit pulses are sent to all PORTC-pins (i.e.,
// analog and I2C pins. The on/off toggle needs 2 cycles, but may, of course,
// disturb the timing a bit. When _LOGDEBUG == 1, some info is printed
// using the ordinary Serial connection (if it is open).
#define _DEBUG 0
#define _LOGDEBUG 0
#define _FASTIRQ 1 // This is the current version, set it to 1!
// 
// Includes
// 
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <Arduino.h>
#include "SingleWireSerial.h"
#include <util/delay_basic.h>

// Statics
// The used attribute is necessary, otherwise the compiler otimizes the variables away!
bool SingleWireSerial::_twoWire;
bool SingleWireSerial::_waitBeforeSending asm("_waitBeforeSending") __attribute__ ((used));
bool SingleWireSerial::_buffer_overflow asm("_buffer_overflow") __attribute__ ((used));
uint16_t SingleWireSerial::_bitDelay asm("_bitDelay") __attribute__ ((used)) = 0;
uint16_t SingleWireSerial::_oneAndAHalfBitDelay asm("_oneAndAHalfBitDelay")  __attribute__ ((used));
uint16_t SingleWireSerial::_endOfByte;

uint8_t SingleWireSerial::_setICfalling asm("_setICfalling") __attribute__ ((used));
uint8_t SingleWireSerial::_setICrising asm("_setICrising") __attribute__ ((used));
uint8_t SingleWireSerial::_setCTC asm("_setCTC") __attribute__ ((used));

uint8_t SingleWireSerial::_receive_buffer[_SS_MAX_RX_BUFF] asm("_receive_buffer")  __attribute__ ((used)); 
volatile uint8_t SingleWireSerial::_receive_buffer_tail asm("_receive_buffer_tail") = 0;
volatile uint8_t SingleWireSerial::_receive_buffer_head asm("_receive_buffer_head") = 0;

//
// Debugging
//
// This function generates a brief pulse
// for debugging or measuring on an oscilloscope.
// All in all, it takes 4 cycles to produce the pulse
#if _DEBUG
inline __attribute__ ((always_inline)) void DebugPulse(byte signal)
{
  PORTC |= signal;
  PORTC &= ~signal;
}
#else 
inline __attribute__ ((always_inline)) void DebugPulse(__attribute__ ((unused)) byte signal) {}
#endif

//
// Interrupt handling
//

/* static */
void SingleWireSerial::handle_interrupt() 
#if _FASTIRQ
{
  asm volatile(
	       // save registers
	       "push r24\n\t"
	       "lds r24,  %A[ICRaddr] ; load ICR low, now high byte is saved!\n\t"
#if _DEBUG
	       "sbi %[PORTCaddr], 0\n\t"
	       "cbi %[PORTCaddr], 0\n\t"
#endif
	       "push r25\n\t"
	       "lds r25,  %B[ICRaddr] ; load ICR high\n\t"
	       "push r30\n\t"
	       "push r31\n\t"
	       "in r30, __SREG__\n\t"
	       "push r30 ; save status register\n\t"
	       
	       // compute time since edge interrupt 
	       "lds r30,  %A[TCNTaddr] ; load TCNT low\n\t"
	       "lds r31,  %B[TCNTaddr] ; load TCNT high\n\t"
	       "sub r30, r24 ; TCNT - ICR: time since edge IRQ\n\t"
	       "sbc r31, r25\n\t"

	       // setup OCR for first 1.5 bit times
	       "lds r24, _oneAndAHalfBitDelay\n\t"
	       "lds r25, _oneAndAHalfBitDelay+1\n\t"
	       "sts %B[OCRAaddr], r25\n\t"
	       "sts %A[OCRAaddr], r24\n\t"

	       // load CTC setting, check for slowness, store TCNT and TCCRB, clear flag
	       "lds r24, _setCTC ; set counter to CTC operation\n\t"
	       "sbrc r24, %[FASTCS] ; when fast bit is clear, skip\n\t"
	       "adiw r30, %[STARTOFFSET] ; time that is unaccounted for\n\t"
	       "sts %B[TCNTaddr], r31 ; store back to TCNT\n\t"
	       "sts %A[TCNTaddr], r30\n\t"
	       "sts %[TCCRBaddr], r24 ; set CTC mode\n\t"
	       "sbi %[TIFRIOaddr], %[OCFAconst] ; clear flag OVF flag\n\t"
#if _DEBUG
	       "sbi %[PORTCaddr], 0\n\t"
	       "cbi %[PORTCaddr], 0\n\t"
#endif	       
	       
	       // compute & remember where to store input char
	       "lds r30, _receive_buffer_tail\n\t"
	       "mov r24, r30 ; remember tail for later\n\t"
	       "ldi r31, 0\n\t"
	       "subi r30, lo8(-(_receive_buffer))\n\t"
	       "sbci r31, hi8(-(_receive_buffer)) ; r30:r31 is now address of _receive_buffer[_receive_buffer_tail]\n\t"
	       "push 30 ; save for later \n\t"
	       "push 31\n\t"

	       // advance _receive_buffer_tail and check for overrun
	       "inc r24 ; increment tail\n\t"
	       "andi r24, %[BUFFMASK] ; do a simple modulo with a power of 2 for a number <= 256\n\t"
	       "lds r25, _receive_buffer_head\n\t"
	       "cp r24, r25 ; compare now with head ptr\n\t"
	       "breq _LOVF ; if equal, mark overflow\n\t"
	       "sts  _receive_buffer_tail, r24 ; store new tail\n\t"
	       "rjmp _LSTARTREAD ; now start to read bits\n\t"
	       "_LOVF: ldi r24, 0x01\n\t"
	       "sts _buffer_overflow, r24 ; overflow bit is set\n\t"

	       // initialize ch (r24) and bit counter (r25)
	       "_LSTARTREAD: ldi r25, 0x08\n\t"
	       "clr r24\n\t"

#if _DEBUG
	       "sbi %[PORTCaddr], 0\n\t"
	       "cbi %[PORTCaddr], 0\n\t"
#endif	       

	       // loop for first 7 bits
	       "_LBYTELOOP: subi r25, 0x01\n\t"
	       "breq _LASTBIT\n\t"

	       // wait for flag to be set
	       "clc ; clear carry bit - needed later for bit to read\n\t"
	       "_LWAIT1: sbis %[TIFRIOaddr], %[OCFAconst]\n\t"
	       "rjmp _LWAIT1\n\t"
	       
	       // read one bit
#if defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
	       "lds r30, %[INPORTaddr]\n\t"
	       "sbrc r30, %[INPIN] ; skip if cleared\n\t"
#else
	       "sbic %[INPORT], %[INPIN] ; skip if cleared\n\t"
#endif
	       "sec ; set carry if bit is set\n\t"
	       "ror r24 ; rotate carry bit into byte\n\t"
#if _DEBUG
	       "sbi %[PORTCaddr], 2\n\t"
	       "cbi %[PORTCaddr], 2\n\t"
#endif
	       // setup for next bit
	       "lds r30, _bitDelay ; set OCRA now to _bitDelay\n\t"
	       "lds r31, _bitDelay+1\n\t"
	       "sts %B[OCRAaddr], r31\n\t"
	       "sts %A[OCRAaddr], r30\n\t"
	       "sbi %[TIFRIOaddr], %[OCFAconst] ; clear flag\n\t"
	       "rjmp _LBYTELOOP\n\t"

	       // now the last bit (r30:31 contains _bitDelay)
	       "_LASTBIT: sbiw r30, %[ENDOFFSET] ; subtract everything for what we do before the last read = 10 cyc\n\t"
	       "sts %B[OCRAaddr], r31\n\t"
	       "sts %A[OCRAaddr], r30\n\t"

      	       // set _waitBeforeSending to true
	       "ldi r30, 1\n\t"
	       "sts _waitBeforeSending, r30\n\t"

	       // wait for flag to be set
	       "clc ; clear carry bit - needed later for bit to read\n\t"
	       "_LWAIT2: sbis %[TIFRIOaddr], %[OCFAconst]\n\t"
	       "rjmp _LWAIT2\n\t"

	       // set TCCRB, clear ICF and restore bufptr
	       "lds r30, _setICfalling\n\t"
	       "sts %[TCCRBaddr], r30\n\t"
	       "pop r31 ; restore bufptr\n\t"
	       "pop r30\n\t"
	       "sbi %[TIFRIOaddr], %[ICFconst]\n\t"

	       // read last bit
#if defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
	       "lds r25, %[INPORTaddr]\n\t"
	       "sbrc r25, %[INPIN] ; skip if cleared\n\t"
#else
	       "sbic %[INPORT], %[INPIN] ; skip if cleared\n\t"
#endif
	       "sec ; set carry if bit is set\n\t"
	       "ror r24 ; rotate carry bit into byte\n\t"
#if _DEBUG
	       "sbi %[PORTCaddr], 2\n\t"
	       "cbi %[PORTCaddr], 2\n\t"
#endif
	       // finish
	       "st z, r24 ; store read character\n\t"
	       "pop r31 ; status register\n\t"
	       "out __SREG__, r31\n\t"
	       "pop r31\n\t"
	       "pop r30\n\t"
	       "pop r25\n\t"
	       "pop r24\n\t"
#if _DEBUG
	       "sbi %[PORTCaddr], 1\n\t"
	       "cbi %[PORTCaddr], 1\n\t"
#endif
	       "reti ; done\n\t"
	       :
	       : [PORTCaddr] "I" (_SFR_IO_ADDR(PORTC)),
		 [ICRaddr] "M" (&ICR),
		 [TCNTaddr] "M" (&TCNT),
		 [STARTOFFSET] "I" (35),
		 [BUFFMASK] "M" (_SS_MAX_RX_BUFF-1),
		 [OCRAaddr] "M" (&OCRA), 
		 [TCCRBaddr] "M" (&TCCRB),
		 [TIFRIOaddr] "M" (_SFR_IO_ADDR(TIFR)),
		 [OCFAconst] "M" (OCFA),
#if defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
		 [INPORTaddr] "n" (&ICPIN),
#else
		 [INPORT] "I" (_SFR_IO_ADDR(ICPIN)),
#endif
		 [INPIN] "I"  (ICBIT),
		 [ENDOFFSET] "M" (10),
		 [ICFconst] "M" (ICF),
	         [FASTCS] "M" (CS0));
}
#else // not _FASTIRQ
{
  uint8_t ch, level;
  uint16_t elapsed,  next; // read input capture register
  uint16_t start;
  byte * bufptr;

  // one only has to save SREG, r0-r1, r18-r27, 30-31
  // since only these are the registers that are
  // used in a C-routine without restoring them
  asm volatile("push r22 \n\t"
	       "push r23 \n\t"
	       "lds r22, _setICrising ; load code for raising edge \n\t"
	       "sts %[TCCRBaddr], r22 ; write to control register \n\t"
	       "lds  r22, %A[ICRaddr] ; load ICR low byte\n\t"
	       "lds  r23, %B[ICRaddr] ; load ICR high byte\n\t"
#if _DEBUG
	       "sbi %[PORTCaddr], 0\n\t"
	       "cbi %[PORTCaddr], 0\n\t"
#endif
	       "push r0 ; save temp reg\n\t"
	       "in r0, __SREG__ ; save status reg\n\t"
	       "push r0\n\t"
	       "push r1\n\t"
	       "eor r1, r1 ; clear r1\n\t"
	       "push r18\n\t"
	       "push r19\n\t"
	       "push r20\n\t"
	       "push r21\n\t"
	       "push r24\n\t"
	       "push r25\n\t"
	       "push r26\n\t"
	       "push r27\n\t"
	       "push r30\n\t"
	       "push r31\n\t"
	       "movw %[START], r22\n\t"
	       : [START] "=d" (start)
	       : [ICRaddr] "M" (&ICR),
		 [TCCRBaddr] "M" (&TCCRB),
		 [EDGEUP] "M" (_BV(ICES)),
		 [PORTCaddr] "I" (_SFR_IO_ADDR(PORTC))
	       );
  setRxIntMsk(false); // disable the ICR interrupts
  ch = 0;
  level = 0;
  next = _oneAndAHalfBitDelay;

  // tail points to where byte goes
  bufptr = &_receive_buffer[_receive_buffer_tail];
  // if buffer full, set the overflow flag
  uint8_t nextix = (_receive_buffer_tail + 1) % _SS_MAX_RX_BUFF;
  if (nextix != _receive_buffer_head)
    {
      // that is where next byte shall go
      _receive_buffer_tail = nextix;
    } 
  else 
    {
      DebugPulse(0x08);
      _buffer_overflow = true;
    }
  DebugPulse(0x01);
  
  while (next <= _endOfByte) {
    if (TIFR & _BV(ICF)) { // capture flag has been set
      DebugPulse(0x04);
      TIFR |= _BV(ICF); // clear flag
      TCCRB ^= _BV(ICES); // toggle edge detector;
      level ^= 0x80;
    }
    elapsed = TCNT - start;  // 16 bit unsigned arithmetic gives correct duration
    if (elapsed > next) { 
      ch >>=1;
      ch |= level;
      next = next + _bitDelay;
    }
  }
  _waitBeforeSending = 1;
  *bufptr = ch; // save new byte
  TCCRB &= ~_BV(ICES); // set edge detector to falling edge

  setRxIntMsk(true); // and enable input capture interrupt again
  DebugPulse(0x02);
  asm volatile(
	       "pop r31\n\t"
	       "pop r30\n\t"
	       "pop r27\n\t"
	       "pop r26\n\t"
	       "pop r25\n\t"
	       "pop r24\n\t"
	       "pop r21\n\t"
	       "pop r20\n\t"
	       "pop r19\n\t"
	       "pop r18\n\t"
	       "pop r1\n\t"
	       "pop r0\n\t"
	       "out __SREG__, r0\n\t"
	       "pop r0\n\t"
	       "pop r23\n\t"
	       "pop r22 \n\t"
#if _DEBUG
	       "sbi %[PORTCaddr], 1\n\t"
	       "cbi %[PORTCaddr], 1\n\t"
#endif
	       "reti"
	       :
	       : [PORTCaddr] "I" (_SFR_IO_ADDR(PORTC))
	       );
}
#endif // not _FASTIRQ

ISR(TIMER_CAPT_vect, ISR_NAKED)
{
  SingleWireSerial::handle_interrupt();
}

//
// Constructor
//
SingleWireSerial::SingleWireSerial(bool twoWire)
{
  _twoWire = twoWire;
  _buffer_overflow = false;
  pinMode(ICArduinoPin, INPUT);
  if (twoWire) {
    digitalWrite(OCArduinoPin, HIGH);
    pinMode(OCArduinoPin, OUTPUT);
  }
}

//
// Destructor
//
SingleWireSerial::~SingleWireSerial()
{
  end();
}

void  SingleWireSerial::setRxIntMsk(bool enable)
{
  if (enable) {
    TCCRB = _setICfalling; // look for falling edge of start bit
    TIFR |= _BV(ICF); // clear input capture flag
    TIMSK |= _BV(ICIE); // enable interrupt
  } else
    TIMSK &= ~_BV(ICIE); // disable input capture interrupt
}

//
// Public methods
//

void SingleWireSerial::begin(long speed)
{
  
  // Precalculate the various delays
  uint32_t bit_delay100 = (F_CPU*100 / speed);
  uint8_t prescaler;

  // init _waitBeforeSending
  _waitBeforeSending = true;
  
  // clear read buffer
   _receive_buffer_tail = _receive_buffer_head;
   _buffer_overflow = false;

  if (bit_delay100 > 400000UL) {
    bit_delay100 = bit_delay100/64;
    prescaler = _BV(CS1)|_BV(CS0); // prescaler = 64
    if (bit_delay100 > 400000UL) {
      bit_delay100 = bit_delay100/4;
      prescaler = _BV(CS2); // prescaler = 256
    }
  } else {
    prescaler = _BV(CS0); // prescaler = 1
  }
  _setICfalling = _BV(ICNC) | prescaler;
  _setICrising = _setICfalling | _BV(ICES);
  _setCTC = _BV(WGM2) | prescaler;

  _bitDelay = (bit_delay100+50)/100; // bit delay time in timer1 ticks
  _oneAndAHalfBitDelay = (bit_delay100+bit_delay100/2+50)/100; // delay until first sample time point
  _endOfByte = _oneAndAHalfBitDelay + (7*_bitDelay); // last sample timepoint

#if _LOGDEBUG
  Serial.print(F("bit_delay100="));
  Serial.println(bit_delay100);
  Serial.print(F("prescaler="));
  Serial.println(prescaler);
  Serial.print(F("_bitDelay="));
  Serial.println(_bitDelay);
  Serial.print(F("_oneAndAHalfBitDelay="));
  Serial.println(_oneAndAHalfBitDelay);
  Serial.print(F("_endOfByte="));
  Serial.println(_endOfByte);
  Serial.print(F("_setCTC="));
  Serial.println(_setCTC,BIN);
#endif
  TCCRA = 0;
  TCCRC = 0;
  setRxIntMsk(true);
#if _DEBUG
  DDRC |= 0x3F;
#endif
}

void SingleWireSerial::end()
{
  _receive_buffer_tail = _receive_buffer_head;
  setRxIntMsk(false);
}


// Read data from buffer
int SingleWireSerial::read()
{
  // Empty buffer?
  if (_receive_buffer_head == _receive_buffer_tail)
    return -1;

  // Read from "head"
  uint8_t d = _receive_buffer[_receive_buffer_head]; // grab next byte
  _receive_buffer_head = (_receive_buffer_head + 1) % _SS_MAX_RX_BUFF;
  return d;
}

int SingleWireSerial::available()
{
  return ((unsigned int)(_SS_MAX_RX_BUFF + _receive_buffer_head - _receive_buffer_tail)) % _SS_MAX_RX_BUFF;
}

size_t SingleWireSerial::write(uint8_t data)
{
  uint8_t oldSREG = SREG;

  setRxIntMsk(false);
  TCCRA = 0;
  TCCRC = 0;
  TCCRB = _setCTC;
  OCRA = _bitDelay-1;

  cli(); // interrupts off

  TCNT = 0;
  TIFR |= _BV(OCFA);
  DebugPulse(0x01);

  if (_waitBeforeSending) { // wait here, because we do not wait to the end of the stop bit when reading
    _waitBeforeSending = false;
    OCRA = _bitDelay << 1; // two bit times
    while (!(TIFR & _BV(OCFA)));
    OCRA = _bitDelay-1;
    TCNT = 0;
    TIFR |= _BV(OCFA);
  }
  
  if (!_twoWire) {
    TCNT = 0;
    ICDDR |= _BV(ICBIT);   // startbit
    DebugPulse(0x04);
    for (uint8_t i = 8; i > 0; --i) {
      while (!(TIFR & _BV(OCFA)));
      if (data & 1) {
	ICDDR &= ~_BV(ICBIT); // make output high-impedance
	DebugPulse(0x02);
      } else {
	ICDDR |= _BV(ICBIT); // pull-down
	DebugPulse(0x04);
      }
      TIFR |= _BV(OCFA);
      data >>= 1;
    }
    while (!(TIFR & _BV(OCFA)));
    ICDDR &= ~_BV(ICBIT); // make output again high-impedance for stop bit
    DebugPulse(0x02);
  } else { // twoWire!
    TCNT = 0;
    OCPORT &= ~_BV(OCBIT);  // startbit
    for (uint8_t i = 8; i > 0; --i) {
      while (!(TIFR & _BV(OCFA)));
      if (data & 1)
	OCPORT |= _BV(OCBIT); // make output high
      else
	OCPORT &= ~_BV(OCBIT); // make output low
      TIFR |= _BV(OCFA);
      data >>= 1;
    }
    while (!(TIFR & _BV(OCFA)));
    OCPORT |= _BV(OCBIT); // make output again high for stop bit
  }
  if (_finishSendingEarly) OCRA = _bitDelay >> 1; // wait only half the stop bit (needed when a response might come early)
  TIFR |= _BV(OCFA); // clear overflow flag

  SREG = oldSREG; // enable interrupts again
  setRxIntMsk(true); //enable input capture input interrupts again

  while (!(TIFR & _BV(OCFA))); // wait for stop bit (or half stop bit) to finish
  DebugPulse(0x01);
  return 1;
}

void SingleWireSerial::flush()
{
  // There is no tx buffering, simply return
}

int SingleWireSerial::peek()
{
  // Empty buffer?
  if (_receive_buffer_head == _receive_buffer_tail)
    return -1;

  // Read from "head"
  return _receive_buffer[_receive_buffer_head];
}

bool SingleWireSerial::overflow()
{
  bool ret = _buffer_overflow;
  if (ret)
    _buffer_overflow = false;
  return ret;
}


