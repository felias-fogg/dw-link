#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <avr/io.h>
#include "pu_config.h"

// make these functions work like type-safe macros
#define MUST_INLINE __attribute((always_inline)) inline

const float PUBIT_CYCLES = F_CPU/(PU_BAUD_RATE*1.0);

// delay based on cycle count of asm code + 0.5 for rounding
const int PUTXWAIT = PUBIT_CYCLES - 7 + 0.5;
const int PURXWAIT = PUBIT_CYCLES - 5 + 0.5;

// correct for PURXWAIT skew in PURXSTART calculation
// skew is half of 7 delay intervals between 8 bits
const float PUSKEW = ( PUBIT_CYCLES - (int)(PUBIT_CYCLES + 0.5) ) * 3.5;

// Time from falling edge of start bit to sample 1st bit is 1.5 *
// bit-time. Subtract 2c for sbic, 1 for ldi, 1 for lsr, and PURXWAIT.
// Subtract 1.5 cycles because start bit detection is accurate to
// +-1.5 cycles.  Add 0.5 cycles for int rounding, and add skew.
const int PURXSTART = PUBIT_CYCLES*1.5 -5.5 -PURXWAIT + 0.5 + PUSKEW;

extern "C" void putx(uint8_t c);
uint8_t purx();

// PCINT-based Rx functions limited to t13 and t85

#ifdef PCMSK

// read purx_data & re-enable ISR
uint8_t pu_read();

// data is ready when PCINT is disabled
MUST_INLINE bool purx_dataready() { return !(PCMSK & 1<<PURXBIT); } 

#endif  // PCMSK
