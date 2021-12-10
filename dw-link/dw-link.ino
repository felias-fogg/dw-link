// This is an implementation of the GDB remote serial protocol for debugWIRE.
// It should run on all ATmega328 boards and provides a hardware debugger
// for the classic ATtinys and some small ATmegas (see below)
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
//
// You can run it on an UNO, a Leonardo, a Mega, a Nano, a Pro Mini, a
// Pro Micro, or a Micro.  For the four latter ones, there exists an
// adapter board, which fits all four of them, using, of course,
// different pin assignments.  For the Nano board, there are
// apparently two different versions around, version 2 and version 3.
// The former one has the A0 pin close to 5V pin, version 3 boards
// have the A0 pin close to the REF pin.  If you use the adapter
// board with a Nano, you need to set the compile time constant
// NANOVERSION, which by defualt is 3.

#define VERSION "1.0.7"

#define INITIALBPS 230400UL // initial expected  commuication speed with the host (115200, 57600, 38400 are alternatives)

#ifndef NANOVERSION
#define NANOVERSION 3
#endif

#if F_CPU < 16000000UL
#error "dw-link needs at least 16 MHz clock frequency"
#endif

#ifndef VARSPEED    // for changing communication speed
#define VARSPEED 0
#endif

#ifndef TXODEBUG        // for debugging the debugger!
#define TXODEBUG    0   
#endif
#ifndef SCOPEDEBUG
#define SCOPEDEBUG 0
#endif
#ifndef FREERAM      // for checking how much memory is left on the stack
#define FREERAM  0   
#endif
#ifndef UNITALL      // test all units
#define UNITALL 0    
#elif UNITALL == 1
#define UNITDW 1
#define UNITTG 1
#define UNITGDB 1
#endif
#ifndef UNITDW
#define UNITDW 0
#endif
#ifndef UNITTG
#define UNITTG 0
#endif
#ifndef UNITGDB
#define UNITGDB 0
#endif



// Pins for different boards
// Note that Nano, Pro Mini, Pro Micro, and Micro use the same socket
// Similarly, UNO, Leonardo and Mega use the same shield
//-----------------------------------------------------------
#if defined(DIRECTISP)   // Binding for a modified ISP plug
#define SCK    13        // SCK  -- directly connected to ISP socket
#define MOSI   11        // MOSI -- directly connected to ISP socket
#define MISO   12        // MISO -- directly connected to ISP socket
#define DWLINE  8        // RESET (needs to be 8 so that we can use it as an input for TIMER1)
#define VSUP    9        // needs to be an extra pin so that we can power-cycle
#define DEBTX   3        // TX line for TXOnlySerial
//#define LEDDDR  DDRB     // DDR of system LED
//#define LEDPORT PORTB    // port register of system LED
//#define LEDPIN  PB5      // pin (=D13)
//-----------------------------------------------------------
#elif defined(ARDUINO_AVR_UNO)  
#define VHIGH   2        // switch, low signals that one should use the 5V supply
#define VON     5        // switch, low signals that dw-probe should deliver the supply charge
#define V5      9        // a low level switches the MOSFET for 5 volt on 
#define V33     7        // a low level switches the MOSFET for 3.3 volt on 
#define VSUP    9        // Vcc - direct supply charge (limit it to 20-30 mA!)
#define SNSGND 14        // If low, then we use a shield
#define DWLINE  8        // RESET (needs to be 8 so that we can use it as an input for TIMER1)
#define SCK    12        // SCK
#define MOSI   10        // MOSI
#define MISO   11        // MISO
#define DEBTX   3        // TX line for TXOnlySerial
#define PROG    6        // if low, signals that one wants to use the ISP programming feature
// System LED = Arduino pin 13
#define LEDDDR  DDRB     // DDR of system LED
#define LEDPORT PORTB    // port register of system LED
#define LEDPIN  PB5      // pin (=D13)
//-----------------------------------------------------------
#elif defined(ARDUINO_AVR_LEONARDO)  
#define VHIGH   2        // switch, low signals that one should use the 5V supply
#define VON     5        // switch, low signals that dw-probe should deliver the supply charge
#define V5      9        // a low level switches the MOSFET for 5 volt on 
#define V33     7        // a low level switches the MOSFET for 3.3 volt on 
#define VSUP    9        // Vcc - direct supply charge (limit it to 20-30 mA!)
#define SNSGND 18        // If low, then we use a shield
#define DWLINE  4        // RESET (needs to be 4 (for Mega32U4) so that we can use it as an input for TIMER1)
#define SCK    12        // SCK
#define MOSI   10        // MOSI
#define MISO   11        // MISO
#define DEBTX   3        // TX line for TXOnlySerial
#define PROG    6        // if low, signals that one wants to use the ISP programming feature
// System LED = Arduino pin 13
#define LEDDDR  DDRC     // DDR of system LED
#define LEDPORT PORTC    // port register of system LED
#define LEDPIN  PC7      // pin (=D13)
//-----------------------------------------------------------
#elif defined(ARDUINO_AVR_MEGA) 
#define VHIGH   2        // switch, low signals that one should use the 5V supply
#define VON     5        // switch, low signals that dw-probe should deliver the supply charge
#define V5      9        // a low level switches the MOSFET for 5 volt on 
#define V33     7        // a low level switches the MOSFET for 3.3 volt on 
#define VSUP    9        // Vcc - direct supply charge (limit it to 20-30 mA!)
#define SNSGND 54        // If low, then we use a shield
#define DWLINE 49        // RESET (needs to be 4 (for Mega32U4) so that we can use it as an input for TIMER1)
#define SCK    12        // SCK
#define MOSI   10        // MOSI
#define MISO   11        // MISO
#define DEBTX   3        // TX line for TXOnlySerial
#define PROG    6        // if low, signals that one wants to use the ISP programming feature
// System LED = Arduino pin 13
#define LEDDDR  DDRB     // DDR of system LED
#define LEDPORT PORTB    // port register of system LED
#define LEDPIN  PB7      // pin (=D13)
//-----------------------------------------------------------
#elif defined(ARDUINO_AVR_NANO)  // on Nano board -- is aligned with Pro Mini and Pro Micro
#define VHIGH   7        // switch, low signals that one should use the 5V supply
#define VON    15        // switch, low signals that dw-probe should deliver the supply charge
#define V33     5        // a low level switches the MOSFET for 3.3 volt on 
#define V5      6        // a low level switches the MOSFET for 5 volt on 
#define VSUP    6        // Vcc - direct supply charge (limit it to 20-30 mA!)
#define SNSGND 11        // If low, then we are on the adapter board
#define DWLINE  8        // RESET (needs to be 8 so that we can use it as an input for TIMER1)
#define SCK     3        // SCK
#define PROG    2        // if low, signals that one wants to use the ISP programming feature
#if NANOVERSION == 3
#define MOSI   16        // MOSI
#define MISO   19        // MISO
#define DEBTX  18        // TX line for TXOnlySerial
#else
#define MOSI   19        // MOSI
#define MISO   16        // MISO
#define DEBTX  17        // TX line for TXOnlySerial
#endif
// System LED = Arduino pin 13 (builtin LED) (pin TX0 on Pro Micro/Mini)
#define LEDDDR  DDRB     // DDR of system LED
#define LEDPORT PORTB    // port register of system LED
#define LEDPIN  PB5      // Arduino pin 13
//-----------------------------------------------------------
#elif defined(ARDUINO_AVR_PRO)  // on a Pro Mini board
#define VHIGH  16        // switch, low signals that one should use the 5V supply
#define VON     2        // switch, low signals tha dw-probe should deliver the supply charge
#define V33    14        // a low level switches the MOSFET for 3.3 volt on 
#define V5     15        // a low level switches the MOSFET for 5 volt on 
#define VSUP   15        // Vcc - direct supply charge (limit it to 20-30 mA!)
#define SNSGND 10        // If low, then we are on the adapter board
#define DWLINE  8        // RESET (needs to be 8 so that we can use it as an input for TIMER1)
#define SCK    12        // SCK
#define MOSI    3        // MOSI
#define MISO    6        // MISO
#define DEBTX   5        // TX line for TXOnlySerial
#define PROG   11        // if low, signals that one wants to use the ISP programming feature
// System LED = Arduino pin 13 (builtin LED) (pin D4 on Nano and D15 on Pro Micro)
//#define LEDDDR  DDRC     // DDR of system LED
//#define LEDPORT PORTC    // port register of system LED
//#define LEDPIN  PC7      // not connected to the outside world!
//-----------------------------------------------------------
#elif defined(ARDUINO_AVR_PROMICRO)  // Pro Micro, i.e., that is a Mega 32U4
#define VHIGH  20        // switch, low signals that one should use the 5V supply
#define VON     2        // switch, low signals tha dw-probe should deliver the supply charge
#define V33    18        // a low level switches the MOSFET for 3.3 volt on 
#define V5     19        // a low level switches the MOSFET for 5 volt on 
#define VSUP   19        // Vcc - direct supply charge (limit it to 20-30 mA!)
#define SNSGND 10        // If low, then we are on the adapter board
#define DWLINE  4        // RESET (needs to be 4 (for Mega32U4) so that we can use it as an input for TIMER1)
#define SCK    14        // SCK
#define MOSI    3        // MOSI
#define MISO    6        // MISO
#define DEBTX   5        // TX line for TXOnlySerial
#define PROG   16        // if low, signals that one wants to use the ISP programming feature
// System LED = Arduino pin 17 (RXLED) (connected to RXI, which is not connected to anything else)
#define LEDDDR  DDRB     // DDR of system LED
#define LEDPORT PORTB    // port register of system LED
#define LEDPIN  PB0      // Arduino pin 17
//-----------------------------------------------------------
#elif defined(ARDUINO_AVR_MICRO)  // Micro, i.e., that is a Mega 32U4
#define VHIGH   7        // switch, low signals that one should use the 5V supply
#define VON    19        // switch, low signals tha dw-probe should deliver the supply charge
#define V33     5        // a low level switches the MOSFET for 3.3 volt on 
#define V5      6        // a low level switches the MOSFET for 5 volt on 
#define VSUP    6        // Vcc - direct supply charge (limit it to 20-30 mA!)
#define SNSGND 11        // If low, then we are on the adapter board
#define DWLINE  4        // RESET (needs to be 4 (for Mega32U4) so that we can use it as an input for TIMER1)
#define SCK     3        // SCK
#define MOSI   20        // MOSI
#define MISO   23        // MISO
#define DEBTX  22        // TX line for TXOnlySerial
#define PROG    2        // if low, signals that one wants to use the ISP programming feature
// System LED = Arduino pin 17 (RXLED) (connected to RXI, which is not connected to anything else)
#define LEDDDR  DDRB     // DDR of system LED
#define LEDPORT PORTB    // port register of system LED
#define LEDPIN  PB0      // Arduino pin 17
//-----------------------------------------------------------#else
#error "Board is not supported yet. dw-link works only on Uno, Leonardo, Mega, Nano, Pro Mini, Micro, and Pro Micro (yet)" 
#endif


#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <util/delay.h>
#include "src/dwSerial.h"
#include "src/SingleWireSerial_config.h"
#ifdef TXODEBUG
#include <TXOnlySerial.h> // only needed for (meta-)debuging
#endif
#include "src/debug.h" // some (meta-)debug macros

// some size restrictions

#define MAXBUF 255
#define MAXBREAK 33 // maximum of active breakpoints (we need double as many entries!)

// communication bit rates 
#define SPEEDHIGH     275000UL // maximum communication speed limit for DW
#define SPEEDNORMAL   137000UL // normal speed limit
#define SPEEDLOW       70000UL // low speed limit (if others is too fast)

#ifdef LINEQUALITY
// maximal rise time for RESET line in clock cycles
#define MAXRISETIME 20 // actually 7-8 cycles are achievable (=500ns),  20 cycles means 1.25us
                       // with weak pullup (50-100k) you have 56 (= 3.5us),
                       // similarly with 1nF, you get 59 (=3.75us)
#define UNCONNRISETIME 60000 // rise time result when unconnected (more than 3.5 ms)
#endif

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
#define CONNERR_WEAK_PULLUP 4 // connection error: weak pullup
#define CONNERR_NO_DW_AFTER_DWEN 5 // connection error: no DW afetr DWEN has been programmed
#define CONNERR_UNKNOWN 6 // unknown connection error
#define NO_FREE_SLOT_FATAL 101 // no free slot in BP structure
#define PACKET_LEN_FATAL 102 // packet length too large
#define WRONG_MEM_FATAL 103 // wrong memory type
#define NEG_SIZE_FATAL 104 // negative size of buffer
#define RESET_FAILED_FATAL 105 // reset failed
#define READ_PAGE_ADDR_FATAL 106 // an address that does not point to start of a page in read operation
#define FLASH_READ_FATAL 107 // error when reading from flash memory
#define SRAM_READ_FATAL 108 //  error when reading from sram memory
#define WRITE_PAGE_ADDR_FATAL 109 // wrong page address when writing
#define ERASE_FAILURE_FATAL 110 // error when erasing flash memory
#define NO_LOAD_FLASH_FATAL 111 // error when loading page into flash buffer
#define PROGRAM_FLASH_FAIL_FATAL 112 // error when programming flash page
#define HWBP_ASSIGNMENT_INCONSISTENT_FATAL 113 // HWBP assignemnt is inconsistent
#define SELF_BLOCKING_FATAL 114 // there shouldn't be a BREAK instruction in the code
#define FLASH_READ_WRONG_ADDR_FATAL 115 // trying to read a flash word at a non-even address
#define NO_STEP_FATAL 116 // could not do a single-step operation
#define RELEVANT_BP_NOT_PRESENT 117 // identified relevant BP not present any longer 
#define INPUT_OVERLFOW_FATAL 118 // input buffer overflow - should not happen at all!

// some masks to interpret memory addresses
#define MEM_SPACE_MASK 0x00FF0000 // mask to detect what memory area is meant
#define FLASH_OFFSET   0x00000000 // flash is addressed starting from 0
#define SRAM_OFFSET    0x00800000 // RAM address from GBD is (real addresss + 0x00800000)
#define EEPROM_OFFSET  0x00810000 // EEPROM address from GBD is (real addresss + 0x00810000)

// instruction codes
#define BREAKCODE 0x9598

// some GDB variables
struct breakpoint
{
  bool used:1;         // bp is in use, i.e., has been set before; will be freed when not activated before next execution
  bool active:1;       // breakpoint is active, i.e., has been set by GDB
  bool inflash:1;      // breakpoint is in flash memory, i.e., BREAK instr has been set in memory
  bool hw:1;           // breakpoint is a hardware breakpoint, i.e., not set in memory, but HWBP is used
  unsigned int waddr;  // word address of breakpoint
  unsigned int opcode; // opcode that has been replaced by BREAK (in little endian mode)
} bp[MAXBREAK*2];

byte bpcnt;             // number of ACTIVE breakpoints (there may be as many as MAXBREAK used ones from the last execution!)
byte bpused;            // number of USED breakpoints, which may not all be active
byte maxbreak = MAXBREAK; // actual number of breakpoints allowed

unsigned int hwbp = 0xFFFF; // the one hardware breakpoint (word address)

enum statetype {NOTCONN_STATE, PWRCYC_STATE, ERROR_STATE, CONN_STATE, RUN_STATE};

struct context {
  unsigned int wpc; // pc (using word addresses)
  unsigned int sp; // stack pointer
  byte sreg;    // status reg
  byte regs[32]; // general purpose regs
  boolean saved:1; // all of the regs have been saved
  statetype state:3; // system state
  boolean von:1; // deliver power to the target
  boolean vhigh:1; // deliver 5 volt instead of 3.3 volt
  boolean snsgnd:1; // true if SNSGDB pin is low
  unsigned long bps; // debugWIRE communication speed
  unsigned long hostbps; // host communication speed
} ctx;

// use LED to signal system state
// LED off = not connected to target system
// LED flashing every second = power-cycle target in order to enable debugWIRE
// LED blinking every 1/10 second = could not connect to target board
// LED constantly on = connected to target and target is halted
// Led blinks every 1/3 second = target is running
const unsigned int ontimes[5] =  {0,  100, 150, 1, 700};
const unsigned int offtimes[5] = {1, 1000, 150, 0, 700};
volatile unsigned int ontime; // number of ms on
volatile unsigned int offtime; // number of ms off

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
  unsigned int ramsz;      // SRAM size
  unsigned int rambase;    // base address of SRAM
  unsigned int eepromsz;   // size of EEPROM
  unsigned int flashsz;    // size of flash memory
  byte         dwdr;       // address of DWDR register
  unsigned int pagesz;     // page size of flash memory
  boolean      erase4pg;   // 1 when the MCU has a 4-page erase operation
  unsigned int bootaddr;   // highest address of possible boot section  (0 if no boot support)
  byte         eecr;       // address of EECR register
  byte         eearh;      // address of EARL register (0 if none)
  byte         rcosc;      // fuse pattern for setting RC osc as clock source
  byte         extosc;     // fuse pattern for setting EXTernal osc as clock source
  byte         xtalosc;    // fuse pattern for setting XTAL osc as clock source
  const char*  name;       // pointer to name in PROGMEM
  byte         dwenfuse;   // bit mask for DWEN fuse in high fuse byte
  byte         ckdiv8;     // bit mask for CKDIV8 fuse in low fuse byte
  byte         ckmsk;      // bit mask for selecting clock source (and startup time)
  byte         eedr;       // address of EEDR (computed from EECR)
  byte         eearl;      // address of EARL (computed from EECR)
  unsigned int targetpgsz; // target page size (depends on pagesize and erase4pg)
} mcu;


struct mcu_info_type {
  unsigned int sig;            // two byte signature
  byte         ramsz_div_64;   // SRAM size
  boolean      rambase_low;    // base address of SRAM; low: 0x60, high: 0x100
  byte         eepromsz_div_64;// size of EEPROM
  byte         flashsz_div_1k; // size of flash memory
  byte         dwdr;           // address of DWDR register
  byte         pagesz_div_2;   // page size of flash memory
  boolean      erase4pg;       // 1 when the MCU has a 4-page erase operation
  unsigned int bootaddr;       // highest address of possible boot section  (0 if no boot support)
  byte         eecr;           // address of EECR register
  byte         eearh;          // address of EARL register (0 if none)
  byte         rcosc;          // fuse pattern for setting RC osc as clock source
  byte         extosc;         // fuse pattern for setting EXTernal osc as clock source
  byte         xtalosc;        // fuse pattern for setting XTAL osc as clock source
  boolean      avreplus;       // AVRe+ architecture
  const char*  name;           // pointer to name in PROGMEM
};

// mcu infos (for all AVR mcus supporting debugWIRE)
// untested ones are marked by a star
const mcu_info_type mcu_info[] PROGMEM = {
  // sig sram low eep flsh dwdr  pg er4 boot    eecr eearh rcosc extosc xtosc plus name
  {0x9007,  1, 1,  1,  1, 0x2E,  16, 0, 0x0000, 0x1C, 0x00, 0x0A, 0x08, 0x00, 0, attiny13},

  {0x910A,  2, 1,  2,  2, 0x1f,  16, 0, 0x0000, 0x1C, 0x00, 0x24, 0x20, 0x3F, 0, attiny2313},
  {0x920D,  4, 1,  4,  4, 0x27,  32, 0, 0x0000, 0x1C, 0x00, 0x24, 0x20, 0x3F, 0, attiny4313},

  {0x920C,  4, 1,  1,  4, 0x27,  32, 0, 0x0000, 0x1C, 0x00, 0x22, 0x20, 0x3F, 0, attiny43},

  {0x910B,  2, 1,  2,  2, 0x27,  16, 0, 0x0000, 0x1C, 0x1F, 0x22, 0x20, 0x3F, 0, attiny24},   
  {0x9207,  4, 1,  4,  4, 0x27,  32, 0, 0x0000, 0x1C, 0x1F, 0x22, 0x20, 0x3F, 0, attiny44},
  {0x930C,  8, 1,  8,  8, 0x27,  32, 0, 0x0000, 0x1C, 0x1F, 0x22, 0x20, 0x3F, 0, attiny84},
  
  {0x9215,  4, 0,  4,  4, 0x27,   8, 1, 0x0000, 0x1C, 0x1F, 0x22, 0x20, 0x3F, 0, attiny441}, //*
  {0x9315,  8, 0,  8,  8, 0x27,   8, 1, 0x0000, 0x1C, 0x1F, 0x22, 0x20, 0x3F, 0, attiny841},
  
  {0x9108,  2, 1,  2,  2, 0x22,  16, 0, 0x0000, 0x1C, 0x1F, 0x22, 0x20, 0x3F, 0, attiny25},
  {0x9206,  4, 1,  4,  4, 0x22,  32, 0, 0x0000, 0x1C, 0x1F, 0x22, 0x20, 0x3F, 0, attiny45},
  {0x930B,  8, 1,  8,  8, 0x22,  32, 0, 0x0000, 0x1C, 0x1F, 0x22, 0x20, 0x3F, 0, attiny85},
  
  {0x910C,  2, 1,  2,  2, 0x20,  16, 0, 0x0000, 0x1C, 0x1F, 0x22, 0x20, 0x3F, 0, attiny261},
  {0x9208,  4, 1,  4,  4, 0x20,  32, 0, 0x0000, 0x1C, 0x1F, 0x22, 0x20, 0x3F, 0, attiny461},
  {0x930D,  8, 1,  8,  8, 0x20,  32, 0, 0x0000, 0x1C, 0x1F, 0x22, 0x20, 0x3F, 0, attiny861},
  
  {0x9387,  8, 0,  8,  8, 0x31,  64, 0, 0x0000, 0x1F, 0x22, 0x22, 0x20, 0x3F, 0, attiny87},  //*
  {0x9487,  8, 0,  8, 16, 0x31,  64, 0, 0x0000, 0x1F, 0x22, 0x22, 0x20, 0x3F, 0, attiny167},

  {0x9314,  8, 0,  4,  8, 0x31,  32, 0, 0x0F80, 0x1F, 0x22, 0x3E, 0x2C, 0x3E, 0, attiny828},

  {0x9209,  4, 0,  1,  4, 0x31,  32, 0, 0x0000, 0x1F, 0x22, 0x2E, 0x2C, 0x00, 0, attiny48},  //*
  {0x9311,  8, 0,  1,  8, 0x31,  32, 0, 0x0000, 0x1F, 0x22, 0x2E, 0x2C, 0x00, 0, attiny88},
  
  {0x9412, 16, 0,  4, 16, 0x2E,  16, 1, 0x0000, 0x1C, 0x00, 0x22, 0x20, 0x2F, 0, attiny1634},
  
  {0x9205,  8, 0,  4,  4, 0x31,  32, 0, 0x0000, 0x1F, 0x22, 0x22, 0x20, 0x3F, 1, atmega48a},
  {0x920A,  8, 0,  4,  4, 0x31,  32, 0, 0x0000, 0x1F, 0x22, 0x22, 0x20, 0x3F, 1, atmega48pa},
  {0x9210,  8, 0,  4,  4, 0x31,  32, 0, 0x0000, 0x1F, 0x22, 0x22, 0x20, 0x3F, 1, atmega48pb}, //*
  {0x930A, 16, 0,  8,  8, 0x31,  32, 0, 0x0F80, 0x1F, 0x22, 0x22, 0x20, 0x3F, 1, atmega88a},
  {0x930F, 16, 0,  8,  8, 0x31,  32, 0, 0x0F80, 0x1F, 0x22, 0x22, 0x20, 0x3F, 1, atmega88pa},
  {0x9316, 16, 0,  8,  8, 0x31,  32, 0, 0x0F80, 0x1F, 0x22, 0x22, 0x20, 0x3F, 1, atmega88pb}, //*
  {0x9406, 16, 0,  8, 16, 0x31,  64, 0, 0x1F80, 0x1F, 0x22, 0x22, 0x20, 0x3F, 1, atmega168a},
  {0x940B, 16, 0,  8, 16, 0x31,  64, 0, 0x1F80, 0x1F, 0x22, 0x22, 0x20, 0x3F, 1, atmega168pa},
  {0x9415, 16, 0,  8, 16, 0x31,  64, 0, 0x1F80, 0x1F, 0x22, 0x22, 0x20, 0x3F, 1, atmega168pb}, //*
  {0x9514, 32, 0, 16, 32, 0x31,  64, 0, 0x3F00, 0x1F, 0x22, 0x22, 0x20, 0x3F, 1, atmega328},
  {0x950F, 32, 0, 16, 32, 0x31,  64, 0, 0x3F00, 0x1F, 0x22, 0x22, 0x20, 0x3F, 1, atmega328p},
  {0x9516, 32, 0, 16, 32, 0x31,  64, 0, 0x3F00, 0x1F, 0x22, 0x22, 0x20, 0x3F, 1, atmega328pb}, //*
  
  {0x9389,  8, 0,  8,  8, 0x31,  32, 0, 0x0000, 0x1F, 0x22, 0x22, 0x20, 0x3F, 1, atmega8u2},   //*
  {0x9489,  8, 0,  8, 16, 0x31,  64, 0, 0x0000, 0x1F, 0x22, 0x22, 0x20, 0x3F, 1, atmega16u2},  //*
  {0x958A, 16, 0, 16, 32, 0x31,  64, 0, 0x0000, 0x1F, 0x22, 0x22, 0x20, 0x3F, 1, atmega32u2},  //*

  {0x9484, 16, 0,  8, 16, 0x31,  64, 0, 0x1F00, 0x1F, 0x22, 0x22, 0x20, 0x3F, 1, atmega16m1},  //*
  {0x9586, 32, 0, 16, 32, 0x31,  64, 0, 0x3F00, 0x1F, 0x22, 0x22, 0x20, 0x3F, 1, atmega32c1},  //*
  {0x9584, 32, 0, 16, 32, 0x31,  64, 0, 0x3F00, 0x1F, 0x22, 0x22, 0x20, 0x3F, 1, atmega32m1},  //*
  //  {0x9686, 64, 0, 32, 64, 0x31, 128, 0, 0x3F00, 0x1F, 0x22, 0x22, 0x20, 0x3F, 1, atmega64c1},  //*
  //  {0x9684, 64, 0, 32, 64, 0x31, 128, 0, 0x3F00, 0x1F, 0x22, 0x22, 0x20, 0x3F, 1, atmega64m1},  //*

  {0x9382,  8, 0,  8,  8, 0x31,  64, 0, 0x1E00, 0x1F, 0x22, 0x22, 0x20, 0x3F, 1, at90usb82},   //*
  {0x9482,  8, 0,  8, 16, 0x31,  64, 0, 0x3E00, 0x1F, 0x22, 0x22, 0x20, 0x3F, 1, at90usb162},  //*

  {0x9383,  8, 0,  8,  8, 0x31,  32, 0, 0x0F80, 0x1F, 0x22, 0x22, 0x20, 0x3F, 1, at90pwm12b3b},//* 

  {0x9388,  4, 0,  8,  8, 0x31,  32, 0, 0x0F80, 0x1C, 0x1F, 0x22, 0x20, 0x3F, 1, at90pwm81},  //*
  {0x948B, 16, 0,  8, 16, 0x31,  64, 0, 0x1F00, 0x1C, 0x1F, 0x22, 0x20, 0x3F, 1, at90pwm161}, //*

  {0x9483, 16, 0,  8, 16, 0x31,  64, 0, 0x1F00, 0x1F, 0x22, 0x22, 0x20, 0x3F, 1, at90pwm216316},  //*
  {0},
};

const byte maxspeedexp = 4; // corresponds to a factor of 16
const byte speedcmd[] PROGMEM = { 0x83, 0x82, 0x81, 0x80, 0xA0, 0xA1 };
unsigned long speedlimit = SPEEDHIGH;

enum Fuses { CkDiv8, CkDiv1, CkRc, CkXtal, CkExt, Erase, DWEN };

const int maxbpsix = 5;
const unsigned long rsp_bps[] = { 230400, 115200, 57600, 38400, 19200, 9600 };

// some statistics
long flashcnt = 0; // number of flash writes 
#if FREERAM
int freeram = 2048; // minimal amount of free memory (only if enabled)
#define measureRam() freeRamMin()
#else
#define measureRam()
#endif

// communcation interface to target
dwSerial      dw;
char          rpt[16];                // Repeat command buffer
byte          lastsignal;

// communication and memory buffer
byte membuf[256]; // used for storing sram, flash, and eeprom values
byte newpage[128]; // one page of flash to program
byte page[128]; // cached page contents - never overwrite it in the program! 
unsigned int lastpg; // address of the cached page
boolean validpg = false; // if cached page contents is valid
byte buf[MAXBUF+1]; // for gdb i/o
int buffill; // how much of the buffer is filled up
byte fatalerror = NO_FATAL;

DEBDECLARE();

/****************** Interrupt blink routine *********************/

#ifdef LEDPIN // is only used if there is a LEDPIN designed
ISR(TIMER0_COMPA_vect, ISR_NOBLOCK)
{
  // the ISR can be interrupted at any point by itself, the only problem
  // may be that a call is not counted, which should not happen too
  // often and is uncritical; there is no danger of data corruption
  // of the cnt variable because any interrupt while assigning
  // a new value to cnt will return immediately
  static int cnt = 0;
  static byte busy = 0;

  if (busy) return;
  busy++; // if this IRQ routine is already active, leave immediately
  cnt--;
  if (LEDPORT & _BV(LEDPIN)) {
    if (cnt < 0) {
      cnt = offtime;
      LEDPORT &= ~_BV(LEDPIN);
    }
  } else {
    if (cnt < 0) {
      cnt = ontime;
      LEDPORT |= _BV(LEDPIN);
    }
  }
  busy--;
}
#endif

/******************* setup & loop ******************************/
void setup(void) {
  DEBINIT(); 
  DEBLN(F("dw-link V" VERSION));
  TIMSK0 = 0; // no millis interrupts
  Serial.begin(INITIALBPS);
  ctx.hostbps = INITIALBPS;
  while (!Serial); // wait for serial port to connect (only needed for native USB ports)
  ctx.von = false;
  ctx.vhigh = false;
  ctx.snsgnd = false;
  
#ifdef LEDDDR
  LEDDDR |= LEDPIN; // switch on output for system LED
#endif
#ifdef VON
  pinMode(VON, INPUT_PULLUP); // configure Von as input from switch
#endif
#ifdef VHIGH
  pinMode(VHIGH, INPUT_PULLUP); // configure Vhigh as input from switch
#endif
#ifdef SNSGND
  pinMode(SNSGND, INPUT_PULLUP);
#endif
#if SCOPEDEBUG
  DDRC = 0xFF;
#endif
  initSession(); // initialize all critical global variables
  configureSupply(); // configure suppy already here
  pinMode(DWLINE, INPUT); // release RESET in order to allow debugWIRE to start up
  detectRSPCommSpeed(); // check for coummication speed and select the right one
}

void loop(void) {
  configureSupply();
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

/****************** system state routines ***********************/

// find out communication speed of host
// try to identify a qSupported packet
//   if found, we are done: send '-' so that it is retransmitted
//   if not: vary the speed and send '-' to provoke resending
//           if one gets a response, send it again through the filter
void detectRSPCommSpeed(void) {
  int initix, ix;
  int timeout;

  initix = -1;
  for (ix = 0; ix <= maxbpsix; ix++)
    if (rsp_bps[ix] == INITIALBPS) initix = ix;

  while (1) {
    if (!Serial.available()) continue;
    ix = maxbpsix + 1;
    if (rightSpeed()) { // already found right speed
      Serial.print("-"); // ask for retransmission
      DEBLN(F("Initial guess right"));
      return; 
    }
    // Now send "-" in all possible speeds and wait for response
    ix = maxbpsix + 1;
    while (ix > 0) {
      ix--;
      if (rsp_bps[ix] == INITIALBPS) continue; // do not try initial speed again
      DEBPR(F("Try bps:")); DEBLN(rsp_bps[ix]);
      Serial.begin(rsp_bps[ix]);
      Serial.print("-");  // ask for retransmission
      timeout = 2000;
      while (!Serial.available() && timeout--);
      if (timeout == 0) continue; // try different speed
      if (rightSpeed()) { // should be right one - check!
	ctx.hostbps = rsp_bps[ix];
	Serial.print("-");  // ask for retransmission
	return;
      }
    }
    Serial.begin(INITIALBPS); // set to initial guess and wait again
  }
}
  
// try to find "qSupported" in a stream
// in any case, wait until no more characters are sent
boolean rightSpeed(void)
{
  char keyseq[] = "qSupported:";
  int ix = 0;
  const int maxix = sizeof(keyseq) - 1;
  int timeout;
  char c;

  measureRam();
  timeout = 2000;
  while (timeout-- && ix < maxix) 
    if (Serial.available()) {
      timeout = 2000;
      c = Serial.read();
      if (c == keyseq[ix]) ix++;
      else
	if (c == keyseq[0]) ix = 1;
	else ix = 0;
    }
  // now read the remaining chars
  timeout = 2000;
  while (timeout--)
    if (Serial.available()) {
      timeout = 2000;
      Serial.read();
    }
  return (ix == maxix); // entire sequence found
}

// configure supply lines according to switch setting
void configureSupply(void)
{
#ifdef SNSGND
  ctx.snsgnd = !digitalRead(SNSGND);
#endif
  if (!ctx.snsgnd) {
    pinMode(VSUP, OUTPUT);
    digitalWrite(VSUP, HIGH);
    ctx.von = false;
    return; 
  } else {
#if defined(VHIGH) && defined(VON) && defined(V33) && defined (V5)
    if (ctx.vhigh != !digitalRead(VHIGH) || ctx.von != !digitalRead(VON)) { // something changed
      _delay_ms(30); // debounce
      DEBLN(F("Some change"));
      DEBPR(F("VHIGH: ")); DEBPR(ctx.vhigh); DEBPR(F(" -> ")); DEBLN(!digitalRead(VHIGH));
      DEBPR(F("VON:   ")); DEBPR(ctx.von); DEBPR(F(" -> ")); DEBLN(!digitalRead(VON));
      ctx.vhigh = !digitalRead(VHIGH);
      ctx.von = !digitalRead(VON);
      pinMode(V33, INPUT); // switch off both supply lines
      pinMode(V5, INPUT);
      if (ctx.von) { // if on, switch on the right MOSFET
	if (ctx.vhigh) pinMode(V5, OUTPUT); // switch on 5V MOSFET
	else pinMode(V33, OUTPUT); // switch on 3.3 V MOSFET
      }
    }
#endif
  }
}

// init all global vars when the debugger connects
void initSession(void)
{
  DEBLN(F("initSession"));
  bpcnt = 0;
  bpused = 0;
  hwbp = 0xFFFF;
  lastsignal = SIGTRAP;
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
// if not, the rror is not recorded, but the conenction is
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
  DEBPR(F("***Report fatal error: "));
  DEBLN(errnum);
  if (fatalerror == NO_FATAL) fatalerror = errnum;
  setSysState(ERROR_STATE);
}

// change system state
// switch on blink IRQ when run, error, or power-cycle state
void setSysState(statetype newstate)
{
  DEBPR(F("setSysState: ")); DEBLN(newstate);
  if (ctx.state == ERROR_STATE && fatalerror) return;
  TIMSK0 &= ~_BV(OCIE0A); // switch off!
  ctx.state = newstate;
  ontime = ontimes[newstate];
  offtime = offtimes[newstate];
#ifdef LEDDDR
  LEDDDR |= _BV(LEDPIN);
  if (ontimes[newstate] == 0) LEDPORT &= ~_BV(LEDPIN);
  else if (offtimes[newstate] == 0) LEDPORT |= _BV(LEDPIN);
  else {
    OCR0A = 0x80;
    TIMSK0 |= _BV(OCIE0A);
  }
#endif
  DEBPR(F("On-/Offtime: ")); DEBPR(ontime); DEBPR(F(" / ")); DEBLN(offtime);
  DEBPR(F("TIMSK0=")); DEBLNF(TIMSK0,BIN);
}

/****************** gdbserver routines **************************/


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
	DEBLN(F("Connection lost"));
      }
    }
    break;
    
  default:
    gdbSendReply(""); /* not supported */
    break;
  }
}

// parse packet and perhaps start executing
void gdbParsePacket(const byte *buff)
{
  byte s;

  DEBPR(F("gdb packet: ")); DEBLN((char)*buff);
  switch (*buff) {
  case '?':                                          /* last signal */
    gdbSendSignal(lastsignal);  
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
    gdbUpdateBreakpoints(true);                      /* remove all BREAKS beforehand! */
    gdbWriteMemory(buff + 1);
    break;
  case 'X':                                          /* write memory from binary data */
    gdbUpdateBreakpoints(true);                      /* remove all BREAKS before writing into flash */
    gdbWriteBinMemory(buff + 1); 
    break;
  case 'D':                                          /* detach the debugger */
    gdbUpdateBreakpoints(true);                      /* remove BREAKS in memory before exit */
    validpg = false;
    fatalerror = NO_FATAL;
    targetContinue();                                /* let the target machine do what it wants to do! */
    setSysState(NOTCONN_STATE);                      /* set to unconnected state */
    gdbSendReply("OK");                              /* and signal that everything is OK */
    break;
  case 'k':                                          /* kill request */
    gdbUpdateBreakpoints(true);                      /* remove BREAKS in memory before exit! */
    break;
  case 'c':                                          /* continue */
  case 'C':                                          /* continue with signal - just ignore signal! */
    s = gdbContinue();                               /* start execution on target at current PC */
    if (s) gdbSendState(s);                          /* if s != 0, it is a signal notifying an error */
                                                     /* otherwise the target is now executing */
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
      gdbReset();                                    /* reset MCU and initialize registers */
      gdbSendState(SIGTRAP);                         /* stop at start address (= 0x000) */
                                                     /* GDB will auto restart! */
    } else {
       gdbSendReply("");                             /* not supported */
    }
    break;
  case 'q':                                          /* query requests */
    if (memcmp_P(buf, (void *)PSTR("qRcmd,"), 6) == 0)   /* monitor command */
	gdbParseMonitorPacket(buf+6);
    else if (memcmp_P(buff, (void *)PSTR("qSupported"), 10) == 0) {
        DEBLN(F("qSupported"));
	initSession();                               /* init all vars when gdb (re-)connects */
	gdbConnect(false);                           /* and try to connect */
	gdbSendPSTR((const char *)PSTR("PacketSize=FF")); 
    } else if (memcmp_P(buf, (void *)PSTR("qC"), 2) == 0)
      /* current thread is always 1 */
      gdbSendReply("QC01");
    else if (memcmp_P(buf, (void *)PSTR("qfThreadInfo"), 12) == 0)
      /* always 1 thread*/
      gdbSendReply("m01");
    else if (memcmp_P(buf, (void *)PSTR("qsThreadInfo"), 12) == 0)
      /* send end of list */
      gdbSendReply("l");
    /* ioreg query does not work!
    else if (memcmp_P(buf, (void *)PSTR("qRavr.io_reg"), 12) == 0) 
      if (mcu.rambase == 0x100) gdbSendReply("e0");
      else gdbSendReply("40");
    else
    */
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
  //DEBPR(F("clen=")); DEBLN(slen);
  
  if (memcmp_P(buf, (void *)PSTR("64776f666600"), max(6,min(12,clen))) == 0)                  
    gdbStop();                                                              /* dwo[ff] */
  else if (memcmp_P(buf, (void *)PSTR("6477636f6e6e65637400"), max(6,min(20,clen))) == 0)
    if (gdbConnect(true)) gdbSendReply("OK");                               /* dwc[onnnect] */
    else gdbSendReply("E03");
  else if (memcmp_P(buf, (void *)PSTR("73657269616c00"), max(6,min(14,clen))) == 0)
    gdbReportRSPbps();
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
  else if (memcmp_P(buf, (void *)PSTR("6572617365666c61736800"), max(4,min(22,clen))) == 0)
    gdbSetFuses(Erase);                                                     /*er[aseflash]*/
  else if (memcmp_P(buf, (void *)PSTR("6877627000"), max(4,min(10,clen))) == 0)
    gdbSetMaxBPs(1);                                                        /* hw[bp] */
  else if (memcmp_P(buf, (void *)PSTR("7377627000"), max(4,min(10,clen))) == 0)
    gdbSetMaxBPs(MAXBREAK);                                                 /* sw[bp] */
  else if (memcmp_P(buf, (void *)PSTR("34627000"), max(2,min(8,clen))) == 0)
    gdbSetMaxBPs(4);                                                        /* 4[bp] */
  else if (memcmp_P(buf, (void *)PSTR("7370"), 2) == 0)
    gdbSetSpeed(buf);
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
  else if (memcmp_P(buf, (void *)PSTR("726573657400"), max(4,min(12,clen))) == 0) {
    if (gdbReset()) gdbSendReply("OK");                                     /* re[set] */
    else gdbSendReply("E09");
  } else gdbSendReply("");
}

// show connectio speed to host
void gdbReportRSPbps(void)
{
  gdbDebugMessagePSTR(PSTR("Current bitrate of serial connection to host: "), ctx.hostbps);
  _delay_ms(5);
  //  flushInput();
  gdbSendReply("OK");
}

// get DW speed
void gdbGetSpeed(void)
{
  gdbDebugMessagePSTR(PSTR("Current debugWIRE bitrate: "), ctx.bps);
  _delay_ms(5);
  //  flushInput();
  gdbSendReply("OK");
}

// set DW communication speed
void gdbSetSpeed(const byte cmd[])
{
  unsigned int newexp;
  byte arg;
  DEBLN(F("gdbSetSpeed"));
  byte argix = findArg(cmd);
  if (argix == 0) {
    gdbSendReply("");
    return;
  }
  DEBPR(F("argix=")); DEBPR(argix); DEBPR(F(" arg=")); DEBPR((char)cmd[argix]); DEBLN((char)cmd[argix+1]);
  if (cmd[argix] == '\0') arg = '\0';
  else arg = (hex2nib(cmd[argix])<<4) + hex2nib(cmd[argix+1]);
  switch (arg) {
  case 'h': speedlimit = SPEEDHIGH;
    break;
  case 'n': speedlimit = SPEEDNORMAL;
    break;
  case 'l': speedlimit = SPEEDLOW;
    break;
  case '\0': gdbGetSpeed();
    return;
  default:
    gdbSendReply("");
  }
  doBreak();
  gdbGetSpeed();
  return;
}

byte findArg(const byte cmd[])
{
  byte ix = 4;
  DEBPR((char)cmd[ix]); DEBLN((char)cmd[ix+1]);
  if (cmd[ix] =='2' && cmd[ix+1] == '0') return ix + 2;
  if (cmd[ix] =='\0') return ix;
  if (cmd[ix] != '6' || cmd[ix+1] != '5')  return 0;
  ix += 2;
  DEBPR((char)cmd[ix]); DEBLN((char)cmd[ix+1]);
  if (cmd[ix] =='2' && cmd[ix+1] == '0') return ix + 2;
  if (cmd[ix] == '\0') return ix;
  if (cmd[ix] != '6' || cmd[ix+1] != '5')  return 0;
  ix +=2;
  DEBPR((char)cmd[ix]); DEBLN((char)cmd[ix+1]);
  if (cmd[ix] =='2' && cmd[ix+1] == '0') return ix + 2;
  if (cmd[ix] =='\0') return ix;
  if (cmd[ix] != '6' || cmd[ix+1] != '4')  return 0;
  ix += 2;
  DEBPR((char)cmd[ix]); DEBLN((char)cmd[ix+1]);
  if (cmd[ix] =='2' && cmd[ix+1] == '0') return ix + 2;
  if (cmd[ix] =='\0') return ix;
  return 0;
}

// "monitor swbp/hwbp/4bp"
// set maximum number of breakpoints
inline void gdbSetMaxBPs(byte num)
{
  maxbreak = num;
  gdbSendReply("OK");
}

// "monitor flashcount"
// report on how many flash pages have been written
void gdbReportFlashCount(void)
{
  gdbDebugMessagePSTR(PSTR("Number of flash write operations: "), flashcnt);
  _delay_ms(5);
  // flushInput();
  gdbSendReply("OK");
}

// "monitor ramusage"
void gdbReportRamUsage(void)
{
#if FREERAM
  gdbDebugMessagePSTR(PSTR("Minimal number of free RAM bytes: "), freeram);
  _delay_ms(5);
  // flushInput();
  gdbSendReply("OK");
#else
  gdbSendReply("");
#endif
}


// "monitor dwconnect"
// try to enable debugWIRE
// this might imply that the user has to power-cycle the target system
boolean gdbConnect(boolean verbose)
{
  int retry = 0;
  byte b;
  int conncode;

  conncode = targetConnect();
  DEBPR(F("conncode=")); DEBLN(conncode);
  switch (conncode) {

  case 1: // everything OK since we are already connected
    setSysState(CONN_STATE);
    if (verbose) {
      gdbDebugMessagePSTR(Connected,-2);
      gdbDebugMessagePSTR(PSTR("debugWIRE is now enabled, bps: "),ctx.bps);
    }
    gdbCleanupBreakpointTable();
    // flushInput();
    // gdbSendReply("OK");
    return true;
    break;
  case 0: // we have changed the fuse and need to powercycle
    dw.enable(false);
    setSysState(PWRCYC_STATE);
    while (retry < 60) {
      DEBPR(F("retry=")); DEBLN(retry);
      if (retry%3 == 0) { // try to power-cycle
	DEBLN(F("Power cycle!"));
	power(false); // cutoff power to target
	_delay_ms(500);
	power(true); // power target again
	_delay_ms(200); // wait for target to startup
	DEBLN(F("Power cycling done!"));	
      }
      if ((retry++)%3 == 0 && retry >= 3) {
	do {
	  //	  flushInput();
	  if (verbose) {
	    gdbDebugMessagePSTR(PSTR("Please power-cycle the target system"),-1);
	    b = gdbReadByte();
	  } else b ='+';
	} while (b == '-');
      }
      _delay_ms(1000);
      dw.enable(true);
      if (doBreak()) {
	setSysState(CONN_STATE);
	// flushInput();
	// gdbReset();
	if (verbose) {
	  gdbDebugMessagePSTR(Connected,-2);
	  gdbDebugMessagePSTR(PSTR("debugWIRE is now enabled, bps: "),ctx.bps);
	  _delay_ms(100);
	}
	// flushInput();
	gdbCleanupBreakpointTable();
	// gdbSendReply("OK");
	return true;
      }
    }
    if (verbose)
      gdbDebugMessagePSTR(PSTR("Cannot connect after setting DWEN fuse"),-1);
    conncode = -5;
    break;
  default:
    setSysState(ERROR_STATE);
    flushInput();
    switch (conncode) {
    case -1: gdbDebugMessagePSTR(PSTR("Cannot connect: Check wiring"),-1); break;
    case -2: gdbDebugMessagePSTR(PSTR("Cannot connect: Unsupported MCU type"),-1); break;
    case -3: gdbDebugMessagePSTR(PSTR("Cannot connect: Lock bits are set"),-1); break;
#ifdef LINEQUALITY
    case -4: gdbDebugMessagePSTR(PSTR("Cannot connect: Weak pull-up or capacitive load on RESET line"),-1); break;
#endif
    default: gdbDebugMessagePSTR(PSTR("Cannot connect for unknown reasons"),-1); conncode = -6; break;
    }
    break;
  }
  if (fatalerror == NO_FATAL) fatalerror = -conncode;
  setSysState(ERROR_STATE);
  flushInput();
  // gdbSendReply("E05");
  return false;
}

void power(boolean on)
{
  DEBPR(F("Power: ")); DEBLN(on);
  if (on) {
    if (!ctx.snsgnd) {
      DEBLN(F("VSUP up"));
      digitalWrite(VSUP, HIGH);
    } else {
      if (ctx.von) {
#if defined(V5) && defined(V33)
	DEBLN(F("VXX up"));
	if (ctx.vhigh) pinMode(V5, OUTPUT);
	else pinMode(V33, OUTPUT);
#endif
      }
    }
  } else { // on=false
    if (!ctx.snsgnd) {
      digitalWrite(VSUP, LOW);
    } else {
#if defined(V5) && defined(V33)
      pinMode(V5, INPUT);
      pinMode(V33, INPUT);
#endif
    }
  }
}

// "monitor dwoff" 
// try to disable the debugWIRE interface on the target system
void gdbStop(void)
{
  if (targetStop()) {
    gdbDebugMessagePSTR(Connected,-2);
    gdbDebugMessagePSTR(PSTR("debugWIRE is now disabled"),-1);
    setSysState(NOTCONN_STATE);
    gdbSendReply("OK");
  } else {
    gdbDebugMessagePSTR(PSTR("debugWIRE could NOT be disabled"),-1);
    gdbSendReply("E05");
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
    else if (res == -2) gdbDebugMessagePSTR(PSTR("Unsupported MCU type"),-1);
    else if (res == -3) gdbDebugMessagePSTR(PSTR("Fuse programming failed"),-1);
    else if (res == -4) gdbDebugMessagePSTR(PSTR("XTAL is not a possible clock source"),-1);
    flushInput();
    gdbSendReply("E05");
    return;
  }
  switch (fuse) {
  case CkDiv8: gdbDebugMessagePSTR(PSTR("CKDIV8 fuse is now programmed"),-1); break;
  case CkDiv1: gdbDebugMessagePSTR(PSTR("CKDIV8 fuse is now unprogrammed"),-1); break;
  case CkRc: gdbDebugMessagePSTR(PSTR("Clock source is now the RC oscillator"),-1); break;
  case CkExt: gdbDebugMessagePSTR(PSTR("Clock source is now the EXTernal oscillator"),-1); break;
  case CkXtal: gdbDebugMessagePSTR(PSTR("Clock source is now the XTAL oscillator"),-1); break;
  case Erase: gdbDebugMessagePSTR(PSTR("Flash memory erased"),-1); break;
  }
  _delay_ms(200);
  flushInput();
  if (offline) {
    gdbSendReply("OK");
    return;
  }
  if (gdbConnect(true))
    gdbSendReply("OK");
  else
    gdbSendReply("E02");
}

// check whether there should be a BREAK instruction at the current PC address 
// if so, give back original instruction 
boolean gdbBreakPresent(unsigned int &opcode)
{
  int bpix = gdbFindBreakpoint(ctx.wpc);
  opcode = 0;
  if (bpix < 0) return false;
  if (!bp[bpix].inflash) return false;
  opcode = bp[bpix].opcode;
  return true;
}


// If there is a BREAK instruction (a not yet restored SW BP) at the current PC
// location, we may need to execute the original instruction offline.
// Call this function in case this is necessary and provide it with the
// instruction word and the argument word (if necessary). The function gdbBreakPresent
// will check the condition and provide the opcode.
// gdbBreakDetour will start with registers in a saved state and return with it
// in a saved state.
inline byte gdbBreakDetour(unsigned int opcode)
{
  //DEBLN(F("gdbBreakDetour"));
  if (targetIllegalOpcode(opcode)) {
    //DEBPR(F("Illop: ")); DEBLNF(opcode,HEX);
    return SIGILL;
  }
  targetRestoreRegisters();
  DWexecOffline(opcode);
  targetSaveRegisters();
  return SIGTRAP;
}

// do one step
// start with saved registers and return with saved regs
// it will return a signal, which in case of success is SIGTRAP
byte gdbStep(void)
{
  unsigned int opcode;
  unsigned int oldpc = ctx.wpc;
  byte sig = SIGTRAP; // SIGTRAP (normal), SIGILL (if ill opcode), SIGABRT (fatal)
  DEBLN(F("Start step operation"));
  if (fatalerror) return SIGABRT;
  if (targetOffline()) return SIGHUP;
  if (gdbBreakPresent(opcode)) { // we have a break instruction inserted here
    return gdbBreakDetour(opcode);
  } else { // just single-step in flash
    //DEBPR(F("Opcode: ")); DEBLNF(targetReadFlashWord(ctx.wpc*2),HEX);
    if (targetIllegalOpcode(targetReadFlashWord(ctx.wpc*2))) {
      DEBPR(F("Illop: ")); DEBLNF(targetReadFlashWord(ctx.wpc*2),HEX);
      return SIGILL;
    }
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
  unsigned int opcode;
  DEBLN(F("Start continue operation"));
  if (fatalerror) sig = SIGABRT;
  else if (targetOffline()) sig = SIGHUP;
  else {
    gdbUpdateBreakpoints(false);  // update breakpoints in flash memory
    if (gdbBreakPresent(opcode)) { // we have a break instruction inserted here
      reportFatalError(SELF_BLOCKING_FATAL, false);
      sig = SIGABRT;
    }  else if (targetIllegalOpcode(targetReadFlashWord(ctx.wpc*2))) {
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
  unsigned int relevant[MAXBREAK*2+1];
  unsigned int addr = 0;

  measureRam();

  DEBPR(F("Update Breakpoints (used/active): ")); DEBPR(bpused); DEBPR(F(" / ")); DEBLN(bpcnt);
  // return immediately if there are too many bps active
  // because in this case we will not start to execute
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
      targetWriteFlashPage(addr, newpage);
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
    DEBPR(F("New recycled BP: ")); DEBPRF(waddr*2,HEX); if (bp[i].inflash) DEBPR(F(" (flash) "));
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
// and cleanup, by making all BPs inactive,
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
  unsigned long pc = (unsigned long)ctx.wpc << 1;	/* convert word address to byte address used by gdb */
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
  buf[i++] = '0'; /* For AVR with up to 16-bit PC */
  buf[i++] = nib2hex((pc >> 16) & 0xf);
  buf[i++] = '0'; /* gdb wants 32-bit value, send 0 */
  buf[i++] = '0'; /* gdb wants 32-bit value, send 0 */
  
  buffill = i;
  gdbSendBuff(buf, buffill);
  
}

// set all registers with values given by GDB
void gdbWriteRegisters(const byte *buff)
{
  byte a;
  unsigned long pc;
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
  unsigned long addr, sz, flag;
  byte i, b;

  measureRam();

  buff += parseHex(buff, &addr);
  /* skip 'xxx,' */
  parseHex(buff + 1, &sz);
  
  if (sz > 127) { // should not happen because we required packet length = 255:
    gdbSendReply("E05");
    reportFatalError(PACKET_LEN_FATAL, false);
    //DEBLN(F("***Packet length > 127"));
    return;
  }

  if (addr == 0xFFFFFFFF) {
    buf[0] = nib2hex(fatalerror >> 4);
    buf[1] = nib2hex(fatalerror & 0x0f);
    gdbSendBuff(buf, 2);
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
  unsigned int addr;

  measureRam();

  for (addr = startaddr; addr < startaddr+size; addr++) {
    if ((addr & 1) && membuf[addr-startaddr] == 0x95) { // uneven address and match with MSB of BREAK
      bpix = gdbFindBreakpoint((addr-1)/2);
      if (bpix >= 0 && bp[bpix].inflash && !bp[bpix].active) 
	membuf[addr-startaddr] = bp[bpix].opcode>>8; // replace with MSB of opcode
    }
    if (((addr&1) == 0) && membuf[addr-startaddr] == 0x98) { // even address and match with LSB of BREAK
      bpix = gdbFindBreakpoint(addr/2);
      if (bpix >= 0 && bp[bpix].inflash  && !bp[bpix].active)
	membuf[addr-startaddr] = bp[bpix].opcode&0xFF;
    }
  }
}

// write to target memory
void gdbWriteMemory(const byte *buff)
{
  unsigned long flag, addr, sz;
  byte i;

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

  for ( i = 0; i < sz; ++i) {
    membuf[i]  = hex2nib(*buff++) << 4;
    membuf[i] |= hex2nib(*buff++);
  }

  flag = addr & MEM_SPACE_MASK;
  addr &= ~MEM_SPACE_MASK;
  if (flag == SRAM_OFFSET) targetWriteSram(addr, membuf, sz);
  else if (flag == FLASH_OFFSET) targetWriteFlash(addr, membuf, sz);
  else if (flag == EEPROM_OFFSET) targetWriteEeprom(addr, membuf, sz);
  else {
    gdbSendReply("E05"); 
    reportFatalError(WRONG_MEM_FATAL, false);
    //DEBLN(F("***Wrong memory type in gdbWriteMemory"));
    return;
  }
  gdbSendReply("OK");
}

static void gdbWriteBinMemory(const byte *buff) {
  unsigned long flag, addr, sz;
  int memsz;

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
  memsz = gdbBin2Mem(buff, membuf, sz);
  if (memsz < 0) { 
    gdbSendReply("E05");
    reportFatalError(NEG_SIZE_FATAL, false);
    //DEBLN(F("***Negative packet size"));
    return;
  }
  
  flag = addr & MEM_SPACE_MASK;
  addr &= ~MEM_SPACE_MASK;
  if (flag == SRAM_OFFSET) targetWriteSram(addr, membuf, memsz);
  else if (flag == FLASH_OFFSET) targetWriteFlash(addr, membuf, memsz);
  else if (flag == EEPROM_OFFSET) targetWriteEeprom(addr, membuf, memsz);
  else {
    gdbSendReply("E05"); 
    reportFatalError(WRONG_MEM_FATAL, false);
    //DEBLN(F("***Wrong memory type in gdbWriteBinMemory"));
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
  if (ctx.state == CONN_STATE || ctx.state == RUN_STATE) return false;
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
}

// blocking read byte from host
inline byte gdbReadByte(void)
{
  while (!Serial.available());
  return Serial.read();
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

// send a message the user can see, if last argument positive, then send the number
// if last argument < -1, then use it as index into MCU name array (index: abs(num)-1)
void gdbDebugMessagePSTR(const char pstr[],long num) {
  byte i = 0, j = 0, c;
  byte numbuf[10];
  char *str;

  buf[i++] = 'O';
  do {
    c = pgm_read_byte(&pstr[j++]);
    if (c) {
      DEBPR((char)c);
      buf[i++] = nib2hex((c >> 4) & 0xf);
      buf[i++] = nib2hex((c >> 0) & 0xf);
    }
  } while (c);
  if (num >= 0) {
    convNum(numbuf,num);
    j = 0;
    while (numbuf[j] != '\0') j++;
    while (j-- > 0) {
      DEBPR((char)numbuf[j]);
      buf[i++] = nib2hex((numbuf[j] >> 4) & 0xf);
      buf[i++] = nib2hex((numbuf[j] >> 0) & 0xf);
    }
  } else if (num == -2) { // print MCU name
    str = mcu.name;
    do {
      c = pgm_read_byte(str++);
      if (c) {
	DEBPR((char)c);
	buf[i++] = nib2hex((c >> 4) & 0xf);
	buf[i++] = nib2hex((c >> 0) & 0xf);
      }
    } while (c);
  }
  buf[i++] = '0';
  buf[i++] = 'A';
  buf[i] = 0;
  gdbSendBuff(buf, i);
  DEBLN();
}


/****************** target functions *************/

// try to establish DW connection 
// if not possible, try to establish ISP connection
// if possible, set DWEN fuse
//   1 if we are in debugWIRE mode and connected 
//   0 if we need to powercycle
//   -1 if we cannot connect
//   -2 if unknown MCU type
//   -3 if lock bits set
//   -4 if connection quality is not good enough

int targetConnect(void)
{
  unsigned int sig;
  int result = 0;
#ifdef LINEQUALITY
  unsigned int quality = DWquality();

  if (quality > UNCONNRISETIME) return -1;
  else if (quality > MAXRISETIME) return -4;
#endif
  if (doBreak()) {
    DEBLN(F("targetConnect: doBreak done"));
    sig = DWgetChipId();
    DEBPR(F("targetConnect: sig=")); DEBLNF(sig,HEX);
    result = setMcuAttr(sig);
    DEBPR(F("setMcuAttr=")); DEBLN(result);
    return (result ? 1 : -2);
  }
  // so we need to set the DWEN fuse bit
  if (!enterProgramMode()) return -1;
  sig = ispGetChipId();
  if (sig == 0) { // no reasonable signature
    result = -1;
  } else if (!setMcuAttr(sig)) {
    result = -2;
  } else if (ispLocked()) {
    result = -3;
  } else if (ispProgramFuse(true, mcu.dwenfuse, 0)) {
    result = 0;
  } else {
    result = -1;
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
 

int targetSetFuses(Fuses fuse)
{
  unsigned int sig;
  boolean succ;

  measureRam();
  if (fuse == CkXtal && mcu.xtalosc == 0) return -4; // this chip does not permit an XTAL as the clock source
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
  if (addr != (addr & ~(mcu.targetpgsz-1))) {
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
  if (!DWreadFlash(addr, temp, 2)) reportFatalError(FLASH_READ_FATAL, true);
  return temp[0] + ((unsigned int)(temp[1]) << 8);
}

// read some portion of flash memory to the buffer pointed at by *mem'
// do not check for cached pages etc.
void targetReadFlash(unsigned int addr, byte *mem, unsigned int len)
{
  if (!DWreadFlash(addr, mem, len)) {
    reportFatalError(FLASH_READ_FATAL, true);
    // DEBPR(F("***Error reading flash memory at ")); DEBLNF(addr,HEX);
  }
}

// read some portion of SRAM into buffer pointed at by *mem
void targetReadSram(unsigned int addr, byte *mem, unsigned int len)
{
  if (!DWreadSramBytes(addr, mem, len)) {
    reportFatalError(SRAM_READ_FATAL, true);
    // DEBPR(F("***Error reading SRAM at ")); DEBLNF(addr,HEX);
  }
}

// read some portion of EEPROM
void targetReadEeprom(unsigned int addr, byte *mem, unsigned int len)
{
  for (unsigned int i=0; i < len; i++) {
    mem[i] = DWreadEepromByte(addr++);
  }
}

// write a flash page,
// check whether the data is already in this flash page,
// if so, do nothing,
// check whether we can get away with simply overwriting,
// if not erase page,
// and finally write page
// remember page content in 'page' buffer
// if the MCU use the 4-page erase operation, then
// do 4 load/program cycles for the 4 sub-pages
void targetWriteFlashPage(unsigned int addr, byte *mem)
{
  byte subpage;
  boolean succ = true;

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
  if (memcmp(mem, page, mcu.targetpgsz) == 0) {
    DEBLN(F("page unchanged"));
    return;
  }
  // DEBLN(F("changed"));

#ifdef TXODEBUG
  DEBLN(F("Changes in flash page:"));
  for (unsigned int i=0; i<mcu.targetpgsz; i++) {
    if (page[i] != mem[i]) {
      DEBPRF(i+addr, HEX);
      DEBPR(": ");
      DEBPRF(mem[i], HEX);
      DEBPR(" -> ");
      DEBPRF(page[i], HEX);
      DEBLN("");
    }
  }
#endif
  
  // check whether we need to erase the page
  boolean dirty = false;
  for (byte i=0; i < mcu.targetpgsz; i++) 
    if (~page[i] & mem[i]) {
      dirty = true;
      break;
    }

  validpg = false;
  
  // erase page when dirty
  if (dirty) {
    // DEBLN(F(" erasing ..."));
    if (!DWeraseFlashPage(addr)) {
      reportFatalError(ERASE_FAILURE_FATAL, true);
      // DEBLN(F(" not possible"));
      DWreenableRWW();
      return;
    } else {
      //DEBLN(F(" will overwrite ..."));
    }
    
    DWreenableRWW();
    // maybe the new page is also empty?
    memset(page, 0xFF, mcu.targetpgsz);
    if (memcmp(mem, page, mcu.targetpgsz) == 0) {
      // DEBLN(" nothing to write");
      validpg = true;
      return;
    }
  }
  
  // now do the programming; for 4-page erase MCUs four subpages
  for (subpage = 0; subpage < 1+(3*mcu.erase4pg); subpage++) {
    //DEBPR(F("writing subpage at ")); DEBLNF(addr+subpage*mcu.pagesz,HEX);
    if (!DWloadFlashPageBuffer(addr+(subpage*mcu.pagesz), &mem[subpage*mcu.pagesz])) {
      DWreenableRWW();
      reportFatalError(NO_LOAD_FLASH_FATAL, true);
      // DEBPR(F("\n***Cannot load page buffer "));
      return;
    } else {
      // DEBPR(F(" flash buffer loaded"));
    }
    succ &= DWprogramFlashPage(addr+subpage*mcu.pagesz);
    DWreenableRWW();
  }

  // remember the last programmed page
  if (succ) {
    memcpy(page, mem, mcu.targetpgsz);
    validpg = true;
    lastpg = addr;
    //DEBLN(F(" page flashed"));
  } else {
    // DEBLN(F("\n***Could not program flash memory"));
    reportFatalError(PROGRAM_FLASH_FAIL_FATAL, true);
  }

}

// write some chunk of data to flash,
// break it up into pages and flash also the
// the partial pages in the beginning and in the end
void targetWriteFlash(unsigned int addr, byte *mem, unsigned int len)
{
  unsigned int pageoffmsk = mcu.targetpgsz-1;
  unsigned int pagebasemsk = ~pageoffmsk;
  unsigned int partbase = addr & pagebasemsk;
  unsigned int partoffset = addr & pageoffmsk;
  unsigned int partlen = min(mcu.targetpgsz-partoffset, len);

  measureRam();

  if (len == 0) return;

  if (addr & pageoffmsk)  { // mem starts in the middle of a page
    targetReadFlashPage(partbase);
    memcpy(newpage, page, mcu.targetpgsz);
    memcpy(newpage + partoffset, mem, partlen);
    targetWriteFlashPage(partbase, newpage);
    addr += partlen;
    mem += partlen;
    len -= partlen;
  }

  // now write whole pages
  while (len >= mcu.targetpgsz) {
    targetWriteFlashPage(addr, mem);
    addr += mcu.targetpgsz;
    mem += mcu.targetpgsz;
    len -= mcu.targetpgsz;
  }

  // write remaining partial page (if any)
  if (len) {
    targetReadFlashPage(addr);
    memcpy(newpage, page, mcu.targetpgsz);
    memcpy(newpage, mem, len);
    targetWriteFlashPage(addr, newpage);
  }
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

  if (ctx.saved) return; // If the regs have been saved, then the machine regs are clobbered, so do not load again!
  ctx.wpc = DWgetWPc(); // needs to be done first, because the PC is advanced when executing instrs in the instr reg
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
  byte cmd[] = { 0xD0, (byte)(ctx.wpc>>8), (byte)(ctx.wpc), 0x30};
  sendCommand(cmd, sizeof(cmd));
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
  // dw.begin(ctx.bps*2); // could be that communication speed is higer after reset!
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
    DEBLN(F("No response from debugWIRE on sending break"));
    return false;
  }
  DEBPR(F("Successfully connected with bps: ")); DEBLN(ctx.bps);
  return true;
}

// re-calibrate on a sent 0x55, then try to set the highest possible speed, i.e.,
// multiply speed by at most 16 up to 250k baud - provided we have another speed than
// before
// return false if syncing was unsuccessful
boolean expectUCalibrate() {
  int8_t speed;
  unsigned long newbps;

  measureRam();
  newbps = dw.calibrate(); // expect 0x55 and calibrate
  DEBPR(F("Rsync (1): ")); DEBLN(newbps);
  if (newbps < 100) {
    ctx.bps = 0;
    return false; // too slow
  }
  if ((100*(abs((long)ctx.bps-(long)newbps)))/newbps <= 1)  { // less than 2% deviation -> ignore change
    DEBLN(F("No change: return"));
    return;
  }
  dw.begin(newbps);
  for (speed = maxspeedexp; speed > 0; speed--) {
    if ((newbps << speed) <= speedlimit) break;
  }
  DEBPR(F("Set speedexp: ")); DEBLN(speed);
#if VARSPEED
  DWsetSpeed(speed);
  ctx.bps = dw.calibrate(); // calibrate again
  DEBPR(F("Rsync (2): ")); DEBLN(ctx.bps);
  if (ctx.bps < 1000) {
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
unsigned int getResponse (int unsigned expected) {
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

// wait for response that should be word, MSB first
unsigned int getWordResponse () {
  byte tmp[2];

  measureRam();

  getResponse(&tmp[0], 2);
  //DEBPR(F("getWordReponse: ")); DEBLNF(((unsigned int) tmp[0] << 8) + tmp[1],HEX);
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
  byte wrRegs[] = {0x66,              // read/write
		   0xD0, 0x00, 0x00,  // start reg
		   0xD1, 0x00, 0x20,  // end reg
		   0xC2, 0x05,        // write registers
		   0x20 };              // go
  measureRam();
  sendCommand(wrRegs,  sizeof(wrRegs));
  sendCommand(regs, 32);
}

// Set register <reg> by building and executing an "in <reg>,DWDR" instruction via the CMD_SET_INSTR register
void DWwriteRegister (byte reg, byte val) {
  byte wrReg[] = {0x64,                                               // Set up for single step using loaded instruction
                  0xD2, inHigh(mcu.dwdr, reg), inLow(mcu.dwdr, reg), 0x23,    // Build "in reg,DWDR" instruction
                  val};                                               // Write value to register via DWDR
  measureRam();

  sendCommand(wrReg,  sizeof(wrReg));
}

// Read all registers
void DWreadRegisters (byte *regs)
{
  byte rdRegs[] = {0x66,
		   0xD0, 0x00, 0x00, // start reg
		   0xD1, 0x00, 0x20, // end reg
		   0xC2, 0x01,       // read registers
		   0x20 };            // start
  measureRam();
  DWflushInput();
  sendCommand(rdRegs,  sizeof(rdRegs));
  getResponse(regs, 32);               // Get value sent as response
}

// Read register <reg> by building and executing an "out DWDR,<reg>" instruction via the CMD_SET_INSTR register
byte DWreadRegister (byte reg) {
  byte res = 0;
  byte rdReg[] = {0x64,                                               // Set up for single step using loaded instruction
                  0xD2, outHigh(mcu.dwdr, reg), outLow(mcu.dwdr, reg),        // Build "out DWDR, reg" instruction
                  0x23};                                              // Execute loaded instruction
  measureRam();
  DWflushInput();
  sendCommand(rdReg,  sizeof(rdReg));
  getResponse(&res, 1);                                                     // Get value sent as response
  return res;
}

// Write one byte to SRAM address space using an SRAM-based value for <addr>, not an I/O address
void DWwriteSramByte (unsigned int addr, byte val) {
  byte wrSram[] = {0x66,                                              // Set up for read/write using repeating simulated instructions
                   0xD0, 0x00, 0x1E,                                  // Set Start Reg number (r30)
                   0xD1, 0x00, 0x20,                                  // Set End Reg number (r31) + 1
                   0xC2, 0x05,                                        // Set repeating copy to registers via DWDR
                   0x20,                                              // Go
		   (byte)(addr & 0xFF), (byte)(addr >> 8),            // r31:r30 (Z) = addr
                   0xD0, 0x00, 0x01,
                   0xD1, 0x00, 0x03,
                   0xC2, 0x04,                                        // Set simulated "in r?,DWDR; st Z+,r?" instructions
                   0x20,                                              // Go
                   val};
  measureRam();
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
  byte rdSram[] = {0x66,                                              // Set up for read/write using repeating simulated instructions
                   0xD0, 0x00, 0x1E,                                  // Set Start Reg number (r30)
                   0xD1, 0x00, 0x20,                                  // Set End Reg number (r31) + 1
                   0xC2, 0x05,                                        // Set repeating copy to registers via DWDR
                   0x20,                                              // Go
                   (byte)(addr & 0xFF), (byte)(addr >> 8),            // r31:r30 (Z) = addr
                   0xD0, 0x00, 0x00,                                  // 
                   0xD1, 0x00, 0x02,                                  // 
                   0xC2, 0x00,                                        // Set simulated "ld r?,Z+; out DWDR,r?" instructions
                   0x20};                                             // Go
  measureRam();
  DWflushInput();
  sendCommand(rdSram, sizeof(rdSram));
  getResponse(&res,1);
  return res;
}

// Read one byte from IO register (via R0)
byte DWreadIOreg (byte ioreg)
{
  byte res = 0;
  byte rdIOreg[] = {0x64,                                               // Set up for single step using loaded instruction
		    0xD2, inHigh(ioreg, 0), inLow(ioreg, 0),        // Build "out DWDR, reg" instruction
		    0x23,
		    0xD2, outHigh(mcu.dwdr, 0), outLow(mcu.dwdr, 0),        // Build "out DWDR, 0" instruction
		    0x23};
  measureRam();
  DWflushInput();
  sendCommand(rdIOreg, sizeof(rdIOreg));
  getResponse(&res,1);
  return res;
}

// Read <len> bytes from SRAM address space into buf[] using an SRAM-based value for <addr>, not an I/O address
// Note: can't read addresses that correspond to  r28-31 (Y & Z Regs) because Z is used for transfer (not sure why Y is clobbered) 
boolean DWreadSramBytes (unsigned int addr, byte *mem, byte len) {
  unsigned int len2 = len * 2;
  byte rsp;
  for (byte ii = 0; ii < 4; ii++) {
    byte rdSram[] = {0x66,                                            // Set up for read/write using repeating simulated instructions
                     0xD0, 0x00, 0x1E,                                // Set Start Reg number (r30)
                     0xD1, 0x00, 0x20,                                // Set End Reg number (r31) + 1
                     0xC2, 0x05,                                      // Set repeating copy to registers via DWDR
                     0x20,                                            // Go
                     (byte)(addr & 0xFF), (byte)(addr >> 8),          // r31:r30 (Z) = addr
                     0xD0, 0x00, 0x00,                                // 
                     0xD1, (byte)(len2 >> 8), (byte)(len2 & 0xFF),    // Set repeat count = len * 2
                     0xC2, 0x00,                                      // Set simulated "ld r?,Z+; out DWDR,r?" instructions
                     0x20};                                           // Go
    measureRam();
    DWflushInput();
    sendCommand(rdSram, sizeof(rdSram));
    rsp = getResponse(mem, len);
    if (rsp == len) {
      break;
    } else {
      // Wait and retry read
      _delay_ms(5);
    }
  }
  return rsp == len;
}

//   EEPROM Notes: This section contains code to read and write from EEPROM.  This is accomplished by setting parameters
//    into registers 28 - r31 and then using the 0xD2 command to send and execute a series of instruction opcodes on the
//    target device. 
// 
//   EEPROM Register Locations for ATTiny25/45/85, ATTiny24/44/84, ATTiny13, Tiny2313, Tiny441/841
//     EECR    0x1C EEPROM Control Register
//     EEDR    0x1D EEPROM Data Register
//     EEARL   0x1E EEPROM Address Register (low byte)
//     EEARH   0x1F EEPROM Address Register (high byte)
// 
//   EEPROM Register Locations for ATMega328, ATMega32U2/16U2/32U2, etc.
//     EECR    0x1F EEPROM Control Register
//     EEDR    0x20 EEPROM Data Register
//     EEARL   0x21 EEPROM Address Register (low byte)
//     EEARH   0x22 EEPROM Address Register (high byte)

// 
//   Read one byte from EEPROM
//   

byte DWreadEepromByte (unsigned int addr) {
  byte retval;
  byte setRegs[] = {0x66,                                               // Set up for read/write using repeating simulated instructions
                    0xD0, 0x00, 0x1C,                                   // Set Start Reg number (r28)
                    0xD1, 0x00, 0x20,                                   // Set End Reg number (r31) + 1
                    0xC2, 0x05,                                         // Set repeating copy to registers via DWDR
                    0x20,                                               // Go
                    0x01, 0x01, (byte)(addr & 0xFF), (byte)(addr >> 8)};// Data written into registers r28-r31
  byte doReadH[] = {0xD2, outHigh(mcu.eearh, 31), outLow(mcu.eearh, 31), 0x23};  // out EEARH,r31  EEARH = ah  EEPROM Address MSB
  byte doRead[]  = {0xD2, outHigh(mcu.eearl, 30), outLow(mcu.eearl, 30), 0x23,  // out EEARL,r30  EEARL = al  EEPROMad Address LSB
                    0xD2, outHigh(mcu.eecr, 28), outLow(mcu.eecr, 28), 0x23,    // out EECR,r28   EERE = 01 (EEPROM Read Enable)
                    0xD2, inHigh(mcu.eedr, 29), inLow(mcu.eedr, 29), 0x23,      // in  r29,EEDR   Read data from EEDR
                    0xD2, outHigh(mcu.dwdr, 29), outLow(mcu.dwdr, 29), 0x23};   // out DWDR,r29   Send data back via DWDR reg
  measureRam();
  DWflushInput();
  sendCommand(setRegs, sizeof(setRegs));
  sendCommand((const byte[]){0x64},1);                                  // Set up for single step using loaded instruction
  if (mcu.eearh)                                                        // if there is a high byte EEAR reg, set it
    sendCommand(doReadH, sizeof(doReadH));
  sendCommand(doRead, sizeof(doRead));                                  // set rest of control regs and query
  getResponse(&retval,1);                                               // Read data from EEPROM location
  return retval;
}

//   
//   Write one byte to EEPROM
//   

void DWwriteEepromByte (unsigned int addr, byte val) {
  byte setRegs[] = {0x66,                                                 // Set up for read/write using repeating simulated instructions
                    0xD0, 0x00, 0x1C,                                     // Set Start Reg number (r30)
                    0xD1, 0x00, 0x20,                                     // Set End Reg number (r31) + 1
                    0xC2, 0x05,                                           // Set repeating copy to registers via DWDR
                    0x20,                                                 // Go
                    0x04, 0x02, (byte)(addr & 0xFF), (byte)(addr >> 8)};  // Data written into registers r28-r31
  byte doWriteH[] ={0xD2, outHigh(mcu.eearh, 31), outLow(mcu.eearh, 31), 0x23};    // out EEARH,r31  EEARH = ah  EEPROM Address MSB
  byte doWrite[] = {0xD2, outHigh(mcu.eearl, 30), outLow(mcu.eearl, 30), 0x23,    // out EEARL,r30  EEARL = al  EEPROM Address LSB
                    0xD2, inHigh(mcu.dwdr, 30), inLow(mcu.dwdr, 30), 0x23,        // in  r30,DWDR   Get data to write via DWDR
                    val,                                                  // Data written to EEPROM location
                    0xD2, outHigh(mcu.eedr, 30), outLow(mcu.eedr, 30), 0x23,      // out EEDR,r30   EEDR = data
                    0xD2, outHigh(mcu.eecr, 28), outLow(mcu.eecr, 28), 0x23,      // out EECR,r28   EECR = 04 (EEPROM Master Program Enable)
                    0xD2, outHigh(mcu.eecr, 29), outLow(mcu.eecr, 29), 0x23};     // out EECR,r29   EECR = 02 (EEPROM Program Enable)
  measureRam();
  sendCommand(setRegs, sizeof(setRegs));
  if (mcu.eearh)                                                        // if there is a high byte EEAR reg, set it
    sendCommand(doWriteH, sizeof(doWriteH));
  sendCommand(doWrite, sizeof(doWrite));
  _delay_ms(5);                                                        // allow EEPROM write to complete
}

//
//  Read len bytes from flash memory area at <addr> into data[] buffer
//
boolean DWreadFlash(unsigned int addr, byte *mem, unsigned int len) {
  // Read len bytes form flash page at <addr>
  unsigned int rsp;
  // DEBPR(F("Read flash ")); DEBPRF(addr,HEX); DEBPR("-"); DEBLNF(addr+len-1, HEX);
  unsigned int lenx2 = len * 2;
  for (byte ii = 0; ii < 4; ii++) {
    byte rdFlash[] = {0x66,                                               // Set up for read/write using repeating simulated instructions
                      0xD0, 0x00, 0x1E,                                   // Set Start Reg number (r30)
                      0xD1, 0x00, 0x20,                                   // Set End Reg number (r31) + 1
                      0xC2, 0x05,                                         // Set repeating copy to registers via DWDR
                      0x20,                                               // Go
                      (byte)(addr & 0xFF), (byte)(addr >> 8),             // r31:r30 (Z) = addr
                      0xD0, 0x00, 0x00,                                   // Set start = 0
                      0xD1, (byte)(lenx2 >> 8),(byte)(lenx2),             // Set end = repeat count = sizeof(flashBuf) * 2
                      0xC2, 0x02,                                         // Set simulated "lpm r?,Z+; out DWDR,r?" instructions
                      0x20};                                              // Go
    DWflushInput();
    sendCommand(rdFlash, sizeof(rdFlash));
    rsp = getResponse(mem, len);                                         // Read len bytes
    if (rsp ==len) {
      break;
    } else {
      // Wait and retry read
      _delay_ms(5);
    }
  }
  measureRam();
  return rsp==len;
}

// erase entire flash page
boolean DWeraseFlashPage(unsigned int addr) {
  measureRam();
  DEBPR(F("Erase: "));  DEBLNF(addr,HEX);
  DWflushInput();
  DWwriteRegister(30, addr & 0xFF); // load Z reg with addr low
  DWwriteRegister(31, addr >> 8  ); // load Z reg with addr high
  DWwriteRegister(29, 0x03); // PGERS value for SPMCSR
  if (mcu.bootaddr) DWsetWPc(mcu.bootaddr); // so that access of all of flash is possible
  byte eflash[] = { 0x64, // single stepping
		    0xD2, // load into instr reg
		    outHigh(0x37, 29), // Build "out SPMCSR, r29"
		    outLow(0x37, 29), 
		    0x23,  // execute
		    0xD2, 0x95 , 0xE8, 0x33 }; // execute SPM
  sendCommand(eflash, sizeof(eflash));
  return expectBreakAndU();
}
		    
// now move the page from temp memory to flash
boolean DWprogramFlashPage(unsigned int addr)
{
  boolean succ;
  unsigned int timeout = 1000;

  DEBLN(F("Program flash page ..."));
  measureRam();
  flashcnt++;
  DWflushInput();
  DWwriteRegister(30, addr & 0xFF); // load Z reg with addr low
  DWwriteRegister(31, addr >> 8  ); // load Z reg with addr high
  DWwriteRegister(29, 0x05); //  PGWRT value for SPMCSR
  if (mcu.bootaddr) DWsetWPc(mcu.bootaddr); // so that access of all of flash is possible
  byte eprog[] = { 0x64, // single stepping
		   0xD2, // load into instr reg
		   outHigh(0x37, 29), // Build "out SPMCSR, r29"
		   outLow(0x37, 29), 
		   0x23,  // execute
		   0xD2, 0x95 , 0xE8, 0x33 }; // execute SPM
  sendCommand(eprog, sizeof(eprog));
  succ = expectBreakAndU(); // wait for feedback
  
  if (mcu.bootaddr) { // no bootloader
    _delay_us(100);
    while ((DWreadSPMCSR() & 0x1F) != 0 && timeout-- != 0) { 
      _delay_us(100);
      //DEBPR("."); // wait
    }
    succ = (timeout != 0);
  }
  DEBLN(F("...done"));
  return succ;
}

// load bytes into temp memory
boolean DWloadFlashPageBuffer(unsigned int addr, byte *mem)
{
  DEBLN(F("Load flash page ..."));
  measureRam();
  DWwriteRegister(30, addr & 0xFF); // load Z reg with addr low
  DWwriteRegister(31, addr >> 8  ); // load Z reg with addr high
  DWwriteRegister(29, 0x01); //  SPMEN value for SPMCSR
  byte ix = 0;
  while (ix < mcu.pagesz) {
    DWwriteRegister(0, mem[ix++]);               // load next word
    DWwriteRegister(1, mem[ix++]);
    if (mcu.bootaddr) DWsetWPc(mcu.bootaddr);
    byte eload[] = { 0x64, 0xD2,
		     outHigh(0x37, 29),       // Build "out SPMCSR, r29"
		     outLow(0x37, 29),
		     0x23,                    // execute
		     0xD2, 0x95, 0xE8, 0x23, // spm
		     0xD2, 0x96, 0x32, 0x23, // addiw Z,2
    };
    sendCommand(eload, sizeof(eload));
  }
  DEBLN(F("...done"));
  return true;
}

void DWreenableRWW(void)
{
  measureRam();
  if (mcu.bootaddr) {
    // DEBLN(F("DWreenableRWW"));
    while ((DWreadSPMCSR() & 0x01) != 0) { 
      _delay_us(100);
      //DEBPR("."); // wait
    }
    DWsetWPc(mcu.bootaddr);
    DWwriteRegister(29, 0x11); //  RWWSRE value for SPMCSR
    byte errw[] = { 0x64, 0xD2,
		    outHigh(0x37, 29),       // Build "out SPMCSR, r29"
		    outLow(0x37, 29),
		    0x23,                    // execute
		    0xD2, 0x95, 0xE8, 0x23 }; // spm
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

unsigned int DWgetWPc () {
  DWflushInput();
  sendCommand((const byte[]) {0xF0}, 1);
  unsigned int pc = getWordResponse();
  //  DEBPR(F("Get PC=")); DEBLNF((pc-1)*2,HEX);
  return (pc - 1);
}

// get hardware breakpoint word address 
unsigned int DWgetWBp () {
  DWflushInput();
  sendCommand((const byte[]) {0xF1}, 1);
  return (getWordResponse());
}

// get chip signature
unsigned int DWgetChipId () {
  DWflushInput();
  sendCommand((const byte[]) {0xF3}, 1);
  return (getWordResponse());
}

// set PC (word address)
void DWsetWPc (unsigned int wpc) {
  // DEBPR(F("Set PC=")); DEBLNF(wpc*2,HEX);
  byte cmd[] = {0xD0, (byte)(wpc >> 8), (byte)(wpc & 0xFF)};
  sendCommand(cmd, sizeof(cmd));
}

// set hardware breakpoint at word address
void DWsetWBp (unsigned int wbp) {
  byte cmd[] = {0xD1, (byte)(wbp >> 8), (byte)(wbp & 0xFF)};
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

void DWflushInput(void)
{
  while (dw.available()) {
    // DEBPR("@");
     [[maybe_unused]] char c = dw.read();
    // DEBLN(c);
  }
}

#ifdef LINEQUALITY
// test quality of DW line by measuring rise time (in clock cycles)
// and return it
unsigned int DWquality(void)
{
  unsigned int rise;
  byte savesreg;
  
  TCCRA = 0;
  TCCRC = 0;
  TCCRB = _BV(CS0) |  _BV(ICES) | _BV(ICNC);// prescaler = 1 + noise canceler + rising edge
  ICDDR |= _BV(ICBIT); // make RESET line an output = low (2 cycles after timer start)
  _delay_ms(5);        // stabilize voltage
  DEBLN(F("Reset timer and go high"));
  savesreg =SREG;
  cli();
  TCNT = 0;            // reset counter
  ICDDR &= ~_BV(ICBIT); // make it an input = high
  while ((TIFR & _BV(ICF)) == 0 && TCNT < UNCONNRISETIME); // wait for edge
  if (TCNT < UNCONNRISETIME) rise = ICR-2;  // get time from input capture (4 cycles late because of noise cancelation)
  else rise = TCNT;
  SREG = savesreg; 
  DEBPR(F("rise time=")); DEBLN(rise);
  return rise;
}
#endif

/***************************** a little bit of SPI programming ********/


void enableSpiPins () {
  DEBLN(F("ESP ..."));
  pinMode(DWLINE, OUTPUT);
  digitalWrite(DWLINE, LOW);
  DEBLN(F("RESET low"));
  _delay_us(1);
  DEBLN(F("waited"));
  pinMode(SCK, OUTPUT);
  digitalWrite(SCK, LOW);
  pinMode(MOSI, OUTPUT);
  digitalWrite(MOSI, HIGH);
  pinMode(MISO, INPUT);
}

void disableSpiPins () {
  pinMode(SCK, INPUT); 
  pinMode(MOSI, INPUT);
  pinMode(MISO, INPUT);
}

byte ispTransfer (byte val) {
  measureRam();

  for (byte ii = 0; ii < 8; ++ii) {
    digitalWrite(MOSI, (val & 0x80) ? HIGH : LOW);
    digitalWrite(SCK, HIGH);
    _delay_us(4);
    val = (val << 1) + digitalRead(MISO);
    digitalWrite(SCK, LOW);
    _delay_us(4);
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

  DEBLN(F("Entering progmode"));
  dw.enable(false);
  do {
    DEBLN(F("Do ..."));
    enableSpiPins();
    DEBLN(F("Pins enabled ..."));
    digitalWrite(DWLINE, HIGH); 
    _delay_us(30);             // short positive RESET pulse of at least 2 clock cycles
    digitalWrite(DWLINE, LOW);  
    _delay_ms(30);            // wait at least 20 ms before sending enable sequence
    if (ispSend(0xAC, 0x53, 0x00, 0x00, false) == 0x53) break;
  } while (--timeout);
  if (timeout == 0) {
    leaveProgramMode();
    DEBLN(F("... not successful"));
    return false;
  } else {
    DEBLN(F("... successful"));
    _delay_ms(15);            // wait after enable programming - avrdude does that!
    return true;
  }
}

void leaveProgramMode()
{
  DEBLN(F("Leaving progmode"));
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
  DEBPR(F("ISP SIG:   "));
  DEBLNF(id,HEX);
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
  unsigned int *ptr;
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
      mcu.avreplus = pgm_read_byte(&mcu_info[ix].avreplus);
      mcu.name = pgm_read_word(&mcu_info[ix].name);
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
static byte parseHex(const byte *buff, unsigned long *hex)
{
  byte nib, len;
  measureRam();

  for (*hex = 0, len = 0; (nib = hex2nib(buff[len])) != 0xFF; ++len)
    *hex = (*hex << 4) + nib;
  return len;
}

// convert number into a string, reading the number backwards
static void convNum(byte numbuf[10], long num)
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

