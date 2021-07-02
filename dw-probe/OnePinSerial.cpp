/*
 * OnePinSerial.cpp (based on SoftwareSerial.cpp, formerly NewSoftSerial.cpp)
 *  2/17/2018 - Added sedCmd() function which calls one of two write() functions
 *  depending upon baud rate.
*/

// 
// Includes
// 
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <Arduino.h>
#include <util/delay_basic.h>
#include "OnePinSerial.h"

#define SCOPE_TIMING  0
//
// Statics
//
OnePinSerial *OnePinSerial::active_object = 0;
uint8_t OnePinSerial::_receive_buffer[_SS_MAX_RX_BUFF]; 
volatile uint8_t OnePinSerial::_receive_buffer_tail = 0;
volatile uint8_t OnePinSerial::_receive_buffer_head = 0;

//
// Private methods
//

/* static */ 
inline void OnePinSerial::tunedDelay(uint16_t delay) { 
  _delay_loop_2(delay);
}

//
// The receive routine called by the interrupt handler
//
void OnePinSerial::recv()
{
  uint8_t d = 0;

  // If RX line is high, then we don't see any start bit
  // so interrupt is probably not for us
  if (!rx_pin_read())
  {
    // Disable further interrupts during reception, this prevents
    // triggering another interrupt directly after we return, which can
    // cause problems at higher baudrates.
    setRxIntMsk(false);

    // Wait approximately 1/2 of a bit width to "center" the sample in the start bit
    tunedDelay(_rx_delay_centering);
    
    // Unroll read loop for more consistent timing at high baud rates
    tunedDelay(_rx_delay_intrabit);
    d >>= 1;
    if (rx_pin_read())
      d |= 0x80;
#if SCOPE_TIMING
    PORTD |= 0x04;
#else
    //    __asm__ __volatile__("nop\n\t": : :"memory");
    //    __asm__ __volatile__("nop\n\t": : :"memory");
#endif
    
    tunedDelay(_rx_delay_intrabit);
    d >>= 1;
    if (rx_pin_read())
      d |= 0x80;
#if SCOPE_TIMING
    PORTD &= ~0x04;
#else
    // __asm__ __volatile__("nop\n\t": : :"memory");
    // __asm__ __volatile__("nop\n\t": : :"memory");
#endif
    
    tunedDelay(_rx_delay_intrabit);
    d >>= 1;
    if (rx_pin_read())
      d |= 0x80;
#if SCOPE_TIMING
    PORTD |= 0x04;
#else
    // __asm__ __volatile__("nop\n\t": : :"memory");
    // __asm__ __volatile__("nop\n\t": : :"memory");
#endif
    
    tunedDelay(_rx_delay_intrabit);
    d >>= 1;
    if (rx_pin_read())
      d |= 0x80;
#if SCOPE_TIMING
    PORTD &= ~0x04;
#else
    // __asm__ __volatile__("nop\n\t": : :"memory");
    // __asm__ __volatile__("nop\n\t": : :"memory");
#endif
    
    tunedDelay(_rx_delay_intrabit);
    d >>= 1;
    if (rx_pin_read())
      d |= 0x80;
#if SCOPE_TIMING
    PORTD |= 0x04;
#else
    // __asm__ __volatile__("nop\n\t": : :"memory");
    // __asm__ __volatile__("nop\n\t": : :"memory");
#endif
    
    tunedDelay(_rx_delay_intrabit);
    d >>= 1;
    if (rx_pin_read())
      d |= 0x80;
#if SCOPE_TIMING
    PORTD &= ~0x04;
#else
    // __asm__ __volatile__("nop\n\t": : :"memory");
    // __asm__ __volatile__("nop\n\t": : :"memory");
#endif
    
    tunedDelay(_rx_delay_intrabit);
    d >>= 1;
    if (rx_pin_read())
      d |= 0x80;
#if SCOPE_TIMING
    PORTD |= 0x04;
#else
    // __asm__ __volatile__("nop\n\t": : :"memory");
    // __asm__ __volatile__("nop\n\t": : :"memory");
#endif
    
    tunedDelay(_rx_delay_intrabit);
    d >>= 1;
    if (rx_pin_read())
      d |= 0x80;
#if SCOPE_TIMING
    PORTD &= ~0x04;
#else
    // __asm__ __volatile__("nop\n\t": : :"memory");
    // __asm__ __volatile__("nop\n\t": : :"memory");
#endif
    
    // if buffer full, set the overflow flag and return
    uint8_t next = (_receive_buffer_tail + 1) % _SS_MAX_RX_BUFF;
    if (next != _receive_buffer_head)
    {
      // save new data in buffer: tail points to where byte goes
      _receive_buffer[_receive_buffer_tail] = d; // save new byte
      _receive_buffer_tail = next;
    } 

    // skip the stop bit
    tunedDelay(_rx_delay_stopbit);

    // Re-enable interrupts when we're sure to be inside the stop bit
    setRxIntMsk(true);

  }
}

uint8_t OnePinSerial::rx_pin_read()
{
  return *_receivePortRegister & _receiveBitMask;
}

//
// Interrupt handling
//

/* static */
inline void OnePinSerial::handle_interrupt()
{
  if (active_object)
  {
    active_object->recv();
  }
}

#if defined(PCINT0_vect)
ISR(PCINT0_vect)
{
  OnePinSerial::handle_interrupt();
}
#endif

#if defined(PCINT1_vect)
ISR(PCINT1_vect, ISR_ALIASOF(PCINT0_vect));
#endif

#if defined(PCINT2_vect)
ISR(PCINT2_vect, ISR_ALIASOF(PCINT0_vect));
#endif

#if defined(PCINT3_vect)
ISR(PCINT3_vect, ISR_ALIASOF(PCINT0_vect));
#endif

//
// Constructor
//
OnePinSerial::OnePinSerial(uint8_t ioPin) : 
  _rx_delay_centering(0),
  _rx_delay_intrabit(0),
  _rx_delay_stopbit(0),
  _tx_delay(0)
{
  _ioPin = ioPin;
	// Setup Tx (Note: transmit one by setting mode bit HIGH (becomes output)
  pinMode(ioPin, INPUT);
  digitalWrite(ioPin, LOW);
  _transmitBitMask = digitalPinToBitMask(ioPin);
  _transmitPortRegister = portModeRegister(digitalPinToPort(ioPin));
  
	// Setup Rx
  _receiveBitMask = digitalPinToBitMask(ioPin);
  _receivePortRegister = portInputRegister(digitalPinToPort(ioPin));

  active_object = this;
}

uint16_t OnePinSerial::subtract_cap(uint16_t num, uint16_t sub) {
  if (num > sub)
    return num - sub;
  else
    return 1;
}

//
// Public methods
//

void OnePinSerial::begin(long speed)
{
#if SCOPE_TIMING
  DDRD |= 0x0C;
  PORTD |= ~(0x08);
  PORTD &= ~(0x04);
#endif
  _rx_delay_centering = _rx_delay_intrabit = _rx_delay_stopbit = _tx_delay = 0;

  // Precalculate the various delays, in number of 4-cycle delays
  uint16_t bit_delay;
  _fastRate = speed > 100000;
  if (_fastRate) {
    // If > 100000, assume target clock is 16 MHz and tweak to suit
    bit_delay = (F_CPU / 124000) / 4;
  } else {
    bit_delay = (F_CPU / speed) / 4;
  }

  // 12 (gcc 4.8.2) or 13 (gcc 4.3.2) cycles from start bit to first bit,
  // 15 (gcc 4.8.2) or 16 (gcc 4.3.2) cycles between bits,
  // 12 (gcc 4.8.2) or 14 (gcc 4.3.2) cycles from last bit to stop bit
  // These are all close enough to just use 15 cycles, since the inter-bit
  // timings are the most critical (deviations stack 8 times)
  _tx_delay = subtract_cap(bit_delay, 15 / 4);
    
#if GCC_VERSION > 40800   // Arduino 1.8.5 is 40902
  // Timings counted from gcc 4.8.2 output. This works up to 115200 on
  // 16Mhz and 57600 on 8Mhz.
  //
  // When the start bit occurs, there are 3 or 4 cycles before the
  // interrupt flag is set, 4 cycles before the PC is set to the right
  // interrupt vector address and the old PC is pushed on the stack,
  // and then 75 cycles of instructions (including the RJMP in the
  // ISR vector table) until the first delay. After the delay, there
  // are 17 more cycles until the pin value is read (excluding the
  // delay in the loop).
  // We want to have a total delay of 1.5 bit time. Inside the loop,
  // we already wait for 1 bit time - 23 cycles, so here we wait for
  // 0.5 bit time - (71 + 18 - 22) cycles.
  _rx_delay_centering = subtract_cap(bit_delay / 2, (4 + 4 + 75 + 17 - 23) / 4);

  // There are 23 cycles in each loop iteration (excluding the delay)
  _rx_delay_intrabit = subtract_cap(bit_delay, 23 / 4);

  // There are 37 cycles from the last bit read to the start of
  // stopbit delay and 11 cycles from the delay until the interrupt
  // mask is enabled again (which _must_ happen during the stopbit).
  // This delay aims at 3/4 of a bit time, meaning the end of the
  // delay will be at 1/4th of the stopbit. This allows some extra
  // time for ISR cleanup, which makes 115200 baud at 16Mhz work more
  // reliably
  _rx_delay_stopbit = subtract_cap(bit_delay * 3 / 4, (37 + 11) / 4);
#else // Timings counted from gcc 4.3.2 output
 // Note that this code is a _lot_ slower, mostly due to bad register
  // allocation choices of gcc. This works up to 57600 on 16Mhz and
  // 38400 on 8Mhz.
  _rx_delay_centering = subtract_cap(bit_delay / 2, (4 + 4 + 97 + 29 - 11) / 4);
  _rx_delay_intrabit = subtract_cap(bit_delay, 11 / 4);
  _rx_delay_stopbit = subtract_cap(bit_delay * 3 / 4, (44 + 17) / 4);
#endif

#if SCOPE_TIMING
  Serial.print(F("_rx_delay_centering: "));
  Serial.print(_rx_delay_centering, DEC);
  Serial.println();
  Serial.print(F("_rx_delay_intrabit:  "));
  Serial.print(_rx_delay_intrabit, DEC);
  Serial.println();
  Serial.print(F("_rx_delay_stopbit:   "));
  Serial.print(_rx_delay_stopbit, DEC);
  Serial.println();
#endif

  // Enable the PCINT for the entire port here, but never disable it
  // (others might also need it, so we disable the interrupt by using
  // the per-pin PCMSK register).
  *digitalPinToPCICR(_ioPin) |= _BV(digitalPinToPCICRbit(_ioPin));
  // Precalculate the pcint mask register and value, so setRxIntMask
  // can be used inside the ISR without costing too much time.
  _pcint_maskreg = digitalPinToPCMSK(_ioPin);
  _pcint_maskvalue = _BV(digitalPinToPCMSKbit(_ioPin));
  
  // Masked used to clear a pending pin change interrupt
   _pcint_clrMask = _BV(digitalPinToPCICRbit(_ioPin));

  tunedDelay(_tx_delay); // if we were low this establishes the end
    
  // Activate receive function
	_receive_buffer_head = _receive_buffer_tail = 0;
	setRxIntMsk(true);
}

void OnePinSerial::setRxIntMsk(bool enable)
{
    if (enable)
      *_pcint_maskreg |= _pcint_maskvalue;
    else
      *_pcint_maskreg &= ~_pcint_maskvalue;
}

// Read data from buffer
int OnePinSerial::read()
{
  // Empty buffer?
  if (_receive_buffer_head == _receive_buffer_tail)
    return -1;

  // Read from "head"
  uint8_t d = _receive_buffer[_receive_buffer_head]; // grab next byte
  _receive_buffer_head = (_receive_buffer_head + 1) % _SS_MAX_RX_BUFF;
  return d;
}

int OnePinSerial::available()
{
  return (_receive_buffer_tail + _SS_MAX_RX_BUFF - _receive_buffer_head) % _SS_MAX_RX_BUFF;
}

void OnePinSerial::sendBreak()
{
  volatile uint8_t *reg = _transmitPortRegister;
  uint8_t reg_mask = _transmitBitMask;
  uint8_t inv_mask = ~_transmitBitMask;
  uint8_t clrMask = _pcint_clrMask;
  uint8_t oldSREG = SREG;
  uint16_t delay = _tx_delay;
  cli();                        // turn off interrupts for a clean txmit

  *reg |= reg_mask;             // send 0 (DDR becomes output)
  for (uint8_t i = 0; i < 15; i++) {
    tunedDelay(delay);
  }
  *reg &= inv_mask;           // send 1 (DDR becomes input with pull up)
  
  PCIFR |= clrMask;           // clear any outstanding pin change interrupt
  SREG = oldSREG;             // turn interrupts back on
}

void OnePinSerial::enable (bool enable) {
  setRxIntMsk(enable);
}

void OnePinSerial::sendCmd(const uint8_t loc[], uint8_t len) {
  if (_fastRate) {
    write(loc, len);
  } else {
    for (byte ii = 0; ii < len; ii++) {
      write(loc[ii]);
    }
  }
} 

void OnePinSerial::write(const uint8_t loc[], uint8_t len)
{
  // By declaring these as local variables, the compiler will put them
  // in registers _before_ disabling interrupts and entering the
  // critical timing sections below, which makes it a lot easier to
  // verify the cycle timings
  volatile uint8_t *reg = _transmitPortRegister;
  uint8_t reg_mask = _transmitBitMask;
  uint8_t inv_mask = ~_transmitBitMask;
  uint8_t clrMask = _pcint_clrMask;
  uint8_t oldSREG = SREG;
  uint16_t delay = _tx_delay;
  
  cli();  // turn off interrupts for a clean txmit
  
  for (byte ii = 0; ii < len; ii++) {
    uint8_t b = loc[ii];
    
    // Write the start bit
    *reg |= reg_mask;       // send 0 (DDR becomes output)
    
    tunedDelay(delay);
    
    // Write each of the 8 bits
    for (uint8_t i = 8; i > 0; --i) {
      if (b & 1) {
        *reg &= inv_mask;         // send 1 (DDR becomes input with pull up)
      } else {
        *reg |= reg_mask;         // send 0 (DDR becomes output)
      }
      tunedDelay(delay);
      b >>= 1;
    }
    // restore pin to natural state
    *reg &= inv_mask;             // send stop bit (1) (DDR becomes input with pull up)
    if (ii < (len - 1)) {
      tunedDelay(delay);
      tunedDelay(delay);
    }
  }
  
  PCIFR |= clrMask;           // clear any outstanding pin change interrupt
  SREG = oldSREG;             // turn interrupts back on
}

void OnePinSerial::write(uint8_t b)
{
  // By declaring these as local variables, the compiler will put them
  // in registers _before_ disabling interrupts and entering the
  // critical timing sections below, which makes it a lot easier to
  // verify the cycle timings
  volatile uint8_t *reg = _transmitPortRegister;
  uint8_t reg_mask = _transmitBitMask;
  uint8_t inv_mask = ~_transmitBitMask;
  uint8_t clrMask = _pcint_clrMask;
  uint8_t oldSREG = SREG;
  uint16_t delay = _tx_delay;

  cli();  // turn off interrupts for a clean txmit

  // Write the start bit
  *reg |= reg_mask; 			// send 0 (DDR becomes output)

  tunedDelay(delay);

  // Write each of the 8 bits
  for (uint8_t i = 8; i > 0; --i)
  {
    if (b & 1) // choose bit
      *reg &= inv_mask;         // send 1 (DDR becomes input with pull up)
    else
      *reg |= reg_mask;         // send 0 (DDR becomes output)

    tunedDelay(delay);
    b >>= 1;
  }

  // restore pin to natural state
  *reg &= inv_mask;           // send 1 (DDR becomes input with pull up)

 
  PCIFR |= clrMask;           // clear any outstanding pin change interrupt
  SREG = oldSREG;             // turn interrupts back on

  tunedDelay(delay);
}

