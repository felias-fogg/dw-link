// picoUART configuration

#ifndef PU_BAUD_RATE
#define PU_BAUD_RATE 19200L            // default baud rate
#endif

#if defined(__AVR_ATtiny13__) || defined(__AVR_ATtiny25__) || defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny85__) 
// port and bit for Tx and Rx - can be same
#define PU_TX B,1
#define PU_RX B,4
#elif defined(__AVR_ATtiny2313__) || defined(__AVR_ATtiny4313__) || defined(__AVR_ATtiny48__) || \
  defined(__AVR_ATtiny88__) || defined(__AVR_ATmega48A__) || defined(__AVR_ATmega48PA__) || \
  defined(__AVR_ATmega88A__) || defined(__AVR_ATmega88PA__) || defined(__AVR_ATmega168A__) || \
  defined(__AVR_ATmega168PA__) || defined(__AVR_ATmega328__) || defined(__AVR_ATmega328P__)
#define PU_TX B,0
#define PU_RX B,1
#elif defined(__AVR_ATtiny24__) || defined(__AVR_ATtiny44__) || defined(__AVR_ATtiny84__) || \
  defined(__AVR_ATtiny261__) || defined(__AVR_ATtiny461__) || defined(__AVR_ATtiny861__) || \
  defined(__AVR_ATtiny87__) ||  defined(__AVR_ATtiny167__) || defined(__AVR_ATtiny1634__) || \
  defined(__AVR_ATtiny441__) || defined(__AVR_ATtiny841__) || define(__AVR_ATtiny43U__) || \
  defined(__AVR_ATtiny828__) 
#define PU_TX A,0
#define PU_RX A,1
#elif defined(__AVR_ATmega8U2__) || defined(__AVR_ATmega16U2__) || defined(__AVR_ATmega32U2__)
#define PU_TX B,5
#define PU_RX B,6
#else
#error "Unsupported MCU"
#endif



// disable interrupts during Tx and Rx
#define PU_DISABLE_IRQ 1

// I/O register macros
#define GBIT(r,b)       b
#define GPORT(r,b)      (PORT ## r)
#define GDDR(r,b)       (DDR ## r)
#define GPIN(r,b)       (PIN ## r)
#define get_bit(io)     GBIT(io)
#define get_port(io)    GPORT(io)
#define get_ddr(io)     GDDR(io)
#define get_pin(io)     GPIN(io)

#define PUTXBIT     get_bit(PU_TX)
#define PUTXPORT    get_port(PU_TX)
#define PUTXDDR     get_ddr(PU_TX)
#define PURXBIT     get_bit(PU_RX)
#define PURXPIN     get_pin(PU_RX)
