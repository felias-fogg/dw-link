// This is an implementation of the GDB remote serial protocol for debugWIRE.
// It should run on all ATmega328 boards and provides a hardware debugger
// for the classic ATtinys and some small ATmegas 
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
// level shifters, which I plan to sell on Tindie.
//
// I thought that the sketch should also work with the Leonardo-like boards
// and with the Mega board. For the former, I got stuck with the flashtest program.
// USB and tight interrupt timing do not seem to go together.
// For the latter, I experienced non-deterministic failures of unit tests, probably
// because relevant input ports are not in the I/O range and therefore the tight timing
// constraints are not satisfied.
#define VERSION "2.1.8"

// some constants, you may want to change
#ifndef HOSTBPS 
#define HOSTBPS 115200UL // safe default speed for the host connection
//#define HOSTBPS 230400UL // works with UNOs that use an ATmega32U2 as the USB interface         
#endif
// #define STUCKAT1PC 1       // allow also MCUs that have PCs with stuck-at-1 bits
// #define NOAUTODWOFF 1      // do not automatically leave debugWIRE mode
// #define HIGHSPEEDDW 1      // allow for DW speed up to 250 kbps

// these should stay undefined for the ordinary user
// #define CONSTDWSPEED 1     // constant communication speed with target
// #define OFFEX2WORD 1       // instead of simu. use offline execution for 2-word instructions
// #define TXODEBUG 1         // allow debug output over TXOnly line
// #define SCOPEDEBUG 1       // activate scope debugging on PORTC
// #define FREERAM  1         // measure free ram
// #define UNITALL 1          // enable all unit tests
// #define UNITDW 1           // enable debugWIRE unit tests
// #define UNITTG 1           // enable target unit tests
// #define UNITGDB 1          // enable gdb function unit tests
// #define NOMONINTORHELP 1   // disable monitor help function 

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
#include <util/delay.h>
#include "src/dwSerial.h"
#include "src/SingleWireSerial_config.h"
#if TXODEBUG
#include "src/TXOnlySerial.h" // only needed for (meta-)debuging
#endif
#include "src/debug.h" // some (meta-)debug macros

// some size restrictions

#define MAXBUF 160 // input buffer for GDB communication (enough for initial packet in gdb 12.1)
#define MAXMEMBUF 150 // size of memory buffer
#define MAXPAGESIZE 256 // maximum number of bytes in one flash memory page (for the 64K MCUs)
#define MAXBREAK 33 // maximum of active breakpoints (we need double as many entries for lazy breakpoint setting/removing!)

// communication bit rates 
#define SPEEDHIGH     300000UL // maximum communication speed limit for DW
#define SPEEDLOW      150000UL // normal speed limit
#if HIGHSPEED
#define SPEEDLIMIT SPEEDHIGH
#else
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
#define CONNERR_NO_ISP_OR_DW_REPLY 1 // connection error: no ISP or DW reply
#define CONNERR_UNSUPPORTED_MCU 2 // connection error: MCU not supported
#define CONNERR_LOCK_BITS 3 // connection error: lock bits are set
#define CONNERR_STUCKAT1_PC 4 // connection error: MCU has PC with stuck-at-one bits
#define CONNERR_UNKNOWN 5 // unknown connection error
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
#define HWBP_ASSIGNMENT_INCONSISTENT_FATAL 113 // HWBP assignemnt is inconsistent
#define SELF_BLOCKING_FATAL 114 // there shouldn't be a BREAK instruction in the code
#define FLASH_READ_WRONG_ADDR_FATAL 115 // trying to read a flash word at a non-even address
#define NO_STEP_FATAL 116 // could not do a single-step operation
#define RELEVANT_BP_NOT_PRESENT 117 // identified relevant BP not present any longer 
#define INPUT_OVERLFOW_FATAL 118 // input buffer overflow - should not happen at all!
#define WRONG_FUSE_SPEC_FATAL 119 // specification of a fuse we are not prepafred to change
#define BREAKPOINT_UPDATE_WHILE_FLASH_PROGRAMMING_FATAL 120 // should not happen!
#define DW_TIMEOUT_FATAL 121 // timeout while reading from DW line
#define DW_READREG_FATAL 122 // timeout while register reading
#define DW_READIOREG_FATAL 123 // timeout during register read operation
#define REENABLERWW_FATAL 124 // timeout during reeanble RWW operation
#define EEPROM_READ_FATAL 125 // timeout during EEPROM read
#define BAD_INTERRUPT_FATAL 126 // bad interrupt

// some masks to interpret memory addresses
#define MEM_SPACE_MASK 0x00FF0000 // mask to detect what memory area is meant
#define FLASH_OFFSET   0x00000000 // flash is addressed starting from 0
#define SRAM_OFFSET    0x00800000 // RAM address from GBD is (real addresss + 0x00800000)
#define EEPROM_OFFSET  0x00810000 // EEPROM address from GBD is (real addresss + 0x00810000)

// instruction codes
const unsigned int BREAKCODE = 0x9598;

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

unsigned int hwbp = 0xFFFF; // the one hardware breakpoint (word address)

enum statetype {NOTCONN_STATE, PWRCYC_STATE, ERROR_STATE, CONN_STATE, LOAD_STATE, RUN_STATE};

struct context {
  unsigned int wpc; // pc (using word addresses)
  unsigned int sp; // stack pointer
  byte sreg;    // status reg
  byte regs[32]; // general purpose regs
  boolean saved:1; // all of the regs have been saved
  statetype state:3; // system state
  unsigned long bps; // debugWIRE communication speed
  boolean safestep; // if true, then single step in a safe way, i.e. not interruptable
} ctx;

// use LED to signal system state
// LED off = not connected to target system
// LED flashing every second = power-cycle target in order to enable debugWIRE
// LED blinking every 1/10 second = could not connect to target board
// LED constantly on = connected to target 
const unsigned int ontimes[6] =  {0,  100, 150, 1, 1, 1};
const unsigned int offtimes[6] = {1, 1000, 150, 0, 0, 0};
volatile unsigned int ontime; // number of ms on
volatile unsigned int offtime; // number of ms off
byte ledmask;
volatile byte *ledout;

// pins
const byte TISP = 4;
const byte TSCK = 13;
const byte TMOSI = 11;
const byte TMISO = 12;
const byte VSUP = 9;
const byte IVSUP = 2;
const byte DEBTX = 3;
const byte SYSLED = 7;
const byte DARKSYSLED = 5;
const byte LEDGND = 6;
const byte DWLINE = 8;

// MCU names
const char attiny13[] PROGMEM = "ATtiny13";
const char attiny43[] PROGMEM = "ATtiny43U";
const char attiny2313[] PROGMEM = "ATtiny2313";
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
const char atmega48a[] PROGMEM = "ATmega48A";
const char atmega48pa[] PROGMEM = "ATmega48PA";
const char atmega48pb[] PROGMEM = "ATmega48PB";
const char atmega88a[] PROGMEM = "ATmega88A";
const char atmega88pa[] PROGMEM = "ATmega88PA";
const char atmega88pb[] PROGMEM = "ATmega88PB";
const char atmega168a[] PROGMEM = "ATmega168A";
const char atmega168pa[] PROGMEM = "ATmega168PA";
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
const char at90pwm12b3b[] PROGMEM = "AT90PWM1/2B/3B";
const char at90pwm81[] PROGMEM = "AT90PWM81";
const char at90pwm161[] PROGMEM = "AT90PWM161";
const char at90pwm216316[] PROGMEM = "AT90PWM216/316";


const char Connected[] PROGMEM = "Connected to ";

//  MCU parameters
struct {
  unsigned int sig;        // two byte signature
  boolean      avreplus;   // is an AVRe+ architecture MCU (includes MUL instruction)
  unsigned int ramsz;      // SRAM size in bytes
  unsigned int rambase;    // base address of SRAM
  unsigned int eepromsz;   // size of EEPROM in bytes
  unsigned int flashsz;    // size of flash memory in bytes
  byte         dwdr;       // address of DWDR register
  unsigned int pagesz;     // page size of flash memory in bytes
  boolean      erase4pg;   // 1 when the MCU has a 4-page erase operation
  unsigned int bootaddr;   // highest address of possible boot section  (0 if no boot support)
  byte         eecr;       // address of EECR register
  byte         eearh;      // address of EARL register (0 if none)
  byte         rcosc;      // fuse pattern for setting RC osc as clock source
  byte         extosc;     // fuse pattern for setting EXTernal osc as clock source
  byte         xtalosc;    // fuse pattern for setting XTAL osc as clock source
  byte         slowosc;    // fuse pattern for setting 128 kHz oscillator
  const char*  name;       // pointer to name in PROGMEM
  byte         dwenfuse;   // bit mask for DWEN fuse in high fuse byte
  byte         ckdiv8;     // bit mask for CKDIV8 fuse in low fuse byte
  byte         ckmsk;      // bit mask for selecting clock source (and startup time)
  byte         eedr;       // address of EEDR (computed from EECR)
  byte         eearl;      // address of EARL (computed from EECR)
  unsigned int targetpgsz; // target page size (depends on pagesize and erase4pg)
  byte         stuckat1byte;   // fixed bits in high byte of pc
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
  byte         rcosc;          // fuse pattern for setting RC osc as clock source
  byte         extosc;         // fuse pattern for setting EXTernal osc as clock source
  byte         xtalosc;        // fuse pattern for setting XTAL osc as clock source
  byte         slowosc;        // fuse pattern for setting 128 kHz oscillator
  boolean      avreplus;       // AVRe+ architecture
  const char*  name;           // pointer to name in PROGMEM
};

// mcu infos (for all AVR mcus supporting debugWIRE)
// untested ones are marked 
const mcu_info_type mcu_info[] PROGMEM = {
  // sig sram low eep flsh dwdr  pg er4 boot    eecr eearh rcosc extosc xtosc slosc plus name
  //{0x9007,  1, 1,  1,  1, 0x2E,  16, 0, 0x0000, 0x1C, 0x00, 0x0A, 0x08, 0x00, 0x0B, 0, attiny13},

  {0x910A,  2, 1,  2,  2, 0x1f,  16, 0, 0x0000, 0x1C, 0x00, 0x24, 0x20, 0x3F, 0x26, 0, attiny2313},
  {0x920D,  4, 1,  4,  4, 0x27,  32, 0, 0x0000, 0x1C, 0x00, 0x24, 0x20, 0x3F, 0x26, 0, attiny4313},

  {0x920C,  4, 1,  1,  4, 0x27,  32, 0, 0x0000, 0x1C, 0x00, 0x22, 0x20, 0x3F, 0x23, 0, attiny43},

  {0x910B,  2, 1,  2,  2, 0x27,  16, 0, 0x0000, 0x1C, 0x1F, 0x22, 0x20, 0x3F, 0x24, 0, attiny24},   
  {0x9207,  4, 1,  4,  4, 0x27,  32, 0, 0x0000, 0x1C, 0x1F, 0x22, 0x20, 0x3F, 0x24, 0, attiny44},
  {0x930C,  8, 1,  8,  8, 0x27,  32, 0, 0x0000, 0x1C, 0x1F, 0x22, 0x20, 0x3F, 0x24, 0, attiny84},
  
  {0x9215,  4, 0,  4,  4, 0x27,   8, 1, 0x0000, 0x1C, 0x1F, 0x22, 0x20, 0x3F, 0x00, 0, attiny441}, 
  {0x9315,  8, 0,  8,  8, 0x27,   8, 1, 0x0000, 0x1C, 0x1F, 0x22, 0x20, 0x3F, 0x00, 0, attiny841},
  
  {0x9108,  2, 1,  2,  2, 0x22,  16, 0, 0x0000, 0x1C, 0x1F, 0x22, 0x20, 0x3F, 0x24, 0, attiny25},
  {0x9206,  4, 1,  4,  4, 0x22,  32, 0, 0x0000, 0x1C, 0x1F, 0x22, 0x20, 0x3F, 0x24, 0, attiny45},
  {0x930B,  8, 1,  8,  8, 0x22,  32, 0, 0x0000, 0x1C, 0x1F, 0x22, 0x20, 0x3F, 0x24, 0, attiny85},
  
  {0x910C,  2, 1,  2,  2, 0x20,  16, 0, 0x0000, 0x1C, 0x1F, 0x22, 0x20, 0x3F, 0x23, 0, attiny261},
  {0x9208,  4, 1,  4,  4, 0x20,  32, 0, 0x0000, 0x1C, 0x1F, 0x22, 0x20, 0x3F, 0x23, 0, attiny461},
  {0x930D,  8, 1,  8,  8, 0x20,  32, 0, 0x0000, 0x1C, 0x1F, 0x22, 0x20, 0x3F, 0x23, 0, attiny861},
  
  {0x9387,  8, 0,  8,  8, 0x31,  64, 0, 0x0000, 0x1F, 0x22, 0x22, 0x20, 0x3F, 0x23, 0, attiny87},  
  {0x9487,  8, 0,  8, 16, 0x31,  64, 0, 0x0000, 0x1F, 0x22, 0x22, 0x20, 0x3F, 0x23, 0, attiny167},

  {0x9314,  8, 0,  4,  8, 0x31,  32, 0, 0x0F80, 0x1F, 0x22, 0x3E, 0x2C, 0x3E, 0x00, 0, attiny828},

  {0x9209,  4, 0,  1,  4, 0x31,  32, 0, 0x0000, 0x1F, 0x22, 0x2E, 0x2C, 0x00, 0x23, 0, attiny48},  
  {0x9311,  8, 0,  1,  8, 0x31,  32, 0, 0x0000, 0x1F, 0x22, 0x2E, 0x2C, 0x00, 0x23, 0, attiny88},
  
  {0x9412, 16, 0,  4, 16, 0x2E,  16, 1, 0x0000, 0x1C, 0x00, 0x02, 0x00, 0x0F, 0x00, 0, attiny1634},
  
  {0x9205,  8, 0,  4,  4, 0x31,  32, 0, 0x0000, 0x1F, 0x22, 0x22, 0x20, 0x3F, 0x23, 1, atmega48a},
  {0x920A,  8, 0,  4,  4, 0x31,  32, 0, 0x0000, 0x1F, 0x22, 0x22, 0x20, 0x3F, 0x23, 1, atmega48pa},
  {0x9210,  8, 0,  4,  4, 0x31,  32, 0, 0x0000, 0x1F, 0x22, 0x22, 0x20, 0x3F, 0x23, 1, atmega48pb}, // untested
  {0x930A, 16, 0,  8,  8, 0x31,  32, 0, 0x0F80, 0x1F, 0x22, 0x22, 0x20, 0x3F, 0x23, 1, atmega88a},
  {0x930F, 16, 0,  8,  8, 0x31,  32, 0, 0x0F80, 0x1F, 0x22, 0x22, 0x20, 0x3F, 0x23, 1, atmega88pa},
  {0x9316, 16, 0,  8,  8, 0x31,  32, 0, 0x0F80, 0x1F, 0x22, 0x22, 0x20, 0x3F, 0x23, 1, atmega88pb}, // untested
  {0x9406, 16, 0,  8, 16, 0x31,  64, 0, 0x1F80, 0x1F, 0x22, 0x22, 0x20, 0x3F, 0x23, 1, atmega168a},
  {0x940B, 16, 0,  8, 16, 0x31,  64, 0, 0x1F80, 0x1F, 0x22, 0x22, 0x20, 0x3F, 0x23, 1, atmega168pa},
  {0x9415, 16, 0,  8, 16, 0x31,  64, 0, 0x1F80, 0x1F, 0x22, 0x22, 0x20, 0x3F, 0x23, 1, atmega168pb}, // untested
  {0x9514, 32, 0, 16, 32, 0x31,  64, 0, 0x3F00, 0x1F, 0x22, 0x22, 0x20, 0x3F, 0x23, 1, atmega328},
  {0x950F, 32, 0, 16, 32, 0x31,  64, 0, 0x3F00, 0x1F, 0x22, 0x22, 0x20, 0x3F, 0x23, 1, atmega328p},
  {0x9516, 32, 0, 16, 32, 0x31,  64, 0, 0x3F00, 0x1F, 0x22, 0x22, 0x20, 0x3F, 0x23, 1, atmega328pb},
  
  {0x9389,  8, 0,  8,  8, 0x31,  32, 0, 0x0000, 0x1F, 0x22, 0x22, 0x20, 0x3F, 0x00, 1, atmega8u2},   // untested
  {0x9489,  8, 0,  8, 16, 0x31,  64, 0, 0x0000, 0x1F, 0x22, 0x22, 0x20, 0x3F, 0x00, 1, atmega16u2},  // untested
  {0x958A, 16, 0, 16, 32, 0x31,  64, 0, 0x0000, 0x1F, 0x22, 0x22, 0x20, 0x3F, 0x00, 1, atmega32u2},  // untested

  {0x9484, 16, 0,  8, 16, 0x31,  64, 0, 0x1F00, 0x1F, 0x22, 0x22, 0x20, 0x3F, 0x00, 1, atmega16m1},  // untested
  {0x9586, 32, 0, 16, 32, 0x31,  64, 0, 0x3F00, 0x1F, 0x22, 0x22, 0x20, 0x3F, 0x00, 1, atmega32c1},  // untested
  {0x9584, 32, 0, 16, 32, 0x31,  64, 0, 0x3F00, 0x1F, 0x22, 0x22, 0x20, 0x3F, 0x00, 1, atmega32m1},  // untested
  {0x9686, 64, 0, 32, 64, 0x31, 128, 0, 0x3F00, 0x1F, 0x22, 0x22, 0x20, 0x3F, 0x00, 1, atmega64c1},  // untested
  {0x9684, 64, 0, 32, 64, 0x31, 128, 0, 0x3F00, 0x1F, 0x22, 0x22, 0x20, 0x3F, 0x00, 1, atmega64m1},  // untested

  {0x9382,  8, 0,  8,  8, 0x31,  64, 0, 0x1E00, 0x1F, 0x22, 0x22, 0x20, 0x3F, 0x00, 1, at90usb82},   // untested
  {0x9482,  8, 0,  8, 16, 0x31,  64, 0, 0x3E00, 0x1F, 0x22, 0x22, 0x20, 0x3F, 0x00, 1, at90usb162},  // untested

  {0x9383,  8, 0,  8,  8, 0x31,  32, 0, 0x0F80, 0x1F, 0x22, 0x22, 0x20, 0x3F, 0x00,  1, at90pwm12b3b},// untested 

  {0x9388,  4, 0,  8,  8, 0x31,  32, 0, 0x0F80, 0x1C, 0x1F, 0x22, 0x20, 0x3F, 0x23, 1, at90pwm81},  // untested
  {0x948B, 16, 0,  8, 16, 0x31,  64, 0, 0x1F00, 0x1C, 0x1F, 0x22, 0x20, 0x3F, 0x23, 1, at90pwm161}, // untested

  {0x9483, 16, 0,  8, 16, 0x31,  64, 0, 0x1F00, 0x1F, 0x22, 0x22, 0x20, 0x3F, 0x00, 1, at90pwm216316},  // untested
  {0,      0,  0,  0, 0,  0,     0,  0, 0,      0,    0,    0,    0,    0,    0,   0, 0},
};

const byte maxspeedexp = 4; // corresponds to a factor of 16
const byte speedcmd[] PROGMEM = { 0x83, 0x82, 0x81, 0x80, 0xA0, 0xA1 };
unsigned long speedlimit = SPEEDLIMIT;

enum Fuses { CkDiv8, CkDiv1, CkRc, CkXtal, CkExt, CkSlow, Erase, DWEN };

// some statistics
long timeoutcnt = 0; // counter for DW read timeouts
long flashcnt = 0; // number of flash writes 
#if FREERAM
int freeram = 2048; // minimal amount of free memory (only if enabled)
#define measureRam() freeRamMin()
#else
#define measureRam()
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

// catch undefined/unwantd irqs: should not happen at all
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
      digitalWrite(SYSLED, LOW);
      digitalWrite(DARKSYSLED, LOW);
      pinMode(DARKSYSLED, OUTPUT);
    }
  } else {
    if (cnt < 0) {
      cnt = ontime;
      digitalWrite(SYSLED, HIGH);
      pinMode(DARKSYSLED, INPUT_PULLUP);
    }
  }
  busy--;
}

byte saveTIMSK0;
byte saveUCSR0B;
// block all irqs but the timer1 interrupt necessary
// for receiving bytes over the dw line
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
  Serial.begin(HOSTBPS);
  ledmask = digitalPinToBitMask(SYSLED);
  ledout = portOutputRegister(digitalPinToPort(SYSLED));
  DEBINIT(DEBTX);
  DEBLN(F("\ndw-link V" VERSION));
  TIMSK0 = 0; // no millis interrupts
  pinMode(LEDGND, OUTPUT);
  digitalWrite(LEDGND, LOW);
  pinMode(VSUP, OUTPUT);
  pinMode(IVSUP, OUTPUT);
  power(true); // switch target on
#if SCOPEDEBUG
  pinMode(DEBTX, OUTPUT); //
  digitalWrite(DEBTX, LOW); // PD3 on UNO
#endif
  initSession(); // initialize all critical global variables
  //  DEBLN(F("Now configuereSupply"));
  pinMode(TISP, OUTPUT);
  digitalWrite(TISP, HIGH); // disable outgoing ISP lines
  pinMode(DWLINE, INPUT); // release RESET in order to allow debugWIRE to start up
  DEBLN(F("Setup done"));
  
  // loop
  while (1) {
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
                              // instead of asnychronous load
	targetFlushFlashProg();
      setSysState(CONN_STATE);
    }
  }
}

// init all global vars when the debugger connects
void initSession(void)
{
  //DEBLN(F("initSession"));
  flashidle = true;
  ctx.safestep = true;
  bpcnt = 0;
  bpused = 0;
  hwbp = 0xFFFF;
  lastsignal = 0;
  validpg = false;
  buffill = 0;
  fatalerror = NO_FATAL;
  setSysState(NOTCONN_STATE);
  targetInitRegisters();
}

// report a fatal error and stop everything
// error will be displayed when trying to execute
// if checkio is set to true, we will check whether
// the connection to the target is still there
// if not, the error is not recorded, but the connection is
// marked as not connected
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
  if (fatalerror == NO_FATAL) fatalerror = errnum;
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
    digitalWrite(DARKSYSLED, LOW);
    pinMode(DARKSYSLED, OUTPUT);
  } else if (offtimes[newstate] == 0) {
    digitalWrite(SYSLED, HIGH);
    pinMode(DARKSYSLED, INPUT_PULLUP);
  } else {
    OCR0A = 0x80;
    TIMSK0 |= _BV(OCIE0A);
  }
  DEBPR(F("On-/Offtime: ")); DEBPR(ontime); DEBPR(F(" / ")); DEBLN(offtime);
  DEBPR(F("TIMSK0=")); DEBLNF(TIMSK0,BIN);
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
	 b != '#' && buffill < MAXBUF; b = gdbReadByte()) {
      buf[buffill++] = b;
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
      targetFlushFlashProg();                        /* finalize flash programming before doing something else */
  } else {
    if (*buff == 'X' || *buff == 'M')
      gdbUpdateBreakpoints(true);                    /* remove all BREAKS before writing into flash */
  }
  switch (*buff) {
  case '?':                                          /* last signal */
    gdbSendSignal(lastsignal);
    break;
  case '!':                                          /* Set to extended mode, always OK */
    gdbSendReply("OK");
    break;
  case 'H':                                          /* Set thread, always OK */
    gdbSendReply("OK");
    break;
  case 'T':                                          /* Is thread alive, always OK */
    gdbSendReply("OK");
    break;
  case 'g':                                          /* read registers */
    gdbReadRegisters();
    break;
  case 'G':                                          /* write registers */
    gdbWriteRegisters(buff + 1);
    break;
  case 'm':                                          /* read memory */
    gdbReadMemory(buff + 1);
    break;
  case 'M':                                          /* write memory */
    gdbWriteMemory(buff + 1, false);
    break;
  case 'X':                                          /* write memory from binary data */
    gdbWriteMemory(buff + 1, true); 
    break;
  case 'D':                                          /* detach from target */
    gdbUpdateBreakpoints(true);                      /* remove BREAKS in memory before exit */
    validpg = false;
    fatalerror = NO_FATAL;
    if (gdbStop(false))                              /* disable DW mode */
      gdbSendReply("OK");                            /* and signal that everything is OK */
    break;
  case 'c':                                          /* continue */
  case 'C':                                          /* continue with signal - just ignore signal! */
    s = gdbContinue();                               /* start execution on target at current PC */
    if (s) gdbSendState(s);                          /* if s != 0, it is a signal notifying an error */
                                                     /* otherwise the target is now executing */
    break;
  case 'k':
    gdbUpdateBreakpoints(true);                      /* remove BREAKS in memory before exit */
    gdbStop(false);                                  /* stop DW mode */
    break;
  case 's':                                          /* single step */
  case 'S':                                          /* step with signal - just ignore signal */
    gdbSendState(gdbStep());                         /* do only one step and report reason why stopped */
    break;              
  case 'z':                                          /* remove break/watch point */
  case 'Z':                                          /* insert break/watch point */
    gdbHandleBreakpointCommand(buf);
    break;
  case 'v':                                          /* Run command */
    if (memcmp_P(buf, (void *)PSTR("vRun"), 4) == 0) {
      if (gdbConnect(false)) {                       /* re-enable DW mode reset MCU and clear PC */
	setSysState(CONN_STATE);
	gdbSendState(0);                             /* no signal */
      } 
    } else
	if (memcmp_P(buf, (void *)PSTR("vKill"), 5) == 0) {
      gdbUpdateBreakpoints(true);                    /* remove BREAKS in memory before exit */
      if (gdbStop(false))                            /* stop DW mode */
	gdbSendReply("OK");                          /* and signal that everything is OK */
    } else {
       gdbSendReply("");                             /* not supported */
    }
    break;
  case 'q':                                          /* query requests */
    if (memcmp_P(buf, (void *)PSTR("qRcmd,"),6) == 0)/* monitor command */
	gdbParseMonitorPacket(buf+6);
    else if (memcmp_P(buff, (void *)PSTR("qSupported"), 10) == 0) {
      //DEBLN(F("qSupported"));
	if (ctx.state != CONN_STATE) initSession();  /* init all vars when gdb connects */
	if (gdbConnect(false))                       /* and try to connect */
	  gdbSendPSTR((const char *)PSTR("PacketSize=90")); 
    } else if (memcmp_P(buf, (void *)PSTR("qC"), 2) == 0)      
      gdbSendReply("QC01");                          /* current thread is always 1 */
    else if (memcmp_P(buf, (void *)PSTR("qfThreadInfo"), 12) == 0)
      gdbSendReply("m01");                           /* always 1 thread*/
    else if (memcmp_P(buf, (void *)PSTR("qsThreadInfo"), 12) == 0)
      gdbSendReply("l");                             /* send end of list */
    /* ioreg query does not work!
    else if (memcmp_P(buf, (void *)PSTR("qRavr.io_reg"), 12) == 0) 
      if (mcu.rambase == 0x100) gdbSendReply("e0");
      else gdbSendReply("40");
    */
    else
      gdbSendReply("");  /* not supported */
    break;
  default:
    gdbSendReply("");  /* not supported */
    break;
  }
}

void gdbParseMonitorPacket(const byte *buf)
{
   [[maybe_unused]] int para = 0;

  measureRam();

  gdbUpdateBreakpoints(true);  // update breakpoints in memory before any monitor ops

  int clen = strlen((const char *)buf);
  //DEBPR(F("clen=")); DEBLN(clen);
  //DEBLN((const char *)buf);
  
  
  if (memcmp_P(buf, (void *)PSTR("64776f666600"), max(6,min(12,clen))) == 0)                  
    gdbStop(true);                                                            /* dwo[ff] */
  else if (memcmp_P(buf, (void *)PSTR("6477636f6e6e65637400"), max(6,min(20,clen))) == 0)
    if (gdbConnect(true)) gdbSendReply("OK");                               /* dwc[onnnect] */
    else gdbSendReply("E03");
#if NOMONITORHELP == 0
  else if (memcmp_P(buf, (void *)PSTR("68656c7000"), max(4,min(10,clen))) == 0)                  
    gdbHelp();                                                              /* he[lp] */
#endif
  else if (memcmp_P(buf, (void *)PSTR("6c6173746572726f7200"), max(4,min(20,clen))) == 0)
    gdbReportLastError();                                                  /* la[sterror] */
  else if (memcmp_P(buf, (void *)PSTR("666c617368636f756e7400"), max(6,min(22,clen))) == 0)
    gdbReportFlashCount();                                                  /* fla[shcount] */
  else if (memcmp_P(buf, (void *)PSTR("72616d757361676500"), max(6,min(18,clen))) == 0)
    gdbReportRamUsage();                                                    /* ram[usage] */
  else if (memcmp_P(buf, (void *)PSTR("636b387072657363616c657200"), max(6,min(26,clen))) == 0)
    gdbSetFuses(CkDiv8);                                                    /* ck8[prescaler] */
  else if (memcmp_P(buf, (void *)PSTR("636b317072657363616c657200"), max(6,min(26,clen))) == 0) 
    gdbSetFuses(CkDiv1);                                                    /* ck1[prescaler] */
  else if (memcmp_P(buf, (void *)PSTR("72636f736300"), max(4,min(12,clen))) == 0)
    gdbSetFuses(CkRc);                                                      /* rc[osc] */
  else if (memcmp_P(buf, (void *)PSTR("6578746f736300"), max(4,min(14,clen))) == 0)
    gdbSetFuses(CkExt);                                                     /* ex[tosc] */
  else if (memcmp_P(buf, (void *)PSTR("7874616c6f736300"), max(4,min(16,clen))) == 0)
    gdbSetFuses(CkXtal);                                                     /* xt[alosc] */
  else if (memcmp_P(buf, (void *)PSTR("736c6f776f736300"), max(4,min(16,clen))) == 0)
    gdbSetFuses(CkSlow);                                                     /* sl[owosc] */
  //  else if (memcmp_P(buf, (void *)PSTR("657261736500"), max(4,min(12,clen))) == 0)
  //  gdbSetFuses(Erase);                                                     /*er[ase]*/
  else if (memcmp_P(buf, (void *)PSTR("6877627000"), max(4,min(10,clen))) == 0)
    gdbSetMaxBPs(1);                                                        /* hw[bp] */
  else if (memcmp_P(buf, (void *)PSTR("7377627000"), max(4,min(10,clen))) == 0)
    gdbSetMaxBPs(MAXBREAK);                                                 /* sw[bp] */
  else if (memcmp_P(buf, (void *)PSTR("34627000"), max(2,min(8,clen))) == 0)
    gdbSetMaxBPs(4);                                                        /* 4[bp] */
  else if (memcmp_P(buf, (void *)PSTR("7370"), 4) == 0)
    gdbSetSpeed(buf);                                                       /* sp[eed] [h|l] */
#if UNITDW
  else if  (memcmp_P(buf, (void *)PSTR("746573746477"), 12) == 0)
    DWtests(para);                                                          /* testdw */
#endif
#if UNITTG
  else if  (memcmp_P(buf, (void *)PSTR("746573747467"), 12) == 0)
    targetTests(para);                                                      /* testtg */
#endif
#if UNITGDB
  else if  (memcmp_P(buf, (void *)PSTR("74657374676462"), 14) == 0)
    gdbTests(para);                                                         /* testgdb */
#endif
#if UNITALL
  else if  (memcmp_P(buf, (void *)PSTR("74657374616c6c"), 14) == 0)
    alltests();                                                             /* testall */
#endif
  else if (memcmp_P(buf, (void *)PSTR("736166657374657000"), max(4,min(18,clen))) == 0)
    gdbSetSteppingMode(true);                                               /* safestep */
  else if (memcmp_P(buf, (void *)PSTR("756e736166657374657000"), max(4,min(12,clen))) == 0)
    gdbSetSteppingMode(false);                                              /* unsafestep */
  else if (memcmp_P(buf, (void *)PSTR("76657273696f6e00"), max(4,min(16,clen))) == 0) 
    gdbVersion();                                                           /* version */
  else if (memcmp_P(buf, (void *)PSTR("74696d656f75747300"), max(4,min(18,clen))) == 0)
    gdbTimeoutCounter();                                                    /* timeouts */
  else if (memcmp_P(buf, (void *)PSTR("726573657400"), max(4,min(12,clen))) == 0) {
    if (gdbReset()) gdbSendReply("OK");                                     /* re[set] */
    else gdbSendReply("E09");
  } else gdbSendReply("");
}

// help function (w/o unit tests)
inline void gdbHelp(void) {
  gdbDebugMessagePSTR(PSTR("monitor help         - help function"), -1);
  gdbDebugMessagePSTR(PSTR("monitor dwconnect    - connect to target and show parameters (*)"), -1);
  gdbDebugMessagePSTR(PSTR("monitor dwoff        - disconnect from target and disable DWEN (*)"), -1);
  // gdbDebugMessagePSTR(PSTR("monitor eraseflash   - erase flash memory (*)"), -1);
  gdbDebugMessagePSTR(PSTR("monitor reset        - reset target (*)"), -1);
  gdbDebugMessagePSTR(PSTR("monitor ck1prescaler - unprogram CK8DIV (*)"), -1);
  gdbDebugMessagePSTR(PSTR("monitor ck8prescaler - program CK8DIV (*)"), -1);
  gdbDebugMessagePSTR(PSTR("monitor extosc       - use external clock source (*)"), -1);
  gdbDebugMessagePSTR(PSTR("monitor xtalosc      - use XTAL as clock source (*)"), -1);
  gdbDebugMessagePSTR(PSTR("monitor rcosc        - use internal RC oscillator as clock source (*)"), -1);
  gdbDebugMessagePSTR(PSTR("monitor slosc        - use internal 128kHz oscillator as clock source (*)"), -1);
  gdbDebugMessagePSTR(PSTR("monitor swbp         - allow 32 software breakpoints (default)"), -1);
  gdbDebugMessagePSTR(PSTR("monitor hwbp         - allow only 1 breakpoint, i.e., the hardware bp"), -1);
  gdbDebugMessagePSTR(PSTR("monitor safestep     - prohibit interrupts while single-stepping(default)"), -1);
  gdbDebugMessagePSTR(PSTR("monitor unsafestep   - allow interrupts while single-stepping"), -1);
#if HIGHSPEED
  gdbDebugMessagePSTR(PSTR("monitor speed [h|l]  - speed limit is h (300kbps) (def.) or l (150kbps)"), -1);
#else
  gdbDebugMessagePSTR(PSTR("monitor speed [h|l]  - speed limit is h (300kbps) or l (150kbps) (def.)"), -1);
#endif  
  gdbDebugMessagePSTR(PSTR("monitor flashcount   - number of flash pages written since start"), -1);
  gdbDebugMessagePSTR(PSTR("monitor timeouts     - number of timeouts"), -1);
  gdbDebugMessagePSTR(PSTR("monitor lasterror    - last fatal error"), -1);
  gdbDebugMessagePSTR(PSTR("monitor version      - version number"), -1);
  gdbDebugMessagePSTR(PSTR("All commands with (*) lead to a reset of the target"), -1);
  gdbSendReply("OK");
}

// report last error number
inline void gdbReportLastError(void)
{
  gdbReplyMessagePSTR(PSTR("Last fatal error: "), fatalerror);
}

// return timeout counter
inline void gdbTimeoutCounter(void)
{
  gdbReplyMessagePSTR(PSTR("Number of timeouts: "), timeoutcnt);
}


// set stepping mode
inline void gdbSetSteppingMode(boolean safe)
{
  ctx.safestep = safe;
  if (safe)
    gdbReplyMessagePSTR(PSTR("Single-stepping now interrupt-free"), -1);
  else
    gdbReplyMessagePSTR(PSTR("Single-stepping now interruptible"), -1);
}

// show version
inline void gdbVersion(void)
{
  gdbReplyMessagePSTR(PSTR("dw-link V" VERSION), -1);
}
  
// get DW speed
inline void gdbGetSpeed(void)
{
  gdbReplyMessagePSTR(PSTR("Current debugWIRE bitrate: "), ctx.bps);
}

// set DW communication speed
void gdbSetSpeed(const byte cmd[])
{
  byte arg;
  //DEBLN(F("gdbSetSpeed"));
  byte argix = findArg(cmd);
  if (argix == 0) {
    gdbSendReply("");
    return;
  }
  //DEBPR(F("argix=")); DEBPR(argix); DEBPR(F(" arg=")); DEBPR((char)cmd[argix]); DEBLN((char)cmd[argix+1]);
  if (cmd[argix] == '\0') arg = '\0';
  else arg = (hex2nib(cmd[argix])<<4) + hex2nib(cmd[argix+1]);
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
  doBreak();
  gdbGetSpeed();
  return;
}

byte findArg(const byte cmd[])
{
  byte ix = 4;
  //DEBPR((char)cmd[ix]); DEBLN((char)cmd[ix+1]);
  if (cmd[ix] =='2' && cmd[ix+1] == '0') return ix + 2;
  if (cmd[ix] =='\0') return ix;
  if (cmd[ix] != '6' || cmd[ix+1] != '5')  return 0;
  ix += 2;
  //DEBPR((char)cmd[ix]); DEBLN((char)cmd[ix+1]);
  if (cmd[ix] =='2' && cmd[ix+1] == '0') return ix + 2;
  if (cmd[ix] == '\0') return ix;
  if (cmd[ix] != '6' || cmd[ix+1] != '5')  return 0;
  ix +=2;
  //DEBPR((char)cmd[ix]); DEBLN((char)cmd[ix+1]);
  if (cmd[ix] =='2' && cmd[ix+1] == '0') return ix + 2;
  if (cmd[ix] =='\0') return ix;
  if (cmd[ix] != '6' || cmd[ix+1] != '4')  return 0;
  ix += 2;
  //DEBPR((char)cmd[ix]); DEBLN((char)cmd[ix+1]);
  if (cmd[ix] =='2' && cmd[ix+1] == '0') return ix + 2;
  if (cmd[ix] =='\0') return ix;
  return 0;
}

// "monitor swbp/hwbp/4bp"
// set maximum number of breakpoints
inline void gdbSetMaxBPs(byte num)
{
  maxbreak = num;
  gdbReplyMessagePSTR(PSTR("Maximum number of breakpoints now: "), num);
}

// "monitor flashcount"
// report on how many flash pages have been written
inline void gdbReportFlashCount(void)
{
  gdbReplyMessagePSTR(PSTR("Number of flash write operations: "), flashcnt);
}

// "monitor ramusage"
inline void gdbReportRamUsage(void)
{
#if FREERAM
  gdbReplyMessagePSTR(PSTR("Minimal number of free RAM bytes: "), freeram);
#else
  gdbSendReply("");
#endif
}


// "monitor dwconnect"
// try to enable debugWIRE
// this might imply that the user has to power-cycle the target system
boolean gdbConnect(boolean verbose)
{
  int conncode = -CONNERR_UNKNOWN;

  _delay_ms(100); // allow for startup of MCU initially
  mcu.sig = 0;
  if (targetDWConnect()) {
    conncode = 1;
  } else {
    conncode = targetISPConnect();
    if (conncode == 0) {
      if (powerCycle(verbose)) 
	conncode = 1;
      else
	conncode = -1;
    }
  }
  DEBPR(F("conncode: "));
  DEBLN(conncode);
  if (conncode == 1) {
#if STUCKAT1PC
    mcu.stuckat1byte = (DWgetWPc(false) & ~((mcu.flashsz>>1)-1))>>8;
    //DEBPR(F("stuckat1byte=")); DEBLNF(mcu.stuckat1byte,HEX);
#else
    mcu.stuckat1byte = 0;
    if (DWgetWPc(false) > (mcu.flashsz>>1)) conncode = -4;
#endif
  }
  if (conncode == 1) {
    setSysState(CONN_STATE);
    if (verbose) {
      gdbDebugMessagePSTR(Connected,-2);
      gdbDebugMessagePSTR(PSTR("debugWIRE is enabled, bps: "),ctx.bps);
    }
    gdbCleanupBreakpointTable();
    targetInitRegisters();
    return true;
  }
  if (verbose) {
    switch (conncode) {
    case -1: gdbDebugMessagePSTR(PSTR("Cannot connect: Check wiring"),-1); break;
    case -2: gdbDebugMessagePSTR(PSTR("Cannot connect: Unsupported MCU"),-1); break;
    case -3: gdbDebugMessagePSTR(PSTR("Cannot connect: Lock bits are set"),-1); break;
    case -4: gdbDebugMessagePSTR(PSTR("Cannot connect: PC with stuck-at-one bits"),-1); break;
    default: gdbDebugMessagePSTR(PSTR("Cannot connect for unknown reasons"),-1); conncode = -CONNERR_UNKNOWN; break;
    }
  }
  DEBPR(F("conncode: "));
  DEBLN(conncode);
  if (fatalerror == NO_FATAL) fatalerror = -conncode;
  setSysState(ERROR_STATE);
  if (conncode == -1) dw.enable(false); // otherwise if DWLINE has no pullup, the program goes astray!
  flushInput();
  targetInitRegisters();
  return false;
}

// power-cycle and check periodically whether it is possible
// to establish a debugWIRE connection, return false when we timeout
boolean powerCycle(boolean verbose)
{
  int retry = 0;
  byte b;

  dw.enable(false);
  setSysState(PWRCYC_STATE);
  while (retry < 20) {
    //DEBPR(F("retry=")); DEBLN(retry);
    if (retry%3 == 0) { // try to power-cycle
      //DEBLN(F("Power cycle!"));
      power(false); // cutoff power to target
      _delay_ms(500);
      power(true); // power target again
      _delay_ms(200); // wait for target to startup
      //DEBLN(F("Power cycling done!"));	
    }
    if ((retry++)%3 == 0 && retry >= 3) {
      do {
	if (verbose) {
	  gdbDebugMessagePSTR(PSTR("Please power-cycle target"),-1);
	  b = gdbReadByte();
	} else b ='+';
      } while (b == '-');
    }
    _delay_ms(1000);
    dw.enable(true);
    if (targetDWConnect()) {
      setSysState(CONN_STATE);
      return true;
    }
  }
  return false;
}

void power(boolean on)
{
  //DEBPR(F("Power: ")); DEBLN(on);
  if (on) {
    digitalWrite(VSUP, HIGH);
    digitalWrite(IVSUP, LOW);
  } else { // on=false
    digitalWrite(VSUP, LOW);
    digitalWrite(IVSUP, HIGH);
  }
}

// "monitor dwoff" 
// try to disable the debugWIRE interface on the target system
boolean gdbStop(boolean verbose)
{
#if NOAUTODWOFF
  if (!verbose) return true; // no silent exits from DW mode
#endif
  if (targetStop()) {
    if (verbose) {
      gdbDebugMessagePSTR(Connected,-2);
      gdbReplyMessagePSTR(PSTR("debugWIRE is disabled"),-1);
    }
    setSysState(NOTCONN_STATE);
    return true;
  } else {
    if (verbose) {
      gdbDebugMessagePSTR(PSTR("debugWIRE could NOT be disabled"),-1);
    }
    gdbSendReply("E05");
    return false;
  }
}

// "monitor reset"
// issue reset on target
boolean gdbReset(void)
{
  if (targetOffline()) {
    gdbDebugMessagePSTR(PSTR("Target offline: Cannot reset"), -1);
    return false;
  }
  targetReset();
  targetInitRegisters();
  return true;
}

// "monitor ck1prescaler/ck8prescaler/rcosc/extosc"
void gdbSetFuses(Fuses fuse)
{
  boolean offline = targetOffline();
  int res; 

  setSysState(NOTCONN_STATE);
  res = targetSetFuses(fuse);
  if (res < 0) {
    if (res == -1) gdbDebugMessagePSTR(PSTR("Cannot connect: Check wiring"),-1);
    else if (res == -2) gdbDebugMessagePSTR(PSTR("Unsupported MCU"),-1);
    else if (res == -3) gdbDebugMessagePSTR(PSTR("Fuse programming failed"),-1);
    else if (res == -4) gdbDebugMessagePSTR(PSTR("XTAL not possible"),-1);
    else if (res == -5) gdbDebugMessagePSTR(PSTR("128 kHz osc. not possible"),-1);
    flushInput();
    gdbSendReply("E05");
    return;
  }
  switch (fuse) {
  case CkDiv8: gdbDebugMessagePSTR(PSTR("CKDIV8 fuse is now programmed"),-1); break;
  case CkDiv1: gdbDebugMessagePSTR(PSTR("CKDIV8 fuse is now unprogrammed"),-1); break;
  case CkRc: gdbDebugMessagePSTR(PSTR("Using RC oscillator"),-1); break;
  case CkExt: gdbDebugMessagePSTR(PSTR("Using EXTernal oscillator"),-1); break;
  case CkXtal: gdbDebugMessagePSTR(PSTR("Using XTAL oscillator"),-1); break;
  case CkSlow: gdbDebugMessagePSTR(PSTR("Using 128 kHz oscillator"),-1); break;
  case Erase: gdbDebugMessagePSTR(PSTR("Chip erased"),-1); break;
  default: reportFatalError(WRONG_FUSE_SPEC_FATAL, false); gdbDebugMessagePSTR(PSTR("Fatal Error: Wrong fuse!"),-1); break;
  }
  if (!offline) gdbDebugMessagePSTR(PSTR("Reconnecting ..."),-1);
  _delay_ms(200);
  flushInput();
  if (offline) {
    gdbSendReply("OK");
    return;
  }
  if (!gdbConnect(true))
    gdbSendReply("E02");
  else 
    gdbSendReply("OK");
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
      val = DWreadSramByte(addr);
      ctx.regs[reg] = val;
      ctx.wpc += 2;
    } else if ((opcode & ~0x1F0) == 0x9200) { // sts 
      reg = (opcode & 0x1F0) >> 4;
      //DEBPR(F("Reg="));
      //DEBLN(reg);
      //DEBPR(F("addr="));
      //DEBLNF(addr,HEX);
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
  unsigned int oldpc = ctx.wpc;
  int bpix = gdbFindBreakpoint(ctx.wpc);
  byte sig = SIGTRAP; // SIGTRAP (normal), SIGILL (if ill opcode), SIGABRT (fatal)

  //DEBLN(F("Start step operation"));
  if (fatalerror) return SIGABRT;
  if (targetOffline()) return SIGHUP;
  getInstruction(opcode, arg);
  if (targetIllegalOpcode(opcode)) {
    //DEBPR(F("Illop: ")); DEBLNF(opcode,HEX);
    return SIGILL;
  }
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
      if (oldpc == ctx.wpc) {
	if (Serial.available())
	  sig = SIGINT; // if we do not make progress in single-stepping, ^C (or other inputs) can stop gdb
      }
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
  //DEBLN(F("Start continue operation"));
  if (fatalerror) sig = SIGABRT;
  else if (targetOffline()) sig = SIGHUP;
  else {
    gdbUpdateBreakpoints(false);  // update breakpoints in flash memory
    if (targetIllegalOpcode(targetReadFlashWord(ctx.wpc*2))) {
      //DEBPR(F("Illop: ")); DEBLNF(targetReadFlashWord(ctx.wpc*2),HEX);
      sig = SIGILL;
    }
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
// When the parameter cleanup is true, we also will remove BREAK instructions
// of active breakpoints, because either an exit or a memory write action will
// follow.
//
void gdbUpdateBreakpoints(boolean cleanup)
{
  int i, j, ix, rel = 0;
  unsigned int relevant[MAXBREAK*2+1]; // word addresses of relevant locations in flash
  unsigned int addr = 0;

  measureRam();

  if (!flashidle) {
    reportFatalError(BREAKPOINT_UPDATE_WHILE_FLASH_PROGRAMMING_FATAL, false);
    return;
  }
  
  DEBPR(F("Update Breakpoints (used/active): ")); DEBPR(bpused); DEBPR(F(" / ")); DEBLN(bpcnt);
  // if there are no used entries, we also can return immediately
  // if the target is not connected, we cannot update breakpoints in any case
  if (bpused == 0 || targetOffline()) return;

  // find relevant BPs
  for (i=0; i < MAXBREAK*2; i++) {
    if (bp[i].used) { // only used breakpoints!
      if (bp[i].active) { // active breakpoint
	if (!cleanup) {
	  if (!bp[i].inflash && !bp[i].hw)  // not in flash yet and not a hw bp
	    relevant[rel++] = bp[i].waddr; // remember to be set
	} else { // active BP && cleanup
	  if (bp[i].inflash)              // remove BREAK instruction, but leave it active
	    relevant[rel++] = bp[i].waddr;
	}
      } else { // inactive bp
	if (bp[i].inflash) { // still in flash 
	  relevant[rel++] = bp[i].waddr; // remember to be removed
	} else {
	  bp[i].used = false; // otherwise free BP already now
	  if (bp[i].hw) { // if hwbp, then free HWBP
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
	if (ix < 0) reportFatalError(RELEVANT_BP_NOT_PRESENT, false);
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
  
  // if we try to set too many bps, return
  if (bpcnt == maxbreak) {
    // DEBLN(F("Too many BPs to be set! Execution will fail!"));
    return false;
  }

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
  if (ctx.state == CONN_STATE || ctx.state == RUN_STATE || ctx.state == LOAD_STATE) return false;
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
  if (!targetOffline()) setSysState(CONN_STATE);
  switch (signo) {
  case SIGHUP:
    gdbDebugMessagePSTR(PSTR("Connection to target lost"),-1);
    setSysState(NOTCONN_STATE);
    break;
  case SIGILL:
    gdbDebugMessagePSTR(PSTR("Illegal instruction"),-1);
    break;
  case SIGABRT:
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
boolean targetDWConnect(void)
{
  unsigned int sig;

  if (doBreak()) {
    //DEBLN(F("targetConnect: doBreak done"));
    sig = DWgetChipId();
    if (mcu.sig == 0) setMcuAttr(sig);
    return true;
  }
  return false;
}

// try to establish an ISP connection and program the DWEN fuse
// if possible, set DWEN fuse
//   1 if we are in debugWIRE mode and connected 
//   0 if we need to powercycle
//   -1 if we cannot connect
//   -2 if unknown MCU type
//   -3 if lock bits set
int targetISPConnect(void)
{
  unsigned int sig;
  int result = 0;
  
  if (!enterProgramMode()) return -1;
  sig = ispGetChipId();
  if (sig == 0) { // no reasonable signature
    result = -1;
  } else if (!setMcuAttr(sig)) {
    result = -2;
  } else if (ispLocked()) {
    ispEraseFlash(); // erase flash mem and lock bits
    leaveProgramMode();
    _delay_ms(1000);
    enterProgramMode();
    if (ispLocked()) 
      result = -3;
    else
      result = 0;
  }
  if (result == 0) {
    if (ispProgramFuse(true, mcu.dwenfuse, 0)) { // set DWEN fuse and powercycle later
      result = 0;
    } else {
      result = -1;
    }
  }
  leaveProgramMode();
  DEBPR(F("Programming result: ")); DEBLN(result);
  return result;
}




// disable debugWIRE mode
boolean targetStop(void)
{
  int ret = targetSetFuses(DWEN);
  leaveProgramMode();
  dw.end();
  return (ret == 1);
}


// set the fuses/clear memory, returns
//  1 - if successful
// -1 - if we cannot enter programming mode or sig is not readable
// -2 - if unknown MCU type
// -3 - programming was unsuccessful
// -4 - no XTAL allowed
// -5 - no slow clock

int targetSetFuses(Fuses fuse)
{
  unsigned int sig;
  boolean succ;

  measureRam();
  if (fuse == CkXtal && mcu.xtalosc == 0) return -4; // this chip does not permit an XTAL as the clock source
  if (fuse == CkSlow && mcu.slowosc == 0) return -5; // this chip cannot run with 128 kHz
  if (doBreak()) {
    sendCommand((const byte[]) {0x06}, 1); // leave debugWIRE mode
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
  case CkDiv1: succ = ispProgramFuse(false, mcu.ckdiv8, mcu.ckdiv8); break;
  case CkDiv8: succ = ispProgramFuse(false, mcu.ckdiv8, 0); break;
  case CkRc:   succ = ispProgramFuse(false, mcu.ckmsk, mcu.rcosc); break;
  case CkExt: succ = ispProgramFuse(false, mcu.ckmsk, mcu.extosc); break;
  case CkXtal: succ = ispProgramFuse(false, mcu.ckmsk, mcu.xtalosc); break;
  case CkSlow: succ = ispProgramFuse(false, mcu.ckmsk, mcu.slowosc); break;
  case Erase: succ = ispEraseFlash(); break;
  case DWEN: succ = ispProgramFuse(true, mcu.dwenfuse, mcu.dwenfuse); break;
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
  DWreadSramBytes(addr, mem, len);
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
  // read old page contents (maybe from page cache)
  targetReadFlashPage(addr);
  // check whether something changed
  // DEBPR(F("Check for change: "));
  if (memcmp(newpage, page, mcu.targetpgsz) == 0) {
    DEBLN(F("page unchanged"));
    return;
  }
  // DEBLN(F("changed"));

#if TXODEBUG
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
  boolean dirty = false;
  for (byte i=0; i < mcu.targetpgsz; i++) 
    if (~page[i] & newpage[i]) {
      dirty = true;
      break;
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

  // remember the last programmed page
  memcpy(page, newpage, mcu.targetpgsz);
  validpg = true;
  lastpg = addr;
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

  for (unsigned int i=0; i < len; i++) 
    DWwriteSramByte(addr+i, mem[i]);
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
    sendCommand((const byte []) { 0x61 }, 1);
    DWsetWBp(hwbp);
  } else {
    sendCommand((const byte []) { 0x60 }, 1);
  }
  DWsetWPc(ctx.wpc);
  sendCommand((const byte []) { 0x30 }, 1);
}

// make a single step
void targetStep(void)
{
  measureRam();

  // DEBPR(F("Single step at (byte address):")); DEBLNF(ctx.wpc*2,HEX);
  // _delay_ms(5);
  byte cmd[] = {0x60, 0xD0, (byte)(ctx.wpc>>8), (byte)(ctx.wpc), 0x31};
  sendCommand(cmd, sizeof(cmd));
}

// reset the MCU
boolean targetReset(void)
{
  unsigned long timeout = 100000;
  
  sendCommand((const byte[]) {0x07}, 1);
  // dw.begin(ctx.bps*2); // could be that communication speed is higher after reset!
  _delay_us(10);
  while (digitalRead(DWLINE) && timeout) timeout--;
  _delay_us(1);
  
  ctx.bps = 0; // set to zero in order to force new speed after reset
  //  if (expectBreakAndU()) {
  if (expectUCalibrate()) {
    DEBLN(F("RESET successful"));
    return true;
  } else {
    DEBLN(F("***RESET failed"));
    reportFatalError(RESET_FAILED_FATAL, true);
    return false;
  }
}

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



/****************** debugWIRE specific functions *************/

// send a break on the RESET line, check for response and calibrate 
boolean doBreak () {
  measureRam();

  DEBLN(F("doBreak"));
  pinMode(DWLINE, INPUT);
  _delay_ms(10);
  ctx.bps = 0; // forget about previous connection
  dw.sendBreak(); // send a break
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
  DEBPR(F("Rsync (1): ")); DEBLN(newbps);
  if (newbps < 10) {
    ctx.bps = 0;
    unblockIRQ();
    return false; // too slow
  }
  if ((100*(abs((long)ctx.bps-(long)newbps)))/newbps <= 1)  { // less than 2% deviation -> ignore change
    DEBLN(F("No change: return"));
    unblockIRQ();
    return true;
  }
  dw.begin(newbps);
  for (speed = maxspeedexp; speed > 0; speed--) {
    if ((newbps << speed) <= speedlimit) break;
  }
  DEBPR(F("Set speedexp: ")); DEBLN(speed);
#if CONSTDWSPEED == 0
  DWsetSpeed(speed);
  ctx.bps = dw.calibrate(); // calibrate again
  unblockIRQ();
  DEBPR(F("Rsync (2): ")); DEBLN(ctx.bps);
  if (ctx.bps < 100) {
    DEBLN(F("Second calibration too slow!"));
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
  unsigned long timeout = 100000; // roughly 100-200 msec
  byte cc;
  
  // wait first for a zero byte
  while (!dw.available() && timeout != 0) timeout--;
  if (timeout == 0) {
    DEBLN(F("Timeout in expectBreakAndU"));
    return false;
  }
  if ((cc = dw.read()) != 0) {
    DEBPR(F("expected 0x00, got: 0x")); DEBLNF(cc,HEX);
    return false;
  }
  return expectUCalibrate();
}


// send a command
void sendCommand(const uint8_t *buf, uint8_t len)
{
  measureRam();

  Serial.flush(); // wait until everything has been written in order to avoid interrupts
  dw.write(buf, len);
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
    DEBPR(F("Timeout: received: "));
    DEBPR(idx);
    DEBPR(F(" expected: "));
    DEBLN(expected);
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
  sendCommand(cmdstr, 1);
  response = getResponse(&tmp[0], 2);
  unblockIRQ();
  if (response != 2) reportFatalError(DW_TIMEOUT_FATAL,true);
  return ((unsigned int) tmp[0] << 8) + tmp[1];
}

// set alternative communcation speed
void DWsetSpeed(byte spix)
{
  byte speedcmdstr[1] = { pgm_read_byte(&speedcmd[spix]) };
  DEBPR(F("Send speed cmd: ")); DEBLNF(speedcmdstr[0], HEX);
  sendCommand(speedcmdstr, 1);
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
  byte wrRegs[] = {0x66,                                // read/write
		   0xD0, mcu.stuckat1byte, 0x00,        // start reg
		   0xD1, mcu.stuckat1byte, 0x20,        // end reg
		   0xC2, 0x05,                          // write registers
		   0x20 };                              // go
  measureRam();
  sendCommand(wrRegs,  sizeof(wrRegs));
  sendCommand(regs, 32);
}

// Set register <reg> by building and executing an "in <reg>,DWDR" instruction via the CMD_SET_INSTR register
void DWwriteRegister (byte reg, byte val) {
  byte wrReg[] = {0x64,                                                    // Set up for single step using loaded instruction
                  0xD2, inHigh(mcu.dwdr, reg), inLow(mcu.dwdr, reg), 0x23, // Build "in reg,DWDR" instruction
                  val};                                                    // Write value to register via DWDR
  measureRam();

  sendCommand(wrReg,  sizeof(wrReg));
}

// Read all registers
void DWreadRegisters (byte *regs)
{
  int response;
  byte rdRegs[] = {0x66,
		   0xD0, mcu.stuckat1byte, 0x00, // start reg
		   0xD1, mcu.stuckat1byte, 0x20, // end reg
		   0xC2, 0x01};                  // read registers
  measureRam();
  DWflushInput();
  sendCommand(rdRegs,  sizeof(rdRegs));
  blockIRQ();
  sendCommand((const byte[]) {0x20}, 1);         // Go
  response = getResponse(regs, 32);
  unblockIRQ();
  if (response != 32) reportFatalError(DW_READREG_FATAL,true);
}

// Read register <reg> by building and executing an "out DWDR,<reg>" instruction via the CMD_SET_INSTR register
byte DWreadRegister (byte reg) {
  int response;
  byte res = 0;
  byte rdReg[] = {0x64,                                                 // Set up for single step using loaded instruction
                  0xD2, outHigh(mcu.dwdr, reg), outLow(mcu.dwdr, reg)}; // Build "out DWDR, reg" instruction
  measureRam();
  DWflushInput();
  sendCommand(rdReg,  sizeof(rdReg));
  blockIRQ();
  sendCommand((const byte[]) {0x23}, 1);                                // Go
  response = getResponse(&res,1);
  unblockIRQ();
  if (response != 1) reportFatalError(DW_READREG_FATAL,true);
  return res;
}

// Write one byte to SRAM address space using an SRAM-based value for <addr>, not an I/O address
void DWwriteSramByte (unsigned int addr, byte val) {
  byte wrSram[] = {0x66,                                              // Set up for read/write 
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
  sendCommand(wrSram, sizeof(wrSram));
}

// Write one byte to IO register (via R0)
void DWwriteIOreg (byte ioreg, byte val)
{
  byte wrIOreg[] = {0x64,                                               // Set up for single step using loaded instruction
		    0xD2, inHigh(mcu.dwdr, 0), inLow(mcu.dwdr, 0), 0x23,    // Build "in reg,DWDR" instruction
		    val,                                                // load val into r0
		    0xD2, outHigh(ioreg, 0), outLow(ioreg, 0),          // now store from r0 into ioreg
		    0x23};
  measureRam();
  DWflushInput();
  sendCommand(wrIOreg, sizeof(wrIOreg));
}

// Read one byte from SRAM address space using an SRAM-based value for <addr>, not an I/O address
byte DWreadSramByte (unsigned int addr) {
  byte res = 0;

#if 1
  DWreadSramBytes(addr, &res, 1);
#else
  unsigned int response;
  byte rdSram[] = {0x66,                                              // Set up for read/write 
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
  sendCommand(rdSram, sizeof(rdSram));
  blockIRQ();
  sendCommand((const byte[]) {0x20}, 1);                              // Go
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
  byte rdIOreg[] = {0x64,                                              // Set up for single step using loaded instruction
		    0xD2, inHigh(ioreg, 0), inLow(ioreg, 0),           // Build "out DWDR, reg" instruction
		    0x23,
		    0xD2, outHigh(mcu.dwdr, 0), outLow(mcu.dwdr, 0)};  // Build "out DWDR, 0" instruction
  measureRam();
  DWflushInput();
  sendCommand(rdIOreg, sizeof(rdIOreg));
  blockIRQ();
  sendCommand((const byte[]) {0x23}, 1);                            // Go
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
  byte rdSram[] = {0x66,                                            // Set up for read/write using 
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
  sendCommand(rdSram, sizeof(rdSram));
  blockIRQ();
  sendCommand((const byte[]) {0x20}, 1);                            // Go
  rsp = getResponse(mem, len);
  unblockIRQ();
  if (rsp != len) reportFatalError(SRAM_READ_FATAL,true);
}

//   Read one byte from EEPROM
byte DWreadEepromByte (unsigned int addr) {
  unsigned int response;
  byte retval;
  byte setRegs[] = {0x66,                                                        // Set up for read/write 
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
  sendCommand(setRegs, sizeof(setRegs));
  blockIRQ();
  sendCommand((const byte[]){0x64},1);                                  // Set up for single step using loaded instruction
  if (mcu.eearh)                                                        // if there is a high byte EEAR reg, set it
    sendCommand(doReadH, sizeof(doReadH));
  sendCommand(doRead, sizeof(doRead));                                  // set rest of control regs and query
  sendCommand((const byte[]) {0x23}, 1);                                // Go
  response = getResponse(&retval,1);
  unblockIRQ();
  if (response != 1) reportFatalError(EEPROM_READ_FATAL,true);
  return retval;
}

//   Write one byte to EEPROM
void DWwriteEepromByte (unsigned int addr, byte val) {
  byte setRegs[] = {0x66,                                                         // Set up for read/write 
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
  sendCommand(setRegs, sizeof(setRegs));
  if (mcu.eearh)                                                                  // if there is a high byte EEAR reg, set it
    sendCommand(doWriteH, sizeof(doWriteH));
  sendCommand(doWrite, sizeof(doWrite));
  _delay_ms(5);                                                                   // allow EEPROM write to complete
}


//  Read len bytes from flash memory area at <addr> into mem buffer
void DWreadFlash(unsigned int addr, byte *mem, unsigned int len) {
  unsigned int rsp;
  unsigned int lenx2 = len * 2;
  byte rdFlash[] = {0x66,                                               // Set up for read/write
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
  sendCommand(rdFlash, sizeof(rdFlash));
  blockIRQ();
  sendCommand((const byte[]) {0x20}, 1);                                // Go
  rsp = getResponse(mem, len);                                          // Read len bytes
  unblockIRQ();
  if (rsp != len) reportFatalError(FLASH_READ_FATAL,true);
}

// erase entire flash page
void DWeraseFlashPage(unsigned int addr) {
  byte timeout = 0;
  byte eflash[] = { 0x64, // single stepping
		    0xD2, // load into instr reg
		    outHigh(0x37, 29), // Build "out SPMCSR, r29"
		    outLow(0x37, 29), 
		    0x23,  // execute
		    0xD2, 0x95 , 0xE8 }; // execute SPM
  measureRam();
  DEBPR(F("Erase: "));  DEBLNF(addr,HEX);
  
  while (timeout < TIMEOUTMAX) {
    DWflushInput();
    DWwriteRegister(30, addr & 0xFF); // load Z reg with addr low
    DWwriteRegister(31, addr >> 8  ); // load Z reg with addr high
    DWwriteRegister(29, 0x03); // PGERS value for SPMCSR
    if (mcu.bootaddr) DWsetWPc(mcu.bootaddr); // so that access of all of flash is possible
    sendCommand(eflash, sizeof(eflash));
    sendCommand((const byte[]) {0x33}, 1);
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
  byte eprog[] = { 0x64, // single stepping
		   0xD2, // load into instr reg
		   outHigh(0x37, 29), // Build "out SPMCSR, r29"
		   outLow(0x37, 29), 
		   0x23,  // execute
		   0xD2, 0x95 , 0xE8}; // execute SPM

  DEBLN(F("Program flash page ..."));
  measureRam();
  flashcnt++;
  while (timeout < TIMEOUTMAX) {
    wait = 1000;
    DWflushInput();
    DWwriteRegister(30, addr & 0xFF); // load Z reg with addr low
    DWwriteRegister(31, addr >> 8  ); // load Z reg with addr high
    DWwriteRegister(29, 0x05); //  PGWRT value for SPMCSR
    if (mcu.bootaddr) DWsetWPc(mcu.bootaddr); // so that access of all of flash is possible
    sendCommand(eprog, sizeof(eprog));
    
    sendCommand((const byte[]) {0x33}, 1);
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
  DEBLN(F("...done"));
}

// load bytes into temp memory
void DWloadFlashPageBuffer(unsigned int addr, byte *mem)
{
  byte eload[] = { 0x64, 0xD2,
		   outHigh(0x37, 29),       // Build "out SPMCSR, r29"
		   outLow(0x37, 29),
		   0x23,                    // execute
		   0xD2, 0x95, 0xE8, 0x23, // spm
		   0xD2, 0x96, 0x32, 0x23, // addiw Z,2
  };

  DEBLN(F("Load flash page ..."));
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
    sendCommand(eload, sizeof(eload));
  }
  DEBLN(F("...done"));
}

void DWreenableRWW(void)
{
  unsigned int wait = 10000;
  byte errw[] = { 0x64, 0xD2,
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
    sendCommand(errw, sizeof(errw));
  }
}

byte DWreadSPMCSR(void)
{
  byte sc[] = { 0x64, 0xD2,        // setup for single step and load instr reg 
		inHigh(0x37, 30),  // build "in 30, SPMCSR"
		inLow(0x37, 30),
		0x23 };             // execute
  measureRam();
  DWflushInput();
  sendCommand(sc, sizeof(sc));
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
  DEBPR(F("Set WPCReg=")); DEBLNF(wpcreg,HEX);
  byte cmd[] = {0xD0, (byte)((wpcreg >> 8)+mcu.stuckat1byte), (byte)(wpcreg & 0xFF)};
  sendCommand(cmd, sizeof(cmd));
}

// set hardware breakpoint at word address
void DWsetWBp (unsigned int wbp) {
  byte cmd[] = {0xD1, (byte)((wbp >> 8)+mcu.stuckat1byte), (byte)(wbp & 0xFF)};
  sendCommand(cmd, sizeof(cmd));
}

// execute an instruction offline (can be 2-byte or 4-byte)
void DWexecOffline(unsigned int opcode)
{
  byte cmd[] = {0xD2, (byte) (opcode >> 8), (byte) (opcode&0xFF), 0x23};
  measureRam();

  //DEBPR(F("Offline exec: "));
  DEBLNF(opcode,HEX);
  sendCommand(cmd, sizeof(cmd));
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


void enableSpiPins () {
  DEBLN(F("Eenable SPI ..."));
  pinMode(DWLINE, OUTPUT);
  digitalWrite(DWLINE, LOW);
  DEBLN(F("RESET low"));
  _delay_us(1);
  pinMode(TSCK, OUTPUT);
  digitalWrite(TSCK, LOW);
  pinMode(TMOSI, OUTPUT);
  digitalWrite(TMOSI, HIGH);
  pinMode(TMISO, INPUT);
  digitalWrite(TISP, LOW);
}

void disableSpiPins () {
  digitalWrite(TISP, HIGH);
  pinMode(TSCK, INPUT); 
  pinMode(TMOSI, INPUT);
  pinMode(TMISO, INPUT);
}

byte ispTransfer (byte val) {
  measureRam();
  // ISP frequency is now 12500
  // that should be slow enough even for
  // MCU clk of 128 KHz
  for (byte ii = 0; ii < 8; ++ii) {
    digitalWrite(TMOSI, (val & 0x80) ? HIGH : LOW);
    digitalWrite(TSCK, HIGH);
    _delay_us(200); 
    val = (val << 1) + digitalRead(TMISO);
    digitalWrite(TSCK, LOW);
    _delay_us(200);
  }
  return val;
}

byte ispSend (byte c1, byte c2, byte c3, byte c4, boolean last) {
  byte res;
  ispTransfer(c1);
  ispTransfer(c2);
  res = ispTransfer(c3);
  if (last)
    res = ispTransfer(c4);
  else
    ispTransfer(c4);
  return res;
}


boolean enterProgramMode ()
{
  byte timeout = 5;

  //DEBLN(F("Entering progmode"));
  dw.enable(false);
  do {
    //DEBLN(F("Do ..."));
    enableSpiPins();
    //DEBLN(F("Pins enabled ..."));
    pinMode(DWLINE, INPUT); 
    _delay_us(30);             // short positive RESET pulse of at least 2 clock cycles
    pinMode(DWLINE, OUTPUT);  
    _delay_ms(30);            // wait at least 20 ms before sending enable sequence
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

void leaveProgramMode()
{
  //DEBLN(F("Leaving progmode"));
  disableSpiPins();
  _delay_ms(10);
  pinMode(DWLINE, INPUT); // allow MCU to run or to communicate via debugWIRE
  dw.enable(true);
}
  

// identify chip
unsigned int ispGetChipId ()
{
  unsigned int id;
  if (ispSend(0x30, 0x00, 0x00, 0x00, true) != 0x1E) return 0;
  id = ispSend(0x30, 0x00, 0x01, 0x00, true) << 8;
  id |= ispSend(0x30, 0x00, 0x02, 0x00, true);
  //DEBPR(F("ISP SIG:   "));
  //DEBLNF(id,HEX);
  return id;
}

// program fuse and/or high fuse
boolean ispProgramFuse(boolean high, byte fusemsk, byte fuseval)
{
  byte newfuse;
  byte lowfuse, highfuse, extfuse;
  boolean succ = true;

  lowfuse = ispSend(0x50, 0x00, 0x00, 0x00, true);
  highfuse = ispSend(0x58, 0x08, 0x00, 0x00, true);
  extfuse = ispSend(0x50, 0x08, 0x00, 0x00, true);

  if (high) newfuse = highfuse;
  else newfuse = lowfuse;

  //DEBPR(F("Old ")); if (high) DEBPR(F("high ")); DEBPR(F("fuse: ")); DEBLNF(newfuse,HEX);
  newfuse = (newfuse & ~fusemsk) | (fuseval & fusemsk);
  //DEBPR(F("New ")); if (high) DEBPR(F("high ")); DEBPR(F("fuse: ")); DEBLNF(newfuse,HEX);

  if (high) highfuse = newfuse;
  else lowfuse = newfuse;

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
      DEBLN(F("MCU struct:"));
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
      mcu.rcosc =  pgm_read_byte(&mcu_info[ix].rcosc);
      mcu.extosc =  pgm_read_byte(&mcu_info[ix].extosc);
      mcu.xtalosc =  pgm_read_byte(&mcu_info[ix].xtalosc);
      mcu.slowosc =  pgm_read_byte(&mcu_info[ix].slowosc);
      mcu.avreplus = pgm_read_byte(&mcu_info[ix].avreplus);
      mcu.name = (const char *)pgm_read_word(&mcu_info[ix].name);
      // the remaining fields will be derived 
      mcu.eearl = mcu.eecr + 2;
      mcu.eedr = mcu.eecr + 1;
      // we treat the 4-page erase MCU as if pages were larger by a factor of 4!
      if (mcu.erase4pg) mcu.targetpgsz = mcu.pagesz*4; 
      else mcu.targetpgsz = mcu.pagesz;
      // dwen, chmsk, ckdiv8 are identical for almost all MCUs, so we treat the exceptions here
      mcu.dwenfuse = 0x40;
      mcu.ckmsk = 0x3F;
      mcu.ckdiv8 = 0x80;
      if (mcu.name == attiny13) {
	mcu.ckdiv8 = 0x10;
	mcu.dwenfuse = 0x08;
	mcu.ckmsk = 0x0F; 
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
      DEBPR(F("rco=0x")); DEBLNF(mcu.rcosc,HEX);
      DEBPR(F("ext=0x")); DEBLNF(mcu.extosc,HEX);
      DEBPR(F("xto=0x")); DEBLNF(mcu.xtalosc,HEX);
      DEBPR(F("slo=0x")); DEBLNF(mcu.slowosc,HEX);
      strcpy_P(buf,mcu.name);
      DEBPR(F("nam=")); DEBLN((char*)buf);
      DEBPR(F("ear=0x")); DEBLNF(mcu.eearl,HEX);
      DEBPR(F("eed=0x")); DEBLNF(mcu.eedr,HEX);
      DEBPR(F("tps=")); DEBLN(mcu.targetpgsz);
      DEBPR(F("dwe=0x")); DEBLNF(mcu.dwenfuse,HEX);
      DEBPR(F("ckm=0x")); DEBLNF(mcu.ckmsk,HEX);
      DEBPR(F("ck8=0x")); DEBLNF(mcu.ckdiv8,HEX);
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

#if FREERAM
void freeRamMin(void)
{
  int f = freeRam();
  // DEBPR(F("RAM: ")); DEBLN(f);
  freeram = min(f,freeram);
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
#endif

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
  
  failed += DWtests(testnum);
  failed += targetTests(testnum);
  failed += gdbTests(testnum);

  testSummary(failed);
  gdbSendReply("OK");
}

// give a summary of the test batch
void testSummary(int failed)
{
  if (failed) {
    gdbDebugMessagePSTR(PSTR("\n****Failed tests:"), failed);
    gdbSendReply("E00");
  } else {
    gdbDebugMessagePSTR(PSTR("\nAll tests succeeded"), -1);
    gdbSendReply("OK");
  }
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
  
  setSysState(CONN_STATE);
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
  setSysState(CONN_STATE);
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
  fatalerror = NO_FATAL; setSysState(CONN_STATE);
  targetWriteFlashPage(flashaddr);
  failed += testResult(fatalerror == NO_FATAL && lastflashcnt == flashcnt);
  
  // write same page again (cache valid flag cleared), but since contents is tha same, do not write
  gdbDebugMessagePSTR(PSTR("targetWriteFlashPage (check contents): "), testnum++);
  fatalerror = NO_FATAL; setSysState(CONN_STATE);
  validpg = false;
  targetWriteFlashPage(flashaddr);
  failed += testResult(fatalerror == NO_FATAL && lastflashcnt == flashcnt);

  // try to write a cache page at an address that is not at a page boundary -> fatal error
  gdbDebugMessagePSTR(PSTR("targetWriteFlashPage (addr error): "), testnum++);
  fatalerror = NO_FATAL; setSysState(CONN_STATE);
  targetWriteFlashPage(flashaddr+2);
  failed += testResult(fatalerror != NO_FATAL && lastflashcnt == flashcnt);

  // read page (should be done from cache)
  gdbDebugMessagePSTR(PSTR("targetReadFlashPage (from cache): "), testnum++);
  fatalerror = NO_FATAL; setSysState(CONN_STATE);
  page[0] = 0x11; // mark first cell in order to see whether things get reloaded
  targetReadFlashPage(flashaddr);
  failed += testResult(fatalerror == NO_FATAL && page[0] == 0x11);

  // read page (force cache to be invalid and read from flash)
  gdbDebugMessagePSTR(PSTR("targetReadFlashPage: "), testnum++);
  fatalerror = NO_FATAL; setSysState(CONN_STATE);
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
  if (!expectBreakAndU()) succ = false;
  targetSaveRegisters();
  failed += testResult(succ && ctx.wpc == 0xda && ctx.regs[18] == 0x49);

  gdbDebugMessagePSTR(PSTR("targetStep (rcall): "), testnum++);
  succ = true;
  ctx.wpc = 0xde; // rcall instruction
  targetRestoreRegisters();
  targetStep(); // one step leads to Break+0x55
  if (!expectBreakAndU()) succ = false;  targetSaveRegisters();
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

  gdbDebugMessagePSTR(PSTR("targetIllegalOpcode (mul r16, r16): "), testnum++);
  failed += testResult(targetIllegalOpcode(0x9F00) == !mcu.avreplus);

  gdbDebugMessagePSTR(PSTR("targetIllegalOpcode (jmp ...): "), testnum++);
  failed += testResult(targetIllegalOpcode(0x940C) == (mcu.flashsz <= 8192));
  
  
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
