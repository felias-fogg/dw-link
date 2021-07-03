/*
 *	OnePinSerial.h (based on SoftwareSerial.h, formerly NewSoftSerial.h) - 
*/

#ifndef OnePinSerial_h
#define OnePinSerial_h

#include <inttypes.h>

/******************************************************************************
* Definitions
******************************************************************************/

#ifndef _SS_MAX_RX_BUFF
#define _SS_MAX_RX_BUFF 128 // RX buffer size
#endif

#ifndef GCC_VERSION
#define GCC_VERSION (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)
#endif

class OnePinSerial 
{
private:
  // per object data
  uint8_t _ioPin;
  uint8_t _receiveBitMask;
  volatile uint8_t *_receivePortRegister;
  uint8_t _transmitBitMask;
  volatile uint8_t *_transmitPortRegister;
  volatile uint8_t *_pcint_maskreg;
  uint8_t _pcint_maskvalue;
  uint8_t _pcint_clrMask;

  // Expressed as 1-cycle delays (must never be 0!)
  uint16_t _rx_delay_centering;
  uint16_t _rx_delay_intrabit;
  uint16_t _rx_delay_stopbit;
  uint16_t _tx_delay;

  // static data
  static uint8_t _receive_buffer[_SS_MAX_RX_BUFF]; 
  static volatile uint8_t _receive_buffer_tail;
  static volatile uint8_t _receive_buffer_head;
  static OnePinSerial *active_object;

  // private methods
  inline void recv() __attribute__((__always_inline__));
  uint8_t rx_pin_read();
  inline void setRxIntMsk(bool enable) __attribute__((__always_inline__));

  // Return num - sub, or 1 if the result would be < 1
  static uint16_t subtract_cap(uint16_t num, uint16_t sub);

  // private static method for timing
  static inline void waitUntil(uint16_t release);
  static inline void startInterval(void);

public:
  // public methods
  OnePinSerial(uint8_t ioPin);
  void begin(long speed);
  void enable(bool);
  void sendBreak();

  virtual void sendCmd(const uint8_t buf[], uint8_t len);
  virtual void write(uint8_t buf);
  virtual void write(const uint8_t buf[], uint8_t len);
  virtual int read();
  virtual int available();
  operator bool() { return true; }
  
  // public only for easy access by interrupt handlers
  static inline void handle_interrupt() __attribute__((__always_inline__));
};

// Arduino 0012 workaround
#undef int
#undef char
#undef long
#undef byte
#undef float
#undef abs
#undef round

#endif
