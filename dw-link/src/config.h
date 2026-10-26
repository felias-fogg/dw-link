// dw-link -- configuration, shared constants, types and system state
// Included by every layer, so that the configuration macros are seen everywhere.
#ifndef DWLINK_CONFIG_H
#define DWLINK_CONFIG_H

#define VERSION "7.0.0-dev"
#define VERSION_MAJOR 7       // reported as firmware version in the mkII sign-on
#define VERSION_MINOR 0

// some constants, you may want to change
// --------------------------------------
#define MKII_BPS 19200UL      // a serial JTAGICE mkII starts at this speed (AVR067)

// these constants should stay undefined for the ordinary user
// -----------------------------------------------------------

// #define TXODEBUG 1         // allow debug output over TXOnly line

#if F_CPU < 16000000UL
#error "dw-link needs at least 16 MHz clock frequency"
#endif

#include <Arduino.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/wdt.h>
#include <util/delay.h>
#include "dwSerial.h"
#include "SingleWireSerial_config.h"
#if TXODEBUG
#include "TXOnlySerial.h" // only needed for (meta-)debuging
#endif
#include "debug.h" // some (meta-)debug macros

#define MAXPAGESIZE 256 // maximum number of bytes in one flash memory page (for the 64K MCUs)

// communication bit rates
#define SPEEDLIMIT    150000UL // default limit for the debugWIRE speed (host: vendor parameter 0x72)

// number of tolerable timeouts for one DW command
#define TIMEOUTMAX 20

// types of fatal errors
// error numbers: the host reads them with the vendor parameter 0x71 (PAR_DWLINK_ERROR) and
// explains them from docs/internal_errors.md; numbers of codes no longer used are not reused
#define NO_FATAL 0
#define CONNERR_NO_ISP_REPLY 1 // connection error: no ISP reply
#define CONNERR_NO_DW_REPLY 2 // connection error: no DW reply
#define CONNERR_CAPACITIVE_LOAD 8 // connection error: capacitive load on reset line
#define CONNERR_NO_TARGET_POWER 9 // target has no power: RESET low before the break
#define RESET_FAILED_FATAL 105 // reset failed
#define READ_PAGE_ADDR_FATAL 106 // an address that does not point to start of a page in read operation
#define FLASH_READ_FATAL 107 // error when reading from flash memory
#define SRAM_READ_FATAL 108 //  error when reading from sram memory
#define WRITE_PAGE_ADDR_FATAL 109 // wrong page address when writing
#define FLASH_ERASE_FATAL 110 // error when erasing flash memory
#define FLASH_PROGRAM_FATAL 112 // error when programming flash page
#define FLASH_VERIFY_FATAL 113 // error when verifying a flashed page
#define FLASH_READ_WRONG_ADDR_FATAL 116 // trying to read a flash word at a non-even address
#define RELEVANT_BP_NOT_PRESENT 118 // identified relevant BP not present any longer
#define INPUT_OVERLFOW_FATAL 119 // input buffer overflow - should not happen at all!
#define DW_TIMEOUT_FATAL 122 // timeout while reading from DW line
#define DW_READREG_FATAL 123 // timeout while register reading
#define DW_READIOREG_FATAL 124 // timeout during register read operation
#define REENABLERWW_FATAL 125 // timeout during reeanble RWW operation
#define EEPROM_READ_FATAL 126 // timeout during EEPROM read
#define BAD_INTERRUPT_FATAL 127 // bad interrupt

// instruction codes
const unsigned int BREAKCODE = 0x9598;
const unsigned int SLEEPCODE = 0x9588;
const unsigned int NOPCODE   = 0x0000;

// some dw commands
const byte DW_STOP_CMD = 0x06;
const byte DW_RESET_CMD = 0x07;

enum statetype {NOTCONN_STATE, PWRCYC_STATE, ERROR_STATE, DWCONN_STATE, LOAD_STATE, RUN_STATE, PROG_STATE};

enum ispspeedtype {SUPER_SLOW_ISP, SLOW_ISP, NORMAL_ISP }; // isp speed: 0.8 kHz, 20 kHz, 50 kHz

struct context {
  unsigned int wpc; // pc (using word addresses)
  unsigned int sp; // stack pointer
  statetype state; // system state
  byte sreg;    // status reg
  byte regs[32]; // general purpose regs
  boolean saved:1; // all of the regs have been saved
  boolean levelshifting:1; // true when using dw-probe sitting on an Arduino board
  unsigned long bps; // debugWIRE communication speed
  ispspeedtype ispspeed;
};
extern context ctx;

// options the host sets (from D1 on through mkII parameters)
struct options {
  byte tmask; // run timers while stopped when tmask = 0xDF, freeze when tmask = 0xFF
  boolean readbeforewrite:1; // read before write when loading
  boolean verifyload:1; // check whether flash was successful
};
extern options opt;

extern byte fatalerror;

// pins
const byte IVSUP = 2;
const byte DEBTX = 3;
const byte AUTODW = 3; // ignored when TXODEBUG uses the pin
const byte TISP = 4;
const byte SENSEBOARD = 5;
const byte LEDGND = 6;
const byte SYSLED = 7;
const byte DWLINE = 8; // This pin cannot be changed since it is the only pin with input capture!
const byte TODSCK = 10;
const byte TMOSI = 11;
const byte TMISO = 12;
const byte TSCK = 13;
const byte SUPANALOG = A4;

extern byte ledmask, todsckmask, tmosimask, tmisomask, tsckmask;
extern volatile byte *ledout, *todsckmode, *tmosiout, *tmosimode, *tmisoin, *tsckout;

//  MCU parameters
struct mcutype {
  unsigned int sig;          // two byte signature
  unsigned long flashsz;     // size of flash memory in bytes
  byte         dwdr;         // address of DWDR register
  unsigned int pagesz;       // page size of flash memory in bytes
  boolean      erase4pg;     // 1 when the MCU has a 4-page erase operation
  unsigned int bootaddr;     // highest address of possible boot section  (0 if no boot support)
  byte         eecr;         // io-reg address of EECR register
  byte         eearh;        // io-reg address of EARL register (0 if none)
  boolean      described;    // device data have been received (descriptor 0x0C or context 0x36)
  unsigned int extiotop;     // last extended I/O address (memory address), 0 if none
  byte         readio[60];   // descriptor: one bit per I/O location from 0x20 on that may be read
  byte         eedr;         // address of EEDR (computed from EECR)
  byte         eearl;        // address of EARL (computed from EECR)
  unsigned int targetpgsz;   // target page size (depends on pagesize and erase4pg)
};
extern mcutype mcu;

#if TXODEBUG
extern TXOnlySerial deb;
#endif

// system functions (dw-link.ino)
void dwlrestart(void);
void blockIRQ(void);
void unblockIRQ(void);
void setupio(void);
void initSession(void);
void initOptions(void);
void reportFatalError(byte, boolean);
void setSysState(statetype);
void power(boolean);
extern boolean poweron; // the target supply (dw-probe shield) is switched on
boolean targetOffline(void);

#endif
