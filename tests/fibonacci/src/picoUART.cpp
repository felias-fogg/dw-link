/* optimized half-duplex high-speed AVR serial uart
 * Ralph Doncaster 2020 open source MIT license
 *
 * picoUART is accurate to the cycle (+- 0.5 cycle error)
 * 0.64% error at 115.2k/8M and 0.4% error at 115.2k/9.6M
 *
 * capable of single-pin operation (PU_TX = PU_RX) as follows:
 * connect MCU pin to host RX line, and a 1.5k-4.7k resistor between
 * host RX and TX line.  Note this also gives local echo.
 * 
 * 20200123 version 0.5
 * 20200123 version 0.6 - improve inline asm
 * 20200201 version 0.7 - use push/pull during tx 
 * 20200203 version 0.8 - add prints, prefix globals with PU_
 * 20200209 version 0.9 - rewrite in mostly C 
 */

#include <avr/io.h>
#include <avr/interrupt.h>

#include "picoUART.h"

// saves SREG, disables interrupts
struct IntModeSave
{
    IntModeSave() {
#ifdef PU_DISABLE_IRQ
        sreg_save = SREG;
        cli();
#endif
    }
    ~IntModeSave() {
#ifdef PU_DISABLE_IRQ
        SREG = sreg_save;
#endif
    }

  private:
    uint8_t sreg_save;
};

#define _concat(a, b) (a ## b)
#define concat(a, b)  _concat(a, b)

typedef union {
    unsigned i16;
    struct { uint8_t lo8; uint8_t hi8; };
} frame;

typedef union {
    uint8_t c;
    struct {
         int b0:1; int b1:1; int b2:1; int b3:1; int b4:1; int b5:1; int b6:1; int b7:1;
    };
} bits;


extern "C" void putx(uint8_t c)
{
    // pin frame to r25:24 since c will already be in r24 for ABI
    register frame f asm("r24");
    f.lo8 = c;

    PUTXPORT &= ~(1<<PUTXBIT);          // disable pullup
    IntModeSave isave;

    PUTXDDR |= (1<<PUTXBIT);            // low for start bit

    // hi8 b0 set for stop bit, b1 set for line idle state
    f.hi8 = 0x03;
    bits psave = {PUTXPORT};

    //do {
    txbit:
        __builtin_avr_delay_cycles(PUTXWAIT);
        // macro hack to set correct bit
        concat(psave.b, PUTXBIT) = f.lo8 & 0x01 ? 1 : 0;
        f.i16 >>= 1;
        PUTXPORT = psave.c; 
    // tx more bits if f.lo8 not equal to 0
    asm goto ("brne %l[txbit]" :::: txbit);
    //} while (f.lo8);
    PUTXDDR &= ~(1<<PUTXBIT);            // revert to input mode
}

uint8_t purx()
{
    // wait for idle state (high) to avoid reading last bit of last frame
    // except for at very high baud rates ( <= 11 cycles per bit )
    if ( PUBIT_CYCLES + 0.5 > 11 )
        while (! (PURXPIN & (1<<PURXBIT)) ); 

    IntModeSave isave;

    // wait for start bit
    while ( PURXPIN & (1<<PURXBIT) ); 
    uint8_t c = 0x80;                   // bit shift counter
    // wait for the middle of the start bit
    __builtin_avr_delay_cycles(PURXSTART);

    rxbit:
        __builtin_avr_delay_cycles(PURXWAIT);
        c /= 2;                         // shift right
        if ( PURXPIN & (1<<PURXBIT) )
            c |= 0x80;
    // read bits until carry set
    asm goto ("brcc %l[rxbit]" :::: rxbit);

    return c;
}

