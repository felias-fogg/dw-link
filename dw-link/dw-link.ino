// This is an implementation of the GDB remote serial protocol for debugWIRE.
// It should run on all ATmega328 boards and provides a hardware debugger
// for the classic ATtinys and some small ATmegas
//
// Copyright (c) 2021-2025 Bernhard Nebel
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// NOTE: The RESET line of the target should have a 10k pull-up resistor and there
//       should not be capacitative load on the RESET line. So, when you want
//       to debug standard Arduino Uno boards, you have to disconnect the capacitor needed
//       for auto reset. On the original Uno boards there is a bridge labeled "RESET EN"
//       that you can cut. This does not apply to the Pro Mini boards! For Pro Mini
//       boards you have to make sure that only the TX/RX as well as the Vcc/GND
//       pins are connected.
//
// Some of the code is inspired by and/or copied from
// - dwire-debugger (https://github.com/dcwbrown/dwire-debug)
// - debugwire-gdb-bridge (https://github.com/ccrause/debugwire-gdb-bridge)
// - DebugWireDebuggerProgrammer (https://github.com/wholder/DebugWireDebuggerProgrammer/),
// - AVR-GDBServer (https://github.com/rouming/AVR-GDBServer),  and
// - avr_debug (https://github.com/jdolinay/avr_debug).
// And, of course, all of it would have not been possible without the work of RikusW
// on reverse engineering of the debugWIRE protocol: http://www.ruemohr.org/docs/debugwire.html
//
// The following documents were helpful in implementing the RSP server:
// The official GDB doc: https://sourceware.org/gdb/current/onlinedocs/gdb/Remote-Protocol.html
// Jeremy Bennett's description of an implementation of an RSP server:
//    https://www.embecosm.com/appnotes/ean4/embecosm-howto-rsp-server-ean4-issue-2.html
// 
// You can run it on an UNO, a Nano, or a Pro Mini.
// For the UNO, I designed a shield with different voltage levels and
// level shifters, whichyou can buy on Tindie.
//
// I thought that the sketch should also work with the Leonardo-like boards
// and with the Mega board. For the former, I got stuck with the flashtest program.
// USB and tight interrupt timing do not seem to go together.
// For the latter, I experienced non-deterministic failures of unit tests, probably
// because relevant input ports are not in the I/O range and therefore the tight timing
// constraints are not satisfied.

#define VERSION "5.2.4"

// some constants, you may want to change
// --------------------------------------
#ifndef PROGBPS
#define PROGBPS 19200         // ISP programmer communication speed
#endif
#ifndef HOSTBPS 
#define HOSTBPS 115200UL      // safe default speed for the host 
//#define HOSTBPS 230400UL    // works with UNOs, but not with Nanos
#endif
// #define STUCKAT1PC 1       // allow also MCUs that have PCs with stuck-at-1 bits
// #define HIGHSPEEDDW 1      // allow for DW speed up to 250 kbps

// these constants should stay undefined for the ordinary user
// -----------------------------------------------------------
// #define TXODEBUG 1         // allow debug output over TXOnly line
// #define CONSTDWSPEED 1     // constant communication speed with target
// #define OFFEX2WORD 1       // instead of simu. use offline execution for 2-word instructions
// #define SCOPEDEBUG 1       // activate scope debugging on PORTC
// #define FREERAM  1         // measure free ram
// #define UNITALL 1          // enable all unit tests
// #define UNITDW 1           // enable debugWIRE unit tests
#define UNITTG 1           // enable target unit tests
// #define UNITGDB 1          // enable gdb function unit tests
// #define NOMONITORHELP 1    // disable monitor help function
// #define NOISPPROG 1        // disable ISP programmer
// #define ILLOPDETECT 1      // detect illegal opcodes when starting execution

#if UNITALL == 1
#undef  UNITDW
#define UNITDW 1
#undef  UNITTG
#define UNITTG 1
#undef  UNITGDB
#define UNITGDB 1
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
#include "src/dwSerial.h"
#include "src/SingleWireSerial_config.h"
#if TXODEBUG
#include "src/TXOnlySerial.h" // only needed for (meta-)debuging
#endif
#include "src/debug.h" // some (meta-)debug macros

// convert an integer literal into its hex string representation (without the 0x prefix)
// some size restrictions

#define MAXBUF 164 // input buffer for GDB communication, initial packet is longer, but will be mostly ignored
#define MAXBUFHEXSTR "90"  // hex representation string of MAXBUF 
#define MAXMEMBUF 150 // size of memory buffer
#define MAXPAGESIZE 256 // maximum number of bytes in one flash memory page (for the 64K MCUs)
#define MAXBREAK 20 // maximum of active breakpoints (we need double as many entries for lazy breakpoint setting/removing!)
#define MAXNAMELEN 16 // maximal length of MCU name (incl. NUL terminator)

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

// signals
#define SIGHUP  1     // connection to target lost
#define SIGINT  2     // Interrupt  - user interrupted the program (UART ISR) 
#define SIGILL  4     // Illegal instruction
#define SIGTRAP 5     // Trace trap  - stopped on a breakpoint
#define SIGABRT 6     // Abort because of some fatal error
// types of fatal errors
#define NO_FATAL 0
#define CONNERR_NO_ISP_REPLY 1 // connection error: no ISP reply
#define CONNERR_NO_DW_REPLY 2 // connection error: no DW reply
#define CONNERR_UNSUPPORTED_MCU 3 // connection error: MCU not supported
#define CONNERR_LOCK_BITS 4 // connection error: lock bits are set
#define CONNERR_STUCKAT1_PC 5 // connection error: MCU has PC with stuck-at-one bits
#define CONNERR_CAPACITIVE_LOAD 6 // connection error: capacitive load on reset line
#define CONNERR_NO_TARGET_POWER 7 // target has no power
#define CONNERR_WRONG_MCU 8 // wrong MCU (detected by monitor mcu command)
#define CONNERR_UNKNOWN 9 // unknown connection error
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
#define WRONG_FUSE_SPEC_FATAL 120 // specification of a fuse we are not prepafred to change
#define BREAKPOINT_UPDATE_WHILE_FLASH_PROGRAMMING_FATAL 121 // should not happen!
#define DW_TIMEOUT_FATAL 122 // timeout while reading from DW line
#define DW_READREG_FATAL 123 // timeout while register reading
#define DW_READIOREG_FATAL 124 // timeout during register read operation
#define REENABLERWW_FATAL 125 // timeout during reeanble RWW operation
#define EEPROM_READ_FATAL 126 // timeout during EEPROM read
#define BAD_INTERRUPT_FATAL 127 // bad interrupt

// some masks to interpret memory addresses
#define MEM_SPACE_MASK 0x00FF0000 // mask to detect what memory area is meant
#define FLASH_OFFSET   0x00000000 // flash is addressed starting from 0
#define SRAM_OFFSET    0x00800000 // RAM address from GBD is (real addresss + 0x00800000)
#define EEPROM_OFFSET  0x00810000 // EEPROM address from GBD is (real addresss + 0x00810000)

// instruction codes
const unsigned int BREAKCODE = 0x9598;
const unsigned int SLEEPCODE = 0x9588;

// some dw commands
const byte DW_STOP_CMD = 0x06;
const byte DW_RESET_CMD = 0x07;

// some GDB variables
struct breakpoint
{
  boolean used:1;         // bp is in use, i.e., has been set before; will be freed when not activated before next execution
  boolean active:1;       // breakpoint is active, i.e., has been set by GDB
  boolean inflash:1;      // breakpoint is in flash memory, i.e., BREAK instr has been set in memory
  boolean hw:1;           // breakpoint is a hardware breakpoint, i.e., not set in memory, but HWBP is used
  unsigned int waddr;  // word address of breakpoint
  unsigned int opcode; // opcode that has been replaced by BREAK (in little endian mode)
} bp[MAXBREAK*2];

byte bpcnt;             // number of ACTIVE breakpoints (there may be as many as MAXBREAK used ones from the last execution!)
byte bpused;            // number of USED breakpoints, which may not all be active
byte maxbreak = MAXBREAK; // actual number of active breakpoints allowed
boolean onlysbp = false; // only software breakpoints allowed

unsigned int hwbp = 0xFFFF; // the one hardware breakpoint (word address)

enum statetype {NOTCONN_STATE, PWRCYC_STATE, ERROR_STATE, DWCONN_STATE, LOAD_STATE, RUN_STATE, PROG_STATE};

enum ispspeedtype {SUPER_SLOW_ISP, SLOW_ISP, NORMAL_ISP }; // isp speed: 0.8 kHz, 20 kHz, 50 kHz

struct context {
  unsigned int wpc; // pc (using word addresses)
  unsigned int sp; // stack pointer
  byte sreg;    // status reg
  byte regs[32]; // general purpose regs
  boolean saved; // all of the regs have been saved
  statetype state; // system state
  boolean levelshifting; // true when using dw-probe sitting on an Arduino board
  boolean autopc; // do an automagic power-cycle
  boolean dwactivated; // will be true after dw has been activated once; if then NOTCONN_STATE, you need to leave!
  boolean readbeforewrite; // read before write when loading
  byte tmask; // run timers while stopped when tmask = 0xDF, freeze when tmask = 0xFF
  unsigned long bps; // debugWIRE communication speed
  boolean safestep; // if true, then single step in a safe way, i.e. not interruptable
  ispspeedtype ispspeed;
  boolean verifyload; // check whether flash was successful
  boolean onlyloaded; // allow execution only after load command
  boolean notloaded; // when no binary has been loaded yet
} ctx;

// use LED to signal system state
// LED off = not connected to target system, ISP connected, or transitional
// LED flashing every second = power-cycle target in order to enable debugWIRE
// LED blinking every 1/10 second = fatal error
// LED constantly on = connected to target
// LED slow blinking = ISP programming
const unsigned int ontimes[8] =  {0, 100, 150, 1, 1, 1, 750};
const unsigned int offtimes[8] = {1, 1000, 150, 0, 0, 0, 750};
volatile unsigned int ontime; // number of ms on
volatile unsigned int offtime; // number of ms off

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

byte ledmask, todsckmask, tmosimask, tmisomask, tsckmask;
volatile byte *ledout, *todsckmode, *tmosiout, *tmosimode, *tmisoin, *tsckout;


// MCU names
const char unknown[] PROGMEM ="Unknown";
const char attiny13[] PROGMEM = "ATtiny13";
const char attiny43[] PROGMEM = "ATtiny43U";
const char attiny2313[] PROGMEM = "ATtiny2313";
const char attiny2313a[] PROGMEM = "ATtiny2313A";
const char attiny4313[] PROGMEM = "ATtiny4313";
const char attiny24[] PROGMEM = "ATtiny24";
const char attiny44[] PROGMEM = "ATtiny44";
const char attiny84[] PROGMEM = "ATtiny84";
const char attiny441[] PROGMEM = "ATtiny441";
const char attiny841[] PROGMEM = "ATtiny841";
const char attiny25[] PROGMEM = "ATtiny25";
const char attiny45[] PROGMEM = "ATtiny45";
const char attiny85[] PROGMEM = "ATtiny85";
const char attiny261[] PROGMEM = "ATtiny261";
const char attiny461[] PROGMEM = "ATtiny461";
const char attiny861[] PROGMEM = "ATtiny861";
const char attiny87[] PROGMEM = "ATtiny87";
const char attiny167[] PROGMEM = "ATtiny167";
const char attiny828[] PROGMEM = "ATtiny828";
const char attiny48[] PROGMEM = "ATtiny48";
const char attiny88[] PROGMEM = "ATtiny88";
const char attiny1634[] PROGMEM = "ATtiny1634";
const char atmega48a[] PROGMEM = "ATmega48";
const char atmega48pa[] PROGMEM = "ATmega48P";
const char atmega48pb[] PROGMEM = "ATmega48PB";
const char atmega88a[] PROGMEM = "ATmega88";
const char atmega88pa[] PROGMEM = "ATmega88P";
const char atmega88pb[] PROGMEM = "ATmega88PB";
const char atmega168a[] PROGMEM = "ATmega168";
const char atmega168pa[] PROGMEM = "ATmega168P";
const char atmega168pb[] PROGMEM = "ATmega168PB";
const char atmega328[] PROGMEM = "ATmega328";
const char atmega328p[] PROGMEM = "ATmega328P";
const char atmega328pb[] PROGMEM = "ATmega328PB";
const char atmega8u2[] PROGMEM = "ATmega8U2";
const char atmega16u2[] PROGMEM = "ATmega16U2";
const char atmega32u2[] PROGMEM = "ATmega32U2";
const char atmega32c1[] PROGMEM = "ATmega32C1";
const char atmega64c1[] PROGMEM = "ATmega64C1";
const char atmega16m1[] PROGMEM = "ATmega16M1";
const char atmega32m1[] PROGMEM = "ATmega32M1";
const char atmega64m1[] PROGMEM = "ATmega64M1";
const char at90usb82[] PROGMEM = "AT90USB82";
const char at90usb162[] PROGMEM = "AT90USB162";
const char at90pwm1[] PROGMEM = "AT90PWM1";
const char at90pwm2b[] PROGMEM = "AT90PWM2B";
const char at90pwm3b[] PROGMEM = "AT90PWM3B";
const char at90pwm81[] PROGMEM = "AT90PWM81";
const char at90pwm161[] PROGMEM = "AT90PWM161";
const char at90pwm216[] PROGMEM = "AT90PWM216";
const char at90pwm316[] PROGMEM = "AT90PWM316";
const char atmega8hva[] PROGMEM = "ATmega8HVA";
const char atmega16hva[] PROGMEM = "ATmega16HVA";
const char atmega16hvb[] PROGMEM = "ATmega16HVB";
const char atmega16hvbrevb[] PROGMEM = "ATmega16HVBREVB";
const char atmega32hvb[] PROGMEM = "ATmega32HVB";
const char atmega32hvbrevb[] PROGMEM = "ATmega32HVBREVB";
const char atmega64hve2[] PROGMEM = "ATmega64HVE2";

const char Connected[] PROGMEM = "Connected to ";

//  MCU parameters
struct {
  char         required[MAXNAMELEN]; // this is the required type of MCU
  unsigned int sig;          // two byte signature
  boolean      avreplus;     // is an AVRe+ architecture MCU (includes MUL instruction)
  unsigned int ramsz;        // SRAM size in bytes
  unsigned int rambase;      // base address of SRAM
  unsigned int eepromsz;     // size of EEPROM in bytes
  unsigned int flashsz;      // size of flash memory in bytes
  byte         dwdr;         // address of DWDR register
  unsigned int pagesz;       // page size of flash memory in bytes
  boolean      erase4pg;     // 1 when the MCU has a 4-page erase operation
  unsigned int bootaddr;     // highest address of possible boot section  (0 if no boot support)
  byte         eecr;         // io-reg address of EECR register
  byte         eearh;        // io-reg address of EARL register (0 if none)
  const char*  name;         // pointer to name in PROGMEM
  const byte*  mask;         // pointer to mask array in PROGMEM
  byte         dwenfuse;     // bit mask for DWEN fuse in high fuse byte
  byte         sutmsk;       // bit mask for selecting SUT
  byte         eedr;         // address of EEDR (computed from EECR)
  byte         eearl;        // address of EARL (computed from EECR)
  unsigned int targetpgsz;   // target page size (depends on pagesize and erase4pg)
  byte         stuckat1byte; // fixed bits in high byte of pc
} mcu;


struct mcu_info_type {
  unsigned int sig;            // two byte signature
  byte         ramsz_div_64;   // SRAM size
  boolean      rambase_low;    // base address of SRAM; low: 0x60, high: 0x100
  byte         eepromsz_div_64;// size of EEPROM
  byte         flashsz_div_1k; // size of flash memory
  byte         dwdr;           // address of DWDR register
  byte         pagesz_div_2;   // page size 
  boolean      erase4pg;       // 1 when the MCU has a 4-page erase operation
  unsigned int bootaddr;       // highest address of possible boot section  (0 if no boot support)
  byte         eecr;           // address of EECR register
  byte         eearh;          // address of EARL register (0 if none)
  boolean      avreplus;       // AVRe+ architecture
  const char*  name;           // pointer to name in PROGMEM
  const byte*  mask;       // I/O register mask
};

const byte t13mask[] PROGMEM =  { 0x4e, 0};
const byte t2313mask[] PROGMEM = { 0x2c, 0x3f, 0x46, 0x57, 0 };
const byte t4313mask[] PROGMEM = { 0x2c, 0x46, 0x47, 0x57, 0 };
const byte tX4mask[] PROGMEM = { 0x42, 0x47, 0};
const byte tX41mask[] PROGMEM = { 0x42, 0x47, 0x80, 0x90, 0xb0, 0xc8, 0};
const byte tX5mask[] PROGMEM = { 0x42, 0 };
const byte tX61mask[] PROGMEM = { 0x40, 0 };
const byte tX7mask[] PROGMEM = { 0x4e, 0x51, 0xd2, 0 };
const byte t43mask[] PROGMEM = { 0x47, 0 };
const byte t828mask[] PROGMEM = { 0x4e, 0x51, 0x82, 0xc6, 0 };
const byte tX8mask[] PROGMEM = { 0x4e, 0x51, 0x82, 0 };
const byte t1634mask[] PROGMEM = { 0x40, 0x4e, 0x73, 0 };
const byte mX8mask[] PROGMEM = { 0x4e, 0x51, 0x82, 0xc6, 0 };
const byte m328pbmsk[] PROGMEM = { 0x4e, 0x51, 0x82, 0x92, 0xa2, 0xae, 0xc6, 0xce, 0 };
const byte mXu2mask[] PROGMEM = { 0x4e, 0x51, 0x7f, 0xce, 0xf1, 0 }; 
const byte mXy1mask[] PROGMEM = { 0x4e, 0x51, 0x82, 0xd2, 0 };
const byte auX2mask[] PROGMEM = { 0x4e, 0x51, 0x7f, 0xce, 0xf1, 0 };
const byte ap1mask[] PROGMEM = { 0x4e, 0x51, 0x82, 0 };
const byte ap81mask[] PROGMEM = { 0x51, 0x56, 0x58, 0 };
const byte ap161mask[] PROGMEM = { 0x51, 0x56, 0x58, 0x59, 0 };
const byte apXmask[] PROGMEM = { 0x4e, 0x51, 0x82, 0xab, 0xc6, 0xce, 0 };
const byte mHVmsk[] PROGMEM = { 0x4e, 0x51, 0 };
const byte m64HVmsk[] PROGMEM = { 0x4e, 0x51, 0xca, 0 };

// mcu infos (for all AVR mcus supporting debugWIRE)
// untested ones are marked 
	    const mcu_info_type mcu_info[] PROGMEM = {
// sig is signature, sram is sram_size/64, low is 1 iff sram starts at 0x60 instead of at 0x100,
// eep is eep_size/64, flsh is flsh_size/1024, dwdr is io-reg addr of DW reg, pg is flash_page_size/2,
// er4 is 1 iff the erase command erase 4 pages, boot is the (highest) boot sector (word-)address,
// eecr is eeprom control io-reg, eearh is eeprom adress reg high io-reg, 
// sig  sram low eep flsh dwdr  pg er4  boot    eecr eearh  plus name            mask
  {0x9007,  1, 1,  1,  1, 0x2E,  16, 0, 0x0000, 0x1C, 0x00, 0, attiny13,      t13mask},

  {0x910A,  2, 1,  2,  2, 0x1f,  16, 0, 0x0000, 0x1C, 0x00, 0, attiny2313,  t2313mask},
  {0x910A,  2, 1,  2,  2, 0x27,  16, 0, 0x0000, 0x1C, 0x00, 0, attiny2313a, t4313mask},
  {0x920D,  4, 1,  4,  4, 0x27,  32, 0, 0x0000, 0x1C, 0x00, 0, attiny4313,  t4313mask},

  {0x920C,  4, 1,  1,  4, 0x27,  32, 0, 0x0000, 0x1C, 0x00, 0, attiny43,      t43mask},

  {0x910B,  2, 1,  2,  2, 0x27,  16, 0, 0x0000, 0x1C, 0x1F, 0, attiny24,      tX4mask},   
  {0x9207,  4, 1,  4,  4, 0x27,  32, 0, 0x0000, 0x1C, 0x1F, 0, attiny44,      tX4mask},
  {0x930C,  8, 1,  8,  8, 0x27,  32, 0, 0x0000, 0x1C, 0x1F, 0, attiny84,      tX4mask},
  
  {0x9215,  4, 0,  4,  4, 0x27,   8, 1, 0x0000, 0x1C, 0x1F, 0, attiny441,    tX41mask}, 
  {0x9315,  8, 0,  8,  8, 0x27,   8, 1, 0x0000, 0x1C, 0x1F, 0, attiny841,    tX41mask},
  
  {0x9108,  2, 1,  2,  2, 0x22,  16, 0, 0x0000, 0x1C, 0x1F, 0, attiny25,      tX5mask},
  {0x9206,  4, 1,  4,  4, 0x22,  32, 0, 0x0000, 0x1C, 0x1F, 0, attiny45,      tX5mask},
  {0x930B,  8, 1,  8,  8, 0x22,  32, 0, 0x0000, 0x1C, 0x1F, 0, attiny85,      tX5mask},
  
  {0x910C,  2, 1,  2,  2, 0x20,  16, 0, 0x0000, 0x1C, 0x1F, 0, attiny261,    tX61mask},
  {0x9208,  4, 1,  4,  4, 0x20,  32, 0, 0x0000, 0x1C, 0x1F, 0, attiny461,    tX61mask},
  {0x930D,  8, 1,  8,  8, 0x20,  32, 0, 0x0000, 0x1C, 0x1F, 0, attiny861,    tX61mask},
  
  {0x9387,  8, 0,  8,  8, 0x31,  64, 0, 0x0000, 0x1F, 0x22, 0, attiny87,      tX7mask},  
  {0x9487,  8, 0,  8, 16, 0x31,  64, 0, 0x0000, 0x1F, 0x22, 0, attiny167,     tX7mask},

  {0x9314,  8, 0,  4,  8, 0x31,  32, 0, 0x0F80, 0x1F, 0x22, 0, attiny828,    t828mask},

  {0x9209,  4, 0,  1,  4, 0x31,  32, 0, 0x0000, 0x1F, 0x22, 0, attiny48,      tX8mask},  
  {0x9311,  8, 0,  1,  8, 0x31,  32, 0, 0x0000, 0x1F, 0x22, 0, attiny88,      tX8mask},
  
  {0x9412, 16, 0,  4, 16, 0x2E,  16, 1, 0x0000, 0x1C, 0x00, 0, attiny1634,  t1634mask},
  
  {0x9205,  8, 0,  4,  4, 0x31,  32, 0, 0x0000, 0x1F, 0x22, 1, atmega48a,     mX8mask},
  {0x920A,  8, 0,  4,  4, 0x31,  32, 0, 0x0000, 0x1F, 0x22, 1, atmega48pa,    mX8mask},
  {0x9210,  8, 0,  4,  4, 0x31,  32, 0, 0x0000, 0x1F, 0x22, 1, atmega48pb,    mX8mask}, // untested
  {0x930A, 16, 0,  8,  8, 0x31,  32, 0, 0x0F80, 0x1F, 0x22, 1, atmega88a,     mX8mask},
  {0x930F, 16, 0,  8,  8, 0x31,  32, 0, 0x0F80, 0x1F, 0x22, 1, atmega88pa,    mX8mask},
  {0x9316, 16, 0,  8,  8, 0x31,  32, 0, 0x0F80, 0x1F, 0x22, 1, atmega88pb,    mX8mask}, // untested
  {0x9406, 16, 0,  8, 16, 0x31,  64, 0, 0x1F80, 0x1F, 0x22, 1, atmega168a,    mX8mask},
  {0x940B, 16, 0,  8, 16, 0x31,  64, 0, 0x1F80, 0x1F, 0x22, 1, atmega168pa,   mX8mask},
  {0x9415, 16, 0,  8, 16, 0x31,  64, 0, 0x1F80, 0x1F, 0x22, 1, atmega168pb,   mX8mask}, // untested
  {0x9514, 32, 0, 16, 32, 0x31,  64, 0, 0x3F00, 0x1F, 0x22, 1, atmega328,     mX8mask},
  {0x950F, 32, 0, 16, 32, 0x31,  64, 0, 0x3F00, 0x1F, 0x22, 1, atmega328p,    mX8mask},
  {0x9516, 32, 0, 16, 32, 0x31,  64, 0, 0x3F00, 0x1F, 0x22, 1, atmega328pb, m328pbmsk},
  
  {0x9389,  8, 0,  8,  8, 0x31,  32, 0, 0x0000, 0x1F, 0x22, 1, atmega8u2,    mXu2mask}, // untested
  {0x9489,  8, 0,  8, 16, 0x31,  64, 0, 0x0000, 0x1F, 0x22, 1, atmega16u2,   mXu2mask}, // untested
  {0x958A, 16, 0, 16, 32, 0x31,  64, 0, 0x0000, 0x1F, 0x22, 1, atmega32u2,   mXu2mask}, // untested

  {0x9484, 16, 0,  8, 16, 0x31,  64, 0, 0x1F00, 0x1F, 0x22, 1, atmega16m1,   mXy1mask}, // untested
  {0x9586, 32, 0, 16, 32, 0x31,  64, 0, 0x3F00, 0x1F, 0x22, 1, atmega32c1,   mXy1mask}, // untested
  {0x9584, 32, 0, 16, 32, 0x31,  64, 0, 0x3F00, 0x1F, 0x22, 1, atmega32m1,   mXy1mask}, // untested
  {0x9686, 64, 0, 32, 64, 0x31, 128, 0, 0x7F00, 0x1F, 0x22, 1, atmega64c1,   mXy1mask}, // untested
  {0x9684, 64, 0, 32, 64, 0x31, 128, 0, 0x7F00, 0x1F, 0x22, 1, atmega64m1,   mXy1mask}, // untested

  {0x9382,  8, 0,  8,  8, 0x31,  64, 0, 0x1E00, 0x1F, 0x22, 1, at90usb82,    auX2mask}, // untested
  {0x9482,  8, 0,  8, 16, 0x31,  64, 0, 0x3E00, 0x1F, 0x22, 1, at90usb162,   auX2mask}, // untested

  {0x9383,  8, 0,  8,  8, 0x31,  32, 0, 0x0F80, 0x1F, 0x22, 1, at90pwm1,      ap1mask}, // untested 
  {0x9383,  8, 0,  8,  8, 0x31,  32, 0, 0x0F80, 0x1F, 0x22, 1, at90pwm2b,     apXmask}, // untested 
  {0x9383,  8, 0,  8,  8, 0x31,  32, 0, 0x0F80, 0x1F, 0x22, 1, at90pwm3b,     apXmask}, // untested 

  {0x9388,  4, 0,  8,  8, 0x31,  32, 0, 0x0F80, 0x1C, 0x1F, 1, at90pwm81,    ap81mask}, // untested
  {0x948B, 16, 0,  8, 16, 0x31,  64, 0, 0x1F00, 0x1C, 0x1F, 1, at90pwm161,  ap161mask}, // untested

  {0x9483, 16, 0,  8, 16, 0x31,  64, 0, 0x1F00, 0x1F, 0x22, 1, at90pwm216,    apXmask}, // untested
  {0x9483, 16, 0,  8, 16, 0x31,  64, 0, 0x1F00, 0x1F, 0x22, 1, at90pwm316,    apXmask}, // untested
  {0x9310,  8, 0,  4,  8, 0x31,  64, 0, 0x0000, 0x1C, 0x1F, 1, atmega8hva,     mHVmsk}, // untested
  {0x940C,  8, 0,  4, 16, 0x31,  64, 0, 0x0000, 0x1C, 0x1F, 1, atmega16hva,    mHVmsk}, // untested
  {0x940D, 16, 0,  8, 16, 0x31,  64, 0, 0x1F00, 0x1C, 0x1F, 1, atmega16hvb,    mHVmsk}, // untested
  {0x9510, 32, 0, 16, 32, 0x31,  64, 0, 0x3F00, 0x1C, 0x1F, 1, atmega32hvb,    mHVmsk}, // untested
  {0x940D, 16, 0,  8, 16, 0x31,  64, 0, 0x1F00, 0x1C, 0x1F, 1, atmega16hvbrevb,mHVmsk}, // untested
  {0x9510, 32, 0, 16, 32, 0x31,  64, 0, 0x3F00, 0x1C, 0x1F, 1, atmega32hvbrevb,mHVmsk}, // untested
  {0x9610, 64, 0, 16, 64, 0x31,  64, 0, 0x7F00, 0x1C, 0x1F, 1, atmega64hve2, m64HVmsk}, // untested
  { 0,      0, 0,  0, 0,  0,      0, 0, 0,      0,    0,    0, NULL,             NULL}, // last mark
};

const byte maxspeedexp = 4; // corresponds to a factor of 16
const byte speedcmd[] PROGMEM = { 0x83, 0x82, 0x81, 0x80, 0xA0, 0xA1 };
unsigned long speedlimit = SPEEDLIMIT;

enum Fuses { CkDiv8, CkDiv1, CkRc, CkARc, CkXtal, CkExt, CkSlow, Erase, DWEN, UnknownFuse };

enum FuseByte { LowFuse, HighFuse, ExtendedFuse };

// monitor command names
const char mohelp[] PROGMEM = "help";
const char moinfo[] PROGMEM = "info";
const char moversion[] PROGMEM = "version";
const char modwire[] PROGMEM = "debugwire";
const char moreset[] PROGMEM = "reset";
const char moload[] PROGMEM = "load";
const char moonly[] PROGMEM = "onlyloaded";
const char moverify[] PROGMEM = "verify";
const char motimers[] PROGMEM = "timers";
const char mobreak[] PROGMEM = "breakpoints";
const char mosinglestep[] PROGMEM = "singlestep";
const char mospeed[] PROGMEM = "speed";
const char mocache[] PROGMEM = "caching";
const char morange[] PROGMEM = "rangestepping";
const char motest[] PROGMEM = "LiveTests";
const char mounk[] PROGMEM ="";
const char moamb[] PROGMEM ="";

// command index
#define MOHELP 0
#define MOINFO 1
#define MOVERSION 2
#define MODWIRE 3
#define MORESET 4
#define MOLOAD 5
#define MOONLY 6
#define MOVERIFY 7
#define MOTIMERS 8
#define MOBREAK 9
#define MOSINGLESTEP 10
#define MOSPEED 11
#define MOTEST 12
#define MOCACHE 13
#define MORANGE 14
#define MOUNK 15
#define MOAMB 16
#define NUMMONCMDS 17

// array with all monitor commands
const char *const mocmds[NUMMONCMDS] PROGMEM = {
  mohelp, moinfo, moversion, modwire, moreset, moload, moonly, moverify, motimers, mobreak, mosinglestep, mospeed, 
  motest, mocache, morange, mounk, moamb }; 

// some statistics
long timeoutcnt = 0; // counter for DW read timeouts
long flashcnt = 0; // number of flash writes 
#if FREERAM
int freeram = 2048; // minimal amount of free memory (only if enabled)
#endif

// communcation interface to target
dwSerial      dw;
byte          lastsignal;

// communication and memory buffer
byte membuf[MAXMEMBUF]; // used for storing sram, flash, and eeprom values
byte newpage[MAXPAGESIZE]; // one page of flash to program
byte page[MAXPAGESIZE]; // cached page contents - never overwrite it in the program! 
unsigned int lastpg; // address of the cached page
boolean validpg = false; // if cached page contents is valid
boolean flashidle; // flash programming is not active
unsigned int flashpageaddr; // current page to be programmed next
byte buf[MAXBUF+1]; // for gdb i/o
int buffill; // how much of the buffer is filled up
byte fatalerror = NO_FATAL;

DEBDECLARE();

#include "dw-link.h"


/****************** Interrupt routines *********************/

// This guards against reset loops caused by resets
// is useless under Arduino's bootloader
void wdt_init(void) __attribute__((naked)) __attribute__((section(".init3"))) __attribute__((used));
void wdt_init(void)
{
  MCUSR = 0;
  wdt_disable();
}

// disconnect and software reset
void dwlrestart(void)
{
  Serial.end();
  wdt_enable(WDTO_15MS);
  while (1);
}

// catch undefined/unwanted irqs: should not happen at all
ISR(BADISR_vect)
{
  reportFatalError(BAD_INTERRUPT_FATAL, false);
}

ISR(TIMER0_COMPA_vect, ISR_NOBLOCK)
{
  // the ISR can be interrupted at any point by itself, the only problem
  // may be that a call is not counted, which should not happen too
  // often and is uncritical; there is no danger of data corruption
  // of the cnt variable because any interrupt while assigning
  // a new value to cnt will return immediately
  static int cnt = 0;
  static byte busy = 0;

  if (busy) return; // if this IRQ routine is already active, leave immediately
  busy++; 
  cnt--;
  if (*ledout & ledmask) {
    if (cnt < 0) {
      cnt = offtime;
      *ledout &= ~ledmask;
    }
  } else {
    if (cnt < 0) {
      cnt = ontime;
      *ledout |= ledmask;
    }
  }
  busy--;
}

byte saveTIMSK0;
byte saveUCSR0B;
// block all irqs but the timer1 interrupt 
// that is necessary for receiving bytes over the dw line
void blockIRQ(void)
{
  saveTIMSK0 = TIMSK0;
  TIMSK0 = 0;
  saveUCSR0B = UCSR0B;
  UCSR0B &= ~(RXCIE0|TXCIE0|UDRIE0);
}

void unblockIRQ(void)
{
  TIMSK0 = saveTIMSK0;
  UCSR0B = saveUCSR0B;
}

  
  

/******************* main ******************************/
int main(void) {
  // Arduino init
  init();
  
  // setup
  ctx.autopc = false;
  pinMode(DWLINE, INPUT); 
  power(false);
  pinMode(SENSEBOARD, INPUT_PULLUP);
  ctx.levelshifting = !digitalRead(SENSEBOARD);
  Serial.begin(HOSTBPS);
  DEBINIT(DEBTX);
  DEBLN(F("\ndw-link version " VERSION));
  setupio();
  TIMSK0 = 0; // no millis interrupts
  pinMode(LEDGND, OUTPUT);
  digitalWrite(LEDGND, LOW);
  power(true); // switch target on
  _delay_ms(50); // let the power state settle

#if SCOPEDEBUG
  pinMode(DEBTX, OUTPUT); //
  digitalWrite(DEBTX, LOW); // PD3 on UNO
#endif
  initSession(); // initialize all critical global variables

  //  DEBLN(F("Now configuereSupply"));
  DEBLN(F("Setup done"));
  
  // loop
  while (1) {
#if (!NOISPPROG)
    if (ctx.state == NOTCONN_STATE) { // check whether there is an ISP programmer
      if (UCSR0A & _BV(FE0))  // frame error -> break, meaning programming!
	ISPprogramming(false);
      else if (Serial.peek() == '0') // sign on for ISP programmer using HOSTBPS
	ISPprogramming(true);
    }
#endif
    monitorSystemLoadState();
    if (Serial.available()) {
      gdbHandleCmd();
    } else if (ctx.state == RUN_STATE) {
      if (dw.available()) {
	byte cc = dw.read();
	if (cc == 0x0) { // break sent by target
	  if (expectUCalibrate()) {
	    DEBLN(F("Execution stopped"));
	    _delay_us(5); // avoid conflicts on the line
	    gdbSendState(SIGTRAP);
	  }
	}
      }
    }
  }
  return 0;
}

void setupio(void) {
  ledmask = digitalPinToBitMask(SYSLED);
  ledout = portOutputRegister(digitalPinToPort(SYSLED));
  todsckmask = digitalPinToBitMask(TODSCK);
  todsckmode = portModeRegister(digitalPinToPort(TODSCK));
  tmosimask = digitalPinToBitMask(TMOSI);
  tmosiout = portOutputRegister(digitalPinToPort(TMOSI));
  tmosimode = portModeRegister(digitalPinToPort(TMOSI));
  tmisomask = digitalPinToBitMask(TMISO);
  tmisoin =  portInputRegister(digitalPinToPort(TMISO));
  tsckmask = digitalPinToBitMask(TSCK);
  tsckout = portOutputRegister(digitalPinToPort(TSCK));
}

/****************** system state routines ***********************/

// monitor the system state LOAD_STATE
// if no input any longer, then flush flash page buffer and
// set state back to connected
void monitorSystemLoadState(void) {
  static unsigned int noinput = 0;

  if (Serial.available()) noinput = 0;
  noinput++;
  if (noinput == 2777) { // roughly 50 msec, based on the fact that one loop is 18 usec
    if (ctx.state == LOAD_STATE) {
      if (ctx.bps >= 30000)  // if too slow, wait for next command
                              // instead of asnychronously finishing load 
	targetFlushFlashProg();
      setSysState(DWCONN_STATE);
    }
  }
}

// init all global vars when the debugger connects
void initSession(void)
{
  DEBLN(F("initSession"));
  flashidle = true;
  ctx.readbeforewrite = true;
  ctx.safestep = true;
  ctx.dwactivated = false;
  ctx.tmask = 0xFF;
  ctx.verifyload = true;
  ctx.onlyloaded = true;
  ctx.notloaded = true;
  bpcnt = 0;
  bpused = 0;
  hwbp = 0xFFFF;
  lastsignal = 0;
  validpg = false;
  buffill = 0;
  fatalerror = NO_FATAL;
  setSysState(NOTCONN_STATE);
  targetInitRegisters();
  mcu.name = (const char *)unknown;
}

// Report a fatal error and stop everything
// error will be displayed when trying to execute.
// If checkio is set to true, we will check whether
// the connection to the target is still there.
// If not, the error is not recorded, but the connection is
// marked as not connected
// We will mark the error and send a message that should
// at least be shown in the gdb-server window
void reportFatalError(byte errnum, boolean checkio)
{
  if (checkio) {
    if (targetOffline()) return; // if offline, ignore error
    dw.sendBreak();
    if (!expectUCalibrate()) { // target is not connected any longer
      setSysState(NOTCONN_STATE); // set state and ignore error
      return;
    }
  }
  DEBPR(F("***Report fatal error: ")); DEBLN(errnum);
  if (fatalerror == NO_FATAL) {
    if (errnum >= 100) { // not a connection error
      gdbDebugMessagePSTR(PSTR("*** Fatal internal error: "),errnum);
    }
    fatalerror = errnum;
  }
  setSysState(ERROR_STATE);
}

// change system state
// switch on blink IRQ when error, or power-cycle state
void setSysState(statetype newstate)
{
  DEBPR(F("setSysState: ")); DEBLN(newstate);
  if (ctx.state == ERROR_STATE && fatalerror) return;
  TIMSK0 &= ~_BV(OCIE0A); // switch off!
  ctx.state = newstate;
  ontime = ontimes[newstate];
  offtime = offtimes[newstate];
  pinMode(SYSLED, OUTPUT);
  if (ontimes[newstate] == 0) {
    digitalWrite(SYSLED, LOW);
  } else if (offtimes[newstate] == 0) {
    digitalWrite(SYSLED, HIGH);
  } else {
    OCR0A = 0x80;
    TIMSK0 |= _BV(OCIE0A);
  }
  //  DEBPR(F("On-/Offtime: ")); DEBPR(ontime); DEBPR(F(" / ")); DEBLN(offtime);
  //  DEBPR(F("TIMSK0=")); DEBLNF(TIMSK0,BIN);
}


/****************** GDB RSP routines **************************/


// handle command from client
void gdbHandleCmd(void)
{
  byte checksum, pkt_checksum;
  byte b;

  measureRam();
  b = gdbReadByte();

  switch(b) {
  case '$':
    buffill = 0;
    for (pkt_checksum = 0, b = gdbReadByte();
	 b != '#' ; b = gdbReadByte()) {
      if (buffill < MAXBUF) buf[buffill++] = b;
      pkt_checksum += b;
    }
    buf[buffill] = 0;
    
    checksum  = hex2nib(gdbReadByte()) << 4;
    checksum |= hex2nib(gdbReadByte());
    
    /* send nack in case of wrong checksum  */
    if (pkt_checksum != checksum) {
      gdbSendByte('-');
      return;
    }
    
    /* ack */
    gdbSendByte('+');

    /* parse received buffer (and perhaps start executing) */
    gdbParsePacket(buf);
    buffill = 0; /* This avoids sending a packet that was sent to us when replying to a '-' */
    break;
			 
  case '-':  /* NACK, repeat previous reply */
    gdbSendBuff(buf, buffill);
    break;
    
  case '+':  /* ACK, great */
    break;

  case 0x03:
    /* user interrupt by Ctrl-C, send current state and
       continue reading */
    if (ctx.state == RUN_STATE) {
      Serial.flush(); // let the output be printed
      while (Serial.available()) Serial.read(); // ignore everything after ^C 
      targetBreak(); // stop target
      if (expectUCalibrate()) {
	gdbSendState(SIGINT);
      } else {
	gdbSendState(SIGHUP);
	//DEBLN(F("Connection lost"));
      }
    }
    break;

  case 0x05: // enquiry, answer back with dw-link
    Serial.print(F("dw-link"));
    break;
    
  default:
    // simply ignore, we only accept records, ACK/NACK, a Ctrl-C, or an ENQ (=0x05) 
    break;
  }
}

// parse packet and perhaps start executing
void gdbParsePacket(const byte *buff)
{
  byte s;

  DEBPR(F("gdb packet: ")); DEBLN((char)*buff);
  if (!flashidle) {
    if (*buff != 'X' && *buff != 'M')
      targetFlushFlashProg();                         /* finalize flash programming before doing something else */
  } else {
    if (*buff == 'X' || *buff == 'M')
      gdbUpdateBreakpoints(true);                     /* remove all BREAKS before writing into flash */
  }
  switch (*buff) {
  case '?':                                           /* last signal */
    gdbSendSignal(lastsignal);
    break;
  case '!':                                           /* Set to extended mode, always OK */
    gdbSendReply("OK");
    break;
  case '=':
    strncpy(mcu.required, (char *)&buff[1], MAXNAMELEN);      /* copy name of required MCU */
    mcu.required[MAXNAMELEN-1] = 0;
    break;
  case 'H':                                           /* Set thread, always OK */
    gdbSendReply("OK");
    break;
  case 'T':                                           /* Is thread alive, always OK */
    gdbSendReply("OK");
    break;
  case 'g':                                           /* read registers */
    gdbReadRegisters();
    break;
  case 'G':                                           /* write registers */
    gdbWriteRegisters(buff + 1);
    break;
  case 'm':                                           /* read memory */
    gdbReadMemory(buff + 1);
    break;
  case 'M':                                           /* write memory */
    gdbWriteMemory(buff + 1, false);
    break;
  case 'X':                                           /* write memory from binary data */
    gdbWriteMemory(buff + 1, true); 
    break;
  case 'D':                                           /* detach from target */
    gdbUpdateBreakpoints(true);                       /* remove BREAKS in memory before exit */
    validpg = false;
    fatalerror = NO_FATAL;
    gdbStopConnection();                              
    gdbSendReply("OK");                               /* and signal that everything is OK */
                                                      /* after that avr-gdb will disconnect */
    _delay_ms(500);                                   /* wait a bit */
    dwlrestart();                                     /* fresh restart */
    break;
  case 'c':                                           /* continue */
  case 'C':                                           /* continue with signal - just ignore signal! */
    s = gdbContinue();                                /* start execution on target at current PC */
    if (s) gdbSendState(s);                           /* if s != 0, it is a signal notifying an error */
                                                      /* otherwise the target is now executing */
    break;
  case 's':                                           /* single step */
  case 'S':                                           /* step with signal - just ignore signal */
    gdbSendState(gdbStep());                          /* do only one step and report reason why stopped */
    break;              
  case 'z':                                           /* remove break/watch point */
  case 'Z':                                           /* insert break/watch point */
    gdbHandleBreakpointCommand(buf);
    break;
  case 'v':                                          
    if (memcmp_P(buf, (void *)PSTR("vRun"), 4) == 0) {/* Run command */
	gdbReset();
	gdbSendState(SIGTRAP);                        /* trap signal */
    } else if (memcmp_P(buf, (void *)PSTR("vKill"), 5) == 0) { /* used only in extended-remote: just reset */
      gdbReset();
      gdbSendReply("OK");                           /* all OK */
    } else {
      gdbSendReply("");                             /* not supported */
    }
    break;
  case 'q':                                           /* query requests */
    if (memcmp_P(buf, (void *)PSTR("qRcmd,"),6) == 0) /* monitor command */
	gdbParseMonitorPacket(buf+6);
    else if (memcmp_P(buff, (void *)PSTR("qSupported"), 10) == 0) {
      //DEBLN(F("qSupported"));
      initSession();                                  /* always init all vars when gdb connects */
      gdbStartConnect(true);                          /* and try to connect */
      gdbSendPSTR((const char *)PSTR("PacketSize=" MAXBUFHEXSTR)); /* needs to be given in hexadecimal! */
    } else if (memcmp_P(buf, (void *)PSTR("qC"), 2) == 0)      
      gdbSendReply("QC01");                           /* current thread is always 1 */
    else if (memcmp_P(buf, (void *)PSTR("qfThreadInfo"), 12) == 0)
      gdbSendReply("m01");                            /* always 1 thread*/
    else if (memcmp_P(buf, (void *)PSTR("qsThreadInfo"), 12) == 0)
      gdbSendReply("l");                              /* send end of list */
    else if (memcmp_P(buf, (void *)PSTR("qAttached"), 9) == 0)
      gdbSendReply("1");                              /* tell GDB to use detach when quitting */
    else
      gdbSendReply("");  /* not supported */
    break;
  default:
    gdbSendReply("");  /* not supported */
    break;
  }
}

      

// parse a monitor command and execute the appropriate actions
void gdbParseMonitorPacket(byte *buf)
{
   [[maybe_unused]] int para = 0;
   char cmdbuf[40];
   int mocmd = -1;
   int mooptix;

  measureRam();

  convBufferHex2Ascii(cmdbuf, buf, 40); // convert to ASCII string
  mocmd = gdbDetermineMonitorCommand(cmdbuf, mooptix); // get command number and option char
  if (strlen(cmdbuf) == 0) mocmd = MOHELP;
  
  switch(mocmd) {
#if (!NOMONITORHELP)
  case MOHELP:
    gdbHelp();
    break;
  case MOINFO:
    gdbInfo();
    break;
  case MOVERSION:
    gdbVersion();
    break;
#endif
  case MODWIRE:
    gdbDwireOption(cmdbuf[mooptix]);
    break;
  case MORESET:
    if (gdbReset()) gdbReplyMessagePSTR(PSTR("MCU has been reset"), -1);
    else gdbReplyMessagePSTR(PSTR("Enable debugWIRE first"), -1);
    break;
  case MOLOAD:
    gdbLoadOption(cmdbuf[mooptix]); 
    break;
  case MOONLY:
    gdbOnlyOption(cmdbuf[mooptix]);
    break;
  case MOVERIFY:
    gdbVerifyOption(cmdbuf[mooptix]);
    break;
  case MOTIMERS:
    gdbTimerOption(cmdbuf[mooptix]);
    break;
  case MOBREAK:
    gdbBreakOption(cmdbuf[mooptix]);
    break;
  case MOSINGLESTEP:
    gdbSteppingOption(cmdbuf[mooptix]);
    break;
  case MOSPEED:
    gdbSpeed(cmdbuf[mooptix]);
    break;
  case MOCACHE:
    gdbReplyMessagePSTR(PSTR("Caching is not implemented"), -1);
    break;
  case MORANGE:
    gdbReplyMessagePSTR(PSTR("Range stepping is not yet implemented"), -1);
    break;
  case MOTEST:
    if (targetOffline()) {
      gdbReplyMessagePSTR(PSTR("Enable debugWIRE first"), -1);
    } else {
      alltests();
    }
    break;
  case MOUNK:
    gdbUnknownCmd();
    break;
  case MOAMB:
    gdbAmbiguousCmd();
    break;
  default:
    gdbUnknownCmd();
    break;
  }
}

// determine command and start index of option for monitor command given in 'line'
int gdbDetermineMonitorCommand(char *line, int &optionix)
{
  unsigned int ix;
  int cmdix, resultcmd = MOUNK;
  boolean succ = false;
  char *checkcmd;

  measureRam();
  
  for (cmdix = 0; cmdix < NUMMONCMDS; cmdix++) {
    checkcmd = (char *)pgm_read_word(&(mocmds[cmdix]));
    for (ix = 0; ix < strlen(line)+1; ix++) {
      if (line[ix] == ' ' || line[ix] == '\0') {
	if (ix > 0) {
	  if (resultcmd == MOUNK) {
	    succ = true;
	    resultcmd = cmdix;
	    break;
	  } else {
	    succ = false;
	    resultcmd = MOAMB;
	    break;
	  }
	} else {
	  return MOHELP;
	}
      } else if (line[ix] != (char)pgm_read_byte(&checkcmd[ix])) {
	break;
      }
    }
  }
  optionix = strlen(line);
  succ = false;
  for (ix=0; ix<strlen(line)+1; ix++) {
    if (line[ix] == '\0') {
      optionix = ix;
      return resultcmd;
    } else if (line[ix] == ' ')
      succ = true;
    else if (succ && line[ix] != ' ') {
      optionix = ix;
      return resultcmd;
    }
  }
  return resultcmd;
}

// unkown command
void gdbUnknownCmd(void) {
  gdbReplyMessagePSTR(PSTR("Unknown 'monitor' command"), -1);
}

// unkown command
void gdbUnknownOpt(void) {
  gdbReplyMessagePSTR(PSTR("Unknown 'monitor' option"), -1);
}

// ambiguous command
void gdbAmbiguousCmd(void) {
  gdbReplyMessagePSTR(PSTR("Ambiguous 'monitor' command string"), -1);
}

inline void gdbBreakOption(char arg) {
  switch (arg) {
  case 'h':
    gdbSetMaxBPs(1, false);
    break;
  case 'a':
    gdbSetMaxBPs(MAXBREAK, false);
    break;
  case '4':
    gdbSetMaxBPs(4, false);
    break;
  case 's':
    gdbSetMaxBPs(MAXBREAK, true);
    break;
  case '\0':
    gdbGetMaxBPs();
    break;
  default:
    gdbUnknownOpt();
    break;
  }
}

// help function (w/o unit tests)
void gdbHelp(void) {
  gdbDebugMessagePSTR(PSTR("monitor help            - help function"), -1);
  gdbDebugMessagePSTR(PSTR("monitor info            - information about target and debugger"), -1);
  gdbDebugMessagePSTR(PSTR("monitor version         - firmware version"), -1);
  gdbDebugMessagePSTR(PSTR("monitor debugwire [e|d] - enables (e) or disables (d) debugWIRE"), -1);
  gdbDebugMessagePSTR(PSTR("monitor reset           - reset target"), -1);
  gdbDebugMessagePSTR(PSTR("monitor load [r|w]      - loading: read before write(r) or write(w)"), -1);
  gdbDebugMessagePSTR(PSTR("monitor verify [e|d]    - verify flash after load(e) or not (d)"), -1);
  gdbDebugMessagePSTR(PSTR("monitor timers [f|r]    - timers freeze or run when stopped"), -1);
  gdbDebugMessagePSTR(PSTR("monitor onlyloaded [e|d]- allow exec only after load (e) or always (d)"), -1);
  gdbDebugMessagePSTR(PSTR("monitor breakpoints [a|h|s]"), -1); 
  gdbDebugMessagePSTR(PSTR("                        - allow all, only hw, or only sw bps"), -1);
  gdbDebugMessagePSTR(PSTR("monitor singlestep [s|i]- safe or interruptible single-stepping"), -1);
  //gdbDebugMessagePSTR(PSTR("monitor mcu [<mcutype>]    - check for MCU type or print current"), -1);
#if HIGHSPEED
  gdbDebugMessagePSTR(PSTR("monitor speed [h|l]     - speed limit is h (300kbps) or l (150kbps)"), -1);
#else
  gdbDebugMessagePSTR(PSTR("monitor speed [h|l]     - speed limit is l (150kbps) or h (300kbps)"), -1);
#endif  
  gdbDebugMessagePSTR(PSTR("Commands given without arguments report status"), -1);
  gdbDebugMessagePSTR(PSTR("First option is always the default"), -1);
  gdbSendReply("OK");
}

// info command
void gdbInfo(void) {
  gdbDebugMessagePSTR(PSTR("dw-link version " VERSION), -1);
  gdbDebugMessagePSTR(PSTR("Target: "),-2);
  if (!targetOffline()) {
    gdbDebugMessagePSTR(PSTR("debugWire is enabled"), -1);
    gdbDebugMessagePSTR(PSTR("debugWIRE bitrate: "), ctx.bps);
  } else {
    gdbDebugMessagePSTR(PSTR("debugWire is disabled"), -1);
  }
  gdbDebugMessagePSTR(PSTR("\nNumber of flash write operations so far: "), flashcnt);
#if FREERAM
  gdbDebugMessagePSTR(PSTR("Minimal number of free RAM bytes: "), freeram);
#endif
  gdbDebugMessagePSTR(PSTR("Number of debugWIRE timeouts: "), timeoutcnt);
  if (fatalerror) {
    if (fatalerror < 100) {
      gdbReportConnectionProblem(fatalerror, true);
    } else {
      gdbReplyMessagePSTR(PSTR("Last fatal error: "), fatalerror);
    }
  } else {
    gdbReplyMessagePSTR(PSTR("No fatal error has occured"), -1);
  }
}


// report whether timers will run when stopped
void gdbReportTimers(void)
{
  if (ctx.tmask == 0xDF) 
    gdbReplyMessagePSTR(PSTR("Timers will run when execution is stopped"), -1);
  else 
    gdbReplyMessagePSTR(PSTR("Timers are frozen when execution is stopped"), -1);
}

// check whether required name fits with actual mcu
// return true if so, otherwise false
boolean gdbCheckMcu(void)
{
  unsigned int reqsig = 0, nameptr;
  int ix = 0;
  if (mcu.sig == 0 || mcu.required[0] == 0)  // if target not determined yet, or not required, skip
    return true;
  if (strcasecmp_P(mcu.required, attiny2313a) != 0) { // the big exception!
    if ((tolower(mcu.required[strlen(mcu.required)-1]) == 'a' &&
	 (tolower(mcu.required[strlen(mcu.required)-2]) == 'p' || isdigit(mcu.required[strlen(mcu.required)-2]))) ||
	tolower(mcu.required[strlen(mcu.required)-1]) == 'v')
      mcu.required[strlen(mcu.required)-1] = 0; // remove unessential suffixes
  }
  while ((nameptr = pgm_read_word(&mcu_info[ix].name))) {
    if (strcasecmp_P(mcu.required, (char*)nameptr) == 0) {
      reqsig = pgm_read_word(&mcu_info[ix].sig);
      break;
    }
    ix++;
  }
  if (mcu.sig == reqsig) return true;
  // now some funny business about MCUs pretending to be a P-type
  if (mcu.sig == 0x930F && reqsig == 0x930A) return true; // pretends to be 88P, but 88 is required
  if (mcu.sig == 0x940B && reqsig == 0x9406) return true; // pretends to be 168P, but 168 is required
  if (mcu.sig == 0x950F && reqsig == 0x9514) return true; // pretends to be 328P, but 328 is required
  return false;
}


// check for the Stuck-At-1 or cap condition (and report error)
boolean stuckAtOneOrCap(void)
{
  mcu.stuckat1byte = 0;
  if (DWgetWPc(false) > (mcu.flashsz>>1)) {
#if STUCKAT1PC
    if (mcu.sig == 0x9205 || mcu.sig == 0x930A) {
      mcu.stuckat1byte = (DWgetWPc(false) & ~((mcu.flashsz>>1)-1))>>8;
      DEBPR(F("stuckat1byte=")); DEBLNF(mcu.stuckat1byte,HEX);
      return false;
    }
#else
    if  (mcu.sig == 0x9205 || mcu.sig == 0x930A) {
      gdbReportConnectionProblem(CONNERR_STUCKAT1_PC,false);
    } else {
      gdbReportConnectionProblem(CONNERR_CAPACITIVE_LOAD,false);
    }
    return true;
  }
#endif
  return false;
}

// setup everything after having entered DW mode
void setupDW(void)
{
  setSysState(DWCONN_STATE);
  gdbCleanupBreakpointTable();
  targetReset(); 
  targetInitRegisters();
  ctx.dwactivated = true; // if we now disconnect, we are done!
  // Now fix DWDR address if necessary.
  // This is a very special case, where the DWDR address
  // is not uniquely determined by the chip signature.
  if (mcu.dwdr == 0x1F) { // DWDR address on the ATTiny2313 (and only on that MCU!)
    if (!DWreadRegister(1, true)) { // read register unsuccessful, must be a different address
      mcu.dwdr = 0x27;
      mcu.name = attiny2313a;
      mcu.mask = t4313mask;
    }
  }
}

// start a connection initially (target remote ...)  trying only DW,
// or through the monitor 'debugwire enable' command, then try everything
boolean gdbStartConnect(boolean initialconnect)
{
  int conncode;
  _delay_ms(100); // allow for startup of MCU initially
  mcu.sig = 0;
  if (digitalRead(DWLINE) == LOW) { // externally un-powered!
    gdbReportConnectionProblem(CONNERR_NO_TARGET_POWER,!initialconnect); // Oops, target is not powered up
    return false;
  }
  if (targetDWConnect(initialconnect)) { // if immediately in DW mode, that is OK!
    if (!gdbCheckMcu()) {
      gdbReportConnectionProblem(CONNERR_WRONG_MCU,!initialconnect);
      return false;
    }
    if (stuckAtOneOrCap())
      return false;
    setupDW();
    return true;
  }
  if (fatalerror != NO_FATAL) return false;
  if (initialconnect)
    return true;
  conncode = targetISPConnect();
  if (conncode < 0) {
    gdbReportConnectionProblem(conncode == -1 ? CONNERR_NO_ISP_REPLY : -conncode + 1,!initialconnect);
    return false;
  }
  if (powerCycle()) { // power-cycle automatically or manually, then connect
    if (stuckAtOneOrCap())
      return false;
    setupDW(); // setup everything
    return true;
  } else {
    flushInput();
    targetInitRegisters();
    return false;
  }
}

// monitor debugwire e
// go to dW state, being in transitional or normal state 
boolean gdbConnectDW(void)
{
  if (ctx.state != DWCONN_STATE) {
    gdbStartConnect(false);
    if (ctx.state == DWCONN_STATE) {
      targetReset();
    }
  }
  if (gdbCheckMcu()) {
    gdbReportConnected();
    return true;
  } else {
    // "wrong MCU" message was alread sent by gdbStartConnect
    gdbReplyMessagePSTR(PSTR("Cannot activate debugWIRE"), -1);
    return false;
  }
}
  
// Stop connection when leaving the debugger
boolean gdbStopConnection(void)
{
  setSysState(NOTCONN_STATE);
  return true; // leave debugger without leaving dW mode
}

// monitor debugwire d
boolean gdbDisconnectDW(void)
{
  boolean succ = targetStop();
  setSysState(NOTCONN_STATE);
  gdbReportConnected();
  return succ;
}

void gdbDwireOption(char arg)
{
  switch (arg) {
  case 'e':
    if (ctx.state == NOTCONN_STATE && ctx.dwactivated) {
      gdbReplyMessagePSTR(PSTR("Cannot reactivate debugWIRE: Do a restart"),-1);
    } else {
      gdbConnectDW();
    }
    break;
  case 'd':
    gdbUpdateBreakpoints(true);
    gdbDisconnectDW(); break;
  case '\0': gdbReportConnected(); break;
  default: gdbUnknownOpt(); break;
  }
}

void gdbReportConnected(void)
{
  if (mcu.name == unknown) {
    gdbReplyMessagePSTR(PSTR("debugWIRE is not yet enabled"),-1);
  } else { 
    gdbDebugMessagePSTR(Connected,-2);
    if (!targetOffline()) {
      gdbReplyMessagePSTR(PSTR("debugWIRE is enabled, bps: "),ctx.bps);
    } else {
      gdbReplyMessagePSTR(PSTR("debugWIRE is disabled"),-1);
    }
  }
}

// report connection problem using gdbDebugMessage with *** prefix
// and halt further execution
void gdbReportConnectionProblem(int errnum, boolean doprint)
{
  if (doprint) {
    switch (errnum) {
    case 0: return;
    case 1: gdbDebugMessagePSTR(PSTR("***Cannot connect: Could not communicate by ISP; check wiring***"),-1); break;
    case 2: gdbDebugMessagePSTR(PSTR("***Cannot connect: Could not activate debugWIRE***"),-1); break;
    case 3: gdbDebugMessagePSTR(PSTR("***Cannot connect: Unsupported MCU***"),-1); break;
    case 4: gdbDebugMessagePSTR(PSTR("***Cannot connect: Lock bits could not be cleared***"),-1); break;
    case 5: gdbDebugMessagePSTR(PSTR("***Cannot connect: PC with stuck-at-one bits***"),-1); break;
    case 6: gdbDebugMessagePSTR(PSTR("***Cannot connect: RESET line has a capacitive load***"),-1); break;
    case 7: gdbDebugMessagePSTR(PSTR("***Cannot connect: Target not powered or RESET shortened to GND***"),-1); break; 
    case 8: gdbDebugMessagePSTR(PSTR("***MCU type does not match***"), -1); break;
    default: gdbDebugMessagePSTR(PSTR("***Cannot connect for unknown reasons***"),-1); break;
    }
  }
  reportFatalError(errnum, false);
}

// report last error number
void gdbReportLastError(void)
{
  gdbReplyMessagePSTR(PSTR("Last fatal error: "), fatalerror);
}



// set stepping mode
void gdbSteppingOption(char arg)
{
  if (arg != '\0') {
    if (arg == 's')
      ctx.safestep = true;
    else if (arg == 'i')
      ctx.safestep = false;
    else {
      gdbUnknownOpt();
      return;
    }
  }
  if (ctx.safestep)
    gdbReplyMessagePSTR(PSTR("Single-stepping is interrupt-safe"), -1);
  else
    gdbReplyMessagePSTR(PSTR("Single-stepping is interruptible"), -1);
}


void gdbLoadOption(char arg)
{
  if (arg != '\0') {
    if (arg == 'r')
      ctx.readbeforewrite = true;
    else if (arg == 'w')
      ctx.readbeforewrite = false;
    else {
      gdbUnknownOpt();
      return;
    }
  }
  if (ctx.readbeforewrite) 
    gdbReplyMessagePSTR(PSTR("Reading before writing when loading"), -1);
  else 
    gdbReplyMessagePSTR(PSTR("No reading before writing when loading"), -1);
}

void gdbOnlyOption(char arg)
{
  if (arg != '\0') {
    if (arg == 'e')
      ctx.onlyloaded = true;
    else if (arg == 'd')
      ctx.onlyloaded = false;
    else {
      gdbUnknownOpt();
      return;
    }
  }
  if (ctx.onlyloaded) 
    gdbReplyMessagePSTR(PSTR("Execution is only possible after a previous load command"), -1);
  else 
    gdbReplyMessagePSTR(PSTR("Execution is always possible"), -1);
}

void gdbVerifyOption(char arg)
{
  if (arg != '\0') {
    if (arg == 'e')
      ctx.verifyload = true;
    else if (arg == 'd')
      ctx.verifyload = false;
    else {
      gdbUnknownOpt();
      return;
    }
  }
  if (ctx.verifyload) 
    gdbReplyMessagePSTR(PSTR("Verifying flash after load"), -1);
  else 
    gdbReplyMessagePSTR(PSTR("Load operations are not verified"), -1);
}



void gdbTimerOption(char arg)
{
    switch (arg) {
    case 'r':
      ctx.tmask = 0xDF;
      gdbReportTimers();
      break;
    case 'f':
      ctx.tmask = 0xFF;
      gdbReportTimers();
      break;
    case '\0':
      gdbReportTimers();
      break;
    default:
      gdbUnknownOpt(); break;
    }
}
// show version
inline void gdbVersion(void)
{
  gdbReplyMessagePSTR(PSTR("dw-link version " VERSION), -1);
}
  
// set DW communication speed
void gdbSpeed(char arg)
{
  switch (arg) {
  case 'h': speedlimit = SPEEDHIGH;
    break;
  case 'l': speedlimit = SPEEDLOW;
    break;
  case '\0':
    gdbDebugMessagePSTR(PSTR("Current limit:             "), speedlimit);
    break;
  default:
    gdbSendReply("");
    return;
  }
  if (arg) doBreak(true);
  gdbReplyMessagePSTR(PSTR("Current debugWIRE bitrate: "), ctx.bps);
  return;
}


// "monitor h|4|a|s"
// set maximum number of breakpoints and allowed types of breakpoints
// h = only 1 hardware bp
// 4 = 4 mixed bp
// a = 20 mixed bp
// s = 20 only software bp
inline void gdbSetMaxBPs(byte num, boolean only)
{
  onlysbp = only;
  maxbreak = num;
  gdbGetMaxBPs();
}

inline void gdbGetMaxBPs(void)
{
  if (onlysbp) gdbReplyMessagePSTR(PSTR("Only software breakpoints, maximum: "), maxbreak);
  else if (maxbreak == 1) gdbReplyMessagePSTR(PSTR("Only hardware breakpoints, maximum: "),maxbreak);
  else gdbReplyMessagePSTR(PSTR("All breakpoints are allowed, maximum: "),maxbreak);
}

// perform an automatic power cycle
// do this only if autodw is 'on'
boolean autoPowerCycle(void)
{
#if !SCOPEDEBUG && !TXODEBUG
  pinMode(AUTODW, INPUT_PULLUP);
  if (digitalRead(AUTODW) == LOW || digitalRead(SENSEBOARD) == HIGH)
    return(false);
#else
  return(false);
#endif
  power(false);
  _delay_ms(500);
  power(true);
  _delay_ms(100);
  if (targetDWConnect(false)) {
    setSysState(DWCONN_STATE);
    return true;
  }
  return false;
}

// Ask the user to power-cycle and check whether power has been removed and reestablished.
// If after roughly one minute, this does not happen, we return with false
boolean manualPowerCycle(void)
{
  int retry = 0;

  while (retry++ < 6000) {
    _delay_ms(10);
    if (retry%1000 == 1) 
      gdbDebugMessagePSTR(PSTR("*** Please power-cycle target ***"),-1);
    if (digitalRead(DWLINE) == LOW) { // power gone
      _delay_ms(10);
      if (digitalRead(DWLINE) == LOW) { // still gone
	while (digitalRead(DWLINE) == LOW && retry++ < 6000) _delay_ms(10);
	if (retry >= 6000) {
	  setSysState(NOTCONN_STATE);
	  return false;
	}
	_delay_ms(300);
	if (targetDWConnect(false)) {
	  setSysState(DWCONN_STATE);
	  return true;
	} else {
	  gdbReportConnectionProblem(CONNERR_NO_DW_REPLY, true);
	  return false;
	}
      }
    }
  }
  setSysState(NOTCONN_STATE);
  return false;
}

boolean powerCycle(void)
{
  if (ctx.state == DWCONN_STATE) return true;
  setSysState(PWRCYC_STATE);
  // try first automatic power-cycle
  if (autoPowerCycle()) return true;
  return manualPowerCycle();
}

void power(boolean on)
{
  //DEBPR(F("Power: ")); DEBLN(on);
  if (on) {
    pinMode(IVSUP, OUTPUT);
    digitalWrite(IVSUP, LOW);
  } else { // on=false
    pinMode(IVSUP, INPUT);
  }
}


// "monitor reset"
// issue reset on target
boolean gdbReset(void)
{
  if (ctx.state == NOTCONN_STATE) {
    gdbDebugMessagePSTR(PSTR("Enable debugWIRE first"), -1);
    return false;
  }
  if (targetOffline()) {
    gdbDebugMessagePSTR(PSTR("Target offline: Cannot reset"), -1);
    return false;
  }
  targetReset();
  targetInitRegisters();
  return true;
}


// retrieve opcode and address at current wpc (regardless of whether it is hidden by break)
void getInstruction(unsigned int &opcode, unsigned int &addr)
{
  opcode = 0;
  addr = 0;
  int bpix = gdbFindBreakpoint(ctx.wpc);
  if ((bpix < 0) || (!bp[bpix].inflash)) 
    opcode = targetReadFlashWord(ctx.wpc<<1);
  else
    opcode = bp[bpix].opcode;
  //DEBPR(F("sim opcode=")); DEBLNF(opcode,HEX);
  if (twoWordInstr(opcode)) {
    //DEBPR(F("twoWord/addr=")); DEBLNF((ctx.wpc+1)<<1,HEX);
    addr = targetReadFlashWord((ctx.wpc+1)<<1);
    //DEBLNF(addr,HEX);
  }
}

#if OFFEX2WORD == 0
// check whether an opcode is a 32-bit instruction
boolean twoWordInstr(unsigned int opcode)
{
  return(((opcode & ~0x1F0) == 0x9000) || ((opcode & ~0x1F0) == 0x9200) ||
	 ((opcode & 0x0FE0E) == 0x940C) || ((opcode & 0x0FE0E) == 0x940E));
}

inline void simTwoWordInstr(unsigned int opcode, unsigned int addr)
{
  if (twoWordInstr(opcode)) {
    byte reg, val;
    if ((opcode & ~0x1F0) == 0x9000) {   // lds 
      reg = (opcode & 0x1F0) >> 4;
      if (addr < 0x20)  // a general register address
	val = ctx.regs[addr];
      else
	val = DWreadSramByte(addr);
      ctx.regs[reg] = val;
      ctx.wpc += 2;
    } else if ((opcode & ~0x1F0) == 0x9200) { // sts 
      reg = (opcode & 0x1F0) >> 4;
      //DEBPR(F("Reg="));
      //DEBLN(reg);
      //DEBPR(F("addr="));
      //DEBLNF(addr,HEX);
      if (addr < 0x20)
	ctx.regs[addr] = ctx.regs[reg];
      else
	DWwriteSramByte(addr,ctx.regs[reg]);
      ctx.wpc += 2;
    } else if ((opcode & 0x0FE0E) == 0x940C) { // jmp 
      // since debugWIRE works only on MCUs with a flash address space <= 64 kwords
      // we do not need to use the bits from the opcode
      ctx.wpc = addr;
    } else if  ((opcode & 0x0FE0E) == 0x940E) { // call
      DWwriteSramByte(ctx.sp, (byte)((ctx.wpc+2) & 0xff)); // save return address on stack
      DWwriteSramByte(ctx.sp-1, (byte)((ctx.wpc+2)>>8));
      ctx.sp -= 2; // decrement stack pointer
      ctx.wpc = addr; // the new PC value
    }
  }
}
#endif

// do one step
// start with saved registers and return with saved regs
// it will return a signal, which in case of success is SIGTRAP
byte gdbStep(void)
{
  unsigned int opcode, arg;
  //  unsigned int oldpc = ctx.wpc;
  int bpix = gdbFindBreakpoint(ctx.wpc);
  byte sig = SIGTRAP; // SIGTRAP (normal), SIGILL (if noload), SIGABRT (fatal)

  //DEBLN(F("Start step operation"));
  if (fatalerror) return SIGABRT;
  if (bpcnt == maxbreak) {
    gdbDebugMessagePSTR(PSTR("Too many active breakpoints"), -1);
    return SIGABRT;
  }
  if (targetOffline()) return SIGHUP;
  if (ctx.onlyloaded and ctx.notloaded) {
    gdbDebugMessagePSTR(PSTR("No program loaded"), -1);
    return SIGILL;
  }
  getInstruction(opcode, arg);
#if ILLOPDETECT
  if (targetIllegalOpcode(opcode)) {
    //DEBPR(F("Illop: ")); DEBLNF(opcode,HEX);
    return SIGILL;
  }
#endif
  if (opcode == SLEEPCODE) {
    ctx.wpc++;
    return sig;
  }
  if (opcode == BREAKCODE) return SIGILL;
  if ((bpix >= 0 &&  bp[bpix].inflash) || ctx.safestep) {
#if OFFEX2WORD == 0
    if (twoWordInstr(opcode)) 
      simTwoWordInstr(opcode, arg);
    else 
#endif
      {
	targetRestoreRegisters();
	DWexecOffline(opcode);
	targetSaveRegisters();
      }
  } else {
    targetRestoreRegisters();
    targetStep();
    if (!expectBreakAndU()) {
      ctx.saved = true; // just reinstantiate the old state
      reportFatalError(NO_STEP_FATAL, true);
      return SIGABRT;
    } else {
      targetSaveRegisters();
//      if (oldpc == ctx.wpc) {
//	if (Serial.available())
//	  sig = SIGINT; // if we do not make progress in single-stepping, ^C (or other inputs) can stop gdb
//      }
    }
  }
  return sig;
}

// start to execute at current PC
// if some error condition exists, return the appropriate signal
// otherwise send command to target in order to start execution and return 0
byte gdbContinue(void)
{
  byte sig = 0;
  unsigned int opcode, arg;
  //DEBLN(F("Start continue operation"));
  if (fatalerror) sig = SIGABRT;
  else if (ctx.onlyloaded and ctx.notloaded) {
    sig = SIGILL;
    gdbDebugMessagePSTR(PSTR("No program loaded"), -1);
  } else if (targetOffline()) sig = SIGHUP;
  else {
    getInstruction(opcode, arg);
    if (opcode == BREAKCODE) return SIGILL;
    if (!gdbUpdateBreakpoints(false))  { // update breakpoints in flash memory
      sig = SIGABRT;
      gdbDebugMessagePSTR(PSTR("Too many active breakpoints"), -1);
    }
#if ILLOPDETECT
    if (targetIllegalOpcode(targetReadFlashWord(ctx.wpc*2))) {
      //DEBPR(F("Illop: ")); DEBLNF(targetReadFlashWord(ctx.wpc*2),HEX);
      sig = SIGILL;
    }
#endif
  }
  if (sig) { // something went wrong
    //DEBPR(F("Error sig=")); DEBLN(sig);
    _delay_ms(2);
    return sig;
  }
  //DEBPR(F("Before exec: ctx.wpc=")); DEBLNF(ctx.wpc*2,HEX);
  targetRestoreRegisters();
  setSysState(RUN_STATE);
  targetContinue();
  return 0;
}


// Remove inactive and set active breakpoints before execution starts or before reset/kill/detach.
// Note that GDB sets breakpoints immediately before it issues a step or continue command and
// GDB removes the breakpoints right after the target has stopped. In order to minimize flash wear,
// we will inactivate the breakpoints when GDB wants to remove them, but we do not remove them
// from flash immediately. Only just before the target starts to execute, we will update flash memory
// according to the status of a breakpoint:
// BP is unused (addr = 0) -> do nothing
// BP is used, active and already in flash or hwbp -> do nothing
// BP is used, active, not hwbp, and not in flash -> write to flash
// BP is used, but not active and in flash -> remove from flash, set BP unused
// BP is used and not in flash -> set BP unused
// BP is used, active and hwbp -> do nothing, will be taken care of when executing
// Order all actionable BPs by increasing address and only then change flash memory
// (so that multiple BPs in one page only need one flash change)
//
// Return value is true iff all BPs can be allocated 
//
// When the parameter cleanup is true, we also will remove BREAK instructions
// of active breakpoints, because either an exit or a memory write action will
// follow.
//
boolean gdbUpdateBreakpoints(boolean cleanup)
{
  int i, j, ix, rel = 0;
  unsigned int relevant[MAXBREAK*2+1]; // word addresses of relevant locations in flash
  unsigned int addr = 0;

  measureRam();

  if (!flashidle) {
    reportFatalError(BREAKPOINT_UPDATE_WHILE_FLASH_PROGRAMMING_FATAL, false);
    return false;
  }

  if (bpcnt > maxbreak) return false;
  
  DEBPR(F("Update Breakpoints (used/active): ")); DEBPR(bpused); DEBPR(F(" / ")); DEBLN(bpcnt);
  // if there are no used entries, we also can return immediately
  // if the target is not connected, we cannot update breakpoints in any case
  if (bpused == 0 || targetOffline()) return true;

  // find relevant BPs
  for (i=0; i < MAXBREAK*2; i++) {
    if (bp[i].used) {                       // only used breakpoints!
      if (onlysbp && bp[i].hw) {            // only software bps are allowed
	bp[i].hw = false;
	bp[i].inflash = false;
	hwbp = 0xFFFF;
      }
      if (bp[i].active) {                   // active breakpoint
	if (!cleanup) {
	  if (!bp[i].inflash && !bp[i].hw)  // not in flash yet and not a hw bp
	    relevant[rel++] = bp[i].waddr;  // remember to be set
	} else {                            // active BP && cleanup
	  if (bp[i].inflash)                // remove BREAK instruction, but leave it active
	    relevant[rel++] = bp[i].waddr;
	}
      } else {                              // inactive bp
	if (bp[i].inflash) {                // still in flash 
	  relevant[rel++] = bp[i].waddr;    // remember to be removed
	} else {
	  bp[i].used = false;               // otherwise free BP already now
	  if (bp[i].hw) {                   // if hwbp, then free HWBP
	    bp[i].hw = false;
	    hwbp = 0xFFFF;
	  }
	  bpused--;
	}
      }
    }
  }
  relevant[rel++] = 0xFFFF; // end marker
  DEBPR(F("Relevant bps: "));  DEBLN(rel-1);

  // sort relevant BPs
  insertionSort(relevant, rel);
  DEBLN(F("BPs sorted: ")); for (i = 0; i < rel-1; i++) DEBLNF(relevant[i]*2,HEX);

  // replace pages that need to be changed
  // note that the addresses in relevant and bp are all word addresses!
  i = 0;
  while (addr < mcu.flashsz && i < rel-1) {
    if (relevant[i]*2 >= addr && relevant[i]*2 < addr+mcu.targetpgsz) {
      j = i;
      while (relevant[i]*2 < addr+mcu.targetpgsz) i++;
      targetReadFlashPage(addr);
      memcpy(newpage, page, mcu.targetpgsz);
      while (j < i) {
	DEBPR(F("RELEVANT: ")); DEBLNF(relevant[j]*2,HEX);
	ix = gdbFindBreakpoint(relevant[j++]);
	if (ix < 0) { reportFatalError(RELEVANT_BP_NOT_PRESENT, false);
	  return false;
	}
	DEBPR(F("Found BP:")); DEBLN(ix);
	if (bp[ix].active && !cleanup) { // enabled but not yet in flash && not a cleanup operation
	  bp[ix].opcode = (newpage[(bp[ix].waddr*2)%mcu.targetpgsz])+
	    (unsigned int)((newpage[((bp[ix].waddr*2)+1)%mcu.targetpgsz])<<8);
	  DEBPR(F("Replace op ")); DEBPRF(bp[ix].opcode,HEX); DEBPR(F(" with BREAK at byte addr ")); DEBLNF(bp[ix].waddr*2,HEX);
	  /* Is actually possible when there is a BREAK in the user program
	  if (bp[ix].opcode == BREAKCODE) { 
	    reportFatalError(BREAK_FATAL, false);
	  }
	  */
	  bp[ix].inflash = true; 
				  
	  newpage[(bp[ix].waddr*2)%mcu.targetpgsz] = 0x98; // BREAK instruction
	  newpage[((bp[ix].waddr*2)+1)%mcu.targetpgsz] = 0x95;
	} else { // disabled but still in flash or cleanup operation
	  DEBPR(F("Restore original op ")); DEBPRF(bp[ix].opcode,HEX); DEBPR(F(" at byte addr ")); DEBLNF(bp[ix].waddr*2,HEX);
	  newpage[(bp[ix].waddr*2)%mcu.targetpgsz] = bp[ix].opcode&0xFF;
	  newpage[((bp[ix].waddr*2)+1)%mcu.targetpgsz] = bp[ix].opcode>>8;
	  bp[ix].inflash = false;
	  if (!bp[ix].active) { // now release this slot!
	    bp[ix].used = false;
	    bpused--;
	  }
	}
      }
      targetWriteFlashPage(addr);
    }
    addr += mcu.targetpgsz;
  }
  DEBPR(F("After updating Breakpoints (used/active): ")); DEBPR(bpused); DEBPR(F(" / ")); DEBLN(bpcnt);
  DEBPR(F("HWBP=")); DEBLNF(hwbp*2,HEX);
  return true;
}

// sort breakpoints so that they can be changed together when they are on the same page
void insertionSort(unsigned int *seq, int len)
{
  unsigned int tmp;
  measureRam();
  for (int i = 1; i < len; i++) {
    for (int j = i; j > 0 && seq[j-1] > seq[j]; j--) {
      tmp = seq[j-1];
      seq[j-1] = seq[j];
      seq[j] = tmp;
    }
  }
}

// find the breakpoint at a given word address
int gdbFindBreakpoint(unsigned int waddr)
{
  measureRam();

  if (bpused == 0) return -1; // shortcut: if no bps used
  for (byte i=0; i < MAXBREAK*2; i++)
    if (bp[i].waddr == waddr && bp[i].used) return i;
  return -1;
}


void gdbHandleBreakpointCommand(const byte *buff)
{
  unsigned long byteflashaddr, sz;
  byte len;

  measureRam();

  if (targetOffline()) return;

  len = parseHex(buff + 3, &byteflashaddr);
  parseHex(buff + 3 + len + 1, &sz);
  
  /* break type */
  switch (buff[1]) {
  case '0': /* software breakpoint */
    if (buff[0] == 'Z') {
      if (gdbInsertBreakpoint(byteflashaddr >> 1)) 
	gdbSendReply("OK");
      else
	gdbSendReply("E03");
      return;
    } else {
      gdbRemoveBreakpoint(byteflashaddr >> 1);
      gdbSendReply("OK");
    }
    return;
  default:
    gdbSendReply("");
    break;
  }
}

/* insert bp, flash addr is in words */
boolean gdbInsertBreakpoint(unsigned int waddr)
{
  int i,j;

  measureRam();

  // check for duplicates
  i = gdbFindBreakpoint(waddr);
  if (i >= 0)
    if (bp[i].active)  // this is a BP set twice, can be ignored!
      return true;
  
  // if bp is already there, but not active, then activate
  i = gdbFindBreakpoint(waddr);
  if (i >= 0) { // existing bp
    bp[i].active = true;
    bpcnt++;
    DEBPR(F("New recycled BP: ")); DEBPRF(waddr*2,HEX);
    if (bp[i].inflash) { DEBPR(F(" (flash) ")); }
    DEBPR(F(" / now active: ")); DEBLN(bpcnt);
    return true;
  }
  // find free slot (should be there, even if there are MAXBREAK inactive bps)
  for (i=0; i < MAXBREAK*2; i++) {
    if (!bp[i].used) {
      bp[i].used = true;
      bp[i].waddr = waddr;
      bp[i].active = true;
      bp[i].inflash = false;
      if (!onlysbp) { // if hardware bps are allowed
	if (hwbp == 0xFFFF) { // hardware bp unused
	  bp[i].hw = true;
	  hwbp = waddr;
	} else { // we steal it from other bp since the most recent should be a hwbp
	  j = gdbFindBreakpoint(hwbp);
	  if (j >= 0 && bp[j].hw) {
	    DEBPR(F("Stealing HWBP from other BP:")); DEBLNF(bp[j].waddr*2,HEX);
	    bp[j].hw = false;
	    bp[i].hw = true;
	    hwbp = waddr;
	  } else reportFatalError(HWBP_ASSIGNMENT_INCONSISTENT_FATAL, false);
	}
      }
      bpcnt++;
      bpused++;
      DEBPR(F("New BP: ")); DEBPRF(waddr*2,HEX); DEBPR(F(" / now active: ")); DEBLN(bpcnt);
      if (bp[i].hw) { DEBLN(F("implemented as a HW BP")); }
      return true;
    }
  }
  reportFatalError(NO_FREE_SLOT_FATAL, false);
  //DEBLN(F("***No free slot in bp array"));
  return false;
}

// inactivate a bp 
void gdbRemoveBreakpoint(unsigned int waddr)
{
  int i;

  measureRam();

  i = gdbFindBreakpoint(waddr);
  if (i < 0) return; // could happen when too many bps were tried to set
  if (!bp[i].active) return; // not active, could happen for duplicate bps 
  //DEBPR(F("Remove BP: ")); DEBLNF(bp[i].waddr*2,HEX);
  bp[i].active = false;
  bpcnt--;
  DEBPR(F("BP removed: ")); DEBPRF(waddr*2,HEX); DEBPR(F(" / now active: ")); DEBLN(bpcnt);
}

// after a restart, go through table
// and cleanup by making all BPs inactive,
// counting the used ones and finally call 'update'
void gdbCleanupBreakpointTable(void)
{
  int i;

  for (i=0; i < MAXBREAK*2; i++) {
    bp[i].active = false;
    if (bp[i].used) bpused++;
  }
  gdbUpdateBreakpoints(false); // now remove all breakpoints
}

// GDB wants to see the 32 8-bit registers (r00 - r31), the
// 8-bit SREG, the 16-bit SP and the 32-bit PC,
// low bytes before high since AVR is little endian.
void gdbReadRegisters(void)
{
  byte a;
  unsigned int b;
  char c;
  unsigned int pc = (unsigned long)ctx.wpc << 1;	/* convert word address to byte address used by gdb */
  byte i = 0;

  a = 32;	/* in the loop, send R0 thru R31 */
  b = (unsigned int) &(ctx.regs);
  
  do {
    c = *(char*)b++;
    buf[i++] = nib2hex((c >> 4) & 0xf);
    buf[i++] = nib2hex((c >> 0) & 0xf);
    
  } while (--a > 0);
  
  /* send SREG as 32 register */
  buf[i++] = nib2hex((ctx.sreg >> 4) & 0xf);
  buf[i++] = nib2hex((ctx.sreg >> 0) & 0xf);
  
  /* send SP as 33 register */
  buf[i++] = nib2hex((ctx.sp >> 4)  & 0xf);
  buf[i++] = nib2hex((ctx.sp >> 0)  & 0xf);
  buf[i++] = nib2hex((ctx.sp >> 12) & 0xf);
  buf[i++] = nib2hex((ctx.sp >> 8)  & 0xf);
  
  /* send PC as 34 register
     gdb stores PC in a 32 bit value.
     gdb thinks PC is bytes into flash, not in words. */
  buf[i++] = nib2hex((pc >> 4)  & 0xf);
  buf[i++] = nib2hex((pc >> 0)  & 0xf);
  buf[i++] = nib2hex((pc >> 12) & 0xf);
  buf[i++] = nib2hex((pc >> 8)  & 0xf);
  buf[i++] = '0'; /* gdb wants 32-bit value, send 0 */
  buf[i++] = '0'; /* gdb wants 32-bit value, send 0 */
  buf[i++] = '0'; /* gdb wants 32-bit value, send 0 */
  buf[i++] = '0'; /* gdb wants 32-bit value, send 0 */
  
  buffill = i;
  gdbSendBuff(buf, buffill);
  
}

// set all registers with values given by GDB
void gdbWriteRegisters(const byte *buff)
{
  byte a;
  unsigned int pc;
  a = 32;	/* in the loop, receive R0 thru R31 */
  byte *ptr = &(ctx.regs[0]);

  measureRam();

  do {
    *ptr  = hex2nib(*buff++) << 4;
    *ptr |= hex2nib(*buff++);
    ptr++; // of course, pointer must be incremented!
  } while (--a > 0);
  
  /* receive SREG as register 32  */
  ctx.sreg = hex2nib(*buff++) << 4;
  ctx.sreg |= hex2nib(*buff++);
  
  /* receive SP as register 33  */
  ctx.sp  = hex2nib(*buff++) << 4;
  ctx.sp |= hex2nib(*buff++);
  ctx.sp |= hex2nib(*buff++) << 12;
  ctx.sp |= hex2nib(*buff++) << 8;
  /* receive PC as register 34 
     gdb stores PC in a 32 bit value.
     gdb thinks PC is bytes into flash, not in words. */
  pc  = hex2nib(*buff++) << 4;
  pc |= hex2nib(*buff++);
  pc |= hex2nib(*buff++) << 12;
  pc |= hex2nib(*buff++) << 8;
  pc |= (unsigned long)hex2nib(*buff++) << 20;
  pc |= (unsigned long)hex2nib(*buff++) << 16;
  pc |= (unsigned long)hex2nib(*buff++) << 28;
  pc |= (unsigned long)hex2nib(*buff++) << 24;
  ctx.wpc = pc >> 1;	/* drop the lowest bit; PC addresses words */
  gdbSendReply("OK");
}

// read out some of the memory and send to it GDB
void gdbReadMemory(const byte *buff)
{
  unsigned long addr;
  unsigned long sz, flag;
  byte i, b;

  measureRam();

  buff += parseHex(buff, &addr);
  /* skip 'xxx,' */
  parseHex(buff + 1, &sz);
  
  if (sz > MAXMEMBUF || sz*2 > MAXBUF) { // should not happen because we required packet length to be less
    gdbSendReply("E04");
    reportFatalError(PACKET_LEN_FATAL, false);
    //DEBLN(F("***Packet length too large"));
    return;
  }

  if (targetOffline()) {
    gdbSendReply("E01");
    return;
  }

  flag = addr & MEM_SPACE_MASK;
  addr &= ~MEM_SPACE_MASK;
  if (flag == SRAM_OFFSET) targetReadSram(addr, membuf, sz);
  else if (flag == FLASH_OFFSET) {
    targetReadFlash(addr, membuf, sz);
    gdbHideBREAKs(addr, membuf, sz);
  } else if (flag == EEPROM_OFFSET) targetReadEeprom(addr, membuf, sz);
  else {
    gdbSendReply("E05");
    return;
  }
  for (i = 0; i < sz; ++i) {
    b = membuf[i];
    buf[i*2 + 0] = nib2hex(b >> 4);
    buf[i*2 + 1] = nib2hex(b & 0xf);
  }
  buffill = sz * 2;
  gdbSendBuff(buf, buffill);
}

// hide BREAK instructions that are not supposed to be there (i.e., those not yet removed)
void gdbHideBREAKs(unsigned int startaddr, byte membuf[], int size)
{
  int bpix;
  unsigned long addr;

  measureRam();

  for (addr = startaddr; addr < startaddr+size; addr++) {
    if ((addr & 1) && membuf[addr-startaddr] == 0x95) { // uneven address and match with MSB of BREAK
      bpix = gdbFindBreakpoint((addr-1)/2);
      if (bpix >= 0 && bp[bpix].inflash) // now hide always:  && !bp[bpix].active) 
	membuf[addr-startaddr] = bp[bpix].opcode>>8; // replace with MSB of opcode
    }
    if (((addr&1) == 0) && membuf[addr-startaddr] == 0x98) { // even address and match with LSB of BREAK
      bpix = gdbFindBreakpoint(addr/2);
      if (bpix >= 0 && bp[bpix].inflash) // now hide always:  && !bp[bpix].active)
	membuf[addr-startaddr] = bp[bpix].opcode&0xFF;
    }
  }
}

// write to target memory
void gdbWriteMemory(const byte *buff, boolean binary)
{
  unsigned long sz, flag, addr,  i;
  long memsz;

  measureRam();

  if (targetOffline()) {
    gdbSendReply("E01");
    return;
  }

  buff += parseHex(buff, &addr);
  /* skip 'xxx,' */
  buff += parseHex(buff + 1, &sz);
  /* skip , and : delimiters */
  buff += 2;

  // convert to binary data by deleting the escapes
  if (sz > MAXMEMBUF) { // should not happen because we required packet length to be less
    gdbSendReply("E05");
    reportFatalError(PACKET_LEN_FATAL, false);
    return;
  }
  if (binary) {
    memsz = gdbBin2Mem(buff, membuf, sz);
    if (memsz < 0) { 
      gdbSendReply("E05");
      reportFatalError(NEG_SIZE_FATAL, false);
      return;
    }
    sz = (unsigned long)memsz;
  } else {
    for ( i = 0; i < sz; ++i) {
      membuf[i]  = hex2nib(*buff++) << 4;
      membuf[i] |= hex2nib(*buff++);
    }
  }
    
  flag = addr & MEM_SPACE_MASK;
  addr &= ~MEM_SPACE_MASK;
  switch (flag) {
  case SRAM_OFFSET:
    if (addr+sz > mcu.ramsz+mcu.rambase) {
      gdbSendReply("E02"); 
      return;
    }
    targetWriteSram(addr, membuf, sz);
    break;
  case FLASH_OFFSET:
    if (addr+sz > mcu.flashsz) {
      //DEBPR(F("addr,sz,flashsz=")); DEBLN(addr); DEBLN(sz); DEBLN(mcu.flashsz);
      gdbSendReply("E03"); 
      return;
    }
    setSysState(LOAD_STATE);
    ctx.notloaded = false;
    targetWriteFlash(addr, membuf, sz);
    break;
  case EEPROM_OFFSET:
    if (addr+sz > mcu.eepromsz) {
      gdbSendReply("E04"); 
      return;
    }
    targetWriteEeprom(addr, membuf, sz);
    break;
  default:
    gdbSendReply("E06"); 
    reportFatalError(WRONG_MEM_FATAL, false);
    return;
  }
  gdbSendReply("OK");
}


// Convert the binary stream in BUF to memory.
// Gdb will escape $, #, and the escape char (0x7d).
// COUNT is the total number of bytes to read
int gdbBin2Mem(const byte *buf, byte *mem, int count) {
  int i, num = 0;
  byte  escape;

  measureRam();
  for (i = 0; i < count; i++) {
    /* Check for any escaped characters. Be paranoid and
       only unescape chars that should be escaped. */
    escape = 0;
    if (*buf == 0x7d) {
      switch (*(buf + 1)) {
      case 0x03: /* # */
      case 0x04: /* $ */
      case 0x5d: /* escape char */
      case 0x0a: /* * - the docu says to escape it only if a response, but avr-gdb escapes it 
                    anyway */
	buf++;
	escape = 0x20;
	break;
      default:
	return -1; // signal error if something has been escaped that shouldn't
	break;
      }
    }
    *mem = *buf++ | escape;
    mem++;
    num++;
  }
  return num;
}

// check whether connected
boolean targetOffline(void)
{
  measureRam();
  if (ctx.state == PWRCYC_STATE || ctx.state == DWCONN_STATE ||
      ctx.state == RUN_STATE || ctx.state == LOAD_STATE) return false;
  return true;
}


/****************** some GDB I/O functions *************/
// clear input buffer
void flushInput()
{
  while (Serial.available()) Serial.read(); 
}

// send byte host
inline void gdbSendByte(byte b)
{
  Serial.write(b);
#if SDEBUG
  Serial1.write(b);
#endif
}

// blocking read byte from host
inline byte gdbReadByte(void)
{
  while (!Serial.available());
#if SDEBUG
  byte b = Serial.read();
  Serial1.write(b);
  return(b);
#else
  return Serial.read();
#endif
} 

void gdbSendReply(const char *reply)
{
  measureRam();

  buffill = strlen(reply);
  if (buffill > (MAXBUF - 4))
    buffill = MAXBUF - 4;

  memcpy(buf, reply, buffill);
  gdbSendBuff(buf, buffill);
}

void gdbSendSignal(byte signo)
{
  char buf[4];
  buf[0] = 'S';
  buf[1] = nib2hex((signo >> 4) & 0x0F);
  buf[2] = nib2hex(signo & 0x0F);
  buf[3] = '\0';
  gdbSendReply(buf);
}

void gdbSendBuff(const byte *buff, int sz)
{
  measureRam();

  byte sum = 0;
  // If a CTRL-C is in the input buffer, do not send the packet
  // That helps to stop a single-stepping loop!
  if (Serial.available() && Serial.peek() == 0x03) return;
  gdbSendByte('$');
  while ( sz-- > 0)
    {
      gdbSendByte(*buff);
      sum += *buff;
      buff++;
    }
  gdbSendByte('#');
  gdbSendByte(nib2hex((sum >> 4) & 0xf));
  gdbSendByte(nib2hex(sum & 0xf));
}


void gdbSendPSTR(const char pstr[])
{
  byte sum = 0;
  byte c;
  int i = 0;
  
  gdbSendByte('$');
  do {
    c = pgm_read_byte(&pstr[i++]);
    if (c) {
      gdbSendByte(c);
      sum += c;
    }
  } while (c);
  gdbSendByte('#');
  gdbSendByte(nib2hex((sum >> 4) & 0xf));
  gdbSendByte(nib2hex(sum & 0xf));
}

void gdbState2Buf(byte signo)
{
  unsigned long wpc = (unsigned long)ctx.wpc << 1;

  /* thread is always 1 */
  /* Real packet sent is e.g. T0520:82;21:fb08;22:021b0000;thread:1;
     05 - last signal response
     Numbers 20, 21,... are number of register in hex in the same order as in read registers
     20 (32 dec) = SREG
     21 (33 dec) = SP
     22 (34 dec) = PC (byte address)
  */
  measureRam();

  memcpy_P(buf,
	   PSTR("TXX20:XX;21:XXXX;22:XXXXXXXX;thread:1;"),
	   38);
  buffill = 38;
  
  /* signo */
  buf[1] = nib2hex((signo >> 4)  & 0xf);
  buf[2] = nib2hex(signo & 0xf);
  
  /* sreg */
  buf[6] = nib2hex((ctx.sreg >> 4)  & 0xf);
  buf[7] = nib2hex(ctx.sreg & 0xf);
  
  /* sp */
  buf[12] = nib2hex((ctx.sp >> 4)  & 0xf);
  buf[13] = nib2hex((ctx.sp >> 0)  & 0xf);
  buf[14] = nib2hex((ctx.sp >> 12) & 0xf);
  buf[15] = nib2hex((ctx.sp >> 8)  & 0xf);
  
  /* pc */
  buf[20] = nib2hex((wpc >> 4)  & 0xf);
  buf[21] = nib2hex((wpc >> 0)  & 0xf);
  buf[22] = nib2hex((wpc >> 12) & 0xf);
  buf[23] = nib2hex((wpc >> 8)  & 0xf);
  buf[24] = '0';
  buf[25] = nib2hex((wpc >> 16) & 0xf);
  buf[26] = '0'; /* gdb wants 32-bit value, send 0 */
  buf[27] = '0'; /* gdb wants 32-bit value, send 0 */
}

void gdbSendState(byte signo)
{
  targetSaveRegisters();
  switch (signo) {
  case SIGHUP:
    gdbDebugMessagePSTR(PSTR("No connection to target"),-1);
    setSysState(NOTCONN_STATE);
    break;
  case SIGILL:
    gdbDebugMessagePSTR(PSTR("Illegal instruction"),-1);
    break;
  case SIGABRT:
    if (fatalerror == 0)
      break;
    if (fatalerror < 100)  
      gdbDebugMessagePSTR(PSTR("***Fatal debugger error: "),fatalerror);
    else
      gdbDebugMessagePSTR(PSTR("***Fatal internal debugger error: "),fatalerror);
    setSysState(ERROR_STATE);
    break;
  }
  gdbState2Buf(signo);
  gdbSendBuff(buf, buffill);
  lastsignal = signo;
}


// reply to a monitor command
// use string from PROGMEN
// if last argument < -1, then use it as index into MCU name array (index: abs(num)-1)
void gdbReplyMessagePSTR(const char pstr[], long num) 
{
  gdbMessage(pstr, num, false, true);
}


// reply to a monitor command
// use string from char array
void gdbReplyMessage(const char str[])
{
  gdbMessage(str, -1, false, false);
  // DEBLN(str);
}

// send a message the user can see, if last argument positive, then send the number
// if last argument < -1, then use it as index into MCU name array (index: abs(num)-1)
void gdbDebugMessagePSTR(const char pstr[],long num) 
{
  gdbMessage(pstr, num, true, true);
  
}

// send a message, either prefixed by 'O' or not
void gdbMessage(const char pstr[],long num, boolean oprefix, boolean progmem)
{
  int i = 0, j = 0, c;
  byte numbuf[10];
  const char *str;

  //DEBLN(pstr);
  //DEBLN(progmem);
  if (oprefix) buf[i++] = 'O';
  do {
    c = (progmem ? pgm_read_byte(&pstr[j++]) : pstr[j++]);
    if (c) {
      //DEBPR((char)c);
      if (i+4 >= MAXBUF) continue;
      buf[i++] = nib2hex((c >> 4) & 0xf);
      buf[i++] = nib2hex((c >> 0) & 0xf);
    }
  } while (c);
  if (num >= 0) {
    convNum(numbuf,num);
    j = 0;
    while (numbuf[j] != '\0') j++;
    while (j-- > 0 ) {
      //DEBPR((char)numbuf[j]);
      if (i+4 >= MAXBUF) continue;
      buf[i++] = nib2hex((numbuf[j] >> 4) & 0xf);
      buf[i++] = nib2hex((numbuf[j] >> 0) & 0xf);
    }
  } else if (num == -2 ) { // print MCU name
    str = mcu.name;
    if (!str) str = unknown;
    do {
      c = pgm_read_byte(str++);
      if (c) {
	//DEBPR((char)c);
	if (i+4 >= MAXBUF) continue;
	buf[i++] = nib2hex((c >> 4) & 0xf);
	buf[i++] = nib2hex((c >> 0) & 0xf);
      }
    } while (c );
  }
  buf[i++] = '0';
  buf[i++] = 'A';
  buf[i] = 0;
  gdbSendBuff(buf, i);
  //DEBLN();
}


/****************** target functions *************/

// try to connect to debugWIRE by issuing a break condition
// set mcu struct if not already set
// return true if successful
boolean targetDWConnect(boolean initialconnect)
{
  unsigned int sig;

  if (doBreak(!initialconnect)) {
    DEBLN(F("targetConnect: doBreak done"));
    sig = DWgetChipId();
    if (mcu.sig == 0) setMcuAttr(sig);
    return true;
  }
  return false;
}

// try to establish an ISP connection and program the DWEN fuse
// if possible, set DWEN fuse
//   0 if we need to powercycle
//   -1 if we cannot connect
//   -2 if unknown MCU type
//   -3 if lock bits set
//   -7 MCU mismatch
int targetISPConnect(void)
{
  unsigned int sig;
  int result = 0;
  boolean succ;
  
  if (!enterProgramMode()) return -1;
  sig = ispGetChipId();
  if (sig == 0) { // no reasonable signature
    result = -1;
  } else if (!setMcuAttr(sig)) {
    result = -2;
  } else {
    if (!gdbCheckMcu())
      return -7;
    if (ispLocked()) {
      ispEraseFlash(); // erase flash mem and lock bits
      leaveProgramMode();
      _delay_ms(1000);
      enterProgramMode();
      if (ispLocked()) 
	result = -3;
      else { // here we disable the bootloader-vector fuse BOOTRST if set
	// ATmega88/168: Ext. Fuse, Bit 0
	// ATmega328: High Fuse, Bit 0
	succ = true;
	if (sig == 0x930A || sig == 0x930F || sig == 0x9316 || sig == 0x9406 || sig == 0x940B || sig == 0x9415) {
	  // ATmega88 or 168: BOOTRST is in bit 0 of the extended fuse byte
	  succ = ispProgramFuse(ExtendedFuse, 1, 1);
	} else if (sig == 0x9514 || sig == 0x950F || sig == 0x9516) {
	  // ATmega328: BOOTRST is bit 0 of high fuse byte
	  succ = ispProgramFuse(HighFuse, 1, 1);
	}
	if (succ) result = 0;
	else result = -3;
      }
    }
  }
  if (result == 0) {
    if (ispProgramFuse(HighFuse, mcu.dwenfuse, 0)) { // set DWEN fuse and powercycle later
      result = 0;
    } else {
      result = -1;
    }
  }
  leaveProgramMode();
  //DEBPR(F("Programming result: ")); DEBLN(result);
  return result;
}




// disable debugWIRE mode
boolean targetStop(void)
{
  int ret = 1;
  ret = targetSetFuses(DWEN);       // otherwise disable DW mode first
  leaveProgramMode();
  return (ret == 1);
}


// set the fuses/clear memory, returns
//  1 - if successful
// -1 - if we cannot enter programming mode or sig is not readable
// -2 - if unknown MCU type
// -3 - programming was unsuccessful
// -4 - no XTAL allowed
// -5 - no slow clock
// -6 - no alternate RC clock

int targetSetFuses(Fuses fuse)
{
  unsigned int sig;
  boolean succ;

  measureRam();
  if (doBreak(true)) {
    dw.sendCmd(DW_STOP_CMD); // leave debugWIRE mode
  } 
  if (!enterProgramMode()) return -1;
  sig = ispGetChipId();
  if (sig == 0) {
    leaveProgramMode();
    return -1;
  }
  if (!setMcuAttr(sig)) {
    leaveProgramMode();
    return -2;
  }
  // now we are in ISP mode and know what processor we are dealing with
  switch (fuse) {
  case Erase:  succ = ispEraseFlash(); break;
  case DWEN:   succ = ispProgramFuse(HighFuse, mcu.dwenfuse, mcu.dwenfuse); break; // disable DWEN!
  default: succ = false;
  }
  return (succ ? 1 : -3);
}



// read one flash page with base address 'addr' into the global 'page' buffer,
// do this only if the page has not been read already,
// remember that page has been read and that the buffer is valid
void targetReadFlashPage(unsigned int addr)
{
  //DEBPR(F("Reading flash page starting at: "));DEBLNF(addr,HEX);
  if (addr != (addr & ~((unsigned int)(mcu.targetpgsz-1)))) {
    // DEBLN(F("***Page address error when reading"));
    reportFatalError(READ_PAGE_ADDR_FATAL, false);
    return;
  }
  if (!validpg || (lastpg != addr)) {
    targetReadFlash(addr, page, mcu.targetpgsz);
    lastpg = addr;
    validpg = true;
  } else {
    // DEBPR(F("using cached page at ")); DEBLNF(lastpg,HEX);
  }
}

// read one word of flash (must be an even address!)
unsigned int targetReadFlashWord(unsigned int addr)
{
  byte temp[2];
  if (addr & 1) reportFatalError(FLASH_READ_WRONG_ADDR_FATAL, false);
  DWreadFlash(addr, temp, 2);
  return temp[0] + ((unsigned int)(temp[1]) << 8);
}

// read some portion of flash memory to the buffer pointed at by *mem'
// do not check for cached pages etc.
void targetReadFlash(unsigned int addr, byte *mem, unsigned int len)
{
  DWreadFlash(addr, mem, len);
}

// read some portion of SRAM into buffer pointed at by *mem
void targetReadSram(unsigned int addr, byte *mem, unsigned int len)
{
  unsigned int offset = 0;
  unsigned int end = addr + len;
  const byte *mask = mcu.mask;
  byte mask_reg = 0;
  DEBPR(F("targetReadSram start at 0x"));
  DEBPRF(addr, HEX);
  DEBPR(F(", length="));
  DEBLN(len);
  if (addr > 0xFF) { // if in sram, simply read
    DEBPR(F("Simple SRAM read starting at 0x"));
    DEBLNF(addr,HEX);
    DWreadSramBytes(addr, mem, len);
    return;
  }
  while (addr+offset < 0x20) {
    DEBPR(F("Reading register 0x"));
    DEBLNF(addr+offset,HEX);
    mem[offset] = ctx.regs[addr+offset];
    offset++;
  }
  DEBPR(F("Start masked read at 0x"));
  DEBLNF(addr+offset, HEX);
  DEBPR(F("End address is 0x"));
  DEBLNF(end,HEX);
  while (mask && (mask_reg = pgm_read_byte(mask++))) {
    DEBPR(F("Reading until mask register: 0x"));
    DEBLNF(mask_reg,HEX);
    DEBPR(F("Current address: 0x"));
    DEBLNF(addr+offset,HEX);
    if (mask_reg >= end or addr + offset >= end) {
      DEBLN(F("We are done here."));
      break;
    }
    if (mask_reg < addr+offset) {
      DEBLN(F("Mask reg addr too small, skip!"));
      continue;
    }
    if (addr + offset < mask_reg) {
      DEBPR(F("Reading starts at: 0x"));
      DEBPRF(addr+offset, HEX);
      DEBPR(F(", length="));
      DEBLN(mask_reg - (addr+offset));
      DWreadSramBytes(addr + offset, &mem[offset], mask_reg - (addr + offset));
      offset = offset + mask_reg - (addr+offset);
    }
    DEBPR(F("Setting value for masked register at 0x"));
    DEBPRF(addr+offset, HEX);
    DEBLN(F(" to 0x00"));
    mem[offset] = 0x00;
    offset = mask_reg + 1 - addr;
  }
  DEBPR(F("Done with masked reading. Now from: 0x"));
  DEBPRF(addr+offset, HEX);
  DEBPR(F(", length="));
  DEBLN(len-offset);
  if (addr + offset < end)
    DWreadSramBytes(addr+offset, &mem[offset], len-offset);
  DEBLN(F("Leaving targetSramRead"));
}

// read some portion of EEPROM
void targetReadEeprom(unsigned int addr, byte *mem, unsigned int len)
{
  for (unsigned int i=0; i < len; i++) {
    mem[i] = DWreadEepromByte(addr++);
  }
}

// write a flash page from buffer 'newpage'
// check whether the data is already in this flash page,
// if so, do nothing,
// check whether we can get away with simply overwriting,
// if not erase page,
// and finally write page
// remember page content in 'page' buffer
// if the MCU use the 4-page erase operation, then
// do 4 load/program cycles for the 4 sub-pages
void targetWriteFlashPage(unsigned int addr)
{
  byte subpage;
  boolean dirty = true;


  measureRam();

  //DEBPR(F("Write flash ... "));
  //DEBPRF(addr, HEX);
  //DEBPR("-");
  //DEBPRF(addr+mcu.targetpgsz-1,HEX);
  //DEBLN(":");
  if (addr != (addr & ~(mcu.targetpgsz-1))) {
    //DEBLN(F("\n***Page address error when writing"));
    reportFatalError(WRITE_PAGE_ADDR_FATAL, false);
    return;
  }
  DWreenableRWW();
  if (ctx.readbeforewrite) {
    // read old page contents (maybe from page cache)
    targetReadFlashPage(addr);
    // check whether something changed
    // DEBPR(F("Check for change: "));
    if (memcmp(newpage, page, mcu.targetpgsz) == 0) {
      //DEBLN(F("page unchanged"));
      return;
    }
    // DEBLN(F("changed"));

#if TXODEBUG && 0
    DEBLN(F("Changes in flash page:"));
    for (unsigned int i=0; i<mcu.targetpgsz; i++) {
      if (page[i] != newpage[i]) {
	DEBPRF(i+addr, HEX);
	DEBPR(": ");
	DEBPRF(newpage[i], HEX);
	DEBPR(" -> ");
	DEBPRF(page[i], HEX);
	DEBLN("");
      }
    }
#endif
  
    // check whether we need to erase the page
    dirty = false;
    for (byte i=0; i < mcu.targetpgsz; i++) 
      if (~page[i] & newpage[i]) {
	dirty = true;
	break;
      }
  }
  validpg = false;
  
  // erase page when dirty
  if (dirty) DWeraseFlashPage(addr);
    
  DWreenableRWW();
  // maybe the new page is also empty?
  memset(page, 0xFF, mcu.targetpgsz);
  if (memcmp(newpage, page, mcu.targetpgsz) == 0) {
    // DEBLN(" nothing to write");
    validpg = true;
    return;
  }
  
  
  // now do the programming; for 4-page erase MCUs four subpages
  for (subpage = 0; subpage < 1+(3*mcu.erase4pg); subpage++) {
    //DEBPR(F("writing subpage at ")); DEBLNF(addr+subpage*mcu.pagesz,HEX);
    DWloadFlashPageBuffer(addr+(subpage*mcu.pagesz), &newpage[subpage*mcu.pagesz]);
    DWprogramFlashPage(addr+subpage*mcu.pagesz);
    DWreenableRWW();
  }

  if (ctx.verifyload) {
    // read back last programmed page and compare
    validpg = false;
    targetReadFlashPage(addr);
    if (memcmp(newpage, page, mcu.targetpgsz) != 0)
      reportFatalError(FLASH_VERIFY_FATAL, false);
  } else {
    // remember the last programmed page
    memcpy(page, newpage, mcu.targetpgsz);
    validpg = true;
    lastpg = addr;
  }
}

// write some chunk of data to flash in a lazy way:
// - if empty length, write active page to flash and return
// - if a new byte to be written leaves the current page, write the page before anything else
// - if this is the first new byte for a page, retrieve this page
// - insert new byte into page

void targetWriteFlash(unsigned int addr, byte *mem, unsigned int len)
{
  unsigned int ix;
  unsigned int newaddr;
  
  measureRam();

  if (len == 0) {
    if (!flashidle) targetWriteFlashPage(flashpageaddr);
    flashidle = true;
    return;
  }

  for (ix = 0; ix < len; ix++) {
    newaddr = addr + ix;
    if ((newaddr < flashpageaddr || newaddr > flashpageaddr + mcu.targetpgsz - 1) && !flashidle) {
      targetWriteFlashPage(flashpageaddr);
      flashidle = true;
    }
    if (flashidle) {
      flashpageaddr = newaddr & ~(mcu.targetpgsz-1);
      targetReadFlashPage(flashpageaddr);
      memcpy(newpage, page, mcu.targetpgsz);
      flashidle = false;
    }
    newpage[newaddr-flashpageaddr] = mem[ix];
  }
}

// flush any bytes that still have to be written to flash memory
inline void targetFlushFlashProg(void)
{
  targetWriteFlash(0, membuf, 0);
}

// write SRAM chunk
void targetWriteSram(unsigned int addr, byte *mem, unsigned int len)
{
  measureRam();
  int offset = 0;

  while (addr+offset < 32 && offset < len) { // if addr points to registers, then write to in-memory copy 
    ctx.regs[addr+offset] = mem[offset];
    offset++;
  }

  while (offset < len) {
    DWwriteSramByte(addr+offset, mem[offset]);
    offset++;
  }
}

// write EEPROM chunk
void targetWriteEeprom(unsigned int addr, byte *mem, unsigned int len)
{
  measureRam();

  for (unsigned int i=0; i < len; i++) {
    DWwriteEepromByte(addr+i, mem[i]);
  }
}

// initialize registers (after RESET)
void targetInitRegisters(void)
{
  byte a;
  a = 32;	/* in the loop, send R0 thru R31 */
  byte *ptr = &(ctx.regs[31]);
  measureRam();

  do {
    *ptr-- = a;
  } while (--a > 0);
  ctx.sreg = 0;
  ctx.wpc = 0;
  ctx.sp = 0x1234;
  ctx.saved = true;
}

// read all registers from target and save them
void targetSaveRegisters(void)
{
  measureRam();

  if (ctx.saved) return;         // If the regs have been saved, then the machine regs are clobbered, so do not load again!
  ctx.wpc = DWgetWPc(true);      // needs to be done first, because the PC is advanced when executing instrs in the instr reg
  DWreadRegisters(&ctx.regs[0]); // now get all GP registers
  ctx.sreg = DWreadIOreg(0x3F);
  ctx.sp = DWreadIOreg(0x3D);
  if (mcu.ramsz+mcu.rambase >= 256) ctx.sp |= DWreadIOreg(0x3E) << 8;
  ctx.saved = true;
}

// restore all registers on target (before execution continues)
void targetRestoreRegisters(void)
{
  measureRam();

  if (!ctx.saved) return; // if not in saved state, do not restore!
  DWwriteIOreg(0x3D, (ctx.sp&0xFF));
  if (mcu.ramsz > 256) DWwriteIOreg(0x3E, (ctx.sp>>8)&0xFF);
  DWwriteIOreg(0x3F, ctx.sreg);
  DWwriteRegisters(&ctx.regs[0]);
  DWsetWPc(ctx.wpc); // must be done last!
  ctx.saved = false; // now, we can save them again and be sure to get the right values
}

// send break in order to stop execution asynchronously
void targetBreak(void)
{
  measureRam();
  dw.sendBreak(); // send the break
}

// start to execute
void targetContinue(void)
{
  measureRam();

  // DEBPR(F("Continue at (byte adress) "));  DEBLNF(ctx.wpc*2,HEX);
  if (hwbp != 0xFFFF) {
    dw.sendCmd((byte)(0x61&ctx.tmask));
    DWsetWBp(hwbp);
  } else {
    dw.sendCmd((byte)(0x60&ctx.tmask));
  }
  DWsetWPc(ctx.wpc);
  dw.sendCmd(0x30, true); // return during sending the stop bit so that nothing surprises us
}

// make a single step
void targetStep(void)
{
  measureRam();

  // DEBPR(F("Single step at (byte address):")); DEBLNF(ctx.wpc*2,HEX);
  // _delay_ms(5);
  byte cmd[] = {(byte)(0x60&ctx.tmask), 0xD0, (byte)(ctx.wpc>>8), (byte)(ctx.wpc), 0x31};
  dw.sendCmd(cmd, sizeof(cmd), true); // return before last bit is sent
}

// reset the MCU
boolean targetReset(void)
{
  dw.sendCmd(DW_RESET_CMD, true); // return before last bit is sent so that we catch the break
  
  if (expectBreakAndU()) {
    DEBLN(F("RESET successful"));
    return true;
  } else {
    DEBLN(F("***RESET failed"));
    reportFatalError(RESET_FAILED_FATAL, true);
    return false;
  }
}

#if ILLOPDETECT
// check for illegal opcodes
// based on: http://lyons42.com/AVR/Opcodes/AVRAllOpcodes.html
boolean targetIllegalOpcode(unsigned int opcode)
{
  byte lsb = opcode & 0xFF;
  byte msb = (opcode & 0xFF00)>>8;

  measureRam();

  switch(msb) {
  case 0x00: // nop
    DEBLNF(msb, HEX); DEBLNF(lsb, HEX); 
    return lsb != 0; 
  case 0x02: // muls
  case 0x03: // mulsu/fmuls
    return !mcu.avreplus;
  case 0x90: 
  case 0x91: // lds, ld, lpm, elpm
    if ((lsb & 0x0F) == 0x3 || (lsb & 0x0F) == 0x6 || (lsb & 0x0F) == 0x7 ||
	(lsb & 0x0F) == 0x8 || (lsb & 0x0F) == 0xB) return true; // unassigned + elpm
    if (opcode == 0x91E1 || opcode == 0x91E2 || opcode == 0x91F1 || opcode == 0x91F2 ||
	opcode == 0x91E5 || opcode == 0x91F5 || 
	opcode == 0x91C9 || opcode == 0x91CA || opcode == 0x91D9 || opcode == 0x91DA ||
	opcode == 0x91AD || opcode == 0x91AE || opcode == 0x91BD || opcode == 0x91BE)
      return true; // undefined behavior for ld and lpm with increment
    return false;
  case 0x92:
  case 0x93:  // sts, st, push
    if (((lsb & 0xF) >= 0x3 && (lsb & 0xF) <= 0x8) || ((lsb & 0xF) == 0xB)) return true;
    if (opcode == 0x93E1 || opcode == 0x93E2 || opcode == 0x93F1 || opcode == 0x93F2 ||
	opcode == 0x93C9 || opcode == 0x93CA || opcode == 0x93D9 || opcode == 0x93DA ||
	opcode == 0x93AD || opcode == 0x93AE || opcode == 0x93BD || opcode == 0x93BE)
      return true; // undefined behavior for st with increment
    return false;
  case 0x94:
  case 0x95: // ALU, ijmp, icall, ret, reti, jmp, call, des, ...
    if (opcode == 0x9409 || opcode == 0x9509) return false; //ijmp + icall
    if (opcode == 0x9508 || opcode == 0x9518 || opcode == 0x9588 || opcode == 0x95A8 ||
	opcode == 0x95C8 || opcode == 0x95E8) return false; // ret, reti, sleep, wdr, lpm, spm
    if ((lsb & 0xF) == 0x4 || (lsb & 0xF) == 0x9 || (lsb & 0xF) == 0xB) return true;
    if ((lsb & 0xF) == 0x8 && msb == 0x95) return true; // unassigned + break + spm z+
    break;
  case 0x9c:
  case 0x9d:
  case 0x9e:
  case 0x9f: // mul
    return !mcu.avreplus;
  default: if (((msb & 0xF8) == 0xF8) && ((lsb & 0xF) >= 8)) return true; 
    return false;
  }
  if (mcu.flashsz <= 8192)  // small ATtinys for which CALL and JMP is not needed/permitted
    if ((opcode & 0x0FE0E) == 0x940C || // jmp
	(opcode & 0x0FE0E) == 0x940E)  // call
      return true;
  return false;
}
#endif


/****************** debugWIRE specific functions *************/

// send a break on the RESET line, check for response and calibrate 
boolean doBreak (boolean doprinterror = true) {
  measureRam();

  DEBLN(F("doBreak"));
  pinMode(DWLINE, INPUT);
  _delay_ms(10);
  ctx.bps = 0; // forget about previous connection
  dw.sendBreak(); // send a break
#if SCOPEDEBUG
  PORTD|=(1<<PD3);
  PORTD&=~(1<<PD3);
#endif
  _delay_us(20);
#if SCOPEDEBUG
  PORTD|=(1<<PD3);
  PORTD&=~(1<<PD3);
#endif
  if (digitalRead(DWLINE) == LOW) { // should be high by now!
    gdbReportConnectionProblem(CONNERR_CAPACITIVE_LOAD, doprinterror);  // if not, we have a capacitive load on the RESET line
    return false;
  }
  if (!expectUCalibrate()) {
    DEBLN(F("No response after break"));
    return false;
  }
  //DEBPR(F("Successfully connected with bps: ")); DEBLN(ctx.bps);
  return true;
}

// re-calibrate on a sent 0x55, then try to set the highest possible speed, i.e.,
// multiply speed by at most 16 up to 250k baud - provided we have another speed than
// before
// return false if syncing was unsuccessful
boolean expectUCalibrate(void) {
  int8_t speed;
  unsigned long newbps;

  measureRam();
  blockIRQ();
  newbps = dw.calibrate(); // expect 0x55 and calibrate
  //DEBPR(F("Rsync (1): ")); DEBLN(newbps);
  if (newbps < 5) {
    ctx.bps = 0;
    unblockIRQ();
    return false; // too slow
  }
  if ((100*(abs((long)ctx.bps-(long)newbps)))/newbps <= 1)  { // less than 2% deviation -> ignore change
    //DEBLN(F("No change: return"));
    unblockIRQ();
    return true;
  }
  dw.begin(newbps);
  for (speed = maxspeedexp; speed > 0; speed--) {
    if ((newbps << speed) <= speedlimit) break;
  }
  //DEBPR(F("Set speedexp: ")); DEBLN(speed);
#if CONSTDWSPEED == 0
  DWsetSpeed(speed);
  ctx.bps = dw.calibrate(); // calibrate again
  unblockIRQ();
  //DEBPR(F("Rsync (2): ")); DEBLN(ctx.bps);
  if (ctx.bps < 70) {
    //DEBLN(F("Second calibration too slow!"));
    return false; // too slow
  }
#else
  ctx.bps = newbps;
#endif
  dw.begin(ctx.bps);
  return true;
}

// expect a break followed by 0x55 from the target and (re-)calibrate
boolean expectBreakAndU(void)
{
  unsigned long timeout = 300000; // roughly 300-600 msec
  byte cc;
  
  // wait first for a zero byte
  while (!dw.available() && timeout != 0) timeout--;
  if (timeout == 0) {
    //DEBLN(F("Timeout in expectBreakAndU"));
    return false;
  }
  if ((cc = dw.read()) != 0) {
    //DEBPR(F("expected 0x00, got: 0x")); DEBLNF(cc,HEX);
    return false;
  }
  return expectUCalibrate();
}


// wait for response and store in buf
unsigned int getResponse (unsigned int expected) {
  measureRam();
  return getResponse(&buf[0], expected);
}

// wait for response and store in some data area
unsigned int getResponse (byte *data, unsigned int expected) {
  unsigned int idx = 0;
  unsigned long timeout = 0;
 
  measureRam();

  if (dw.overflow())
    reportFatalError(INPUT_OVERLFOW_FATAL, true);
  do {
    if (dw.available()) {
      data[idx++] = dw.read();
      timeout = 0;
      if (expected > 0 && idx == expected) {
        return expected;
      }
    }
  } while (timeout++ < 20000);
  if (expected > 0) {
    //DEBPR(F("Timeout: received: "));
    //DEBPR(idx);
    //DEBPR(F(" expected: "));
    //DEBLN(expected);
  }
  return idx;
}

// send command and wait for response that should be word, MSB first
unsigned int getWordResponse (byte cmd) {
  unsigned int response;
  byte tmp[2];
  byte cmdstr[] =  { cmd };
  measureRam();

  DWflushInput();
  blockIRQ();
  dw.sendCmd(cmdstr, 1, true); // better stop early so that we are not surprised by the response
  response = getResponse(&tmp[0], 2);
  unblockIRQ();
  if (response != 2) reportFatalError(DW_TIMEOUT_FATAL,true);
  return ((unsigned int) tmp[0] << 8) + tmp[1];
}

// set alternative communcation speed
void DWsetSpeed(byte spix)
{
  byte speedcmdstr[1] = { pgm_read_byte(&speedcmd[spix]) };
  //DEBPR(F("Send speed cmd: ")); DEBLNF(speedcmdstr[0], HEX);
  dw.sendCmd(speedcmdstr, 1, true); // here the early return from writing is strictly necessary!
                                        // returning already after half a stop bit is instrumental
                                        // in capturing the 'U' response for the calibration
}

//  The functions used to read read and write registers, SRAM and flash memory use "in reg,addr" and "out addr,reg" instructions 
//  to transfer data over debugWIRE via the DWDR register.  However, because the location of the DWDR register can vary from device
//  to device, the necessary "in" and "out" instructions need to be build dynamically using the following 4 functions:
// 
//         -- --                            In:  1011 0aar rrrr aaaa
//      D2 B4 02 23 xx   - in r0,DWDR (xx)       1011 0100 0000 0010  a=100010(22), r=00000(00) // How it's used
//         -- --                            Out: 1011 1aar rrrr aaaa
//      D2 BC 02 23 <xx> - out DWDR,r0           1011 1100 0000 0010  a=100010(22), r=00000(00) // How it's used
//
//  Note: 0xD2 sets next two bytes as instruction which 0x23 then executes.  So, in first example, the sequence D2 B4 02 23 xx
//  copies the value xx into the r0 register via the DWDR register.  The second example does the reverse and returns the value
//  in r0 as <xx> by sending it to the DWDR register.

// Build high byte of opcode for "out addr, reg" instruction
byte outHigh (byte add, byte reg) {
  // out addr,reg: 1011 1aar rrrr aaaa
  return 0xB8 + ((reg & 0x10) >> 4) + ((add & 0x30) >> 3);
}

// Build low byte of opcode for "out addr, reg" instruction
byte outLow (byte add, byte reg) {
  // out addr,reg: 1011 1aar rrrr aaaa
  return (reg << 4) + (add & 0x0F);
}

// Build high byte of opcode for "in reg,addr" instruction
byte inHigh  (byte add, byte reg) {
  // in reg,addr:  1011 0aar rrrr aaaa
  return 0xB0 + ((reg & 0x10) >> 4) + ((add & 0x30) >> 3);
}

// Build low byte of opcode for "in reg,addr" instruction
byte inLow  (byte add, byte reg) {
  // in reg,addr:  1011 0aar rrrr aaaa
  measureRam();

  return (reg << 4) + (add & 0x0F);
}

// Write all registers 
void DWwriteRegisters(byte *regs)
{
  byte wrRegs[] = {(byte)(0x66&ctx.tmask),              // read/write
		   0xD0, mcu.stuckat1byte, 0x00,        // start reg
		   0xD1, mcu.stuckat1byte, 0x20,        // end reg
		   0xC2, 0x05,                          // write registers
		   0x20 };                              // go
  measureRam();
  dw.sendCmd(wrRegs,  sizeof(wrRegs));
  dw.sendCmd(regs, 32);
}

// Set register <reg> by building and executing an "in <reg>,DWDR" instruction via the CMD_SET_INSTR register
void DWwriteRegister (byte reg, byte val) {
  byte wrReg[] = {(byte)(0x64&ctx.tmask),                                  // Set up for single step using loaded instruction
                  0xD2, inHigh(mcu.dwdr, reg), inLow(mcu.dwdr, reg), 0x23, // Build "in reg,DWDR" instruction
                  val};                                                    // Write value to register via DWDR
  measureRam();

  dw.sendCmd(wrReg,  sizeof(wrReg));
}

// Read all registers
void DWreadRegisters (byte *regs)
{
  int response;
  byte rdRegs[] = {(byte)(0x66&ctx.tmask),
		   0xD0, mcu.stuckat1byte, 0x00, // start reg
		   0xD1, mcu.stuckat1byte, 0x20, // end reg
		   0xC2, 0x01};                  // read registers
  measureRam();
  DWflushInput();
  dw.sendCmd(rdRegs,  sizeof(rdRegs));
  blockIRQ();
  dw.sendCmd(0x20, true);         // Go
  response = getResponse(regs, 32);
  unblockIRQ();
  if (response != 32) reportFatalError(DW_READREG_FATAL,true);
}

// Read register <reg> by building and executing an "out DWDR,<reg>" instruction via the CMD_SET_INSTR register
// This function is also used to check whether we have the right DWDR address.
byte DWreadRegister (byte reg, bool checkdwdr=false) {
  int response;
  byte res = 0;
  byte rdReg[] = {(byte)(0x64&ctx.tmask),                               // Set up for single step using loaded instruction
                  0xD2, outHigh(mcu.dwdr, reg), outLow(mcu.dwdr, reg)}; // Build "out DWDR, reg" instruction
  measureRam();
  DWflushInput();
  dw.sendCmd(rdReg,  sizeof(rdReg));
  blockIRQ();
  dw.sendCmd(0x23, true);                                            // Go
  response = getResponse(&res,1);
  unblockIRQ();
  if (checkdwdr) 
    return (response == 1);
  else
    if (response != 1) reportFatalError(DW_READREG_FATAL,true);
  return res;
}

// Write one byte to SRAM address space using an SRAM-based value for <addr>, not an I/O address
void DWwriteSramByte (unsigned int addr, byte val) {
  byte wrSram[] = {(byte)(0x66&ctx.tmask),                           // Set up for read/write 
                   0xD0, mcu.stuckat1byte, 0x1E,                      // Set Start Reg number (r30)
                   0xD1, mcu.stuckat1byte, 0x20,                      // Set End Reg number (r31) + 1
                   0xC2, 0x05,                                        // Set repeating copy to registers via DWDR
                   0x20,                                              // Go
		   (byte)(addr & 0xFF), (byte)(addr >> 8),            // r31:r30 (Z) = addr
                   0xD0, mcu.stuckat1byte, 0x01,
                   0xD1, mcu.stuckat1byte, 0x03,
                   0xC2, 0x04,                                        // Set simulated "in r?,DWDR; st Z+,r?" instructions
                   0x20,                                              // Go
                   val};
  measureRam();
  DWflushInput();
  dw.sendCmd(wrSram, sizeof(wrSram));
}

// Write one byte to IO register (via R0)
void DWwriteIOreg (byte ioreg, byte val)
{
  byte wrIOreg[] = {(byte)(0x64&ctx.tmask),                                // Set up for single step using loaded instruction
		    0xD2, inHigh(mcu.dwdr, 0), inLow(mcu.dwdr, 0), 0x23,    // Build "in reg,DWDR" instruction
		    val,                                                // load val into r0
		    0xD2, outHigh(ioreg, 0), outLow(ioreg, 0),          // now store from r0 into ioreg
		    0x23};
  measureRam();
  DWflushInput();
  dw.sendCmd(wrIOreg, sizeof(wrIOreg));
}

// Read one byte from SRAM address space using an SRAM-based value for <addr>, not an I/O address
byte DWreadSramByte (unsigned int addr) {
  byte res = 0;

#if 1
  DWreadSramBytes(addr, &res, 1);
#else
  unsigned int response;
  byte rdSram[] = {(byte)(0x66&ctx.tmask),                            // Set up for read/write 
                   0xD0, mcu.stuckat1byte, 0x1E,                      // Set Start Reg number (r30)
                   0xD1, mcu.stuckat1byte, 0x20,                      // Set End Reg number (r31) + 1
                   0xC2, 0x05,                                        // Set repeating copy to registers via DWDR
                   0x20,                                              // Go
                   (byte)(addr & 0xFF), (byte)(addr >> 8),            // r31:r30 (Z) = addr
                   0xD0, mcu.stuckat1byte, 0x00,                                  // 
                   0xD1, mcu.stuckat1byte, 0x02,                                  // 
                   0xC2, 0x00};                                        // Set simulated "ld r?,Z+; out DWDR,r?" instructions
  measureRam();
  DWflushInput();
  dw.sendCmd(rdSram, sizeof(rdSram));
  blockIRQ();
  dw.sendCmd(0x20,true);                                              // Go
  response = getResponse(&res,1);
  unblockIRQ();
  if (response != 1) reportFatalError(SRAM_READ_FATAL,true);
#endif
  return res;
}

// Read one byte from IO register (via R0)
byte DWreadIOreg (byte ioreg)
{
  unsigned int response; 
  byte res = 0;
  byte rdIOreg[] = {(byte)(0x64&ctx.tmask),                            // Set up for single step using loaded instruction
		    0xD2, inHigh(ioreg, 0), inLow(ioreg, 0),           // Build "out DWDR, reg" instruction
		    0x23,
		    0xD2, outHigh(mcu.dwdr, 0), outLow(mcu.dwdr, 0)};  // Build "out DWDR, 0" instruction
  measureRam();
  DWflushInput();
  dw.sendCmd(rdIOreg, sizeof(rdIOreg));
  blockIRQ();
  dw.sendCmd(0x23, true);                                              // Go
  response = getResponse(&res,1);
  unblockIRQ();
  if (response != 1) reportFatalError(DW_READIOREG_FATAL,true);
  return res;
}

// Read <len> bytes from SRAM address space into buf[] using an SRAM-based value for <addr>, not an I/O address
// Note: can't read addresses that correspond to  r28-31 (Y & Z Regs) because Z is used for transfer (not sure why Y is clobbered) 
void DWreadSramBytes (unsigned int addr, byte *mem, byte len) {
  unsigned int len2 = len * 2;
  unsigned int rsp;
  byte rdSram[] = {(byte)(0x66&ctx.tmask),                          // Set up for read/write using 
		   0xD0, mcu.stuckat1byte, 0x1E,                    // Set Start Reg number (r30)
		   0xD1, mcu.stuckat1byte, 0x20,                    // Set End Reg number (r31) + 1
		   0xC2, 0x05,                                      // Set repeating copy to registers via DWDR
		   0x20,                                            // Go
		   (byte)(addr & 0xFF), (byte)(addr >> 8),          // r31:r30 (Z) = addr
		   0xD0, mcu.stuckat1byte, 0x00,                    
		   0xD1, (byte)((len2 >> 8)+mcu.stuckat1byte), (byte)(len2 & 0xFF),  // Set repeat count = len * 2
		   0xC2, 0x00};                                     // Set simulated "ld r?,Z+; out DWDR,r?" instructions
  measureRam();
  
  DWflushInput();
  dw.sendCmd(rdSram, sizeof(rdSram));
  blockIRQ();
  dw.sendCmd(0x20, true);                                            // Go
  rsp = getResponse(mem, len);
  unblockIRQ();
  if (rsp != len) reportFatalError(SRAM_READ_FATAL,true);
}

//   Read one byte from EEPROM
byte DWreadEepromByte (unsigned int addr) {
  unsigned int response;
  byte retval;
  byte setRegs[] = {(byte)(0x66&ctx.tmask),                                      // Set up for read/write 
                    0xD0, mcu.stuckat1byte, 0x1C,                                // Set Start Reg number (r28)
                    0xD1, mcu.stuckat1byte, 0x20,                                // Set End Reg number (r31) + 1
                    0xC2, 0x05,                                                  // Set repeating copy to registers via DWDR
                    0x20,                                                        // Go
                    0x01, 0x01, (byte)(addr & 0xFF), (byte)(addr >> 8)};         // Data written into registers r28-r31
  byte doReadH[] = {0xD2, outHigh(mcu.eearh, 31), outLow(mcu.eearh, 31), 0x23};  // out EEARH,r31  EEARH = ah  EEPROM Address MSB
  byte doRead[]  = {0xD2, outHigh(mcu.eearl, 30), outLow(mcu.eearl, 30), 0x23,   // out EEARL,r30  EEARL = al  EEPROM Address LSB
                    0xD2, outHigh(mcu.eecr, 28), outLow(mcu.eecr, 28), 0x23,     // out EECR,r28   EERE = 01 (EEPROM Read Enable)
                    0xD2, inHigh(mcu.eedr, 29), inLow(mcu.eedr, 29), 0x23,       // in  r29,EEDR   Read data from EEDR
                    0xD2, outHigh(mcu.dwdr, 29), outLow(mcu.dwdr, 29)};          // out DWDR,r29   Send data back via DWDR reg
  measureRam();
  
  DWflushInput();
  dw.sendCmd(setRegs, sizeof(setRegs));
  blockIRQ();
  dw.sendCmd((byte)(0x64&ctx.tmask));                                   // Set up for single step using loaded instruction
  if (mcu.eearh)                                                        // if there is a high byte EEAR reg, set it
    dw.sendCmd(doReadH, sizeof(doReadH));
  dw.sendCmd(doRead, sizeof(doRead));                                   // set rest of control regs and query
  dw.sendCmd(0x23, true);                                               // Go
  response = getResponse(&retval,1);
  unblockIRQ();
  if (response != 1) reportFatalError(EEPROM_READ_FATAL,true);
  return retval;
}

//   Write one byte to EEPROM
void DWwriteEepromByte (unsigned int addr, byte val) {
  byte setRegs[] = {(byte)(0x66&ctx.tmask),                                       // Set up for read/write 
                    0xD0, mcu.stuckat1byte, 0x1C,                                 // Set Start Reg number (r30)
                    0xD1, mcu.stuckat1byte, 0x20,                                 // Set End Reg number (r31) + 1
                    0xC2, 0x05,                                                   // Set repeating copy to registers via DWDR
                    0x20,                                                         // Go
                    0x04, 0x02, (byte)(addr & 0xFF), (byte)(addr >> 8)};          // Data written into registers r28-r31
  byte doWriteH[] ={0xD2, outHigh(mcu.eearh, 31), outLow(mcu.eearh, 31), 0x23};   // out EEARH,r31  EEARH = ah  EEPROM Address MSB
  byte doWrite[] = {0xD2, outHigh(mcu.eearl, 30), outLow(mcu.eearl, 30), 0x23,    // out EEARL,r30  EEARL = al  EEPROM Address LSB
                    0xD2, inHigh(mcu.dwdr, 30), inLow(mcu.dwdr, 30), 0x23,        // in  r30,DWDR   Get data to write via DWDR
                    val,                                                          // Data written to EEPROM location
                    0xD2, outHigh(mcu.eedr, 30), outLow(mcu.eedr, 30), 0x23,      // out EEDR,r30   EEDR = data
                    0xD2, outHigh(mcu.eecr, 28), outLow(mcu.eecr, 28), 0x23,      // out EECR,r28   EECR = 04 (EEPROM Master Program Enable)
                    0xD2, outHigh(mcu.eecr, 29), outLow(mcu.eecr, 29), 0x23};     // out EECR,r29   EECR = 02 (EEPROM Program Enable)
  measureRam();
  dw.sendCmd(setRegs, sizeof(setRegs));
  if (mcu.eearh)                                                                  // if there is a high byte EEAR reg, set it
    dw.sendCmd(doWriteH, sizeof(doWriteH));
  dw.sendCmd(doWrite, sizeof(doWrite));
  _delay_ms(5);                                                                   // allow EEPROM write to complete
}


//  Read len bytes from flash memory area at <addr> into mem buffer
void DWreadFlash(unsigned int addr, byte *mem, unsigned int len) {
  unsigned int rsp;
  unsigned int lenx2 = len * 2;
  byte rdFlash[] = {(byte)(0x66&ctx.tmask),                             // Set up for read/write
		    0xD0, mcu.stuckat1byte, 0x1E,                       // Set Start Reg number (r30)
		    0xD1, mcu.stuckat1byte, 0x20,                       // Set End Reg number (r31) + 1
		    0xC2, 0x05,                                         // Set repeating copy to registers via DWDR
		    0x20,                                               // Go
		    (byte)(addr & 0xFF), (byte)(addr >> 8),             // r31:r30 (Z) = addr
		    0xD0, mcu.stuckat1byte, 0x00,                       // Set start = 0
		    0xD1, (byte)((lenx2 >> 8)+mcu.stuckat1byte),(byte)(lenx2),// Set end = repeat count = sizeof(flashBuf) * 2
		    0xC2, 0x02};                                        // Set simulated "lpm r?,Z+; out DWDR,r?" instructions
  measureRam();
  DWflushInput();
  dw.sendCmd(rdFlash, sizeof(rdFlash));
  blockIRQ();
  dw.sendCmd(0x20, true);                                               // Go
  rsp = getResponse(mem, len);                                          // Read len bytes
  unblockIRQ();
  if (rsp != len) reportFatalError(FLASH_READ_FATAL,true);
}

// erase entire flash page
void DWeraseFlashPage(unsigned int addr) {
  byte timeout = 0;
  byte eflash[] = { (byte)(0x64&ctx.tmask), // single stepping
		    0xD2, // load into instr reg
		    outHigh(0x37, 29), // Build "out SPMCSR, r29"
		    outLow(0x37, 29), 
		    0x23,  // execute
		    0xD2, 0x95 , 0xE8 }; // execute SPM
  measureRam();
  //DEBPR(F("Erase: "));  DEBLNF(addr,HEX);
  
  while (timeout < TIMEOUTMAX) {
    DWflushInput();
    DWwriteRegister(30, addr & 0xFF); // load Z reg with addr low
    DWwriteRegister(31, addr >> 8  ); // load Z reg with addr high
    DWwriteRegister(29, 0x03); // PGERS value for SPMCSR
    if (mcu.bootaddr) DWsetWPc(mcu.bootaddr); // so that access of all of flash is possible
    dw.sendCmd(eflash, sizeof(eflash));
    dw.sendCmd(0x33, true);
    if (expectBreakAndU()) break;
    _delay_us(1600);
    timeout++;
    timeoutcnt++;
  }
  if (timeout >= TIMEOUTMAX) reportFatalError(FLASH_ERASE_FATAL,true);
}
		    
// now move the page from temp memory to flash
void DWprogramFlashPage(unsigned int addr)
{
  boolean succ;
  byte timeout = 0;
  unsigned int wait = 10000;
  byte eprog[] = { (byte)(0x64&ctx.tmask), // single stepping
		   0xD2, // load into instr reg
		   outHigh(0x37, 29), // Build "out SPMCSR, r29"
		   outLow(0x37, 29), 
		   0x23,  // execute
		   0xD2, 0x95 , 0xE8}; // execute SPM

  //DEBLN(F("Program flash page ..."));
  measureRam();
  flashcnt++;
  while (timeout < TIMEOUTMAX) {
    wait = 1000;
    DWflushInput();
    DWwriteRegister(30, addr & 0xFF); // load Z reg with addr low
    DWwriteRegister(31, addr >> 8  ); // load Z reg with addr high
    DWwriteRegister(29, 0x05); //  PGWRT value for SPMCSR
    if (mcu.bootaddr) DWsetWPc(mcu.bootaddr); // so that access of all of flash is possible
    dw.sendCmd(eprog, sizeof(eprog));
    
    dw.sendCmd(0x33, true);
    succ = expectBreakAndU(); // wait for feedback
  
    if (mcu.bootaddr && succ) { // no bootloader
      _delay_us(100);
      while ((DWreadSPMCSR() & 0x1F) != 0 && --wait != 0) { 
	_delay_us(10);
	//DEBPR("."); // wait
      }
      succ = (wait != 0);
    }
    if (succ) break;
    _delay_ms(1);
    timeout++;
    timeoutcnt++;
  }
  if (timeout >= TIMEOUTMAX) reportFatalError(FLASH_PROGRAM_FATAL,true);
  //DEBLN(F("...done"));
}

// load bytes into temp memory
void DWloadFlashPageBuffer(unsigned int addr, byte *mem)
{
  byte eload[] = { (byte)(0x64&ctx.tmask), 0xD2,
		   outHigh(0x37, 29),       // Build "out SPMCSR, r29"
		   outLow(0x37, 29),
		   0x23,                    // execute
		   0xD2, 0x95, 0xE8, 0x23, // spm
		   0xD2, 0x96, 0x32, 0x23, // addiw Z,2
  };

  //DEBLN(F("Load flash page ..."));
  measureRam();

  DWflushInput();
  DWwriteRegister(30, addr & 0xFF); // load Z reg with addr low
  DWwriteRegister(31, addr >> 8  ); // load Z reg with addr high
  DWwriteRegister(29, 0x01); //  SPMEN value for SPMCSR
  byte ix = 0;
  while (ix < mcu.pagesz) {
    DWwriteRegister(0, mem[ix++]);               // load next word
    DWwriteRegister(1, mem[ix++]);
    if (mcu.bootaddr) DWsetWPc(mcu.bootaddr);
    dw.sendCmd(eload, sizeof(eload));
  }
  //DEBLN(F("...done"));
}

void DWreenableRWW(void)
{
  unsigned int wait = 10000;
  byte errw[] = { (byte)(0x64&ctx.tmask), 0xD2,
		  outHigh(0x37, 29),       // Build "out SPMCSR, r29"
		  outLow(0x37, 29),
		  0x23,                    // execute
		  0xD2, 0x95, 0xE8, 0x23 }; // spm

  measureRam();
  if (mcu.bootaddr) {
    // DEBLN(F("DWreenableRWW"));
    while ((DWreadSPMCSR() & 0x01) != 0 && --wait) { 
      _delay_us(10);
      //DEBPR("."); // wait
    }
    if (wait == 0) {
      reportFatalError(REENABLERWW_FATAL, true);
      return;
    }
    DWsetWPc(mcu.bootaddr);
    DWwriteRegister(29, 0x11); //  RWWSRE value for SPMCSR
    dw.sendCmd(errw, sizeof(errw));
  }
}

byte DWreadSPMCSR(void)
{
  byte sc[] = { (byte)(0x64&ctx.tmask), 0xD2,        // setup for single step and load instr reg 
		inHigh(0x37, 30),  // build "in 30, SPMCSR"
		inLow(0x37, 30),
		0x23 };             // execute
  measureRam();
  DWflushInput();
  dw.sendCmd(sc, sizeof(sc));
  return DWreadRegister(30);
}

unsigned int DWgetWPc (boolean corrected) {
  unsigned int pc = getWordResponse(0xF0);
  // DEBPR(F("Get uncorrected WPC=")); DEBLNF(pc,HEX);
  if (corrected) {
    pc &= ~(mcu.stuckat1byte<<8);
    pc--;
    // DEBPR(F("Corrected WPC=")); DEBLNF(pc,HEX);
  }
  return (pc);
}


// get chip signature
unsigned int DWgetChipId () {
  unsigned int result;
  result = getWordResponse(0xF3);
  return result;
}

// set PC Reg (word address) - that is set all the bits (including the ones in the stuckat1byte)
void DWsetWPc (unsigned int wpcreg) {
  //DEBPR(F("Set WPCReg=")); DEBLNF(wpcreg,HEX);
  byte cmd[] = {0xD0, (byte)((wpcreg >> 8)+mcu.stuckat1byte), (byte)(wpcreg & 0xFF)};
  dw.sendCmd(cmd, sizeof(cmd));
}

// set hardware breakpoint at word address
void DWsetWBp (unsigned int wbp) {
  byte cmd[] = {0xD1, (byte)((wbp >> 8)+mcu.stuckat1byte), (byte)(wbp & 0xFF)};
  dw.sendCmd(cmd, sizeof(cmd));
}

// execute an instruction offline (can be 2-byte or 4-byte)
// if 4-byte, the trailing 2 bytes are taken from flash!!!
void DWexecOffline(unsigned int opcode)
{
  byte cmd[] = {0xD2, (byte) (opcode >> 8), (byte) (opcode&0xFF), 0x23};
  measureRam();

  //DEBPR(F("Offline exec: "));
  //DEBLNF(opcode,HEX);
  dw.sendCmd(cmd, sizeof(cmd));
}

byte DWflushInput(void)
{
  byte c = 0;
  while (dw.available()) {
    // DEBPR("@");
    c = dw.read();
    // DEBLN(c);
  }
  //  _delay_us(1);
  return c;
}

/***************************** a little bit of SPI programming ********/


void enableSpiPins (void) {
  //DEBLN(F("Enable SPI ..."));
  if (ctx.levelshifting) {
    pinMode(TISP, OUTPUT);
    digitalWrite(TISP, LOW); // enable pull-ups
    _delay_us(50);
  }
  pinMode(DWLINE, OUTPUT);
  digitalWrite(DWLINE, LOW);
  //DEBLN(F("RESET low"));
  _delay_us(50);
  if (ctx.levelshifting) {
    pinMode(TODSCK, OUTPUT); // draws SCK low
    digitalWrite(TODSCK, LOW);
    pinMode(TMOSI, INPUT);  // MOSI is HIGH
    digitalWrite(TMOSI, LOW); 
    _delay_us(10);
  } else {
    pinMode(TSCK, OUTPUT);
    digitalWrite(TSCK, LOW);
    pinMode(TMOSI, OUTPUT);
    digitalWrite(TMOSI, HIGH);
  }
  pinMode(TMISO, INPUT);
}

void disableSpiPins (void) {
  if (ctx.levelshifting) {
    pinMode(TODSCK, INPUT); // disconnect TSCK (always LOW!)
  } else {
    pinMode(TSCK, INPUT); // disconnect TSCK = High state
    digitalWrite(TSCK, LOW); // make sure that internal pullups are off
  }
  pinMode(TMOSI, INPUT); // disconnect TMOSI = High state
  digitalWrite(TMOSI, LOW); // make sure that internal pullups are off
  pinMode(TISP, INPUT); // release TISP = switch off external pullups (now completely unconnected)
  pinMode(TMISO, INPUT); // should be input in any case
}

byte ispTransfer (byte val, boolean slower) {
  measureRam();
  for (byte ii = 0; ii < 8; ++ii) {
    if (ctx.levelshifting) {
      // pinMode(TMOSI,  (val & 0x80) ? INPUT : OUTPUT);
      if (val & 0x80) *tmosimode &= ~tmosimask; else  *tmosimode |= tmosimask;
      // pinMode(TODSCK, INPUT);
      *todsckmode &= ~todsckmask;
    } else {
      // digitalWrite(TMOSI, (val & 0x80) ? HIGH : LOW);
      if (val & 0x80) *tmosiout |= tmosimask; else  *tmosiout &= ~tmosimask;
      // digitalWrite(TSCK, HIGH);
      *tsckout |= tsckmask;
    }
    ispDelay(slower);
    // val = (val << 1) + digitalRead(TMISO);
    val = (val << 1) + (*tmisoin & tmisomask ? 1 : 0);
    if (ctx.levelshifting) {
      // pinMode(TODSCK, OUTPUT);
      *todsckmode |= todsckmask;
    } else {
      // digitalWrite(TSCK, LOW);
      *tsckout &= ~tsckmask;
    }
    ispDelay(slower);
  }
  return val;
}

inline void ispDelay(boolean slower) {
  if (slower) {
    if (ctx.ispspeed == NORMAL_ISP) _delay_us(0.5); // meaning 7 us = 140 kHz (OK for 1 MHz clock)
    else if (ctx.ispspeed == SLOW_ISP) _delay_us(20); // meaning 50 us == 20 kHz (OK for 128 kHz clock)
    else _delay_us(700); // meaning 1400 us == 714 Hz (OK for 4 kHz clock)
  } else {
    if (ctx.ispspeed != NORMAL_ISP) {
      if (ctx.ispspeed == SLOW_ISP) _delay_us(15); 
      else _delay_us(600); // 
    }
  }
}
  

byte ispSend (byte c1, byte c2, byte c3, byte c4, boolean last) {
  byte res;
  ispTransfer(c1, last);
  ispTransfer(c2, last);
  res = ispTransfer(c3, last);
  if (last)
    res = ispTransfer(c4, last);
  else
    ispTransfer(c4, last);
  return res;
}


boolean enterProgramMode (void)
{
  byte timeout = 6;

  //DEBLN(F("Entering progmode"));
  ctx.ispspeed = NORMAL_ISP;
  do {
    if (timeout < 5) ctx.ispspeed = SLOW_ISP;
    if (timeout < 3) ctx.ispspeed = SUPER_SLOW_ISP;
    //DEBLN(F("Do ..."));
    enableSpiPins();
    //DEBLN(F("Pins enabled ..."));
    pinMode(DWLINE, INPUT); 
    _delay_us(600);             // short positive RESET pulse of at least 2 clock cycles (enough for 4 kHz)
    pinMode(DWLINE, OUTPUT);  
    _delay_ms(25);            // wait at least 20 ms before sending enable sequence
    if (ispSend(0xAC, 0x53, 0x00, 0x00, false) == 0x53) break;
  } while (--timeout);
  if (timeout == 0) {
    leaveProgramMode();
    //DEBLN(F("... not successful"));
    return false;
  } else {
    //DEBLN(F("... successful"));
    _delay_ms(15);            // wait after enable programming - avrdude does that!
    return true;
  }
}

void leaveProgramMode(void)
{
  //DEBLN(F("Leaving progmode"));
  disableSpiPins();
  _delay_ms(10);
  pinMode(DWLINE, INPUT); // allow MCU to run or to communicate via debugWIRE
}
  

// identify chip
unsigned int ispGetChipId (void)
{
  unsigned int id;
  if (ispSend(0x30, 0x00, 0x00, 0x00, true) != 0x1E) return 0;
  id = ispSend(0x30, 0x00, 0x01, 0x00, true) << 8;
  id |= ispSend(0x30, 0x00, 0x02, 0x00, true);
  //DEBPR(F("ISP SIG:   "));
  //DEBLNF(id,HEX);
  return id;
}

// read high or low fuse
byte ispReadFuse(boolean high)
{
  if (high)
    return ispSend(0x58, 0x08, 0x00, 0x00, true);
  else
    return ispSend(0x50, 0x00, 0x00, 0x00, true);
}

// program fuse and/or high fuse
boolean ispProgramFuse(FuseByte fuse, byte fusemsk, byte fuseval)
{
  byte lowfuse, highfuse, extfuse;
  boolean succ = true;

  lowfuse = ispSend(0x50, 0x00, 0x00, 0x00, true);
  highfuse = ispSend(0x58, 0x08, 0x00, 0x00, true);
  extfuse = ispSend(0x50, 0x08, 0x00, 0x00, true);

  switch (fuse) {
  case LowFuse: lowfuse = (lowfuse & ~fusemsk) | (fuseval & fusemsk); break;
  case HighFuse: highfuse = (highfuse & ~fusemsk) | (fuseval & fusemsk); break;
  case ExtendedFuse: extfuse = (extfuse & ~fusemsk) | (fuseval & fusemsk); break;
  }

  ispSend(0xAC, 0xA0, 0x00, lowfuse, true);
  _delay_ms(15);
  succ &= (ispSend(0x50, 0x00, 0x00, 0x00, true) == lowfuse);

  ispSend(0xAC, 0xA4, 0x00, extfuse, true);
  _delay_ms(15);
  succ &= (ispSend(0x50, 0x08, 0x00, 0x00, true) == extfuse);

  ispSend(0xAC, 0xA8, 0x00, highfuse, true);
  _delay_ms(15);
  succ &= (ispSend(0x58, 0x08, 0x00, 0x00, true) == highfuse);

  return succ;
}

boolean ispEraseFlash(void)
{
  ispSend(0xAC, 0x80, 0x00, 0x00, true);
  _delay_ms(20);
  pinMode(DWLINE, INPUT); // short positive pulse
  _delay_ms(1);
  pinMode(DWLINE, OUTPUT); 
  return true;
}

boolean ispLocked()
{
  return (ispSend(0x58, 0x00, 0x00, 0x00, true) != 0xFF);
}


boolean setMcuAttr(unsigned int id)
{
  int ix = 0;
  unsigned int sig;
  measureRam();

  while ((sig = pgm_read_word(&mcu_info[ix].sig))) {
    if (sig == id) { // found the right mcu type
      //DEBLN(F("MCU struct:"));
      mcu.sig = sig;
      mcu.ramsz = pgm_read_byte(&mcu_info[ix].ramsz_div_64)*64;
      mcu.rambase = (pgm_read_byte(&mcu_info[ix].rambase_low) ? 0x60 : 0x100);
      mcu.eepromsz = pgm_read_byte(&mcu_info[ix].eepromsz_div_64)*64;
      mcu.flashsz = pgm_read_byte(&mcu_info[ix].flashsz_div_1k)*1024;
      mcu.dwdr = pgm_read_byte(&mcu_info[ix].dwdr);
      mcu.pagesz = pgm_read_byte(&mcu_info[ix].pagesz_div_2)*2;
      mcu.erase4pg = pgm_read_byte(&mcu_info[ix].erase4pg);
      mcu.bootaddr = pgm_read_word(&mcu_info[ix].bootaddr);
      mcu.eecr =  pgm_read_byte(&mcu_info[ix].eecr);
      mcu.eearh =  pgm_read_byte(&mcu_info[ix].eearh);
      mcu.avreplus = pgm_read_byte(&mcu_info[ix].avreplus);
      mcu.name = (const char *)pgm_read_word(&mcu_info[ix].name);
      mcu.mask = (const byte *)pgm_read_word(&mcu_info[ix].mask);
      // the remaining fields will be derived 
      mcu.eearl = mcu.eecr + 2;
      mcu.eedr = mcu.eecr + 1;
      // we treat the 4-page erase MCU as if pages were larger by a factor of 4!
      if (mcu.erase4pg) mcu.targetpgsz = mcu.pagesz*4; 
      else mcu.targetpgsz = mcu.pagesz;
      // dwen, chmsk, ckdiv8 are identical for almost all MCUs, so we treat the exceptions here
      mcu.dwenfuse = 0x40;
      if (mcu.name == attiny13) {
	mcu.dwenfuse = 0x08;
      } else if (mcu.name == attiny2313 || mcu.name == attiny4313) {
	mcu.dwenfuse = 0x80;
      } 
#if 0
      DEBPR(F("sig=")); DEBLNF(mcu.sig,HEX);
      DEBPR(F("aep="));  DEBLN(mcu.avreplus);
      DEBPR(F("ram=")); DEBLN(mcu.ramsz);
      DEBPR(F("bas=0x")); DEBLNF(mcu.rambase,HEX);
      DEBPR(F("eep=")); DEBLN(mcu.eepromsz);
      DEBPR(F("fla="));  DEBLN(mcu.flashsz);
      DEBPR(F("dwd=0x")); DEBLNF(mcu.dwdr,HEX);
      DEBPR(F("pgs=")); DEBLN(mcu.pagesz);
      DEBPR(F("e4p=")); DEBLN(mcu.erase4pg);
      DEBPR(F("boo=0x")); DEBLNF(mcu.bootaddr,HEX);
      DEBPR(F("eec=0x")); DEBLNF(mcu.eecr,HEX);
      DEBPR(F("eea=0x")); DEBLNF(mcu.eearh,HEX);
      strcpy_P(buf,mcu.name);
      DEBPR(F("nam=")); DEBLN((char*)buf);
      DEBPR(F("ear=0x")); DEBLNF(mcu.eearl,HEX);
      DEBPR(F("eed=0x")); DEBLNF(mcu.eedr,HEX);
      DEBPR(F("tps=")); DEBLN(mcu.targetpgsz);
      DEBPR(F("dwe=0x")); DEBLNF(mcu.dwenfuse,HEX);
#endif
      return true;
    }
    ix++;
  }
  // DEBPR(F("Could not determine MCU type with SIG: ")); DEBLNF(id, HEX);
  return false;
}


/***************************** some conversion routines **************/


// convert 4 bit value to hex character
char nib2hex(byte b)
{
  measureRam();

  return((b) > 9 ? 'a' - 10 + b: '0' + b);
}

// convert hex character to 4 bit value
byte hex2nib(char hex)
{
  measureRam();

 hex = toupper(hex);
  return(hex >= '0' && hex <= '9' ? hex - '0' :
	 (hex >= 'A' && hex <= 'F' ? hex - 'A' + 10 : 0xFF));
}

// parse 4 character sequence into 4 byte hex value until no more hex numbers
byte parseHex(const byte *buff, unsigned long *hex)
{
  byte nib, len;
  measureRam();

  for (*hex = 0, len = 0; (nib = hex2nib(buff[len])) != 0xFF; ++len)
    *hex = (*hex << 4) + nib;
  return len;
}

// convert number into a string, reading the number backwards
void convNum(byte numbuf[10], long num)
{
  int i = 0;
  if (num == 0) numbuf[i++] = '0';
  while (num > 0) {
    numbuf[i++] = (num%10) + '0';
    num = num / 10;
  }
  numbuf[i] = '\0';
}

// convert two ASCII characters representing a hex number into an ASCII char
char convHex2Ascii(char char1, char char2)
{
  return (hex2nib(char1) << 4) | hex2nib(char2);
}

// convert a buffer with a string of hex numbers into a string in place
void convBufferHex2Ascii(char *outbuf, const byte *buf, int maxlen)
{
  int i, clen = strlen((const char *)buf);

  for (i=0; i<min(clen/2,maxlen-1); i++) 
    outbuf[i] = convHex2Ascii(buf[i*2], buf[i*2+1]);
  outbuf[min(clen/2,maxlen-1)] = '\0';
}


void measureRam(void)
{
#if FREERAM 
  int f = freeRam();
  // DEBPR(F("RAM: ")); DEBLN(f);
  freeram = min(f,freeram);
#endif
}

int freeRam(void)
{
  extern unsigned int __heap_start;
  extern void *__brkval;

  int free_memory;
  int stack_here;

  if (__brkval == 0)
    free_memory = (int) &stack_here - (int) &__heap_start;
  else
    free_memory = (int) &stack_here - (int) __brkval; 
  return (free_memory);
}


/*****************************************************************************************************/
/******************************************** Unit tests *********************************************/
/*****************************************************************************************************/



/********************************* General setup and reporting functions ******************************/

// run all unit tests
void alltests(void)
{
  int failed = 0;
  int testnum = 1;

  if (targetOffline()) {
    gdbSendReply("E00");
    return;
  }
#if UNITDW
  failed += DWtests(testnum);
#endif
#if UNITTG
  failed += targetTests(testnum);
#endif
#if UNITGDB
  failed += gdbTests(testnum);
#endif
  testSummary(failed);
}

// give a summary of the test batch
void testSummary(int failed)
{
  if (failed) {
    gdbDebugMessagePSTR(PSTR("\n... some live tests failed:"), failed);
  } else {
    gdbDebugMessagePSTR(PSTR("\n...live tests successfully finished"), -1);
  }
  gdbSendReply("OK");
}

int testResult(boolean succ)
{
  if (succ) {
    gdbDebugMessagePSTR(PSTR("  -> succeeded"), -1);
    return 0;
  } else {
    gdbDebugMessagePSTR(PSTR("  -> failed ***"), -1);
    return 1;
  }
}
 


/* Testcode - for checking execution related functions (to loaded into to target memory)
 bADDR  wADDR
 1aa:	d5: 00 00       	nop
 1ac:	d6: 00 00       	nop
 1ae:	d7: 00 00       	nop
 1b0:	d8: ff cf       	rjmp	.-2      	; 0x1b0 <DL>
 1b2:	d9: 29 e4       	ldi	r18, 0x49	; 73
 1b4:	da: 20 93 00 01 	sts	0x0100, r18	; 0x800100 <goal>
 1b8:	dc: 00 91 00 01 	lds	r16, 0x0100	; 0x800100 <goal>
 1bc:	de: 05 d0       	rcall	.+10     	; 0x1c8 <SUBR>
 1be:	df: f5 cf       	rjmp	.-22     	; 0x1aa <START>
 1c0:	e0: 0e 94 e4 00 	call	0x1c8	        ; 0x1c8 <SUBR>
 1c4:	e2: 0c 94 d5 00 	jmp	0x1aa	        ; 0x1aa <START>
 1c8:	e4: 11 e9       	ldi	r17, 0x91	; 145
 1ca:	e5: 08 95       	ret 
 1cc:   e6: 01 00               ILLEGAL
*/

const byte testcode[] PROGMEM = {

  0x00, 0x00,    	
  0x00, 0x00,
  0x00, 0x00,
  0xff, 0xcf,       	
  0x29, 0xe4,       	
  0x20, 0x93, 0x00, 0x01,
  0x00, 0x91, 0x00, 0x01,
  0x05, 0xd0,       	
  0xf5, 0xcf,       	
  0x0e, 0x94, 0xe4, 0x00,
  0x0c, 0x94, 0xd5, 0x00,
  0x11, 0xe9,       	
  0x08, 0x95,
  0x01, 0x00 
};
 


void setupTestCode()
{
  // execution related functions: setup test code in target
 memcpy_P(membuf, testcode, sizeof(testcode));
 if (mcu.rambase == 0x60) { // small ATtinys with only little memory
   membuf[12] = 0x60; // use address 0x0060 instead of 0x0100
   membuf[13] = 0x00;
   membuf[16] = 0x60;
   membuf[17] = 0x00;
 }
 targetWriteFlash(0x1aa, membuf, sizeof(testcode));
 targetFlushFlashProg();
}

/********************************* GDB interface function specific tests ******************************/


int gdbTests(int &num) {
  int failed = 0;
  boolean succ;
  int testnum;
  unsigned int oldsp;

  if (targetOffline()) {
    if (num == 0) gdbSendReply("E00");
    return 0;
  }
  
  if (num >= 1) testnum = num;
  else testnum = 1;

  /* We do not test the I/O functions. They have been taken over from avr_gdb and
   * seem to work quite robustly. We only test the breakpoint and execution functions.
   * The tests build on each other, so do not rearrange.
   */
  
  setupTestCode(); // insert the test code into memory
  /* Testcode - for checking execution related functions (to loaded into to target memory)
     bADDR      wADDR
     1aa:	d5:   00 00       	nop
     1ac:	d6:   00 00       	nop
     1ae:	d7:   00 00       	nop
     1b0:	d8:   ff cf       	rjmp	.-2      	; 0x1b0 <DL>
     1b2:	d9:   29 e4       	ldi	r18, 0x49	; 73
     1b4:	da:   20 93 00 01 	sts	0x0100, r18	; 0x800100 <goal>
     1b8:	dc:   00 91 00 01 	lds	r16, 0x0100	; 0x800100 <goal>
     1bc:	de:   05 d0       	rcall	.+10     	; 0x1c8 <SUBR>
     1be:	df:   f5 cf       	rjmp	.-22     	; 0x1aa <START>
     1c0:	e0:   0e 94 e4 00 	call	0x1c8	        ; 0x1c8 <SUBR>
     1c4:	e2:   0c 94 d5 00 	jmp	0x1aa	        ; 0x1aa <START>
     1c8:	e4:   11 e9       	ldi	r17, 0x91	; 145
     1ca:	e5:   08 95       	ret 
     1cc:       e6:   01 00             ILLEGAL
  */

  // insert 4 BPs (one of it is a duplicate) 
  gdbDebugMessagePSTR(PSTR("gdbInsertBreakpoint: "), testnum++);
  gdbInsertBreakpoint(0xe4);
  gdbInsertBreakpoint(0xd5);
  gdbInsertBreakpoint(0xda);
  gdbInsertBreakpoint(0xd5);
  failed += testResult(bpcnt == 3 && bpused == 3 && hwbp == 0xda && bp[0].waddr == 0xe4
		       && bp[1].waddr == 0xd5 && bp[2].waddr == 0xda && bp[2].hw);

  // will insert two software breakpoints and the most recent one is a hardware breakpoint
  gdbDebugMessagePSTR(PSTR("gdbUpdateBreakpoints: "), testnum++);
  gdbUpdateBreakpoints(false);
  failed += testResult(bp[0].inflash && bp[0].used && bp[0].active  && bp[1].inflash
		       && bp[1].opcode == 0 && !bp[2].inflash && bp[0].opcode == 0xe911
		       && targetReadFlashWord(0xe4*2) == 0x9598
		       && targetReadFlashWord(0xd5*2) == 0x9598
		       && targetReadFlashWord(0xda*2) == 0x9320);

  // remove all breakpoints (the software breakpoints will still be in flash memory)
  gdbDebugMessagePSTR(PSTR("gdbRemoveBreakpoints: "), testnum++);
  gdbRemoveBreakpoint(0xd5);
  gdbRemoveBreakpoint(0xe4);
  gdbRemoveBreakpoint(0xda);
  gdbRemoveBreakpoint(0xd5);
  gdbRemoveBreakpoint(0xd5);
  failed += testResult(bpcnt == 0 && bpused == 3 && hwbp == 0xda && bp[0].inflash && bp[0].used
		       && !bp[0].active && bp[1].inflash && bp[1].used && !bp[1].active
		       && !bp[2].inflash && bp[2].used && !bp[2].active && bp[2].hw);

  // insert two new breakpoints: they will 'steal' the hardware breakpoint from the former breakpoint entry 2
  gdbDebugMessagePSTR(PSTR("Test gdbInsertBreakpoint (with old inactive BPs): "), testnum++);
  gdbInsertBreakpoint(0xe0); // CALL instruction
  gdbInsertBreakpoint(0xd6); // second NOP instruction, should become hwbp!
  failed += testResult(bpcnt == 2 && bpused == 5 && hwbp == 0xd6 && !bp[2].inflash && bp[2].used
		       && !bp[2].active && !bp[2].hw  && !bp[3].inflash && bp[3].used 
		       && bp[3].active && !bp[3].hw && bp[3].waddr == 0xe0
		       && !bp[4].inflash && bp[4].used 
		       && bp[4].active && bp[4].hw && bp[4].waddr == 0xd6);
  
  // "reinsert" two of the former breakpoints (in particular the former hardware breakpoint)
  // Then call gdbUpdateBreakpoints: the former hardware breakpoint now is a software breakpoint and therefore
  // a BREAK instruction is inserted at this point
  // All in all: 3 active BPs
  gdbDebugMessagePSTR(PSTR("gdbUpdateBreakpoint (after reinserting 2 of 3 inactive BPs): "), testnum++);
  gdbInsertBreakpoint(0xe4);
  gdbInsertBreakpoint(0xda);
  gdbUpdateBreakpoints(false);
  failed += testResult(bpcnt == 4 && bpused == 4 && hwbp == 0xd6 && bp[0].inflash
		       && bp[0].used && bp[0].active && bp[0].inflash && bp[0].waddr == 0xe4
		       && !bp[1].used && !bp[1].active &&  targetReadFlashWord(0xd5*2) == 0
		       && bp[2].inflash && bp[2].used && bp[2].active && !bp[2].hw
		          && bp[2].waddr == 0xda && targetReadFlashWord(0xda*2) == 0x9598
		       && bp[3].inflash && bp[3].used && bp[3].active && !bp[3].hw
		          && bp[3].waddr == 0xe0 && targetReadFlashWord(0xe0*2) == 0x9598
		       && !bp[4].inflash && bp[4].used && bp[4].active && bp[4].hw && bp[4].waddr == 0xd6 && hwbp == 0xd6);

  // execute starting at 0xd5 (word address) with a NOP and run to the hardware breakpoint (next instruction)
  gdbDebugMessagePSTR(PSTR("gdbContinue (with HWBP): "), testnum++);
  targetInitRegisters();
  ctx.sp = mcu.ramsz+mcu.rambase-1;
  ctx.wpc = 0xd5;
  gdbContinue();
  succ = expectBreakAndU();
  DEBLN(F("Execution did not stop"));
  if (!succ) {
    targetBreak();
    expectUCalibrate();
  }
  targetSaveRegisters();
  DEBPR(F("WPC = ")); DEBLNF(ctx.wpc,HEX);
  failed += testResult(succ && ctx.wpc == 0xd6);

  // execute starting at 0xdc (an RCALL instruction) and stop at the software breakpoint at 0xe4
  gdbDebugMessagePSTR(PSTR("gdbContinue (with sw BP): "), testnum++);
  ctx.wpc = 0xdc;
  oldsp = ctx.sp;
  gdbContinue();
  succ = expectBreakAndU();
  if (!succ) {
    DEBLN(F("Execution did not stop"));
    targetBreak();
    expectUCalibrate();
  }
  targetSaveRegisters();
  targetReadSram(ctx.sp+1,membuf,2); // return addr
  DEBPR(F("WPC = ")); DEBLNF(ctx.wpc,HEX);
  DEBPR(F("Return addr = ")); DEBLNF((membuf[0]<<8)+membuf[1], HEX);
  DEBPR(F("oldsp = ")); DEBLNF(oldsp,HEX);
  DEBPR(F("ctx.sp = ")); DEBLNF(ctx.sp,HEX);
  DEBPR(succ);DEBPR(ctx.wpc == 0xe4); DEBPR(ctx.sp == oldsp - 2); DEBPR(((membuf[0]<<8)+membuf[1]) == (0xDF+mcu.stuckat1byte));
  failed += testResult(succ && ctx.wpc == 0xe4 && ctx.sp == oldsp - 2
		       && ((membuf[0]<<8)+membuf[1]) == (0xDF+(mcu.stuckat1byte<<8)));
  
  // remove the first 3 breakpoints from being active (they are still marked as used and the BREAK
  // instruction is still in flash)
  gdbDebugMessagePSTR(PSTR("gdbRemoveBreakpoint (3): "), testnum++);
  gdbRemoveBreakpoint(0xe4);
  gdbRemoveBreakpoint(0xda);
  gdbRemoveBreakpoint(0xd6);
  failed += testResult(bpcnt == 1 && bpused == 4 && hwbp == 0xd6 && bp[0].used && !bp[0].active 
		       && !bp[1].used && bp[2].used && !bp[2].active && bp[3].used && bp[3].active
		       && bp[4].used && !bp[4].active);

  // perform  a single step at location 0xda at which a BREAK instruction has been inserted,
  // replacing the first word of a STS __,r18 instruction; execution happens using
  // simulation.
  gdbDebugMessagePSTR(PSTR("gdbStep on 4-byte instr. hidden by BREAK: "), testnum++);
  //DEBLN(F("Test simulated write:"));
  unsigned int sramaddr = (mcu.rambase == 0x60 ? 0x60 : 0x100);
  ctx.regs[18] = 0x42;
  ctx.wpc = 0xda;
  membuf[0] = 0xFF;
  targetWriteSram(sramaddr, membuf, 1);
  targetReadSram(sramaddr, membuf, 1);
  membuf[0] = 0;
  //DEBLNF(membuf[0],HEX);
  gdbStep();
  targetReadSram(sramaddr, membuf, 1);
  //DEBLNF(membuf[0],HEX);
  //DEBLNF(ctx.wpc,HEX);
  failed += testResult(membuf[0] == 0x42 && ctx.wpc == 0xdc);

  // perform a single stop at location 0xe5 at which a BREAK instruction has been inserted,
  // replacing a "ldi r17, 0x91" instruction
  // execution is done by simulation
  gdbDebugMessagePSTR(PSTR("gdbStep on 2-byte instr. hidden by BREAK: "), testnum++);
  ctx.regs[17] = 0xFF;
  ctx.wpc = 0xe4;
  gdbStep();
  //DEBLNF(ctx.regs[17],HEX);
  //DEBLNF(ctx.wpc,HEX);
  failed += testResult(ctx.wpc == 0xe5 && ctx.regs[17] == 0x91);

  // perform a single step at location 0xe5 on instruction RET
  gdbDebugMessagePSTR(PSTR("gdbStep on normal instr. (2-byte): "), testnum++);
  ctx.wpc = 0xe5;
  oldsp = ctx.sp;
  gdbStep();
  failed += testResult(ctx.sp == oldsp + 2 && ctx.wpc == 0xdf);

  // perform single step at location 0xdc on the instruction LDS r16, 0x100
  gdbDebugMessagePSTR(PSTR("gdbStep on normal instr. (4-byte): "), testnum++);
  ctx.regs[16] = 0;
  ctx.wpc = 0xdc;
  gdbStep();
  failed += testResult(ctx.regs[16] == 0x42 && ctx.wpc == 0xde);

  // check the "BREAK hiding" feature by loading part of the flash memory and
  // replacing BREAKs with the original instructions in the buffer to be sent to gdb
  gdbDebugMessagePSTR(PSTR("gdbHideBREAKs: "), testnum++);
  targetReadFlash(0x1ad, newpage, 0x1C); // from 0x1ad (uneven) to 0x1e4 (even)
  succ = (newpage[0x1ad-0x1ad] == 0x00 && newpage[0x1b4-0x1ad] == 0x98
	  && newpage[0x1b4-0x1ad+1] == 0x95 && newpage[0x1c8-0x1ad] == 0x98);
  //DEBLNF(newpage[0x1ad-0x1ad],HEX);
  //DEBLNF(newpage[0x1b4-0x1ad],HEX);
  //DEBLNF(newpage[0x1b4-0x1ad+1],HEX);
  //DEBLNF(newpage[0x1c8-0x1ad],HEX);
  //DEBLN();
  gdbHideBREAKs(0x1ad, newpage, 0x1C);
  //DEBLNF(newpage[0x1ad-0x1ad],HEX);
  //DEBLNF(newpage[0x1b4-0x1ad],HEX);
  //DEBLNF(newpage[0x1b4-0x1ad+1],HEX);
  //DEBLNF(newpage[0x1c8-0x1ad],HEX);
  //DEBLN();
  failed += testResult(succ && newpage[0x1ad-0x1ad] == 0x00 && newpage[0x1b4-0x1ad] == 0x20
		       && newpage[0x1b4-0x1ad+1] == 0x93 && newpage[0x1c8-0x1ad] == 0x11);

  // cleanup
  gdbDebugMessagePSTR(PSTR("Delete BPs & BP update: "), testnum++);
  gdbRemoveBreakpoint(0xe0);
  gdbUpdateBreakpoints(false);
  failed += testResult(bpcnt == 0 && bpused == 0 && hwbp == 0xFFFF);

  // test the illegal opcode detector for single step
  gdbDebugMessagePSTR(PSTR("gdbStep on illegal instruction: "), testnum++);
  ctx.wpc = 0xe6;
  byte sig = gdbStep();
  //DEBLNF(ctx.wpc,HEX);
  //DEBLN(sig);
  failed += testResult(sig == SIGILL && ctx.wpc == 0xe6);
  
  // test the illegal opcode detector for continue
  gdbDebugMessagePSTR(PSTR("gdbContinue on illegal instruction: "), testnum++);
  targetInitRegisters();
  ctx.sp = mcu.ramsz+mcu.rambase-1;
  ctx.wpc = 0xe6;
  sig = gdbContinue();
  succ = ctx.saved && sig == SIGILL; 
  if (sig == 0) {
    targetBreak();
    expectUCalibrate();
  }
  targetSaveRegisters();
  failed += testResult(succ && ctx.wpc == 0xe6);
  
  setSysState(DWCONN_STATE);
  if (num >= 1) {
    num = testnum;
    return failed;
  } else {
    testSummary(failed);
    gdbSendReply("OK");
    return 0;
  }
}

/********************************* Target specific tests ******************************************/

int targetTests(int &num) {
  int failed = 0;
  boolean succ;
  int testnum;
  unsigned int i;
  long lastflashcnt;

  if (targetOffline()) {
    if (num == 0) gdbSendReply("E00");
    return 0;
  }

  if (num >= 1) testnum = num;
  else testnum = 1;

  // write a (target-size) flash page (only check that no fatal error)
  gdbDebugMessagePSTR(PSTR("targetWriteFlashPage: "), testnum++);
  const int flashaddr = 0x80;
  fatalerror = NO_FATAL;
  setSysState(DWCONN_STATE);
  DWeraseFlashPage(flashaddr);
  DWreenableRWW();
  validpg = false;
  for (i=0; i < mcu.targetpgsz; i++) page[i] = 0;
  for (i=0; i < mcu.targetpgsz; i++) newpage[i] = i;
  targetWriteFlashPage(flashaddr);
  lastflashcnt = flashcnt;
  failed += testResult(fatalerror == NO_FATAL);

  // write same page again (since cache is valid, should not happen)
  gdbDebugMessagePSTR(PSTR("targetWriteFlashPage (check vaildpg): "), testnum++);
  fatalerror = NO_FATAL; setSysState(DWCONN_STATE);
  targetWriteFlashPage(flashaddr);
  failed += testResult(fatalerror == NO_FATAL && lastflashcnt == flashcnt);
  
  // write same page again (cache valid flag cleared), but since contents is tha same, do not write
  gdbDebugMessagePSTR(PSTR("targetWriteFlashPage (check contents): "), testnum++);
  fatalerror = NO_FATAL; setSysState(DWCONN_STATE);
  validpg = false;
  targetWriteFlashPage(flashaddr);
  failed += testResult(fatalerror == NO_FATAL && lastflashcnt == flashcnt);

  // try to write a cache page at an address that is not at a page boundary -> fatal error
  gdbDebugMessagePSTR(PSTR("targetWriteFlashPage (addr error): "), testnum++);
  fatalerror = NO_FATAL; setSysState(DWCONN_STATE);
  targetWriteFlashPage(flashaddr+2);
  failed += testResult(fatalerror != NO_FATAL && lastflashcnt == flashcnt);

  // read page (should be done from cache)
  gdbDebugMessagePSTR(PSTR("targetReadFlashPage (from cache): "), testnum++);
  fatalerror = NO_FATAL; setSysState(DWCONN_STATE);
  page[0] = 0x11; // mark first cell in order to see whether things get reloaded
  targetReadFlashPage(flashaddr);
  failed += testResult(fatalerror == NO_FATAL && page[0] == 0x11);

  // read page (force cache to be invalid and read from flash)
  gdbDebugMessagePSTR(PSTR("targetReadFlashPage: "), testnum++);
  fatalerror = NO_FATAL; setSysState(DWCONN_STATE);
  for (i=0; i < mcu.targetpgsz; i++) page[i] = 0;
  validpg = false;
  succ = true;
  targetReadFlashPage(flashaddr);
  for (i=0; i < mcu.targetpgsz; i++) {
    if (page[i] != i) succ = false;
  }
  failed += testResult(fatalerror == NO_FATAL && succ);
  fatalerror = NO_FATAL;	

  // restore registers (send to target) and save them (read from target)
  gdbDebugMessagePSTR(PSTR("targetRestoreRegisters/targetSaveRegisters: "), testnum++);
  unsigned int spinit = mcu.rambase+mcu.ramsz-1;
  succ = true;
  for (i = 0; i < 32; i++) ctx.regs[i] = i+1;
  ctx.wpc = 0x123;
  ctx.sp = spinit;
  ctx.sreg = 0xF7;
  ctx.saved = true;
  targetRestoreRegisters(); // store all regs to target
  if (ctx.saved) succ = false;
  ctx.wpc = 0;
  ctx.sp = 0;
  ctx.sreg = 0;
  for (i = 0; i < 32; i++) ctx.regs[i] = 0;
  targetSaveRegisters(); // get all regs from target
  //DEBLN(F("All regs from target"));
	
  if (!ctx.saved || ctx.wpc != 0x123-1 || ctx.sp != spinit || ctx.sreg != 0xF7) succ = false;
  for (i = 0; i < 32; i++) {
    //DEBLNF(ctx.regs[i],HEX);
    if (ctx.regs[i] != i+1) succ = false;
  }
  //DEBPR(F("wpc/sp/sreg = ")); DEBPRF(ctx.wpc,HEX); DEBPR(F("/")); DEBPRF(ctx.sp,HEX); DEBPR(F("/")); DEBLNF(ctx.sreg,HEX); 
  failed += testResult(succ);

  // test ergister init procedure
  gdbDebugMessagePSTR(PSTR("targetInitRegisters: "), testnum++);
  targetInitRegisters();
  failed += testResult(ctx.wpc == 0 && ctx.saved == true); // this is the only requirement!

  gdbDebugMessagePSTR(PSTR("targetWriteEeprom/targetReadEeprom: "), testnum++);
  DEBLN(F("targetEEPROM"));
  succ = true;
  const int eeaddr = 0x15;
  membuf[0] = 0x30;
  membuf[1] = 0x45;
  membuf[2] = 0x67;
  targetWriteEeprom(eeaddr, membuf, 3);
  targetReadEeprom(eeaddr, &membuf[3], 3);
  for (i = 0; i <3; i++)
    if (membuf[i] != membuf[i+3]) succ = false;
  DEBLNF(membuf[0],HEX);
  DEBLNF(membuf[1],HEX);
  DEBLNF(membuf[2],HEX);
  DEBLNF(membuf[3],HEX);
  DEBLNF(membuf[4],HEX);
  DEBLNF(membuf[5],HEX);
  for (i = 0; i < 6; i++) membuf[i] = 0xFF;
  targetWriteEeprom(eeaddr, membuf, 3);
  targetReadEeprom(eeaddr, &membuf[3], 3);
  for (i = 0; i <3; i++)
    if (membuf[3+i] != 0xFF) succ = false;
  DEBLNF(membuf[0],HEX);
  DEBLNF(membuf[1],HEX);
  DEBLNF(membuf[2],HEX);
  DEBLNF(membuf[3],HEX);
  DEBLNF(membuf[4],HEX);
  DEBLNF(membuf[5],HEX);
  failed += testResult(succ);

  gdbDebugMessagePSTR(PSTR("targetWriteSram/targetReadSram: "), testnum++);
  succ = true;
  const int ramaddr = mcu.rambase;
  membuf[0] = 0x31;
  membuf[1] = 0x46;
  membuf[2] = 0x68;
  targetWriteSram(ramaddr, membuf, 3);
  targetReadSram(ramaddr, &membuf[3], 3);
  for (i = 0; i <3; i++)
    if (membuf[i] != membuf[i+3]) succ = false;
  for (i = 0; i < 6; i++) membuf[i] = 0xFF;
  targetWriteSram(ramaddr, membuf, 3);
  for (i = 0; i < 3; i++) membuf[i] = 0;
  targetReadSram(ramaddr, membuf, 3);
  for (i = 0; i <3; i++)
    if (membuf[i] != 0xFF) succ = false;
  failed += testResult(succ);

  setupTestCode(); // store testcode to memory
  /* Testcode - for checking execution related functions (to loaded into to target memory)
     bADDR      wADDR
     1aa:	d5:   00 00       	nop
     1ac:	d6:   00 00       	nop
     1ae:	d7:   00 00       	nop
     1b0:	d8:   ff cf       	rjmp	.-2      	; 0x1b0 <DL>
     1b2:	d9:   29 e4       	ldi	r18, 0x49	; 73
     1b4:	da:   20 93 00 01 	sts	0x0100, r18	; 0x800100 <goal>
     1b8:	dc:   00 91 00 01 	lds	r16, 0x0100	; 0x800100 <goal>
     1bc:	de:   05 d0       	rcall	.+10     	; 0x1c8 <SUBR>
     1be:	df:   f5 cf       	rjmp	.-22     	; 0x1aa <START>
     1c0:	e0:   0e 94 e4 00 	call	0x1c8	        ; 0x1c8 <SUBR>
     1c4:	e2:   0c 94 d5 00 	jmp	0x1aa	        ; 0x1aa <START>
     1c8:	e4:   11 e9       	ldi	r17, 0x91	; 145
     1ca:	e5:   08 95       	ret 
     1cc:       e6:   01 00             ILLEGAL
  */


  gdbDebugMessagePSTR(PSTR("targetStep (ldi r18,0x49): "), testnum++);
  succ = true;
  targetInitRegisters();
  ctx.wpc = 0xd9; // ldi instruction
  ctx.sp = mcu.ramsz+mcu.rambase-1; // SP to upper limit of RAM
  targetRestoreRegisters(); 
  targetStep();
  if (!expectBreakAndU())
    succ = false;
  targetSaveRegisters();
  failed += testResult(succ && ctx.wpc == 0xda && ctx.regs[18] == 0x49);

  gdbDebugMessagePSTR(PSTR("targetStep (rcall): "), testnum++);
  succ = true;
  ctx.wpc = 0xde; // rcall instruction
  targetRestoreRegisters();
  targetStep(); // one step leads to Break+0x55
  if (!expectBreakAndU()) succ = false;  
  targetSaveRegisters();
  failed += testResult(succ && ctx.wpc == 0xe4);
  gdbDebugMessagePSTR(PSTR("Test targetContinue/targetBreak: "), testnum++);
  succ = true;
  hwbp = 0xFFFF;
  targetRestoreRegisters();
  targetContinue();
  targetBreak(); // DW responds with 0x55 on break
  if (!expectUCalibrate()) succ = false;
  targetSaveRegisters();
  failed += testResult(succ && ctx.wpc == 0xd8 && ctx.regs[17] == 0x91);

  gdbDebugMessagePSTR(PSTR("targetReset: "), testnum++);
  ctx.sreg= 0xFF; // SREG
  DEBPR(F("SREG before: ")); DEBLNF(ctx.sreg,HEX);
  DEBPR(F("WPC before:  ")); DEBLNF(ctx.wpc,HEX);
  targetRestoreRegisters();
  targetReset(); // response is taken care of by the function itself
  targetSaveRegisters();
  DEBPR(F("SREG after: ")); DEBLNF(ctx.sreg,HEX);
  DEBPR(F("WPC after:  ")); DEBLNF(ctx.wpc,HEX);
  failed += testResult((ctx.wpc & 0x7F) == 0 && ctx.sreg == 0); // PC can be set to boot area!

#if ILLOPDETECT
  gdbDebugMessagePSTR(PSTR("targetIllegalOpcode (mul r16, r16): "), testnum++);
  failed += testResult(targetIllegalOpcode(0x9F00) == !mcu.avreplus);

  gdbDebugMessagePSTR(PSTR("targetIllegalOpcode (jmp ...): "), testnum++);
  failed += testResult(targetIllegalOpcode(0x940C) == (mcu.flashsz <= 8192));
#endif
  
  if (num >= 1) {
    num = testnum;
    return failed;
  } else {
    testSummary(failed);
    gdbSendReply("OK");
    return 0;
  }
}

/********************************* debugWIRE specific tests ******************************************/

int DWtests(int &num)
{
  int failed = 0;
  boolean succ;
  int testnum;
  byte temp;
  unsigned int i;

  if (targetOffline()) {
    if (num == 0) gdbSendReply("E00");
    return 0;
  }

  if (num >= 1) testnum = num;
  else testnum = 1;

  // write and read 3 registers
  gdbDebugMessagePSTR(PSTR("DWwriteRegister/DWreadRegister: "), testnum++);
  DWwriteRegister(0, 0x55);
  DWwriteRegister(15, 0x9F);
  DWwriteRegister(31, 0xFF);
  failed += testResult(DWreadRegister(0) == 0x55 && DWreadRegister(15) == 0x9F && DWreadRegister(31) == 0xFF);

  // write registers in one go and read them in one go (much faster than writing/reading individually) 
  gdbDebugMessagePSTR(PSTR("DWwriteRegisters/DWreadRegisters: "), testnum++);
  for (byte i=0; i < 32; i++) membuf[i] = i*2+1;
  DWwriteRegisters(membuf);
  for (byte i=0; i < 32; i++) membuf[i] = 0;
  DWreadRegisters(membuf);
  succ = true;
  for (byte i=0; i < 32; i++) {
    if (membuf[i] != i*2+1) {
      succ = false;
      break;
    }
  }
  failed += testResult(succ);

  // write to and read from an IO reg (0x3F = SREG)
  gdbDebugMessagePSTR(PSTR("DWwriteIOreg/DWreadIOreg: "), testnum++);
  DWwriteIOreg(0x3F, 0x55);
  failed += testResult(DWreadIOreg(0x3F) == 0x55);

  // write into (lower) sram and read it back from corresponding IO reag 
  gdbDebugMessagePSTR(PSTR("DWwriteSramByte/DWreadIOreg: "), testnum++);
  DWwriteSramByte(0x3F+0x20, 0x1F);
  temp = DWreadIOreg(0x3F);
  failed += testResult(temp == 0x1F);

  // write into IO reg and read it from the ocrresponding sram addr
  gdbDebugMessagePSTR(PSTR("DWwriteIOreg/DWreadSramByte: "), testnum++);
  DWwriteIOreg(0x3F, 0xF2);
  failed += testResult(DWreadSramByte(0x3F+0x20) == 0xF2);

  // write a number of bytes to sram and read them again byte by byte
  gdbDebugMessagePSTR(PSTR("DWwriteSramByte/DWreadSramByte: "), testnum++);
  for (byte i=0; i < 32; i++) DWwriteSramByte(mcu.rambase+i, i+1);
  succ = true;
  for (byte i=0; i < 32; i++) {
    if (DWreadSramByte(mcu.rambase+i) != i+1) {
      succ = false;
      break;
    }
  }
  failed += testResult(succ);

  // sram bulk reading
  gdbDebugMessagePSTR(PSTR("DWreadSram (bulk): "), testnum++);
  for (byte i=0; i < 32; i++) membuf[i] = 0;
  DWreadSramBytes(mcu.rambase, membuf, 32);
  succ = true;
  for (byte i=0; i < 32; i++) {
    if (membuf[i] != i+1) {
      succ = false;
      break;
    }
  }
  failed += testResult(succ);

  // write to EEPROM (addr 0x15) and read from it
  gdbDebugMessagePSTR(PSTR("DWwriteEepromByte/DWreadEepromByte: "), testnum++);
  const int eeaddr = 0x15;
  succ = true;
  DWwriteEepromByte(eeaddr, 0x38);
  if (DWreadEepromByte(eeaddr) != 0x38) succ = false;
  DWwriteEepromByte(eeaddr, 0xFF);
  if (DWreadEepromByte(eeaddr) != 0xFF) succ = false;
  failed += testResult(succ);
  
  // erase flash page (check only for errors)
  gdbDebugMessagePSTR(PSTR("DWeraseFlashPage: "), testnum++);
  const int flashaddr = 0x100;
  DWeraseFlashPage(flashaddr);
  failed += testResult(fatalerror == NO_FATAL);
  fatalerror = NO_FATAL;

  // read the freshly cleared flash page
  gdbDebugMessagePSTR(PSTR("DWreadFlash (empty page): "), testnum++);
  for (i=0; i < mcu.pagesz; i++) newpage[i] = 0;
  succ = true;
  DWreenableRWW();
  DWreadFlash(flashaddr, newpage, mcu.pagesz);
  for (i=0; i < mcu.pagesz; i++) {
    if (newpage[i] != 0xFF) succ = false;
  }
  failed += testResult(succ);
    
  // program one flash page (only check for error code returns)
  gdbDebugMessagePSTR(PSTR("DWloadFlashPage/DWprogramFlashPage: "), testnum++);
  for (i=0; i < mcu.pagesz; i++) newpage[i] = 255-i;
  DWloadFlashPageBuffer(flashaddr, newpage);
  DWprogramFlashPage(flashaddr);
  failed += testResult(fatalerror == NO_FATAL);
  fatalerror = NO_FATAL;

  // now try to read the freshly flashed page
  gdbDebugMessagePSTR(PSTR("DWreenableRWW/DWreadFlash: "), testnum++);
  for (i=0; i < mcu.pagesz; i++) newpage[i] = 0;
  DEBLN(F("newpage cleared"));
  succ = true;
  DWreenableRWW();
  DEBLN(F("reeanbledRWW"));
  DWreadFlash(flashaddr, newpage, mcu.pagesz);
  DEBLN(F("Read Flash:"));
  for (i=0; i < mcu.pagesz; i++) {
    DEBLNF(newpage[i],HEX);
    if (newpage[i] != 255-i) {
      succ = false;
    }
  }
  failed += testResult(succ);

  // if a device with boot sector, try everything immediately after each other in the boot area 
  if (mcu.bootaddr != 0) {
    for (i=0; i < mcu.pagesz; i++) newpage[i] = 255-i;
    gdbDebugMessagePSTR(PSTR("DWFlash prog. in boot section: "), testnum++);
    DWeraseFlashPage(mcu.bootaddr);
    succ = (fatalerror == NO_FATAL);
    if (succ) {
      //DEBLN(F("erase successful"));
      DWreenableRWW();
      DWloadFlashPageBuffer(mcu.bootaddr, newpage);
      succ = (fatalerror == NO_FATAL);
      //for (i=0; i < mcu.pagesz; i++) DEBLN(newpage[i]);
    }
    if (succ) {
      //DEBLN(F("load temp successful"));
      DWprogramFlashPage(mcu.bootaddr);
      succ = (fatalerror == NO_FATAL);
    }
    if (succ) {
      DWreenableRWW();
      DEBLN(F("program successful"));
      for (i=0; i < mcu.pagesz; i++) newpage[i] = 0;
      DWreadFlash(mcu.bootaddr, newpage, mcu.pagesz);
      for (i=0; i < mcu.pagesz; i++) {
	DEBLN(membuf[i]);
	if (newpage[i] != 255-i) {
	  DEBPR(F("Now wrong!"));
	  succ = false;
	}
      }
    }
    failed += testResult(succ);
  }
  fatalerror = NO_FATAL;


  // get chip id
  gdbDebugMessagePSTR(PSTR("DWgetChipId: "), testnum++);
  failed += testResult(mcu.sig != 0 && (DWgetChipId() == mcu.sig ||
					(mcu.sig == 0x9514 && DWgetChipId() == 0x950F) || // imposter 328P!
					(mcu.sig == 0x9406 && DWgetChipId() == 0x940B) || // imposter 168PA!
					(mcu.sig == 0x930A && DWgetChipId() == 0x930F) || // imposter 88PA!
					(mcu.sig == 0x9205 && DWgetChipId() == 0x920A))); // imposter 48PA!
  
  // Set/get PC (word address)
  gdbDebugMessagePSTR(PSTR("DWsetWPc/DWgetWPc: "), testnum++);
  unsigned int pc = 0x3F; 
  DWsetWPc(pc);
  unsigned int newpc = DWgetWPc(true);
  DEBLNF(newpc, HEX);
  failed += testResult(newpc == pc - 1);

  /*
  // Set/get hardware breakpoint
  gdbDebugMessagePSTR(PSTR("Test DWsetWBp/DWgetWBp: "), testnum++);
  DWsetWBp(pc);
  failed += testResult(DWgetWBp() == pc);
  */
  
  // execute one instruction offline
  gdbDebugMessagePSTR(PSTR("DWexecOffline (eor r1,r1 at WPC=0x013F): "), testnum++);
  DWwriteIOreg(0x3F, 0); // write SREG
  DWwriteRegister(1, 0x55);
  pc = 0x13F;
  DWsetWPc(pc);
  DWexecOffline(0x2411); // specify opcode as MSB LSB (bigendian!)
  succ = false;
  if (pc + 1 == DWgetWPc(true)) // PC advanced by one, then +1 for break, but this is autmatically subtracted
    if (DWreadRegister(1) == 0)  // reg 1 should be zero now
      if (DWreadIOreg(0x3F) == 0x02) // in SREG, only zero bit should be set
	succ = true;
  failed += testResult(succ);

  // execute MUL offline
  gdbDebugMessagePSTR(PSTR("DWexecOffline (mul r16, r16 at WPC=0x013F): "), testnum++);
  DWwriteRegister(16, 5);
  DWwriteRegister(1, 0x55);
  DWwriteRegister(0, 0x55);
  DWsetWPc(pc);
  DWexecOffline(0x9F00); // specify opcode as MSB LSB (bigendian!)
  newpc = DWgetWPc(true);
  succ = false;
  DEBPR(F("reg 1:")); DEBLN(DWreadRegister(1));
  DEBPR(F("reg 0:")); DEBLN(DWreadRegister(0));
  DEBLN(mcu.avreplus);
  DEBLNF(newpc,HEX);
  if (pc+1 == newpc) // PC advanced by one, then +1 for break, but this is autmatically subtracted
    succ = ((DWreadRegister(0) == 25 && DWreadRegister(1) == 0 && mcu.avreplus) ||
	    (DWreadRegister(0) == 0x55 && DWreadRegister(0) == 0x55 && !mcu.avreplus));
  failed += testResult(succ);

  // execute a rjmp instruction offline 
  gdbDebugMessagePSTR(PSTR("DWexecOffline (rjmp 0x002E at WPC=0x0001): "), testnum++);
  DWsetWPc(0x01);
  DWexecOffline(0xc02C);
  failed += testResult(DWgetWPc(true) == 0x2E); // = byte addr 0x005C

  
  if (num >= 1) {
    num = testnum;
    return failed;
  } else {
    testSummary(failed);
    gdbSendReply("OK");
    return 0;
  }
}

/************************************** ISP programmer ***********************************/

//
//  In-System Programmer (adapted from ArduinoISP sketch by Randall Bohn and its mods by Wayne Holder)
//
//  My changes to Mr Bohn's original code include removing and renaming functions that were redundant with
//  my code, converting other functions to cases in a master switch() statement, removing code that served
//  no purpose, and making some structural changes to reduce the size of the code.
//  
//  Portions of the code after this point were originally Copyright (c) 2008-2011 Randall Bohn
//    If you require a license, see:  http://www.opensource.org/licenses/bsd-license.php
//

// STK Definitions
#define STK_OK      0x10    // DLE
#define STK_FAILED  0x11    // DC1
#define STK_UNKNOWN 0x12    // DC2
#define STK_INSYNC  0x14    // DC4
#define STK_NOSYNC  0x15    // NAK
#define CRC_EOP     0x20    // Ok, it is a space...

// Code definitions
#define EECHUNK   (32)

// Variables used by In-System Programmer code

uint16_t      pagesize;
uint16_t      eepromsize;
bool          rst_active_high;
int           pmode = 0;
unsigned int  here;           // Address for reading and writing, set by 'U' command
unsigned long  hMask;          // Pagesize mask for 'here" address

void ISPprogramming(__attribute__((unused)) boolean fast) {
#if (!NOISPPROG)
  setSysState(PROG_STATE);
  wdt_enable(WDTO_8S); // enable watch dog timmer
  if (!fast) {
    Serial.end();
    Serial.begin(PROGBPS);
  }

  while (true) {
    if (Serial.available()) {
      avrisp();
    }
    if (ctx.state != PROG_STATE) {
      if (!fast) {
	Serial.end();
	Serial.begin(HOSTBPS);
      }
      leaveProgramMode();
      wdt_disable();
      dwlrestart();
    }
  }
#endif
}

byte getch (void) {
  while (!Serial.available());
  return Serial.read();
}

void fill (int n) {
  for (int x = 0; x < n; x++) {
    buf[x] = getch();
  }
}

void empty_reply (void) {
  if (CRC_EOP == getch()) {
    Serial.write(STK_INSYNC);
    Serial.write(STK_OK);
  } else {
    Serial.write(STK_NOSYNC);
  }
}

void breply (byte b) {
  if (CRC_EOP == getch()) {
    Serial.write((char)STK_INSYNC);
    Serial.write((char)b);
    Serial.write((char)STK_OK);
  } else {
    Serial.write((char)STK_NOSYNC);
  }
}

////////////////////////////////////
//      Command Dispatcher
////////////////////////////////////

void avrisp (void) {
  byte ch = getch();
  char memtype;
  char result;
  unsigned int length, start, remaining, page, ii, addr;
  
  switch (ch) {
    case 0x30:                                  // '0' - 0x30 Get Synchronization (Sign On)
      empty_reply();
      break;
      
    case 0x31:                                  // '1' - 0x31 Check if Starterkit Present
      if (getch() == CRC_EOP) {
        Serial.write((char)STK_INSYNC);
        Serial.print("AVR ISP");
        Serial.write((char)STK_OK);
      } else {
        Serial.write((char)STK_NOSYNC);
      }
      break;

    case 0x41:                                  // 'A' - 0x41 Get Parameter Value
      switch (getch()) {
        case 0x80:
          breply(0x02); // HWVER
          break;
        case 0x81:
          breply(0x01); // SWMAJ
          break;
        case 0x82:
          breply(0x12); // SWMIN
          break;
        case 0x93:
          breply('S');  // Serial programmer
          break;
        default:
          breply(0);
      }
      break;
      
    case 0x42:                                  // 'B' - 0x42 Set Device Programming Parameters
      fill(20);
      // AVR devices have active low reset, AT89Sx are active high
      rst_active_high = (buf[0] >= 0xE0);
      pagesize   = (buf[12] << 8) + buf[13];
      // Setup page mask for 'here' address variable
      if (pagesize == 32) {
        hMask = 0xFFFFFFF0UL;
      } else if (pagesize == 64) {
        hMask = 0xFFFFFFE0UL;
      } else if (pagesize == 128) {
        hMask = 0xFFFFFFC0UL;
      } else if (pagesize == 256) {
        hMask = 0xFFFFFF80UL;
      } else {
        hMask = 0xFFFFFFFFUL;
      }
      eepromsize = (buf[14] << 8) + buf[15];
      empty_reply();
      break;
      
    case 0x45:                                  // 'E' - 0x45 Set Extended Device Programming Parameters (ignored)
      fill(5);
      empty_reply();
      break;
      
    case 0x50:                                  // 'P' - 0x50 Enter Program Mode
      wdt_reset();
      if (!pmode) {
	DEBLN(F("enter PM"));
	if (enterProgramMode()) {
	  DEBLN(F("SUCCESS!"));
	  pmode = 1;
	} else {
	  DEBLN(F("No prog state"));
	  if (targetSetFuses(DWEN) == 1) {
	    DEBLN(F("DW off"));
	    pmode = 1;
	  } else {
	    DEBLN(F("DW still on"));
	    leaveProgramMode();
	  }
	}
      }
      if (pmode) {
	empty_reply();
      } else {
	setSysState(ERROR_STATE);
	return;
      }
      break;
      
    case 0x51:                                  // 'Q' - 0x51 Leave Program Mode
      if (pmode) wdt_reset();
      // We're about to take the target out of reset so configure SPI pins as input
      leaveProgramMode();
      pmode = 0;
      setSysState(NOTCONN_STATE);
      empty_reply();
      break;

    case  0x55:                                 // 'U' - 0x55 Load Address (word)
      if (pmode) wdt_reset();      
      here = getch();
      here += 256 * getch();
      empty_reply();
      break;

    case 0x56:                                  // 'V' - 0x56 Universal Command
      if (pmode) wdt_reset();            
      fill(4);
      breply(ispSend(buf[0], buf[1], buf[2], buf[3], true));
      break;

    case 0x60:                                  // '`' - 0x60 Program Flash Memory
      if (pmode) wdt_reset();      
      getch(); // low addr
      getch(); // high addr
      empty_reply();
      break;
      
    case 0x61:                                  // 'a' - 0x61 Program Data Memory
      if (pmode) wdt_reset();      
      getch(); // data
      empty_reply();
      break;

    case 0x64: {                                // 'd' - 0x64 Program Page (Flash or EEPROM)
      if (pmode) wdt_reset();      
      result = STK_FAILED;
      length = 256 * getch();
      length += getch();
      memtype = getch();
      // flash memory @here, (length) bytes
      if (memtype == 'F') {
        fill(length);
        if (CRC_EOP == getch()) {
          Serial.write((char)STK_INSYNC);
          ii = 0;
          page = here & hMask;
          while (ii < length) {
            if (page != (here & hMask)) {
              ispSend(0x4C, (page >> 8) & 0xFF, page & 0xFF, 0, true);  // commit(page);
              page = here & hMask;
            }
            ispSend(0x40 + 8 * LOW, here >> 8 & 0xFF, here & 0xFF, buf[ii++], true);
            ispSend(0x40 + 8 * HIGH, here >> 8 & 0xFF, here & 0xFF, buf[ii++], true);
            here++;
          }
          ispSend(0x4C, (page >> 8) & 0xFF, page & 0xFF, 0, true);      // commit(page);
          Serial.write((char)STK_OK);
          break;
        } else {
          Serial.write((char)STK_NOSYNC);
        }
        break;
      } else if (memtype == 'E') {
        // here is a word address, get the byte address
        start = here * 2;
        remaining = length;
        if (length > eepromsize) {
          result = STK_FAILED;
        } else {
          while (remaining > EECHUNK) {
            // write (length) bytes, (start) is a byte address
            // this writes byte-by-byte, page writing may be faster (4 bytes at a time)
            fill(length);
            for (ii = 0; ii < EECHUNK; ii++) {
              addr = start + ii;
              ispSend(0xC0, (addr >> 8) & 0xFF, addr & 0xFF, buf[ii], true);
              _delay_ms(45);
            }
            start += EECHUNK;
            remaining -= EECHUNK;
          }
          // write (length) bytes, (start) is a byte address
          // this writes byte-by-byte, page writing may be faster (4 bytes at a time)
          fill(length);
          for (ii = 0; ii < remaining; ii++) {
            addr = start + ii;
            ispSend(0xC0, (addr >> 8) & 0xFF, addr & 0xFF, buf[ii], true);
            _delay_ms(45);
          }
          result = STK_OK;
        }
        if (CRC_EOP == getch()) {
          Serial.write((char)STK_INSYNC);
          Serial.write((char)result);
        } else {
          Serial.write((char)STK_NOSYNC);
        }
        break;
      }
      Serial.write((char)STK_FAILED);
    } break;

    case 0x74: {                                // 't' - 0x74 Read Page (Flash or EEPROM)
      if (pmode) wdt_reset();      
      result = STK_FAILED;
      length = 256 * getch();
      length += getch();
      memtype = getch();
      if (CRC_EOP != getch()) {
        Serial.write((char)STK_NOSYNC);
        break;
      }
      Serial.write((char)STK_INSYNC);
      if (memtype == 'F') {
        for (ii = 0; ii < length; ii += 2) {
          Serial.write(ispSend(0x20 + LOW * 8, (here >> 8) & 0xFF, here & 0xFF, 0, true));   // low
          Serial.write(ispSend(0x20 + HIGH * 8, (here >> 8) & 0xFF, here & 0xFF, 0, true));  // high
          here++;
        }
        result = STK_OK;
      } else if (memtype == 'E') {
        // here again we have a word address
        start = here * 2;
        for (ii = 0; ii < length; ii++) {
          addr = start + ii;
          Serial.write(ispSend(0xA0, (addr >> 8) & 0xFF, addr & 0xFF, 0xFF, true));
        }
        result = STK_OK;
      }
      Serial.write((char)result);
    } break;

    case 0x75:                                  // 'u' - 0x75 Read Signature Bytes
      if (pmode) wdt_reset();      
      if (CRC_EOP != getch()) {
        Serial.write(STK_NOSYNC);
        break;
      }
      Serial.write((char)STK_INSYNC);
      Serial.write(ispSend(0x30, 0x00, 0x00, 0x00, true)); // high
      Serial.write(ispSend(0x30, 0x00, 0x01, 0x00, true)); // middle
      Serial.write(ispSend(0x30, 0x00, 0x02, 0x00, true)); // low
      Serial.write((char)STK_OK);
      break;

    case 0x20:                                  // ' ' - 0x20 CRC_EOP
      // expecting a command, not CRC_EOP, this is how we can get back in sync
      Serial.write((char)STK_NOSYNC);
      break;

    default:                                    // anything else we will return STK_UNKNOWN
      if (CRC_EOP == getch()) {
        Serial.write((char)STK_UNKNOWN);
      } else {
        Serial.write((char)STK_NOSYNC);
      }
  }
}
