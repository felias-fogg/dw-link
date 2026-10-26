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
#ifdef ARDUINO_AVR_ATMEL_ATMEGA328P_XMINI
#define MAXHOSTBPS 57600UL    // the mEDBG of the xmini cannot do 115200 bps
#else
#define MAXHOSTBPS 115200UL   // the fastest PAR_BAUD_RATE the host may ask for
#endif
// #define STUCKAT1PC 1       // allow also MCUs that have PCs with stuck-at-1 bits
// #define HIGHSPEEDDW 1      // allow for DW speed up to 250 kbps

// these constants should stay undefined for the ordinary user
// -----------------------------------------------------------

// #define NO90   1           // remove AT90 MCUs
// #define NOMEGA 1           // remove ATmega MCUs
// #define NOTINY 1           // remove ATtiny MCUs
// #define TXODEBUG 1         // allow debug output over TXOnly line
// #define CONSTDWSPEED 1     // constant communication speed with target
// #define SCOPEDEBUG 1       // activate scope debugging on PORTC
// #define FREERAM  1         // measure free ram
// #define UNITALL 1          // enable all unit tests
// #define UNITDW 1           // enable debugWIRE unit tests
// #define UNITTG 1           // enable target unit tests

#if UNITALL == 1
#undef  UNITDW
#define UNITDW 1
#undef  UNITTG
#define UNITTG 1
#endif

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
#define SPEEDHIGH     300000UL // maximum communication speed limit for DW
#define SPEEDLOW      150000UL // normal speed limit
#if HIGHSPEED
#define SPEEDLIMIT SPEEDHIGH
#else
#undef SPEEDLIMIT
#define SPEEDLIMIT SPEEDLOW
#endif

// number of tolerable timeouts for one DW command
#define TIMEOUTMAX 20

// types of fatal errors
#define NO_FATAL 0
#define CONNERR_NO_ISP_REPLY 1 // connection error: no ISP reply
#define CONNERR_NO_DW_REPLY 2 // connection error: no DW reply
#define CONNERR_UNSUPPORTED_MCU 3 // connection error: MCU not supported
#define CONNERR_CANNOT_CLEAR_LOCK_BITS 4 // connection error: lock bits could not be cleared
#define CONNERR_LOCK_BITS_NOT_MANAGED 5 // connection error: lock bits are set
#define CONNERR_BOOTRST 6 // connection error: could not unprogram bootrst
#define CONNERR_STUCKAT1_PC 7 // connection error: MCU has PC with stuck-at-one bits
#define CONNERR_CAPACITIVE_LOAD 8 // connection error: capacitive load on reset line
#define CONNERR_NO_TARGET_POWER 9 // target has no power
#define CONNERR_WRONG_MCU 10 // wrong MCU (detected by monitor mcu command)
#define CONNERR_CANNOT_SET_DWEN 11 // Cannot set DWEN with ISP programming
#define CONNERR_DWEN_NOT_MANAGED 12 // DWEN not managed by dw-link
#define CONNERR_UNKNOWN 14 // unknown connection error
#define NO_FREE_SLOT_FATAL 101 // no free slot in BP structure
#define PACKET_LEN_FATAL 102 // packet length too large
#define WRONG_MEM_FATAL 103 // wrong memory type
#define NEG_SIZE_FATAL 104 // negative size of buffer
#define RESET_FAILED_FATAL 105 // reset failed
#define READ_PAGE_ADDR_FATAL 106 // an address that does not point to start of a page in read operation
#define FLASH_READ_FATAL 107 // error when reading from flash memory
#define SRAM_READ_FATAL 108 //  error when reading from sram memory
#define WRITE_PAGE_ADDR_FATAL 109 // wrong page address when writing
#define FLASH_ERASE_FATAL 110 // error when erasing flash memory
#define NO_LOAD_FLASH_FATAL 111 // error when loading page into flash buffer
#define FLASH_PROGRAM_FATAL 112 // error when programming flash page
#define FLASH_VERIFY_FATAL 113 // error when verifying a flashed page
#define HWBP_ASSIGNMENT_INCONSISTENT_FATAL 114 // HWBP assignemnt is inconsistent
#define SELF_BLOCKING_FATAL 115 // there shouldn't be a BREAK instruction in the code
#define FLASH_READ_WRONG_ADDR_FATAL 116 // trying to read a flash word at a non-even address
#define NO_STEP_FATAL 117 // could not do a single-step operation
#define RELEVANT_BP_NOT_PRESENT 118 // identified relevant BP not present any longer
#define INPUT_OVERLFOW_FATAL 119 // input buffer overflow - should not happen at all!
#define WRONG_FUSE_SPEC_FATAL 120 // specification of a fuse we are not prepared to change
#define BREAKPOINT_UPDATE_WHILE_FLASH_PROGRAMMING_FATAL 121 // should not happen!
#define DW_TIMEOUT_FATAL 122 // timeout while reading from DW line
#define DW_READREG_FATAL 123 // timeout while register reading
#define DW_READIOREG_FATAL 124 // timeout during register read operation
#define REENABLERWW_FATAL 125 // timeout during reeanble RWW operation
#define EEPROM_READ_FATAL 126 // timeout during EEPROM read
#define BAD_INTERRUPT_FATAL 127 // bad interrupt
#define INCONS_OPCODE_CLASSIFCATION_FATAL 128 // inconsistent opcode classification

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
const byte AUTODW = 3; // will be ignored when SCOPEDEBUG or TXODEBUG
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
  boolean      avreplus;     // is an AVRe+ architecture MCU (includes MUL instruction)
  unsigned int ramsz;        // SRAM size in bytes
  unsigned int rambase;      // base address of SRAM
  unsigned int eepromsz;     // size of EEPROM in bytes
  unsigned long flashsz;     // size of flash memory in bytes
  byte         dwdr;         // address of DWDR register
  unsigned int pagesz;       // page size of flash memory in bytes
  boolean      erase4pg;     // 1 when the MCU has a 4-page erase operation
  unsigned int bootaddr;     // highest address of possible boot section  (0 if no boot support)
  byte         eecr;         // io-reg address of EECR register
  byte         eearh;        // io-reg address of EARL register (0 if none)
  boolean      described;    // a device descriptor has been received (the self-test sets the fields itself)
  unsigned int extiotop;     // last extended I/O address (memory address), 0 if none
  byte         readio[60];   // descriptor: one bit per I/O location from 0x20 on that may be read
  byte         dwenbase;     // base address for dwen
  byte         dwenmask;     // bit mask for DWEN fuse
  byte         bootrstbase;  // base address for BOOTRST fuse
  byte         bootrstmask;  // bit mask for BOOTRST
  byte         sutmsk;       // bit mask for selecting SUT
  byte         eedr;         // address of EEDR (computed from EECR)
  byte         eearl;        // address of EARL (computed from EECR)
  unsigned int targetpgsz;   // target page size (depends on pagesize and erase4pg)
  byte         stuckat1byte; // fixed bits in high byte of pc
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
void measureRam(void);
int freeRam(void);

#endif
