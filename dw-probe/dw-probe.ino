// This is a gdbserver implementation using the debugWIRE protocol.
// It should run on all ATmega328 boards and provides a hardware debugger
// for most of the ATtinys (13, xx13,  x4, x41, x5, x61, x7, 1634)  and some small
// ATmegas (x8, xU2)
// NOTE: The RESET line of the target should have a 10k pull-up resistor and there
//       should not be capacitative load on the RESET line. So, when you want
//       to debug standard Arduino Uno boards, you have to disconnect the capacitor needed
//       for auto reset. On the original Uno boards there is a bridge labeled "RESET EN"
//       that you can cut. This does not apply to the Pro Mini boards! For Pro Mini
//       boards you have to make sure that only the TX/RX as well as the Vcc/GND
//       pins are connected. 
// NOTE: In order to enable debugWIRE mode, one needs to enable the DWEN fuse.
//       Use the gdb command "monitor init" to do so. Afterwards one has to power-cycle the
//       target system (if this is not supported by the hardware debugger).
//       From now on, one cannot reset the MCU using the reset line nor
//       is it possible to change fuses with ISP programming or load programs using
//       the ISP interface. The program has to be loaded using the debug load command.
//       With the gdb command "monitor stop", one can switch the MCU back to normal behavior
//       using this hardware debugger.
// Some of the code is inspired and/or copied  by/from
// - dwire-debugger (https://github.com/dcwbrown/dwire-debug)
// - DebugWireDebuggerProgrammer (https://github.com/wholder/DebugWireDebuggerProgrammer/),
// - AVR-GDBServer (https://github.com/rouming/AVR-GDBServer),  and
// - avr_debug (https://github.com/jdolinay/avr_debug).
// And, of course, all of it would have not been possible without the work of Rue Mohr's
// attempts on reverse enginering of the debugWIRE protocol:
// http://www.ruemohr.org/docs/debugwire.html
//

#define VERSION "0.9.9"
#ifndef DEBUG 
#define DEBUG    0   // for debugging the debugger!
#endif
#ifndef FREERAM
#define FREERAM  0   // for checking how much memory is left on the stack
#endif
#ifndef UNITTESTS
#define UNITTESTS 0  // unit testing
#endif
#define NEWCONN 1    // new method for connecting: first break and 'U', then reset
                     // and another (sometimes faster) 'U'

// pins
#define DEBTX    3    // TX line for TXOnlySerial
#define VCC      9    // Vcc control
#define RESET    8    // Target Pin 1 - RESET (needs to be 8 so that we can use it as an input for TIMER1)
#define MOSI    11    // Target Pin 5 - MOSI
#define MISO    12    // Target Pin 6 - MISO
#define SCK     13    // Target Pin 7 - SCK

// System LED
// change, if you want a different port for state signaling
#define LEDDDR  DDRB   // DDR of system LED
#define LEDPORT PORTB  // port register of system LED
#define LEDPIN  PB2    // pin 

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <util/delay.h>
#include "src/dwSerial.h"
#ifdef DEBUG
#include <TXOnlySerial.h> // only needed for (meta-)debuging
#endif
#include "src/debug.h" // some (meta-)debug macros

// some size restrictions

 #define MAXBUF 255
#define MAXBREAK 33 // maximum of active breakpoints (we need double as many!)

// clock rates 
#define GDB_BAUD      115200 // communcation speed with the host
#define DWIRE_RATE    (1000000 / 128) // Set default baud rate (1 MHz / 128)

// timeout for waiting for response, should be > 70ms for RESET response
// loop time in the the getResponse routine is roughly 2-3us, so 40,000 should do.
#define WAITLIMIT 40000UL 

// signals
#define SIGHUP  1     // connection to target lost
#define SIGINT  2     // Interrupt  - user interrupted the program (UART ISR) 
#define SIGILL  4     // Illegal instruction
#define SIGTRAP 5     // Trace trap  - stopped on a breakpoint
#define SIGSYS  12    // When some fatal failure in the debugger was detected
#define SIGABRT 6     // Too many breakpoints

// types of fatal errors
#define NO_FATAL 0
#define NO_EXEC_FATAL 1 // Error in executing instruction offline
#define BREAK_FATAL 2 // saved BREAK instruction in BP slot
#define NO_FREE_SLOT_FATAL 3 // no free slot in BP structure
#define INCONS_BP_FATAL 4 // inconsistency in BP counting
#define PACKET_LEN_FATAL 5 // packet length too large
#define WRONG_MEM_FATAL 6 // wrong memory type
#define NEG_SIZE_FATAL 7 // negative size of buffer
#define RESET_FAILED_FATAL 8 // reset failed
#define READ_PAGE_ADDR_FATAL 9 // an address that does not point to start of a page 
#define FLASH_READ_FATAL 10 // error when reading from flash memory
#define SRAM_READ_FATAL 11 //  error when reading from sram memory
#define WRITE_PAGE_ADDR_FATAL 12 // wrong page address when writing
#define ERASE_FAILURE_FATAL 13 // error when erasing flash memory
#define NO_LOAD_FLASH_FATAL 14 // error when loading page into flash buffer
#define PROGRAM_FLASH_FAIL_FATAL 15 // error when programming flash page
#define REENABLERWW_FAILED_FATAL 16 // Could not reenable RWW
#define EXEC_BREAK_FATAL 17 // Tried to execute the BREAK instruction offline
#define HWBP_ASSIGNMENT_INCONSISTENT_FATAL 18 // HWBP assignemnt is inconsistent
#define SELF_BLOCKING_FATAL 19 // there shouldn't be a BREAK instruction in the code
#define FLASH_READ_WRONG_ADDR_FATAL 20 // trying to read a flash word at a non-even address
#define NO_STEP_FATAL 21 // could not do a single-step operation
#define REMAINING_BREAKPOINTS_FATAL 22 // there are still some active BPs that shouldn't be there
#define REMOVE_NON_EXISTING_BP_FATAL 23 // tried to remove non-existing BP
#define RELEVANT_BP_NOT_PRESENT 24 // identified relevant BP not present any longer 
#define INPUT_OVERLFOW_FATAL 25 // input buffer overlfow - should not happen at all!

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
boolean toomanybps = false;

unsigned int hwbp = 0xFFFF; // the one hardware breakpoint (word address)

enum statetype {NOTCONN_STATE, PWRCYC_STATE, ERROR_STATE, CONN_STATE, RUN_STATE};

struct context {
  unsigned int wpc; // pc (using word addresses)
  unsigned int sp; // stack pointer
  byte sreg;    // status reg
  byte regs[32]; // general purpose regs
  boolean saved:1; // all of the regs have been saved
  statetype state:3; // system state
  unsigned long bps; // communication speed
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
const char attiny43[] PROGMEM = "ATtiny43";
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
const char atmega88a[] PROGMEM = "ATmega88A";
const char atmega88pa[] PROGMEM = "ATmega88PA";
const char atmega168a[] PROGMEM = "ATmega168A";
const char atmega168pa[] PROGMEM = "ATmega168PA";
const char atmega328[] PROGMEM = "ATmega328";
const char atmega328p[] PROGMEM = "ATmega328P";
const char atmega8u2[] PROGMEM = "ATmega8U2";
const char atmega16u2[] PROGMEM = "ATmega16U2";
const char atmega32u2[] PROGMEM = "ATmega32U2";

const char Connected[] PROGMEM = "Connected to ";

//  MCU parameters
struct mcu_type {
  unsigned int sig;        // two byte signature
  unsigned int ramsz;      // SRAM size
  unsigned int rambase;    // base address of SRAM
  unsigned int eepromsz;   // size of EEPROM
  unsigned int flashsz;    // size of flash memory
  unsigned int dwdr;       // address of DWDR register
  unsigned int pagesz;     // page size of flash memory
  unsigned int erase4pg;   // 1 when the MCU has a 4-page erase operation
  unsigned int bootaddr;   // highest address of possible boot section  (0 if no boot support)
  unsigned int eecr;       // address of EECR register
  unsigned int eearh;      // address of EARL register (0 if none)
  unsigned int rcosc;      // fuse pattern for setting RC osc as clock source
  unsigned int xtosc;      // fuse pattern for setting XTAL osc as clock source
  char         *name;      // pointer to name in PROGMEM
  unsigned int dwenfuse;   // bit mask for DWEN fuse in high fuse byte
  unsigned int ckdiv8;     // bit mask for CKDIV8 fuse in low fuse byte
  unsigned int ckmsk;      // bit mask for selecting clock source (and startup time)
  unsigned int eedr;       // address of EEDR (computed from EECR)
  unsigned int eearl;      // address of EARL (computed from EECR)
  unsigned int targetpgsz; // target page size (depends on pagesize and erase4pg)
} mcu;
  
// mcu attributes (for all AVR mcus supporting debugWIRE)
const unsigned int mcu_attr[] PROGMEM = {
  // sig sram   base eeprom flash  dwdr   pg  er4  boot    eecr eearh  rcosc xtosc name
  0x9007,  64,  0x60,   64,  1024, 0x2E,  32,   0, 0x0000, 0x1C, 0x00, 0x0A, 0x0A, attiny13,

  0x920C, 256,  0x60,   64,  4096, 0x27,  64,   0, 0x0000, 0x1C, 0x1F, 0x22, 0x3F, attiny43,

  0x910A, 128,  0x60,  128,  2048, 0x1f,  32,   0, 0x0000, 0x1C, 0x00, 0x24, 0x3F, attiny2313,
  0x920D, 256,  0x60,  256,  4096, 0x27,  64,   0, 0x0000, 0x1C, 0x00, 0x24, 0x3F, attiny4313,

  0x910B, 128,  0x60,  128,  2048, 0x27,  32,   0, 0x0000, 0x1C, 0x1F, 0x22, 0x3F, attiny24,   
  0x9207, 256,  0x60,  256,  4096, 0x27,  64,   0, 0x0000, 0x1C, 0x1F, 0x22, 0x3F, attiny44,
  0x930C, 512,  0x60,  512,  8192, 0x27,  64,   0, 0x0000, 0x1C, 0x1F, 0x22, 0x3F, attiny84,
  
  0x9215, 256, 0x100,  256,  4096, 0x27,  16,   1, 0x0000, 0x1C, 0x1F, 0x22, 0x3F, attiny441, 
  0x9315, 512, 0x100,  512,  8192, 0x27,  16,   1, 0x0000, 0x1C, 0x1F, 0x22, 0x3F, attiny841,
  
  0x9108, 128,  0x60,  128,  2048, 0x22,  32,   0, 0x0000, 0x1C, 0x1F, 0x22, 0x3F, attiny25,
  0x9206, 256,  0x60,  256,  4096, 0x22,  64,   0, 0x0000, 0x1C, 0x1F, 0x22, 0x3F, attiny45,
  0x930B, 512,  0x60,  512,  8192, 0x22,  64,   0, 0x0000, 0x1C, 0x1F, 0x22, 0x3F, attiny85,
  
  0x910C, 128,  0x60,  128,  2048, 0x20,  32,   0, 0x0000, 0x1C, 0x1F, 0x22, 0x3F, attiny261,
  0x9208, 256,  0x60,  256,  4096, 0x20,  64,   0, 0x0000, 0x1C, 0x1F, 0x22, 0x3F, attiny461,
  0x930D, 512,  0x60,  512,  8192, 0x20,  64,   0, 0x0000, 0x1C, 0x1F, 0x22, 0x3F, attiny861,
  
  0x9387, 512, 0x100,  512,  8192, 0x31, 128,   0, 0x0000, 0x1F, 0x22, 0x22, 0x3F, attiny87,
  0x9487, 512, 0x100,  512, 16384, 0x31, 128,   0, 0x0000, 0x1F, 0x22, 0x22, 0x3F, attiny167,

  0x9314, 512, 0x100,  256,  8192, 0x31,  64,   0, 0x0F7F, 0x1F, 0x22, 0x3E, 0x3E, attiny828,

  0x9209, 256, 0x100,   64,  4096, 0x31,  64,   0, 0x0000, 0x1F, 0x22, 0x2E, 0x2E, attiny48,
  0x9311, 512, 0x100,   64,  8192, 0x31,  64,   0, 0x0000, 0x1F, 0x22, 0x2E, 0x2E, attiny88,
  
  0x9412,1024, 0x100,  256, 16384, 0x2E,  32,   1, 0x0000, 0x1C, 0x1F, 0x22, 0x2F, attiny1634,
  
  0x9205, 512, 0x100,  256,  4096, 0x31,  64,   0, 0x0000, 0x1F, 0x22, 0x22, 0x3F, atmega48a,
  0x920A, 512, 0x100,  256,  4096, 0x31,  64,   0, 0x0000, 0x1F, 0x22, 0x22, 0x3F, atmega48pa,
  0x930A,1024, 0x100,  512,  8192, 0x31,  64,   0, 0x0F80, 0x1F, 0x22, 0x22, 0x3F, atmega88a,
  0x930F,1024, 0x100,  512,  8192, 0x2F,  64,   0, 0x0F80, 0x1F, 0x22, 0x22, 0x3F, atmega88pa,
  0x9406,1024, 0x100,  512, 16384, 0x31, 128,   0, 0x1F80, 0x1F, 0x22, 0x22, 0x3F, atmega168a,
  0x940B,1024, 0x100,  512, 16384, 0x31, 128,   0, 0x1F80, 0x1F, 0x22, 0x22, 0x3F, atmega168pa,
  0x9514,2048, 0x100, 1024, 32768, 0x31, 128,   0, 0x3F00, 0x1F, 0x22, 0x22, 0x3F, atmega328,
  0x950F,2048, 0x100, 1024, 32768, 0x31, 128,   0, 0x3F00, 0x1F, 0x22, 0x22, 0x3F, atmega328p,
  
  0x9389, 512, 0x100,  512,  8192, 0x31,  64,   0, 0x0000, 0x1F, 0x22, 0x22, 0x3F, atmega8u2,
  0x9489, 512, 0x100,  512, 16384, 0x31, 128,   0, 0x0000, 0x1F, 0x22, 0x22, 0x3F, atmega16u2,
  0x958A,1024, 0x100, 1024, 32768, 0x31, 128,   0, 0x0000, 0x1F, 0x22, 0x22, 0x3F, atmega32u2,
  0,
};

enum CkFuses { CkDiv8, CkDiv1, CkRc, CkXtal };

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
boolean       reportTimeout = true;   // If true, report read timeout errors
char          rpt[16];                // Repeat command buffer
byte          lastsignal = 0;

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

ISR(TIMER0_COMPA_vect)
{
  // only active during power-cycle wait, error, and when running
  // since it can be interrupted, it won't block SingleWireSerial input
  static int cnt = 0;
  static byte busy = 0;

  if (busy) return;
  busy++; // if this IRQ routine is already active, leave immediately
  sei();  // no need to protect against other interrupts
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

/******************* setup & loop ******************************/
void setup(void) {
  TIMSK0 = 0; // no millis interrupts
  
  LEDDDR |= LEDPIN; // switch on output for system LED
  DEBINIT(); 
  Serial.begin(GDB_BAUD);
  initSession();
}

void loop(void) {
  if (Serial.available()) {
    gdbHandleCmd();
  } else if (ctx.state == RUN_STATE) {
    if (dw.available()) {
      byte cc = dw.read();
      if (cc == 0x55) { // breakpoint reached
	//DEBLN(F("Execution stopped"));
	_delay_ms(5); // we need that in order to avoid conflicts on the line
	gdbSendState(SIGTRAP);
      }
    }
  }
}

/****************** system state routines ***********************/

// init all global vars when the debugger connects
void initSession(void)
{
  DEBLN(F("initSession"));
  bpcnt = 0;
  bpused = 0;
  toomanybps = false;
  hwbp = 0xFFFF;
  reportTimeout = true;
  lastsignal = 0;
  validpg = false;
  buffill = 0;
  fatalerror = NO_FATAL;
  setSysState(NOTCONN_STATE);
  pinMode(VCC, OUTPUT);
  digitalWrite(VCC, HIGH);
  if (!targetOffline()) gdbCleanupBreakpointTable();
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
    if (!checkCmdOk()) { // target is not connected any longer
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
  DEBPR(F("setSysState: "));
  DEBLN(newstate);
  TIMSK0 &= ~_BV(OCIE0A); // switch off!
  ctx.state = newstate;
  ontime = ontimes[newstate];
  offtime = offtimes[newstate];
  LEDDDR |= _BV(LEDPIN);
  if (ontimes[newstate] == 0) LEDPORT &= ~_BV(LEDPIN);
  else if (offtimes[newstate] == 0) LEDPORT |= _BV(LEDPIN);
  else {
    OCR0A = 0x80;
    TIMSK0 |= _BV(OCIE0A);
  }
  //DEBPR(F("On-/Offtime: ")); DEBPR(ontime); DEBPR(F(" / ")); DEBLN(offtime);
  //DEBPR(F("TIMSK0=")); DEBLNF(TIMSK0,BIN);
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
      if (checkCmdOk()) {
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
    gdbUpdateBreakpoints();                          /* remove all inactive breakpoints before! */
    gdbWriteMemory(buff + 1);
    break;
  case 'X':                                          /* write memory from binary data */
    gdbUpdateBreakpoints();                          /* remove all inactive breakpoints before */
    gdbWriteBinMemory(buff + 1); 
    break;
  case 'D':                                          /* detach the debugger */
    gdbCheckForRemainingBreakpoints();               /* check if there are still BPs */
    gdbUpdateBreakpoints();                          /* update breakpoints in memory before exit */
    validpg = false;
    setSysState(NOTCONN_STATE);                      /* set to unconnected state */
    targetContinue();                                /* let the target machine do what it wants to do! */
    gdbSendReply("OK");                              /* and signal that evefrything is OK */
    break;
  case 'k':                                          /* kill request */
    gdbCheckForRemainingBreakpoints();               /* check if there are still BPs */
    gdbUpdateBreakpoints();                          /* update breakpoints in memory before exit! */
    digitalWrite(RESET,LOW);                         /* hold reset line low so that MCU does not start */
    pinMode(RESET,OUTPUT);
    break;
  case 'c':                                          /* continue */
  case 'C':                                          /* continue with signal - just ignore signal! */
    s = gdbContinue();                               /* start execution on target at current PC */
    if (s) gdbSendState(s);                          /* if s != 0, it is a signal notifying an error */
    break;
  case 's':                                          /* single step */
  case 'S':                                          /* step with signal - just ignore signal */
    gdbSendSignal(gdbStep());                        /* do only one step and report reason why stopped */
    break;              
  case 'v':                                          /* notfication that process stopped */
    if (memcmp_P(buff, (void *)PSTR("vStopped"), 8) == 0) 
      gdbSendReply("OK");      
    else
      gdbSendReply("");
    break;
  case 'z':                                          /* remove break/watch point */
  case 'Z':                                          /* insert break/watch point */
    gdbHandleBreakpointCommand(buf);
    break;
  case 'q':                                          /* query requests */
    if (memcmp_P(buf, (void *)PSTR("qRcmd,"), 6) == 0)   /* monitor command */
	gdbParseMonitorPacket(buf+6);
    else if (memcmp_P(buff, (void *)PSTR("qSupported"), 10) == 0) {
      DEBLN(F("qSupported"));
	gdbSendPSTR((const char *)PSTR("PacketSize=FF;swbreak+")); 
	initSession();                               /* init all vars when gdb (re-)connects */
    } else if (memcmp_P(buf, (void *)PSTR("qC"), 2) == 0)
      /* current thread is always 1 */
      gdbSendReply("QC01");
    else if (memcmp_P(buf, (void *)PSTR("qfThreadInfo"), 12) == 0)
      /* always 1 thread*/
      gdbSendReply("m1");
    else if (memcmp_P(buf, (void *)PSTR("qsThreadInfo"), 12) == 0)
      /* send end of list */
      gdbSendReply("l");
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
  int para = 0;

  measureRam();

  gdbCheckForRemainingBreakpoints(); // check if there are still BPs 
  gdbUpdateBreakpoints();  // update breakpoints in memory before any monitor ops
  
  if (memcmp_P(buf, (void *)PSTR("73746f70"), 8) == 0)                     /* stop */
    gdbStop();
  else if  (memcmp_P(buf, (void *)PSTR("696e6974"), 8) == 0)               /* init */
    gdbConnect();
  else if (memcmp_P(buf, (void *)PSTR("666c617368636f756e74"), 20) == 0)   /* flashcount */
    gdbReportFlashCount();
  else if (memcmp_P(buf, (void *)PSTR("72616d"), 6) == 0)                  /* ram usage */
    gdbReportRamUsage();
  else if  (memcmp_P(buf, (void *)PSTR("636b64697638"), 12) == 0)          /* ckdiv8 */
    gdbSetCkFuses(CkDiv8);
  else if  (memcmp_P(buf, (void *)PSTR("636b64697631"), 12) == 0)          /* ckdiv1 */
    gdbSetCkFuses(CkDiv1);
  else if  (memcmp_P(buf, (void *)PSTR("72636f7363"), 10) == 0)            /* rcosc */
    gdbSetCkFuses(CkRc);
  else if  (memcmp_P(buf, (void *)PSTR("78746f7363"), 10) == 0)            /* xtosc */
    gdbSetCkFuses(CkXtal);  
  else if  (memcmp_P(buf, (void *)PSTR("68776270"), 8) == 0)               /* hwbp */
    gdbSetMaxBPs(1);
  else if  (memcmp_P(buf, (void *)PSTR("73776270"), 8) == 0)               /* swbp */
    gdbSetMaxBPs(MAXBREAK);
  else if  (memcmp_P(buf, (void *)PSTR("346270"), 6) == 0)                 /* 4bp */
    gdbSetMaxBPs(4);
#if UNITTESTS
  else if  (memcmp_P(buf, (void *)PSTR("746573746477"), 12) == 0)          /* testdw */
    DWtests(para);
  else if  (memcmp_P(buf, (void *)PSTR("746573747467"), 12) == 0)          /* testtg */
    targetTests(para);
  else if  (memcmp_P(buf, (void *)PSTR("74657374676462"), 14) == 0)        /* testgdb */
    gdbTests(para);
  else if  (memcmp_P(buf, (void *)PSTR("74657374616c6c"), 14) == 0)        /* testall */
    alltests();
#endif
  else if (memcmp_P(buf, (void *)PSTR("7265736574"), 10) == 0) {           /* reset */
    gdbReset();
    gdbSendReply("OK");
  } else gdbSendReply("");
}


// set maximum number of breakpoints
inline void gdbSetMaxBPs(byte num)
{
  maxbreak = num;
  gdbSendReply("OK");
}

// report on how many flash pages have been written
void gdbReportFlashCount(void)
{
  gdbDebugMessagePSTR(PSTR("Number of flash write operations: "), flashcnt);
  _delay_ms(5);
  flushInput();
  gdbSendReply("OK");
}

void gdbReportRamUsage(void)
{
#if FREERAM
  gdbDebugMessagePSTR(PSTR("Minimal number of free RAM bytes: "), freeram);
  _delay_ms(5);
  flushInput();
  gdbSendReply("OK");
#else
  gdbSendReply("");
#endif
}


// "monitor init"
// try to enable debugWIRE
// this might imply that the user has to power-cycle the target system
boolean gdbConnect(void)
{
  int retry = 0;
  byte b;
  int conncode;

  conncode = targetConnect();
  switch (conncode) {

  case 1: // everything OK since we are already connected
    setSysState(CONN_STATE);
    gdbDebugMessagePSTR(Connected,-2);
    gdbDebugMessagePSTR(PSTR("debugWIRE is now enabled, bps: "),ctx.bps);
    gdbReset();
    flushInput();
    gdbSendReply("OK");
    return true;
    break;
  case 0: // we have changed the fuse and need to powercycle
    setSysState(PWRCYC_STATE);
    while (retry < 60) {
      if (retry%3 == 0) { // try to power-cycle
	digitalWrite(VCC, LOW); // cutoff power to target
	_delay_ms(500);
	digitalWrite(VCC, HIGH); // power target again
	_delay_ms(200); // wait for target to startup
      }
      if ((retry++)%3 == 0 && retry >= 3) {
	do {
	  flushInput();
	  gdbDebugMessagePSTR(PSTR("Please power-cycle the target system"),-1);
	  b = gdbReadByte();
	} while (b == '-');
      }
      _delay_ms(1000);
      if (doBreak()) {
#if NEWCONN==0
	gdbReset();
#endif
	flushInput();
	gdbDebugMessagePSTR(Connected,-2);
	gdbDebugMessagePSTR(PSTR("debugWIRE is now enabled, bps: "),ctx.bps);
	_delay_ms(100);
	flushInput();
	gdbSendReply("OK");
	setSysState(CONN_STATE);
	return true;
      }
    }
    break;
  default:
    setSysState(ERROR_STATE);
    flushInput();
    switch (conncode) {
    case -1: gdbDebugMessagePSTR(PSTR("Cannot connect: Check wiring"),-1); break;
    case -2: gdbDebugMessagePSTR(PSTR("Cannot connect: Unsupported MCU type"),-1); break;
    case -3: gdbDebugMessagePSTR(PSTR("Cannot connect: Lock bits are set"),-1); break;
    default: gdbDebugMessagePSTR(PSTR("Cannot connect for unknown reasons"),-1); break;
    }
    break;
  }
  setSysState(ERROR_STATE);
  flushInput();
  gdbSendReply("E05");
  return false;
}

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
    
// issue reset on target
void gdbReset(void)
{
  targetReset();
  targetInitRegisters();
}

void gdbSetCkFuses(CkFuses fuse)
{
  boolean offline = targetOffline();
  int res; 

  setSysState(NOTCONN_STATE);
  res = targetSetCkFuses(fuse);
  if (res < 0) {
    if (res == -1) gdbDebugMessagePSTR(PSTR("Cannot connect: Check wiring"),-1);
    else if (res == -2) gdbDebugMessagePSTR(PSTR("Unsupported MCU type"),-1);
    else if (res == -3) gdbDebugMessagePSTR(PSTR("Fuse programming failed"),-1);
    flushInput();
    gdbSendReply("E05");
    return;
  }
  switch (fuse) {
  case CkDiv8: gdbDebugMessagePSTR(PSTR("CKDIV8 fuse is now programmed"),-1); break;
  case CkDiv1: gdbDebugMessagePSTR(PSTR("CKDIV8 fuse is now unprogrammed"),-1); break;
  case CkRc: gdbDebugMessagePSTR(PSTR("Clock source is now the RC oscillator"),-1); break;
  case CkXtal: gdbDebugMessagePSTR(PSTR("Clock source is now the XTAL oscillator"),-1); break;
  }
  _delay_ms(200);
  flushInput();
  if (offline) {
    gdbSendReply("OK");
    return;
  }
  gdbConnect();
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
  DEBLN(F("gdbBreakDetour"));
  if (targetIllegalOpcode(opcode)) {
    DEBPR(F("Illop: ")); DEBLNF(opcode,HEX);
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
  byte sig = SIGTRAP; // SIGTRAP (normal), SIGILL (if ill opcode), SIGSYS (fatal), SIGABRT (too many bps)
  DEBLN(F("Start step operation"));
  if (fatalerror) return SIGSYS;
  if (targetOffline()) return SIGHUP;
  if (toomanybps) return SIGABRT;
  if (gdbBreakPresent(opcode)) { // we have a break instruction inserted here
    return gdbBreakDetour(opcode);
  } else { // just single-step in flash
    DEBPR(F("Opcode: ")); DEBLNF(targetReadFlashWord(ctx.wpc*2),HEX);
    if (targetIllegalOpcode(targetReadFlashWord(ctx.wpc*2))) {
      DEBPR(F("Illop: ")); DEBLNF(targetReadFlashWord(ctx.wpc*2),HEX);
      return SIGILL;
    }
    targetRestoreRegisters();
    targetStep();
    if (!checkCmdOk2()) {
      ctx.saved = true; // just reinstantiate the old state
      reportFatalError(NO_STEP_FATAL, true);
      return SIGSYS;
    } else {
      targetSaveRegisters();
      if (oldpc == ctx.wpc) {
	if (Serial.available())
	  sig = SIGINT; // if we do not make progress in single-stepping, ^C can stop gdb
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
  if (fatalerror) sig = SIGSYS;
  else if (targetOffline()) sig = SIGHUP;
  else if (toomanybps) sig = SIGABRT;
  else {
    gdbUpdateBreakpoints();  // update breakpoints in flash memory
    if (gdbBreakPresent(opcode)) { // we have a break instruction inserted here
      reportFatalError(SELF_BLOCKING_FATAL, false);
      sig = SIGSYS;
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
void gdbUpdateBreakpoints(void)
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
  if (toomanybps || bpused == 0 || targetOffline()) return;

  // find relevant BPs
  for (i=0; i < MAXBREAK*2; i++) {
    if (bp[i].used) { // only used breakpoints!
      if (bp[i].active) { // active breakpoint
	if (!bp[i].inflash && !bp[i].hw)  // not in flash yet and not a hw bp
	  relevant[rel++] = bp[i].waddr; // remember to be set
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
	DEBPR(F("RELEVANT: ")); DEBLNF(relevant[j],HEX);
	ix = gdbFindBreakpoint(relevant[j++]);
	if (ix < 0) reportFatalError(RELEVANT_BP_NOT_PRESENT, false);
	DEBPR(F("Found BP:")); DEBLN(ix);
	if (bp[ix].active) { // enabled but not yet in flash
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
	} else { // disabled but still in flash
	  DEBPR(F("Restore original op ")); DEBPRF(bp[ix].opcode,HEX); DEBPR(F(" at byte addr ")); DEBLNF(bp[ix].waddr*2,HEX);
	  newpage[(bp[ix].waddr*2)%mcu.targetpgsz] = bp[ix].opcode&0xFF;
	  newpage[((bp[ix].waddr*2)+1)%mcu.targetpgsz] = bp[ix].opcode>>8;
	  bp[ix].used = false;
	  bp[ix].inflash = false;
	  bpused--; 
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
      gdbInsertBreakpoint(byteflashaddr >> 1);
    } else 
      gdbRemoveBreakpoint(byteflashaddr >> 1);
    gdbSendReply("OK");
    break;
  default:
    gdbSendReply("");
    break;
  }
}

/* insert bp, flash addr is in words */
void gdbInsertBreakpoint(unsigned int waddr)
{
  int i,j;

  measureRam();

  // check for duplicates
  i = gdbFindBreakpoint(waddr);
  if (i >= 0)
    if (bp[i].active)  // this is a BP set twice, can be ignored!
      return;
  
  // if we try to set too many bps, return
  if (bpcnt == maxbreak) {
    // DEBLN(F("Too many BPs to be set! Execution will fail!"));
    toomanybps = true;
    return;
  }

  // if bp is already there, but not active, then activate
  i = gdbFindBreakpoint(waddr);
  if (i >= 0) { // existing bp
    bp[i].active = true;
    bpcnt++;
    DEBPR(F("New recycled BP: ")); DEBPRF(waddr*2,HEX); DEBPR(F(" / now active: ")); DEBLN(bpcnt);
    return;
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
	  DEBLN(F("Stealing HWBP from other BP"));
	  bp[j].hw = false;
	  bp[i].hw = true;
	  hwbp = waddr;
	} else reportFatalError(HWBP_ASSIGNMENT_INCONSISTENT_FATAL, false);
      }
      bpcnt++;
      bpused++;
      DEBPR(F("New BP: ")); DEBPRF(waddr*2,HEX); DEBPR(F(" / now active: ")); DEBLN(bpcnt);
      if (bp[i].hw) { DEBLN(F("implemented as a HW BP")); }
      return;
    }
  }
  reportFatalError(NO_FREE_SLOT_FATAL, false);
  //DEBLN(F("***No free slot in bp array"));
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
  if (bpcnt <= maxbreak) toomanybps = false;
}

// check whether ther are still active breakpoints, if so -> fatal error
void gdbCheckForRemainingBreakpoints(void)
{
  int cnt = 0;

  measureRam();

  for (byte i=0; i < MAXBREAK*2; i++) {
    if (bp[i].active) {
      //DEBPR(F("Active BP found at: ")); DEBLNF(bp[i].waddr*2,HEX);
      cnt++;
    }
  }
  if (bpcnt != 0 || cnt != 0)
    reportFatalError(REMAINING_BREAKPOINTS_FATAL, false);
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
  gdbUpdateBreakpoints(); // now remove all breakpoints
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

  if (targetOffline()) {
    gdbSendReply("E01");
    return;
  }

  buff += parseHex(buff, &addr);
  /* skip 'xxx,' */
  parseHex(buff + 1, &sz);
  
  if (sz > 127) { // should not happen because we required packet length = 255:
    gdbSendReply("E05");
    reportFatalError(PACKET_LEN_FATAL, false);
    //DEBLN(F("***Packet length > 127"));
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
    reportFatalError(WRONG_MEM_FATAL, false);
    //DEBLN(F("***Wrong memory type in gdbReadMemory"));
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

// chec k whether connected
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
  case SIGSYS:
    gdbDebugMessagePSTR(PSTR("***Fatal internal debugger error: "),fatalerror);
    setSysState(ERROR_STATE);
    break;
  case SIGABRT:
    gdbDebugMessagePSTR(PSTR("Too many active breakpoints!"),-1);
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

// try to establish ISP connection
// if possible, set DWEN fuse
// if not possible, trx to connect directly to debugWIRE
//   1 if we are in debugWIRE mode and connected 
//   0 if we need to powercycle
//   -1 if we cannot connect
//   -2 if unknown MCU type
//   -3 if lock bits set

int targetConnect(void)
{
  unsigned int sig;
  int result = 0;
  
  if (doBreak()) {
    return (setMcuAttr(DWgetChipId()) ? 1 : -2);
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
  boolean succ = false;
  unsigned int sig;
  if (doBreak()) {
    sendCommand((const byte[]) {0x06}, 1); // leave debugWIREMode
    _delay_ms(50);
  }
  if (!enterProgramMode()) return false;
  sig = ispGetChipId();
  if (sig && setMcuAttr(sig)) {
    if (ispProgramFuse(true, mcu.dwenfuse, mcu.dwenfuse)) {
      DEBLN(F("DWEN disabled"));
      succ = true;
    }
  }
  leaveProgramMode();
  return succ;
}


// set the CKDIV fuse (true for CHKDIV8 to be programmed), returns
// 1 - if programmed
// -1 - if we cannot enter programming mode or sig is not readable
// -2 - if unknown MCU type
// -3 - programming was unsuccessful
 

int targetSetCkFuses(CkFuses fuse)
{
  byte newfuse, lowfuse;
  unsigned int sig;
  boolean succ;

  measureRam();
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
  case CkXtal: succ = ispProgramFuse(false, mcu.ckmsk, mcu.xtosc); break;
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
    DEBPR(F("using cached page at ")); DEBLNF(lastpg,HEX);
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

  DEBPR(F("Write flash ... "));
  DEBPRF(addr, HEX);
  DEBPR("-");
  DEBPRF(addr+mcu.targetpgsz-1,HEX);
  DEBLN(":");
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

#ifdef DEBUG
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
    DEBLN(F(" erasing ..."));
    if (!DWeraseFlashPage(addr)) {
      reportFatalError(ERASE_FAILURE_FATAL, true);
      // DEBLN(F(" not possible"));
      DWreenableRWW();
      return;
    } else {
      DEBLN(F(" will overwrite ..."));
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
    DEBPR(F("writing subpage at "));
    DEBLNF(addr+subpage*mcu.pagesz,HEX);
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
    DEBLN(F(" page flashed"));
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
  byte *ptr = &(ctx.regs[0]);
  measureRam();

  do {
    *ptr++ = a;
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
  dw.sendBreak();
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
  int timeout = 100;

  measureRam();

  sendCommand((const byte[]) {0x07}, 1);
  while (timeout--)
    if (dw.available()) break;
    else _delay_ms(1);
  if (checkCmdOk2()) {
    // DEBLN(F("RESET successful"));
    ctx.wpc = 0;
    return true;
  } else {
    // DEBLN(F("***RESET failed"));
    reportFatalError(RESET_FAILED_FATAL, true);
    return false;
  }
}

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
    return true;
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
    return true;
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
  byte timeout = 150;

  measureRam();

  digitalWrite(RESET, LOW);
  pinMode(RESET, INPUT);
  _delay_ms(10);
  DEBLN(F("Start calibrating"));
  ctx.bps = dw.calibrate();
  if (ctx.bps == 0) {
    // DEBLN(F("No response from debugWIRE on sending break"));
    return false;
  }
  DEBPR(F("First Speed: ")); DEBPRF(ctx.bps, DEC); DEBLN(F(" bps"));
  dw.begin(ctx.bps);                        // Set computed bit rate
#if NEWCONN==1
  // now we send a reset command, wait for a falling edge, then calibrate again
  sendCommand((const byte[]){0x07}, 1);
  dw.enable(false);
  while (timeout--)
    if (digitalRead(RESET) == LOW) break;
    else _delay_ms(1);
  if (timeout != 0) ctx.bps = dw.calibrate();
  if (ctx.bps == 0) {
    // DEBLN(F("No response from debugWIRE on sending break"));
    return false;
  }
  DEBPR(F("Second Speed: ")); DEBPRF(ctx.bps, DEC); DEBLN(F(" bps"));
  dw.begin(ctx.bps);                        // Set computed bit rate
  return true;
#else 
  DEBLN(F("Sending BREAK: "));
  dw.sendBreak();
  if (checkCmdOk()) {
    DEBLN(F("debugWIRE Enabled"));
    return true;
  } else {
    DEBLN(F("debugWIRE not enabled, is DWEN enabled?"));
  }
  return false;
#endif
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
  } while (timeout++ < WAITLIMIT);
  if (reportTimeout && expected > 0) {
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
  return ((unsigned int) tmp[0] << 8) + tmp[1];
}

// wait for a 0x55 response
boolean checkCmdOk () {
  byte tmp[1];
  byte len = getResponse(&tmp[0], 1);

  measureRam();
  if (len == 1 && tmp[0] == 0x55) {
    return true;
  } else {
    DEBPR(F("checkCmdOK Error: len="));
    DEBPR(len);
    DEBPR(" tmp[0]=");
    DEBLNF(tmp[0], HEX);
    return false;
  }
}

// wait for a BREAK/0x55 response
boolean checkCmdOk2 () {
  byte tmp[2];
  byte len = getResponse(&tmp[0], 2);

  measureRam();
if ( len == 2 && tmp[0] == 0x00 && tmp[1] == 0x55) {
    return true;
  } else {
    DEBPR(F("checkCmd2 Error: len="));
    DEBPR(len);
    DEBPR(" tmp[0]=");
    DEBPRF(tmp[0], HEX);
    DEBPR(" tmp[1]=");
    DEBLNF(tmp[1], HEX);
    return false;
  }
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
  //reportTimeout = false;
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
  reportTimeout = true;
  return rsp == len;
}

//   EEPROM Notes: This section contains code to read and write from EEPROM.  This is accomplished by setting parameters
//    into registers 28 - r31 and then using the 0xD2 command to send and execure a series of instruction opcodes on the
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
  //  reportTimeout = false;
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
  reportTimeout = true;
  return rsp==len;
}

// erase entire flash page
boolean DWeraseFlashPage(unsigned int addr) {
  // DEBPR(F("Erase: "));  DEBLNF(addr,HEX);
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
  measureRam();
  return checkCmdOk2();
}
		    
// now move the page from temp memory to flash
boolean DWprogramFlashPage(unsigned int addr)
{
  boolean succ;
  unsigned int timeout = 1000;
  
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
  
  if (!mcu.bootaddr) { // no bootloader
    succ = checkCmdOk2(); // wait for feedback
  } else { // bootloader: wait for spm to finish
    while ((DWreadSPMCSR() & 0x1F) != 0 && timeout-- != 0) { 
      _delay_us(100);
      //DEBPR("."); // wait
    }
    succ = (timeout != 0);
  }
  return succ;
}

// load bytes into temp memory
boolean DWloadFlashPageBuffer(unsigned int addr, byte *mem)
{
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
  return (getWordResponse()) ;
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

// execute a 2-byte instruction offline
void DWexecOffline(unsigned int opcode)
{
  byte cmd[] = {0xD2, (byte) (opcode >> 8), (byte) (opcode&0xFF), 0x23};
  measureRam();

  DEBPR(F("Offline exec: "));
  DEBLNF(opcode,HEX);
  sendCommand(cmd, sizeof(cmd));
}

void DWflushInput(void)
{
  while (dw.available()) {
    // DEBPR("@");
    char c = dw.read();
    // DEBLN(c);
  }
}


/***************************** a little bit of SPI programming ********/


void enableSpiPins () {
  pinMode(RESET, OUTPUT);
  digitalWrite(RESET, LOW);
  _delay_us(1);
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
  do {
    enableSpiPins();
    digitalWrite(RESET, HIGH); 
    _delay_us(30);             // short positive RESET pulse of at least 2 clock cycles
    digitalWrite(RESET, LOW);  
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
  pinMode(RESET, INPUT); // allow MCU to run or to communicate via debugWIRE
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
  byte rep;
  boolean succ = true;

  lowfuse = ispSend(0x50, 0x00, 0x00, 0x00, true);
  highfuse = ispSend(0x58, 0x08, 0x00, 0x00, true);
  extfuse = ispSend(0x50, 0x08, 0x00, 0x00, true);

  if (high) newfuse = highfuse;
  else newfuse = lowfuse;

  DEBPR(F("Old ")); if (high) DEBPR(F("high ")); DEBPR(F("fuse: ")); DEBLNF(newfuse,HEX);
  newfuse = (newfuse & ~fusemsk) | (fuseval & fusemsk);
  DEBPR(F("New ")); if (high) DEBPR(F("high ")); DEBPR(F("fuse: ")); DEBLNF(newfuse,HEX);

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

  while ((sig = pgm_read_word(&mcu_attr[ix*14]))) {
    if (sig == id) { // found the right mcu type
      ptr = &mcu.sig;
      for (byte f = 0; f < 14; f++) 
	*ptr++ = pgm_read_word(&mcu_attr[ix*14+f]);
      mcu.eearl = mcu.eecr + 2;
      mcu.eedr = mcu.eecr + 1;
// we treat the 4-page erase MCU as if pages were larger by a factor of 4!
      if (mcu.erase4pg) mcu.targetpgsz = mcu.pagesz*4; 
      else mcu.targetpgsz = mcu.pagesz;
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
      // DEBPR(F("Found MCU Sig: ")); DEBLNF(id, HEX);
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

