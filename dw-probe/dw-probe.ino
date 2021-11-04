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
// And, of course, all of it would not have been possible without the work of Rue Mohr's
// attempts on reverse enginering of the debugWire protocol: http://www.ruemohr.org/docs/debugwire.html
//
// Version 0.1 (27-May-21)
//   - initial version with only a minimal amount of coverage
//   - can connect via serial interface to host
//   - 'monitor init' establishes the connection to the target
//   - 'monitor stop' switches the MCU back to the normal state
//   - more accurate baud determination than in other programs based on TIMER1!
//   - already all high-level functions from avr_debug (?,H,T,g,G,m,M (except flash),
//     D, k, c, s (not functional yet), z, Z, v, q) are implemented
//   - all relevant low level functions from DebugWireDebuggerProgrammer are ported
//   - erase flash page implemented
//
// Version 0.2 (28-May-21)
//   - writing to flash works
//   - loading a file works (using the M command and X commands)
//
// Version 0.3 (28-May-21)
//   - fixed problem with not being able to read the PC after a break
//   - use LED_BUILTIN to signal system status
//   - fixed breakpoint address problem (converting from word to byte addresses)
//   - fixed inconsistent PC addresses (byte vs. word)
//   - hw breakpoint integrated
//
// Version 0.4 (29-MAY-21)
//   - use of hw breakpoint as one of the ordinary breakpoints
//   - new used field in bp struct
//   - works now in PlatformIO (if one uses a .gdbinit file!)
//   - fixed bp address bug
//
// Version 0.5 (31-May-21)
//   - less register saving and restoring makes single-stepping faster!
//   - fixed problem with clobbered PC after offline execution of insturuction by
//     incrementing internal PC twice
//   - also disallowed branching / jumping / calling / returning instructions in this context
//   - dynamic assignment of HW breakpoint:
//     if the same address is used twice in a row for a HW breakpoint and the second time
//     there is another breakpoint that has not been written to flash memory yet,
//     we reassign the HW BP to the new breakpoint. This way, the HW breakpoint
//     is more effectively used for single-stepping dynamic breakpoints, e.g. overstepping
//     a function.
//   - changed the handling of too many breakpoints to using an error message
//     when a breakpoint beyond the limit is going to be inserted. This allows us
//     to continue when we have been stopped using a temporary breakpoint inserted by GDB
//   - new monitor command "flashcount" that reports on how often a flash page write operation
//     had occured.
//   - new monitor command "ram" that reports on the minimal number of free bytes in RAM,
//     this command is usually disabled, though (see compile time constant FREERAM).
//   - added '*' as an charachter to be escaped in bin2mem; the documentation says that
//     it only needs to be escaped when comming from the stub, but avr-gdb seems to escape it
//     anyway.
//
// Version 0.6 (03-Jun-21)
//   - added support for ATtiny828 and ATtiny43
//   - issue an error when a byte is escaped although it should not have been instead
//     of silently ignoring it
//   - added gdbRemoveAllBreakpoints in order to avoid leaving active bps before reset etc.
//   - changed the number of entries of bp from MAXBREAKS*2+1 to one less, because we now
//     refuse to acknowledge every extra BP above the allowed number
//   - detach function now really detaches, i.e., continues execution on the target and leaves it alone.
//
// Version 0.7 (03-Jun-21)
//   - implementation of monitor ckdivX (promised already in the manual)
//
// Version 0.8 (08-Jun-21)
//   - problem with unsuccessful erase operations fixed: was actually an I/O unsynchronization bug.
//     Forgot to read the BREAK/'U' after the single-step operation when skipping a SW breakpoint
//     in gdbStepOverBP.
//   - added message explaining connection error
//
// Version 0.9 (30-Jun-21)
//   - make distinction between wiring error and unsupported MCU type when connecting
//   - implement automatic power-cycling
//   - waiting longer after trying to power-cycle before requesting it in order to avoid
//     the message to "power-cycle" 
//   - not confusing the user by saying that dW is "still enabled". You always get that it is
//     "now enabled"
//   
// Version 0.9.1 (02-Jul-21)
//   - added "defensive code" into the debugger that detects fatal errors
//     such as page erase failures and then reports them to the user
//     instead of silently ignoring it
//   - added code to detect loss of connection to the target
//   - added code to detect when lock bits are set
//   - excluded all MCUs with bootloader memory
//
// Version 0.9.2 (04-Jul-21)
//   - show speed of connection when calling "monitor init"
//   - initialize all vars when gdb reconnects (i.e., when gdb sends a qSupported packet)
//   - now we use TIMER1 for measuring the delay instead of counting execution cycles in
//     OnePinSerial: much more accurate!
//   - since this is still not enough, the millis and the status blinking interrupts have been
//     disabled (we now use _delay_ms and _delay_us); actually, the blink IRQ is now only used
//     for error blinking and for power-cycle
//     flashing -- all other interrupts are disabled, except, of course, for the
//     PCI interrupt for the debugWIRE line and the interrupt for the serial line;
//   - in order to get the center of the first bit, we now wait for 1,5 bits after the first edge
//     (minus the time used in the program for serving the IRQ etc - which is empirically determined);
//   - changed setTimeoutDelay(DWIRE_RATE) to setTimeoutDelay(ctx.baud) in order to minimize the delay
//     in the getResponse loop; it used to be 4ms, which was ridiculous and showed badly with the
//     new OnePinSerial class;
//   - in addition, the targetReset routine needed a seperate delay loop because the delay to the
//     response is not depended on the baud rate, but it is constant 75ms;
//   - finally, we can now also write flash memory in the ATmegas, so the exclusion of the
//     bootloader MCUs has been revoked
//
// Version 0.9.3 (04-Jul-21)
//   - Now it seems to work with targets running at 16 MHz. With SCOPE_TIMING enabled in OnePinSerial,
//     it worked beautifully; without, there were a number of glitches when reading.
//     So I put in some NOPs when SCOPE_TIMING is disabled ... and it
//     works. The only problem is, I do not know why, and this makes me nervous. Maybe time
//     to rework OnePinSerial from the ground up.
//
// Version 0.9.4 (02-Nov-21)
//   - Instead of OnePinSerial, we use now dwSerial, which uses in turn the (new) base class SingleWireSerial.
//     One order of magnitude more accurate and robust! Works up to 250 kbps without a hickup
//     (if the millis interrupt is disabled).
//   - Timeout in getResponse is now done using the number of cycle iterations, which is roughly 2-3us.
//     Meaning we should wait roughly 40,000 iterations if we want to wait up to 100 ms (works so far).
//
// Version 0.9.5 (04-Nov-21)
//   - changed the TXOnlySerial bps to 57600
//   - decreased timeout for "power-cycle" to 3 seconds in order to supress "timeout" message by gdb 
//   - all basic debugWIRE functions have now a DW prefix
//   - created "monitor testtg" and "monitor testbp" for unit testing the target functions
//     and the breakpoint functions, but have to come up with unit tests for them
//   - inserted "unit test" code for low level DebugWIRE functions, which can be called using "monitor testdw"
//   - registers are only saved (right after start or after a break) and restored (before execution or single-stepping)
//     no more partial saves etc. Is much faster since we use the bulk register transfer.
//   - no more noJumpInstr() since jumps can actually be executed offline
//   - calls to dw.sendCmd replaced by calls to new function sendCommand, which now waits for
//     Serial output to be finished in order to avoid any output interrupt, which can take up to 8.75 us
//     and by that confuse SingleWireSerial. It can withstand 6.6 us, but 8.75 us is too much at 125 kbps!
//
// Version 0.9.6 (04-Nov-21)
//   - reshuffeld code in gdbConnect without external effects. Added a case for "unknown reason" of a connection error.
//   - added more DW test cases and streamlined DW unit tests
//   - added target unit tests 
//   - added a 'monitor testall' command that executes all unit tests

#define VERSION "0.9.6"
#define DEBUG    0  // for debugging the debugger!
#define FREERAM  0  // for checking how much memory is left on the stack
#define UNITTESTS 1  // unit testing

// pins
#define DEBTX    3    // TX line for TXOnlySerial
#define VCC      9    // Vcc control
#define RESET    8    // Target Pin 1 - RESET (needs to be 8 so that we can use it as an input for TIMER1)
#define MOSI    11    // Target Pin 5 - MOSI
#define MISO    12    // Target Pin 6 - MISO
#define SCK     13    // Target Pin 7 - SCK

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <util/delay.h>
#include "dwSerial.h"
#ifdef DEBUG
#include <TXOnlySerial.h> // only needed for (meta-)debuging
#endif
#include "debug.h" // some (meta-)debug macros

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
#define SIGINT  2      // Interrupt  - user interrupted the program (UART ISR) 
#define SIGILL  4      // Illegal instruction
#define SIGTRAP 5      // Trace trap  - stopped on a breakpoint
#define SIGABRT 6      // Abort - when there are too many breakpoints
#define SIGSTOP  17    // When some fatal failure in the debugger was detected

// types of fatal errors
#define NO_FATAL 0
#define NO_STOP_FATAL 1 // Cannot stop execution by Ctrl-C
#define NO_EXEC_FATAL 2 // Error in executing intruction offline
#define NO_SINGLESTEP_FATAL 3 // error in single stepping
#define BREAK_FATAL 4 // saved BREAK instruction in BP slot
#define NO_FREE_SLOT_FATAL 5 // no free slot in BP structure
#define INCONS_BP_FATAL 6 // inonconsistency in BP counting
#define PACKET_LEN_FATAL 7 // packet length too large
#define WRONG_MEM_FATAL 8 // wrong memory type
#define NEG_SIZE_FATAL 9 // negative size of buffer
#define RESET_FAILED_FATAL 10 // reset failed
#define READ_PAGE_ADDR_FATAL 11 // an address that does not point to start of a page 
#define FLASH_READ_FATAL 12 // error when reading from flash memory
#define SRAM_READ_FATAL 13 //  error when reading from sram memory
#define WRITE_PAGE_ADDR_FATAL 14 // wrong page address when writing
#define ERASE_FAILURE_FATAL 15 // error when erasing flash memory
#define NO_LOAD_FLASH_FATAL 16 // error when loading page into flash buffer
#define PROGRAM_FLASH_FAIL_FATAL 17 // error when programming flash page
#define REENABLERWW_FAILED_FATAL 18 // Could not reenable RWW
#define EXEC_BREAK_FATAL 19 // Tried to execute the BREAK instruction offline

// some masks to interpret memory addresses
#define MEM_SPACE_MASK 0x00FF0000 // mask to detect what memory area is meant
#define FLASH_OFFSET   0x00000000 // flash is addressed starting from 0
#define SRAM_OFFSET    0x00800000 // RAM address from GBD is (real addresss + 0x00800000)
#define EEPROM_OFFSET  0x00810000 // EEPROM address from GBD is (real addresss + 0x00810000)

// some GDB variables
struct breakpoint
{
  bool used:1;      // bp is in use, i.e., has been set before; will be freed when not activated before next execution
  bool active:1;    // breakpoint is active, i.e., has been set by GDB
  bool inflash:1;   // breakpoint is in flash memory, i.e., BREAK instr has been set in memory
  bool hw:1;        // breakpoint is a hardware breakpoint, i.e., not set in memory, but HWBP is used
  unsigned int waddr; // word address of breakpoint
  unsigned int opcode; // opcode that has been replaced by BREAK (in little endian mode)
} bp[MAXBREAK*2];
int bpcnt = 0;        // number of ACTIVE breakpoints (there may be as many as MAXBREAK used ones from the last execution!)
boolean toomanybps = false;

unsigned int hwbp = 0xFFFF; // the one hardware breakpoint (word address)
unsigned int lasthwbp = 0xFFFF; // address we stopped last

struct context {
  unsigned int wpc; // pc (using word addresses)
  unsigned int sp; // stack pointer
  byte sreg;    // status reg
  byte regs[32]; // general purpose regs
  boolean running:1;    // whether target is running
  boolean targetcon:1;  // whether target is connected
  boolean saved:1; // all of the regs have been saved
  unsigned long bps; // communication speed
} ctx;

//  MCU parameters
struct mcu_type {
  unsigned int sig;
  unsigned int iosz;
  unsigned int ramsz;
  unsigned int rambase;
  unsigned int eepromsz;
  unsigned int flashsz;
  unsigned int dwdr;
  unsigned int pagesz;
  unsigned int bootaddr;
  unsigned int eecr;
  unsigned int eearh;
  unsigned int dwenfuse;
  unsigned int ckdiv8;
  unsigned int eedr;
  unsigned int eearl;
  boolean infovalid;
} mcu;
  
// mcu attributes (for all AVR mcus supporting debug wire)
const unsigned int mcu_attr[] PROGMEM = {
  // sig   io  sram   base eeprom flash  dwdr   pg    boot  eecr eearh  DWEN  CKD8
  0x9007,  64,   64,  0x60,   64,  1024, 0x2E,  32, 0x0000, 0x1C, 0x1F, 0x04, 0x10, // ATtiny13

  0x920C,  64,  256,  0x60,   64,  4096, 0x27,  64, 0x0000, 0x1C, 0x1F, 0x40, 0x80, // ATtiny43

  0x910A,  64,  128,  0x60,  128,  2048, 0x1f,  32, 0x0000, 0x1C, 0x1F, 0x80, 0x80, // ATtiny2313
  0x920D,  64,  256,  0x60,  256,  4096, 0x1f,  64, 0x0000, 0x1C, 0x1F, 0x80, 0x80, // ATtiny4313

  0x910B,  64,  128,  0x60,  128,  2048, 0x27,  32, 0x0000, 0x1C, 0x1F, 0x40, 0x80, // ATtiny24   
  0x9207,  64,  256,  0x60,  256,  4096, 0x27,  64, 0x0000, 0x1C, 0x1F, 0x40, 0x80, // ATtiny44
  0x930C,  64,  512,  0x60,  512,  8192, 0x27,  64, 0x0000, 0x1C, 0x1F, 0x40, 0x80, // ATtiny84
  
  0x9215, 224,  256, 0x100,  256,  4096, 0x27,  16, 0x0000, 0x1C, 0x1F, 0x40, 0x80, // ATtiny441
  0x9315, 224,  512, 0x100,  512,  8192, 0x27,  16, 0x0000, 0x1C, 0x1F, 0x40, 0x80, // ATtiny841
  
  0x9108,  64,  128,  0x60,  128,  2048, 0x22,  32, 0x0000, 0x1C, 0x1F, 0x40, 0x80, // ATtiny25
  0x9206,  64,  256,  0x60,  256,  4096, 0x22,  64, 0x0000, 0x1C, 0x1F, 0x40, 0x80, // ATtiny45
  0x930B,  64,  512,  0x60,  512,  8192, 0x22,  64, 0x0000, 0x1C, 0x1F, 0x40, 0x80, // ATtiny85  
  
  0x910C,  64,  128,  0x60,  128,  2048, 0x20,  32, 0x0000, 0x1C, 0x1F, 0x40, 0x80, // ATtiny261
  0x9208,  64,  256,  0x60,  256,  4096, 0x20,  64, 0x0000, 0x1C, 0x1F, 0x40, 0x80, // ATtiny461  
  0x930D,  64,  512,  0x60,  512,  8192, 0x20,  64, 0x0000, 0x1C, 0x1F, 0x40, 0x80, // ATtiny861
  
  0x9387, 224,  512, 0x100,  512,  8192, 0x31, 128, 0x0000, 0x1F, 0x22, 0x40, 0x80, // ATtiny87    
  0x9487, 224,  512, 0x100,  512, 16384, 0x31, 128, 0x0000, 0x1F, 0x22, 0x40, 0x80, // ATtiny167

  0x9314, 224,  512, 0x100,  256,  8192, 0x31,  64, 0x0F7F, 0x1F, 0x22, 0x40, 0x80, // ATtiny828 
  
  0x9412,  96, 1024, 0x100,  256, 16384, 0x2E,  32, 0x0000, 0x1C, 0x1F, 0x40, 0x80, // ATtiny1634
  
  0x9205, 224,  512, 0x100,  256,  4096, 0x31,  64, 0x0000, 0x1F, 0x22, 0x40, 0x80, // ATmega48A
  0x920A, 224,  512, 0x100,  256,  4096, 0x31,  64, 0x0000, 0x1F, 0x22, 0x40, 0x80, // ATmega48PA
  0x930A, 224, 1024, 0x100,  512,  8192, 0x31,  64, 0x0F80, 0x1F, 0x22, 0x40, 0x80, // ATmega88A
  0x930F, 224, 1024, 0x100,  512,  8192, 0x31,  64, 0x0F80, 0x1F, 0x22, 0x40, 0x80, // ATmega88PA
  0x9406, 224, 1024, 0x100,  512, 16384, 0x31, 128, 0x1F80, 0x1F, 0x22, 0x40, 0x80, // ATmega168A
  0x940B, 224, 1024, 0x100,  512, 16384, 0x31, 128, 0x1F80, 0x1F, 0x22, 0x40, 0x80, // ATmega168PA
  0x9514, 224, 2048, 0x100, 1024, 32768, 0x31, 128, 0x3F00, 0x1F, 0x22, 0x40, 0x80, // ATmega328
  0x950F, 224, 2048, 0x100, 1024, 32768, 0x31, 128, 0x3F00, 0x1F, 0x22, 0x40, 0x80, // ATmega328P
  
  0x9389, 224,  512, 0x100,  512,  8192, 0x31,  64, 0x0000, 0x1F, 0x22, 0x40, 0x80, // ATmega8U2
  0x9489, 224,  512, 0x100,  512, 16384, 0x31, 128, 0x0000, 0x1F, 0x22, 0x40, 0x80, // ATmega16U2
  0x958A, 224, 1024, 0x100, 1024, 32768, 0x31, 128, 0x0000, 0x1F, 0x22, 0x40, 0x80, // ATmega32U2
  0,
};

// some statistics
long flashcnt = 0; // number of flash writes 
#if FREERAM
int freeram = 2048; // minimal amount of free memory (only if enabled)
#define measureRam() freeRamMin()
#else
#define measureRam()
#endif

// communcation interface to target
dwSerial  dw;
boolean       reportTimeout = true;   // If true, report read timeout errors
boolean       progmode = false;
char          rpt[16];                // Repeat command buffer
byte          lastsignal = 0;

// use LED to signal system state
// LED off = not connected to target system
// LED flashing every half second = power-cycle target in order to enable debugWire
// LED blinking every 1/10 second = could not connect to target board
// LED constantly on = connected to target and target is halted
// Led blinks every 1/3 second = target is running
typedef  enum {INIT_STATE, NOTCONN_STATE, PWRCYC_STATE, ERROR_STATE, CONN_STATE, RUN_STATE} statetype;
volatile statetype sysstate = INIT_STATE;
volatile statetype laststate;
const int ontimes[6] =  {1,     0,   50, 100, 16000, 500};
const int offtimes[6] = {1, 16000, 1000, 100,     0, 500};

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
  static int cnt;

  if (laststate != sysstate) {
    laststate = sysstate;
    cnt = ontimes[sysstate];
  }
  if (cnt == ontimes[sysstate] && cnt) {
    digitalWrite(LED_BUILTIN, HIGH);
  } else if (cnt <= -offtimes[sysstate]) {
    cnt = ontimes[sysstate] + 1;
  } else if (cnt == -1) {
    digitalWrite(LED_BUILTIN, LOW);
  }
  cnt--;
}

/******************* setup & loop ******************************/
void setup(void) {
  pinMode(VCC, OUTPUT);
  digitalWrite(VCC, HIGH); // power target
  _delay_ms(100); // give target time to power up
  DEBINIT(); 
  dw.begin(DWIRE_RATE);    
  dw.enable(true);
  Serial.begin(GDB_BAUD);
  pinMode(LED_BUILTIN, OUTPUT);
  TIMSK0 = 0; // no millis interrupts
  initSession();
}

void loop(void) {
  if (Serial.available()) {
    gdbHandleCmd();
  } else if (ctx.running) {
    if (dw.available()) {
      byte cc = dw.read();
      if (cc == 0x55) { // breakpoint reached
	DEBLN(F("Execution stopped"));
	ctx.running = false;
	setSysState(CONN_STATE);
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
  ctx.running = false;
  ctx.targetcon = false;
  mcu.infovalid = false;
  bpcnt = 0;
  toomanybps = false;
  hwbp = 0xFFFF;
  lasthwbp = 0xFFFF;
  flashcnt = 0;
  reportTimeout = true;
  progmode = false;
    lastsignal = 0;
  validpg = false;
  buffill = 0;
  fatalerror = NO_FATAL;
  setSysState(INIT_STATE);
}

// report a fatal error and stop everything
// error will be displayed when trying to execute
void reportFatalError(byte errnum)
{
  DEBPR(F("***Report fatal error: "));
  DEBLN(errnum);
  if (fatalerror == NO_FATAL) fatalerror = errnum;
  setSysState(ERROR_STATE);
}

// change system state
// switch on blink IRQ when error or power-cycle state
void setSysState(statetype newstate)
{
  DEBPR(F("setSysState: "));
  DEBLN(newstate);
  if (newstate == INIT_STATE) {
    sysstate = INIT_STATE;
    newstate = NOTCONN_STATE;
  }
  laststate = sysstate;
  sysstate = newstate;
  if (newstate == PWRCYC_STATE || newstate == ERROR_STATE) {
    OCR0A = 0x80;
    TIMSK0 |= _BV(OCIE0A);
  } else {
    TIMSK0 &= ~_BV(OCIE0A);
    if (newstate == NOTCONN_STATE) digitalWrite(LED_BUILTIN, LOW);
    else digitalWrite(LED_BUILTIN, HIGH);
  }
}

/****************** gdbserver routines **************************/


// handle command from client
void gdbHandleCmd(void)
{
  byte checksum, pkt_checksum;
  byte b;
  
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
    if (ctx.running) {
      targetBreak(); // stop target
      ctx.running = false;
      if (checkCmdOk()) {
	setSysState(CONN_STATE);
      } else {
	setSysState(NOTCONN_STATE);
	ctx.targetcon = false;
	DEBLN(F("Connection lost"));
      }
      gdbSendState(SIGINT);
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
  switch (*buff) {
  case '?':               /* last signal */
    gdbSendSignal(lastsignal);  
    break;
  case 'H':               /* Set thread, always OK */
    gdbSendReply("OK");
    break;
  case 'T':               /* Is thread alive, always OK */
    gdbSendReply("OK");
    break;
  case 'g':               /* read registers */
    gdbReadRegisters();
    break;
  case 'G':               /* write registers */
    gdbWriteRegisters(buff + 1);
    break;
  case 'm':               /* read memory */
    gdbReadMemory(buff + 1);
    break;
  case 'M':               /* write memory */
    gdbWriteMemory(buff + 1);
    break;
  case 'X':               /* write memory from binary data */
    gdbWriteBinMemory(buff + 1); 
    break;
  case 'D':               /* detach the debugger */
    gdbRemoveAllBreakpoints(); // remove all BPs if there are still ones
    gdbUpdateBreakpoints();  // update breakpoints in memory before exit!
    ctx.targetcon = false;
    validpg = false;
    setSysState(NOTCONN_STATE);
    targetContinue();      // let the target machine do what it wants to do!
    gdbSendReply("OK");
    break;
  case 'k':               /* kill request */
    gdbRemoveAllBreakpoints(); // remove all BPs if there are still ones
    gdbUpdateBreakpoints();  // update breakpoints in memory before exit!
    pinMode(RESET,INPUT_PULLUP);
    digitalWrite(RESET,LOW); // hold reset line low so that MCU does not start
    break;
  case 'c':               /* continue */
  case 'C':               /* continue with signal - just ignore signal! */
    ctx.running = true;
    setSysState(RUN_STATE);
    gdbContinue();       /* start execution on target and continue with ctx.running = true */
    break;
  case 's':               /* step */
  case 'S':               /* step with signal - just ignore signal */
    ctx.running = true;
    setSysState(RUN_STATE);
    gdbStep();          /* do only one step */
    break;
  case 'v':
    if (memcmp_P(buff, (void *)PSTR("vStopped"), 8) == 0) 
      gdbSendReply("OK");      
    else
      gdbSendReply("");
    break;
  case 'z':               /* remove break/watch point */
  case 'Z':               /* insert break/watch point */
    gdbInsertRemoveBreakpoint(buf);
    break;
  case 'q':               /* query requests */
    if (memcmp_P(buf, (void *)PSTR("qRcmd,"), 6) == 0)   /* monitor command */
	gdbParseMonitorPacket(buf+6);
    else if (memcmp_P(buff, (void *)PSTR("qSupported"), 10) == 0) {
	gdbSendPSTR((const char *)PSTR("PacketSize=FF;swbreak+")); 
	initSession(); // init all vars when gdb (re-)connects
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

  if (memcmp_P(buf, (void *)PSTR("73746f70"), 8) == 0)   /* stop */
    gdbStop();
  else if  (memcmp_P(buf, (void *)PSTR("696e6974"), 8) == 0)     /* init */
    gdbConnect();
  else if (memcmp_P(buf, (void *)PSTR("666c617368636f756e74"), 20) == 0) /* flashcount */
    gdbReportFlashCount();
  else if (memcmp_P(buf, (void *)PSTR("72616d"), 6) == 0)            /* ram */
    gdbReportRamUsage();
  else if  (memcmp_P(buf, (void *)PSTR("636b64697638"), 12) == 0)    /* ckdiv8 */
    gdbSetCkdiv8(true);
  else if  (memcmp_P(buf, (void *)PSTR("636b64697631"), 12) == 0)    /* ckdiv1 */
    gdbSetCkdiv8(false);
#if UNITTESTS
  else if  (memcmp_P(buf, (void *)PSTR("746573746477"), 12) == 0)    /* testdw */
    DWtests(para);
  else if  (memcmp_P(buf, (void *)PSTR("746573747467"), 12) == 0)    /* testtg */
    targetTests(para);
  else if  (memcmp_P(buf, (void *)PSTR("746573746270"), 12) == 0)    /* testbp */
    BPtests(para);
  else if  (memcmp_P(buf, (void *)PSTR("74657374616c6c"), 14) == 0)   /* testall */
    alltests();
#endif
  else if (memcmp_P(buf, (void *)PSTR("7265736574"), 10) == 0) {     /* reset */
    gdbRemoveAllBreakpoints();
    gdbUpdateBreakpoints();  // update breakpoints in memory before reset
    gdbReset();
    gdbSendReply("OK");
  } else gdbSendReply("");
}


// run all unit tests
void alltests(void)
{
  int failed = 0;
  int testnum = 1;

  failed += DWtests(testnum);
  failed += targetTests(testnum);
  failed += BPtests(testnum);

  testSummary(failed);
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
// try to enable debugWire
// this might imply that the user has to power-cycle the target system
boolean gdbConnect(void)
{
  int retry = 0;
  byte b;
  int conncode = targetConnect();

  fatalerror = NO_FATAL; // in case we want to start over!
  switch (conncode) {

  case 1: // everything OK since we are already connected
    setSysState(CONN_STATE);
    ctx.targetcon = true;
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
	setSysState(CONN_STATE);
	gdbReset();
	flushInput();
	gdbDebugMessagePSTR(PSTR("debugWIRE is now enabled, bps:"),ctx.bps);
	_delay_ms(100);
	flushInput();
	gdbSendReply("OK");
	ctx.targetcon = true;
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
  ctx.targetcon = false;
  flushInput();
  gdbSendReply("E05");
  return false;
}

// try to disable the debugWire interface on the target system
void gdbStop(void)
{
  if (targetStop()) {
    gdbDebugMessagePSTR(PSTR("debugWire is now disabled"),-1);
    gdbSendReply("OK");
    ctx.targetcon = false;
    setSysState(NOTCONN_STATE);
  } else
    gdbDebugMessagePSTR(PSTR("debugWire could NOT be disabled"),-1);
    gdbSendReply("E05");
}

// issue reset on target
void gdbReset(void)
{
  targetReset();
  targetInitRegisters();
}

void gdbSetCkdiv8(boolean program)
{
  boolean oldcon = ctx.targetcon;
  int res; 

  ctx.targetcon = false;
  res = targetSetCKFuse(program);
  if (res < 0) {
    if (res == -1) gdbDebugMessagePSTR(PSTR("Cannot connect: Check wiring"),-1);
    else gdbDebugMessagePSTR(PSTR("Cannot connect: Unsupported MCU type"),-1);
    flushInput();
    gdbSendReply("E05");
    return;
  }
  if (res == 0) 
    gdbDebugMessagePSTR(program ? PSTR("Fuse CKDIV8 was already programmed") : PSTR("Fuse CKDIV8 was already unprogrammed"), -1);
  else
    gdbDebugMessagePSTR(program ? PSTR("Fuse CKDIV8 is now programmed") : PSTR("Fuse CKDIV8 is now unprogrammed"), -1);
  _delay_ms(200);
  flushInput();
  if (oldcon == false) {
    gdbSendReply("OK");
    return;
  }
  gdbConnect();
}

void gdbStep(void)
{
  DEBLN(F("Start step operation"));
  if (toomanybps || fatalerror|| !ctx.targetcon) {
    targetRestoreRegisters();
    gdbExecProblem(fatalerror || !ctx.targetcon);
    return;
  }
  if (!gdbStepOverBP(true)) { // only if not already stepped over BP
    targetRestoreRegisters();
    targetStep();
  }
}

void gdbContinue(void)
{
  DEBLN(F("Start continue operation"));
  targetRestoreRegisters();
  if (toomanybps || fatalerror || !ctx.targetcon) {
    gdbExecProblem(fatalerror || !ctx.targetcon);
    return;
  }
  gdbStepOverBP(false);    // either step over or remove BREAK from flash
  gdbUpdateBreakpoints();  // update breakpoints in memory
  targetRestoreRegisters();
  targetContinue();
}

void gdbExecProblem(byte fatal)
{
  if (fatal) {
    flushInput();
    targetBreak(); // check whether we lost connection
    if (checkCmdOk()) { // still connected, must be fatal error
      DEBLN(F("Fatal error: No restart"));
      gdbDebugMessagePSTR(PSTR("***Fatal internal debugger error: "),fatalerror);
      setSysState(ERROR_STATE);
    } else {
      DEBLN(F("Connection lost"));
      gdbDebugMessagePSTR(PSTR("Connection to target lost"),-1);
      setSysState(NOTCONN_STATE);
      ctx.targetcon = false;
    }
  } else {
    DEBLN(F("Too many bps"));
    gdbDebugMessagePSTR(PSTR("Too many active breakpoints!"),-1);
    setSysState(CONN_STATE);
  }
  _delay_ms(100);
  flushInput();
  DWsetWPc(ctx.wpc+1); // set to PC to current PC+1 because the 1 will be subtracted when reading the registers
  ctx.running = false;
  gdbSendState(fatal ? SIGSTOP : SIGABRT);
}

// If there is a breakpoint at the current PC
// location, we may need to execute the instruction offline
// or remove the BREAK from flash if the instruction is a
// 4 byte instruction that cannot be executed offline.
// onlystep = true means that there is just one step to do
// True is returned if the step was done, false if one still has to do it
boolean gdbStepOverBP(boolean onlyonestep)
{
  int bpix = gdbFindBreakpoint(ctx.wpc);
  boolean execsucc;
  if (bpix >= 0) { // bp at the current pc location
    DEBLN(F("Found BP at PC loc"));
    if (bp[bpix].inflash) { // there is a bp in flash!
      DEBLN(F("In flash!"));
      if (twoByteInstr(bp[bpix].opcode)) { // a two byte instruction
	DEBLN(F("Two byte instr / noJump"));
	targetRestoreRegisters(); // set all regs
	execsucc = DWexecOffline(bp[bpix].opcode); // execute the instruction offline
	if (!execsucc) {
	  DEBLN(F("***Offline execution error when single-stepping"));
	  reportFatalError(NO_EXEC_FATAL);
	  ctx.running = false; // mark that we are not executing any longer
	  gdbSendState(SIGSTOP);
	  return true;
	} else if (onlyonestep) { // if we do not continue
	  ctx.running = false; // mark that we are not executing any longer
	  setSysState(CONN_STATE);
	  gdbSendState(SIGTRAP); // and signal that to GDB
	  return true;
	}
      } else { // 4 bytes -> we need to replace the bp in flash!
	DEBLN(F("Four byte instr or jump"));
	DEBPR(F("Restore original opcode: "));
	DEBLNF(bp[bpix].opcode,HEX);
	targetReadFlashPage((bp[bpix].waddr*2) & ~(mcu.pagesz-1));
	memcpy(newpage, page, mcu.pagesz);
	DEBLN("");
	newpage[(bp[bpix].waddr*2)%mcu.pagesz] = bp[bpix].opcode&0xFF;
	newpage[((bp[bpix].waddr*2)+1)%mcu.pagesz] = bp[bpix].opcode>>8;
	targetWriteFlashPage((bp[bpix].waddr*2) & ~(mcu.pagesz-1), newpage);
	bp[bpix].inflash = false;
	if (!bp[bpix].active) {
	  bp[bpix].used = false; // delete bp if not active
	  DEBLN(F("BP removed"));
	}
	if (!onlyonestep) { // if not only one step, then step in order to be able to continue
	  DEBLN(F("Step over!"));
	  targetRestoreRegisters(); // set all regs
	  targetStep();         // step over the 4byte/call instr
	  if (!checkCmdOk2()) { 
	    reportFatalError(NO_SINGLESTEP_FATAL);
	    DEBLN(F("***Single step error"));
	    ctx.running = false; // mark that we are not executing any longer
	    gdbSendState(SIGSTOP);
	    return true;
	  }
	}
      }
    }
  }
  return false;
}


// Remove inactive and set active breakpoints before execution starts or before reset/kill/detach.
// Note that GDB sets breakpoints immediately before it issues a step or continue command and
// GDB removes the breakpoints right after the target has stopped. In order to minimize flash wear,
// we will inactivate the breakpoints when GDB wants to remove them, but we do not remove them
// from flash immediately. Only just before the target starts to execute, we will update flash memory
// according to the status of a breakpoint:
// BP is unused (addr = 0) -> do nothing
// BP is enabled and already in flash -> do nothing
// BP is enabled and not in flash -> write to flash
// BP is disabled but written in flash -> remove from flash, set BP unused
// BP is disabled and not in flash -> set BP unused
// order all actionable BPs by increasing address and only then change flash memory
// (so that multiple BPs in one page only need one flash change)
//
// Further, the hardware breakpoint is assigned dymically. If it is used twice in a row,
// then we look for a new breakpoint and will mark the old one for writing to flash.
void gdbUpdateBreakpoints(void)
{
  int i, j, ix, rel = 0;
  unsigned int relevant[MAXBREAK*2+1];
  unsigned addr = 0;

  DEBPR(F("Update Breakpoints: "));
  DEBPR(bpcnt);
  DEBPR(F(" / lasthwbp: "));
  DEBLNF(lasthwbp*2,HEX);
  // return immediately if there are too many bps active
  // because in this case we will not start to execute
  if (bpcnt > MAXBREAK) return;

  // reassign HW BP if not assigned to 4-byte instr, if used twice and if  possible 
  if (hwbp != 0xFFFF &&     // hwbp is in use
      hwbp == lasthwbp &&   //  and the same as last one
      bpcnt > 1) {          // and there are others
    for (i=0; i < MAXBREAK*2; i++) {
      if (bp[i].active &&        // active breakpoint
	  bp[i].waddr != hwbp && // not the HW breakpoint itself
	  !bp[i].inflash) {      // and not in flash yet
	j = gdbFindBreakpoint(hwbp);
	if (j < 0) {
	  DEBPR(F("Did not find HWBP for addr "));
	  DEBLN(hwbp*2);
	  break; // should not happen!
	}
	DEBPR(F("Reassign HW BP from "));
	DEBPRF(hwbp*2,HEX);
	DEBPR(F(" to "));
	DEBLNF(bp[i].waddr*2, HEX);
	bp[j].hw = false;
	bp[i].hw =true;
	hwbp = bp[i].waddr;
	break;
      }
    }
  }
  
  // find relevant BPs
  for (i=0; i < MAXBREAK*2; i++) {
    if (bp[i].used) { // only used breakpoints!
      if (bp[i].active) { // active breakpoint
	if (!bp[i].inflash && !bp[i].hw)  // not in flash yet and not a hw bp
	  relevant[rel++] = bp[i].waddr; // remember to be set
      } else { // inactive bp
	if (bp[i].inflash) // still in flash 
	  relevant[rel++] = bp[i].waddr; // remember to be removed
	else 
	  bp[i].used = false; // otherwise free BP
      }
    }
  }
  relevant[rel++] = 0xFFFF; // end marker
  DEBPR(F("Relevant bps: "));
  DEBLN(rel-1);

  // sort relevant BPs
  insertionSort(relevant, rel);
  DEBLN(F("BPs sorted: "));
  for (i = 0; i < rel-1; i++) DEBLNF(relevant[i]*2,HEX);

  // replace pages that need to be changed
  // note that the addresses in relevant and bp are all word addresses!
  i = 0;
  while (addr < mcu.flashsz && i < rel-1) {
    if (relevant[i]*2 >= addr && relevant[i]*2 < addr+mcu.pagesz) {
      j = i;
      while (relevant[i]*2 < addr+mcu.pagesz) i++;
      targetReadFlashPage(addr);
      memcpy(newpage, page, mcu.pagesz);
      while (j < i) {
	ix = gdbFindBreakpoint(relevant[j++]);
	DEBPR(F("Found BP:"));
	DEBLN(ix);
	if (bp[ix].active) { // enabled but not yet in flash
	  bp[ix].opcode = (newpage[(bp[ix].waddr*2)%mcu.pagesz])+
	    (unsigned int)((newpage[((bp[ix].waddr*2)+1)%mcu.pagesz])<<8);
	  DEBPR(F("Replace op in flash "));
	  DEBPRF(bp[ix].opcode,HEX);
	  DEBPR(F(" with BREAK at byte addr "));
	  DEBLNF(bp[ix].waddr*2,HEX);
	  if (bp[ix].opcode == 0x9598) { 
	    DEBLN(F("***Saved BREAK instruction in bp struct! This SHOULDN'T happen!"));
	    reportFatalError(BREAK_FATAL);
	  }
	  bp[ix].inflash = true;
	  newpage[(bp[ix].waddr*2)%mcu.pagesz] = 0x98; // BREAK instruction
	  newpage[((bp[ix].waddr*2)+1)%mcu.pagesz] = 0x95;
	} else { // disabled but still in flash
	  DEBPR(F("Restore original op "));
	  DEBPRF(bp[ix].opcode,HEX);
	  DEBPR(F(" at byte addr "));
	  DEBLNF(bp[ix].waddr*2,HEX);
	  newpage[(bp[ix].waddr*2)%mcu.pagesz] = bp[ix].opcode&0xFF;
	  newpage[((bp[ix].waddr*2)+1)%mcu.pagesz] = bp[ix].opcode>>8;
	  bp[ix].used = false;
	  bp[ix].inflash = false;
	}
      }
      targetWriteFlashPage(addr, newpage);
    }
    addr += mcu.pagesz;
  }
  lasthwbp = hwbp;
}

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


int gdbFindBreakpoint(unsigned int waddr)
{
  measureRam();

  for (byte i=0; i < MAXBREAK*2; i++)
    if (bp[i].waddr == waddr && bp[i].used) return i;
  return -1;
}

void gdbInsertRemoveBreakpoint(const byte *buff)
{
  unsigned long byteflashaddr, sz;
  byte len;

  if (targetoffline()) return;

  len = parseHex(buff + 3, &byteflashaddr);
  parseHex(buff + 3 + len + 1, &sz);
  
  /* get break type */
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
  int i;

  // if we try to set too many bps, return
  if (bpcnt == MAXBREAK) {
    DEBLN(F("Too many BPs to be set! Execution will fail!"));
    toomanybps = true;
    return;
  }
  // if bp is already there, but not active, then activate
  i = gdbFindBreakpoint(waddr);
  if (i >= 0) { // existing bp
    if (bp[i].active) return; //  can happen if duplicate bp
    bp[i].active = true;
    bpcnt++;
    DEBPR(F("New recycled BP: "));
    DEBPRF(waddr*2,HEX);
    DEBPR(F(" / now active: "));
    DEBLN(bpcnt);
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
      } else bp[i].hw = false;
      bpcnt++;
      DEBPR(F("New BP: "));
      DEBPRF(waddr*2,HEX);
      DEBPR(F(" / now active: "));
      DEBLN(bpcnt);
      if (bp[i].hw) {
	DEBLN(F("implemented as a HW BP"));
      }
      return;
    }
  }
  reportFatalError(NO_FREE_SLOT_FATAL);
  DEBLN(F("***No free slot in bp array"));
}

// inactivate a bp
void gdbRemoveBreakpoint(unsigned int waddr)
{
  int i;
  i = gdbFindBreakpoint(waddr);
  if (i < 0) return; // not found, could happen for duplicate bps
  DEBPR(F("Remove BP: "));
  DEBLNF(bp[i].waddr*2,HEX);
  bp[i].active = false;
  if (bp[i].hw) { // a HW BP can be freed
    bp[i].waddr = 0;
    bp[i].used = false;
    bp[i].hw = false;
    hwbp = 0xFFFF;
    DEBLN(F("HW BP inactivated & freed"));
  } else {
    DEBLN(F("SW BP inactivated"));
    if (!bp[i].inflash) { // a SW BP not in flash can be freed
      bp[i].waddr = 0;
      bp[i].used = false;
      DEBLN(F("SW BP freed"));
    }
  }
  bpcnt--;
  DEBPR(F("BP removed: "));
  DEBPRF(waddr*2,HEX);
  DEBPR(F(" / now active: "));
  DEBLN(bpcnt);
  if (bpcnt < MAXBREAK) toomanybps = false;
}

// remove all active breakpoints before reset etc
void gdbRemoveAllBreakpoints(void)
{
  int cnt = 0;
  DEBPR(F("Remove all breakpoints. There are still: "));
  DEBLN(bpcnt);
  for (byte i=0; i < MAXBREAK*2; i++) {
    if (bp[i].active) {
      bp[i].active = false;
      bp[i].hw = false;
      if (!bp[i].inflash) bp[i].used = false;
      cnt++;
      DEBPR(F("BP removed at: "));
      DEBLNF(bp[i].waddr*2,HEX);
    }
  }
  if (cnt != bpcnt) {
    DEBLN(F("***Inconsistent bpcnt number: "));
    DEBPR(cnt);
    DEBLN(F(" BPs removed!"));
    reportFatalError(INCONS_BP_FATAL);
  }
  bpcnt = 0;
  lasthwbp = 0xFFFF;
}

int BPtests(int &num) {
  (void)(num);
  return 0;
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

  if (targetoffline()) return;

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

  if (targetoffline()) return;

  buff += parseHex(buff, &addr);
  /* skip 'xxx,' */
  parseHex(buff + 1, &sz);
  
  if (sz > 127) { // should not happen because we required packet length = 255:
    gdbSendReply("E05");
    reportFatalError(PACKET_LEN_FATAL);
    DEBLN(F("***Packet length > 127"));
    return;
  }

  flag = addr & MEM_SPACE_MASK;
  addr &= ~MEM_SPACE_MASK;
  if (flag == SRAM_OFFSET) targetReadSram(addr, membuf, sz);
  else if (flag == FLASH_OFFSET) targetReadFlash(addr, membuf, sz);
  else if (flag == EEPROM_OFFSET) targetReadEeprom(addr, membuf, sz);
  else {
    gdbSendReply("E05");
    reportFatalError(WRONG_MEM_FATAL);
    DEBLN(F("***Wrong memory type in gdbReadMemory"));
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

// write to target memory
void gdbWriteMemory(const byte *buff)
{
  unsigned long flag, addr, sz;
  byte i;

  if (targetoffline()) return;
  
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
    reportFatalError(WRONG_MEM_FATAL);
    DEBLN(F("***Wrong memory type in gdbWriteMemory"));
    return;
  }
  gdbSendReply("OK");
}

static void gdbWriteBinMemory(const byte *buff) {
  unsigned long flag, addr, sz;
  int memsz;

  if (targetoffline()) return;

  buff += parseHex(buff, &addr);
  /* skip 'xxx,' */
  buff += parseHex(buff + 1, &sz);
  /* skip , and : delimiters */
  buff += 2;
  
  // convert to binary data by deleting the escapes
  memsz = gdbBin2Mem(buff, membuf, sz);
  if (memsz < 0) { 
    gdbSendReply("E05");
    reportFatalError(NEG_SIZE_FATAL);
    DEBLN(F("***Negative packet size"));
    return;
  }
  
  flag = addr & MEM_SPACE_MASK;
  addr &= ~MEM_SPACE_MASK;
  if (flag == SRAM_OFFSET) targetWriteSram(addr, membuf, memsz);
  else if (flag == FLASH_OFFSET) targetWriteFlash(addr, membuf, memsz);
  else if (flag == EEPROM_OFFSET) targetWriteEeprom(addr, membuf, memsz);
  else {
    gdbSendReply("E05"); 
    reportFatalError(WRONG_MEM_FATAL);
    DEBLN(F("***Wrong memory type in gdbWriteBinMemory"));
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

boolean targetoffline(void)
{
  // if not connected to target yet, reply with error and return immediately
  if (ctx.targetcon) return false;
  gdbSendReply("E05");
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
  gdbState2Buf(signo);
  gdbSendBuff(buf, buffill);
  lastsignal = signo;
}

// send a message the user can see, if last argument positive, then send the number
void gdbDebugMessagePSTR(const char pstr[],long num) {
  byte i = 0, j = 0, c;
  byte numbuf[10];

  buf[i++] = 'O';
  do {
    c = pgm_read_byte(&pstr[j++]);
    if (c) {
      buf[i++] = nib2hex((c >> 4) & 0xf);
      buf[i++] = nib2hex((c >> 0) & 0xf);
    }
  } while (c);
  if (num >= 0) {
    convNum(numbuf,num);
    j = 0;
    while (numbuf[j] != '\0') j++;
    while (j-- > 0) {
      buf[i++] = nib2hex((numbuf[j] >> 4) & 0xf);
      buf[i++] = nib2hex((numbuf[j] >> 0) & 0xf);
    }
  }
  buf[i++] = '0';
  buf[i++] = 'A';
  buf[i] = 0;
  gdbSendBuff(buf, i);
}

/****************** target functions *************/

// enable DebugWire, returns
//   1 if we are in debugWIRE mode and connected 
//   0 if we need to powercycle
//   -1 if we cannot connect
//   -2 if unknown MCU type
//   -3 if lock bits set
int targetConnect(void)
{
  unsigned int sig;
  if (ctx.targetcon) return 1;
  if (doBreak()) {
    leaveProgramMode();
    return (setMcuAttr(DWgetChipId()) ? 1 : -2);
  }
  if (!enterProgramMode()) {
    DEBLN(F("Neither in debugWIRE nor ISP mode"));
    leaveProgramMode();
    return -1;
  }
  sig = SPIgetChipId();
  if (sig == 0) {
    leaveProgramMode();
    return -1;
  }
  if (!setMcuAttr(sig)) {
    leaveProgramMode();
    return -2;
  }
  byte lockbits = ispSend(0x58, 0x00, 0x00, 0x00);
  if (lockbits != 0xFF) return -3;
  byte highfuse = ispSend(0x58, 0x08, 0x00, 0x00);
  byte newfuse = highfuse & ~mcu.dwenfuse;
  if (newfuse != highfuse) {
    ispSend(0xAC, 0xA8, 0x00, newfuse);
    DEBLN(F("DWEN enabled"));
  } else {
    DEBLN(F("DWEN was already enabled"));
  }
  leaveProgramMode();
  return 0;
}

// disable debugWIRE mode
boolean targetStop(void)
{
  if (doBreak()) {
    if (setMcuAttr(DWgetChipId())) {
      sendCommand((const byte[]) {0x06}, 1); // leave debugWireMode
      _delay_ms(50);
      enterProgramMode();
      byte highfuse = ispSend(0x58, 0x08, 0x00, 0x00);
      byte newfuse;
      newfuse = highfuse | mcu.dwenfuse;
      if (newfuse != highfuse) {
	ispSend(0xAC, 0xA8, 0x00, newfuse);
	DEBLN(F("DWEN disabled"));
	leaveProgramMode();
	return true;
      }
      leaveProgramMode();
    }
  }
  leaveProgramMode();
  return false;
}

// set the CKDIV fuse (true for CHKDIV8 to be programmed), returns
// 1 - if programmed
// 0 - if no change is necessary
// -1 - if we cannot enter programming mode or sig is not readable
// -2 - if unknown MCU type
 

int targetSetCKFuse(boolean programfuse)
{
  byte newfuse, lowfuse;
  unsigned int sig;
  if (doBreak()) {
    if (setMcuAttr(DWgetChipId())) 
      sendCommand((const byte[]) {0x06}, 1); // leave debugWIRE mode
    else
      return -1;
  } else {
    if (!enterProgramMode()) return -1;
    sig = SPIgetChipId();
    if (sig == 0) {
      leaveProgramMode();
      return -1;
    }
    if (!setMcuAttr(sig)) {
      leaveProgramMode();
      return -2;
    }
  }
  if (!enterProgramMode()) return -1;
  // now we are in ISP mode and know what processor we are dealing with
  lowfuse = ispSend(0x50, 0x00, 0x00, 0x00);
  DEBPR(F("Old low fuse: ")); DEBLNF(lowfuse,HEX);
  if (programfuse) newfuse = lowfuse & ~mcu.ckdiv8;
  else newfuse = lowfuse | mcu.ckdiv8;
  DEBPR(F("New fuse: ")); DEBLNF(newfuse,HEX);
  if (newfuse == lowfuse) {
    leaveProgramMode();
    return 0;
  }
  else ispSend(0xAC, 0xA0, 0x00, newfuse);
  DEBLN(F("New fuse programmed"));
  leaveProgramMode();
  return 1;
}


// read one flash page with base address 'addr' into the global 'page' buffer,
// do this only if the page has not been read already,
// remember that page has been read and that the buffer is valid
void targetReadFlashPage(unsigned int addr)
{
  DEBPR(F("Reading flash page starting at: "));
  DEBLNF(addr,HEX);
  if (addr != (addr & ~(mcu.pagesz-1))) {
    DEBLN(F("***Page address error when reading"));
    reportFatalError(READ_PAGE_ADDR_FATAL);
    return;
  }
  if (!validpg || (lastpg != addr)) {
    targetReadFlash(addr, page, mcu.pagesz);
    lastpg = addr;
    validpg = true;
  } else {
    DEBPR(F("using cached page at "));
    DEBLNF(lastpg,HEX);
  }
}

// read some portion of flash memory to the buffer pointed at by *mem'
// do not check for cached pages etc.
void targetReadFlash(unsigned int addr, byte *mem, unsigned int len)
{
  if (!DWreadFlash(addr, mem, len)) {
    reportFatalError(FLASH_READ_FATAL);
    DEBPR(F("***Error reading flash memory at "));
    DEBLNF(addr,HEX);
  }
}

// read some portion of SRAM into buffer pointed at by *mem
void targetReadSram(unsigned int addr, byte *mem, unsigned int len)
{
  if (!DWreadSramBytes(addr, mem, len)) {
    reportFatalError(SRAM_READ_FATAL);
    DEBPR(F("***Error reading SRAM at "));
    DEBLNF(addr,HEX);
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
void targetWriteFlashPage(unsigned int addr, byte *mem)
{
  DEBPR(F("Write flash ... "));
  DEBPRF(addr, HEX);
  DEBPR("-");
  DEBPRF(addr+mcu.pagesz-1,HEX);
  DEBLN(":");
  if (addr != (addr & ~(mcu.pagesz-1))) {
    DEBLN(F("\n***Page address error when writing"));
    reportFatalError(WRITE_PAGE_ADDR_FATAL);
    return;
  }
  DWreenableRWW();
  // read old page contents
  targetReadFlashPage(addr);
  // check whether something changed
  DEBPR(F("Check for change: "));
  if (memcmp(mem, page, mcu.pagesz) == 0) {
    DEBLN(F("unchanged"));
    return;
  }
  DEBLN(F("changed"));

#ifdef DEBUG
  for (unsigned int i=0; i<mcu.pagesz; i++) {
    if (page[i] != mem[i]) {
      DEBPR(i);
      DEBPR(":");
      DEBPRF(page[i], HEX);
      DEBPR(" ");
      DEBPRF(mem[i], HEX);
      DEBLN("");
    }
  }
#endif
  
  // check whether we need to erase the page
  boolean dirty = false;
  for (byte i=0; i < mcu.pagesz; i++) 
    if (~page[i] & mem[i]) {
      dirty = true;
      break;
    }

  validpg = false;
  
  // erase page when dirty
  if (dirty) {
    DEBPR(F(" erasing ..."));
    if (!DWeraseFlashPage(addr)) {
      reportFatalError(ERASE_FAILURE_FATAL);
      DEBLN(F(" not possible"));
      DWreenableRWW();
      return;
    } else {
      DEBPR(F(" will overwrite ..."));
    }
    
    // maybe the new page is also empty?
    memset(page, 0xFF, mcu.pagesz);
    if (memcmp(mem, page, mcu.pagesz) == 0) {
      DEBLN(" nothing to write");
      DWreenableRWW();
      validpg = true;
      return;
    }
  }
  if (!DWloadFlashPageBuffer(addr, mem)) {
    DWreenableRWW();
    reportFatalError(NO_LOAD_FLASH_FATAL);
    DEBPR(F("\n***Cannot load page buffer "));
    return;
  } else {
    DEBPR(F(" flash buffer loaded"));
  }
    
  boolean succ = DWprogramFlashPage(addr);
  DWreenableRWW();
  if (succ) {
    memcpy(page, mem, mcu.pagesz);
    validpg = true;
    lastpg = addr;
    DEBLN(F(" page flashed"));
  } else {
    DEBLN(F("\n***Could not program flash memory"));
    reportFatalError(PROGRAM_FLASH_FAIL_FATAL);
  }
}

// write some chunk of data to flash,
// break it up into pages and flash also the
// the partial pages in the beginning and in the end
void targetWriteFlash(unsigned int addr, byte *mem, unsigned int len)
{
  unsigned int pageoffmsk = mcu.pagesz-1;
  unsigned int pagebasemsk = ~pageoffmsk;
  unsigned int partbase = addr & pagebasemsk;
  unsigned int partoffset = addr & pageoffmsk;
  unsigned int partlen = min(mcu.pagesz-partoffset, len);

  if (len == 0) return;

  if (addr & pageoffmsk)  { // mem starts in the middle of a page
    targetReadFlashPage(partbase);
    memcpy(newpage, page, mcu.pagesz);
    memcpy(newpage + partoffset, mem, partlen);
    targetWriteFlashPage(partbase, newpage);
    addr += partlen;
    mem += partlen;
    len -= partlen;
  }

  // now write whole pages
  while (len >= mcu.pagesz) {
    targetWriteFlashPage(addr, mem);
    addr += mcu.pagesz;
    mem += mcu.pagesz;
    len -= mcu.pagesz;
  }

  // write remaining partial page (if any)
  if (len) {
    targetReadFlashPage(addr);
    memcpy(newpage, page, mcu.pagesz);
    memcpy(newpage, mem, len);
    targetWriteFlashPage(addr, newpage);
  }
}

// write SRAM chunk
void targetWriteSram(unsigned int addr, byte *mem, unsigned int len)
{
  for (unsigned int i=0; i < len; i++) 
    DWwriteSramByte(addr+i, mem[i]);
}

// write EEPROM chunk
void targetWriteEeprom(unsigned int addr, byte *mem, unsigned int len)
{
  for (unsigned int i=0; i < len; i++) 
    DWwriteEepromByte(addr+i, mem[i]);
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
  if (ctx.saved) return; // If the regs have been saved, then the machine regs are clobbered, so do not load again!
  ctx.wpc = DWgetWPc(); // needs to be done first, because the PC is advanced when executing instrs in the instr reg
  DWreadRegisters(&ctx.regs[0]); // now get all GP registers
  ctx.sreg = DWreadIOreg(0x3F);
  ctx.sp = DWreadIOreg(0x3D);
  if (mcu.ramsz > 256) ctx.sp |= DWreadIOreg(0x3E) << 8;
  ctx.saved = true;
}

// restore all registers on target (before execution continues)
void targetRestoreRegisters(void)
{
  if (!ctx.saved) return; // if not in saved state, do not restore!
  DWwriteIOreg(0x3D, (ctx.sp&0xFF));
  if (mcu.ramsz > 256) DWwriteIOreg(0x3E, (ctx.sp>>8)&0xFF);
  DWwriteIOreg(0x3F, ctx.sreg);
  DWwriteRegisters(&ctx.regs[0]);
  DWsetWPc(ctx.wpc); // must be done last!
  ctx.saved = false; // now, we can save them again and be sure to get the right values
}

// send break in order to sop execution asynchronously
void targetBreak(void)
{
  dw.sendBreak();
}

// start to execute
void targetContinue(void)
{
  measureRam();

  DEBPR(F("Continue at (byte adress) "));
  DEBLNF(ctx.wpc*2,HEX);
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

  DEBPR(F("Single step at (byte address):"));
  DEBLNF(ctx.wpc*2,HEX);
  // _delay_ms(5);
  byte cmd[] = {0x60, 0xD0, (byte)(ctx.wpc>>8), (byte)(ctx.wpc), 0x31};
  sendCommand(cmd, sizeof(cmd));
}

// reset the MCU
boolean targetReset(void)
{
  int timeout = 100;

  sendCommand((const byte[]) {0x07}, 1);
  while (timeout--)
    if (dw.available()) break;
    else _delay_ms(1);
  if (checkCmdOk2()) {
    DEBLN(F("RESET successful"));
    ctx.wpc = 0;
    return true;
  } else {
    DEBLN(F("***RESET failed"));
    reportFatalError(RESET_FAILED_FATAL);
    return false;
  }
}

// check whether an opcode is a 16-bit opcode
boolean twoByteInstr(unsigned int opcode)
{
  measureRam();

  if (((opcode & 0b1111110000001111) == (unsigned int)0b1001000000000000) || // STS/LDS
      ((opcode & 0b1111111000001100) == (unsigned int)0b1001010000001100))   // JMP/CALL
    return false;
  return true;
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
  0x08, 0x95
};
 


void setupTestCode()
{
  // execution related functions: setup test code in target
 memcpy_P(membuf, testcode, sizeof(testcode));
 targetWriteFlash(0x1aa, membuf, sizeof(testcode));
}


int targetTests(int &num) {
  int failed = 0;
  bool succ;
  int testnum;
  byte i;
  long lastflashcnt;

  if (num >= 1) testnum = num;
  else testnum = 1;

  gdbDebugMessagePSTR(PSTR("Test targetStop (not possible): "), testnum++);
  failed += testResult(ctx.targetcon);

  gdbDebugMessagePSTR(PSTR("Test targetConnect: "), testnum++);
  failed += testResult(targetConnect() == 1);

  gdbDebugMessagePSTR(PSTR("Test targetSetCKFuse (not possible): "), testnum++);
  failed += testResult(ctx.targetcon);

  gdbDebugMessagePSTR(PSTR("Test targetWriteFlashPage: "), testnum++);
  const int flashaddr = 0x80;
  fatalerror = NO_FATAL; setSysState(CONN_STATE);
  DWeraseFlashPage(flashaddr);
  DWreenableRWW();
  validpg = false;
  for (i=0; i < mcu.pagesz; i++) page[i] = 0;
  for (i=0; i < mcu.pagesz; i++) membuf[i] = i;
  targetWriteFlashPage(flashaddr, membuf);
  lastflashcnt = flashcnt;
  failed += testResult(fatalerror == NO_FATAL);

  gdbDebugMessagePSTR(PSTR("Test targetWriteFlashPage (no 2nd write when vaildpg): "), testnum++);
  fatalerror = NO_FATAL; setSysState(CONN_STATE);
  targetWriteFlashPage(flashaddr, membuf);
  failed += testResult(fatalerror == NO_FATAL && lastflashcnt == flashcnt);

  gdbDebugMessagePSTR(PSTR("Test targetWriteFlashPage (no 2nd write when same contents): "), testnum++);
  fatalerror = NO_FATAL; setSysState(CONN_STATE);
  validpg = false;
  targetWriteFlashPage(flashaddr, membuf);
  failed += testResult(fatalerror == NO_FATAL && lastflashcnt == flashcnt);

  gdbDebugMessagePSTR(PSTR("Test targetWriteFlashPage (addr error): "), testnum++);
  fatalerror = NO_FATAL; setSysState(CONN_STATE);
  targetWriteFlashPage(flashaddr+2, membuf);
  failed += testResult(fatalerror != NO_FATAL && lastflashcnt == flashcnt);
  
  gdbDebugMessagePSTR(PSTR("Test targetReadFlashPage (from cache): "), testnum++);
  fatalerror = NO_FATAL; setSysState(CONN_STATE);
  page[0] = 0x11; // mark first cell in order to see whether things get reloaded
  targetReadFlashPage(flashaddr);
  failed += testResult(fatalerror == NO_FATAL && page[0] == 0x11);

  gdbDebugMessagePSTR(PSTR("Test targetReadFlashPage (from flash memory): "), testnum++);
  fatalerror = NO_FATAL; setSysState(CONN_STATE);
  for (i=0; i < mcu.pagesz; i++) page[i] = 0;
  validpg = false;
  succ = true;
  targetReadFlashPage(flashaddr);
  DEBLN(F("Values:"));
  for (i=0; i < mcu.pagesz; i++) {
    DEBLN(page[i]);
    if (page[i] != i) succ = false;
  }
  failed += testResult(fatalerror == NO_FATAL && succ);

  gdbDebugMessagePSTR(PSTR("Test targetReadFlash/targetWriteFlash (read bytes from flash - not chache!): "), testnum++);
  fatalerror = NO_FATAL; setSysState(CONN_STATE);
  membuf[0] = 22; 
  membuf[1] = 33;
  targetWriteFlash(flashaddr+2, membuf, 2);
  membuf[0] = 0;
  membuf[1] = 0;
  for (i=0; i < mcu.pagesz; i++) page[i] = 0;
  validpg = false;
  targetReadFlash(flashaddr+2, membuf, 2);
  failed += testResult(membuf[0] == 22 && membuf[1] == 33);

  gdbDebugMessagePSTR(PSTR("Test targetRestoreRegisters/targetSaveRegisters: "), testnum++);
  succ = true;
  for (i = 0; i < 32; i++) ctx.regs[i] = i+1;
  ctx.wpc = 0x123;
  ctx.sp = 0x5F;
  ctx.sreg = 0xF7;
  ctx.saved = true;
  targetRestoreRegisters(); // store all regs to target
  if (ctx.saved) succ = false;
  ctx.wpc = 0;
  ctx.sp = 0;
  ctx.sreg = 0;
  for (i = 0; i < 32; i++) ctx.regs[i] = 0;
  targetSaveRegisters(); // get all regs from target
  if (!ctx.saved || ctx.wpc != 0x123-1 || ctx.sp != 0x5F || ctx.sreg != 0xF7) succ = false;
  for (i = 0; i < 32; i++) {
    if (ctx.regs[i] != i+1) succ = false;
  }
  failed += testResult(succ);

  gdbDebugMessagePSTR(PSTR("Test targetInitRegisters: "), testnum++);
  targetInitRegisters();
  failed += testResult(ctx.wpc == 0 && ctx.saved == true); // this is the only requirement!

  gdbDebugMessagePSTR(PSTR("Test targetWriteEeprom/targetReadEeprom: "), testnum++);
  succ = true;
  const int eeaddr = 0x25;
  membuf[0] = 0x30;
  membuf[1] = 0x45;
  membuf[2] = 0x67;
  targetWriteEeprom(eeaddr, membuf, 3);
  targetReadEeprom(eeaddr, &membuf[3], 3);
  for (i = 0; i <3; i++)
    if (membuf[i] != membuf[i+3]) succ = false;
  for (i = 0; i < 6; i++) membuf[i] = 0xFF;
  targetWriteEeprom(eeaddr, membuf, 3);
  for (i = 0; i < 3; i++) membuf[i] = 0;
  targetReadEeprom(eeaddr, membuf, 3);
  for (i = 0; i <3; i++)
    if (membuf[i] != 0xFF) succ = false;
  failed += testResult(succ);

  gdbDebugMessagePSTR(PSTR("Test targetWriteSram/targetReadSram: "), testnum++);
  succ = true;
  const int ramaddr = 0x125;
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

  gdbDebugMessagePSTR(PSTR("Test targetStep (ldi r18,0x49): "), testnum++);
  succ = true;
  targetInitRegisters();
  ctx.wpc = 0xd9; // ldi instruction
  ctx.sp = mcu.ramsz+mcu.rambase-1; // SP to upper limit of RAM
  targetRestoreRegisters(); 
  targetStep();
  if (!checkCmdOk2()) succ = false;
  targetSaveRegisters();
  failed += testResult(succ && ctx.wpc == 0xda && ctx.regs[18] == 0x49);

  gdbDebugMessagePSTR(PSTR("Test targetStep (rcall): "), testnum++);
  succ = true;
  ctx.wpc = 0xde; // rcall instruction
  targetRestoreRegisters();
  targetStep(); // one step leads to Break+0x55
  if (!checkCmdOk2()) succ = false;
  targetSaveRegisters();
  failed += testResult(succ && ctx.wpc == 0xe4);

  gdbDebugMessagePSTR(PSTR("Test targetContinue/targetBreak: "), testnum++);
  succ = true;
  hwbp = 0xFFFF;
  targetRestoreRegisters();
  targetContinue();
  targetBreak(); // DW responds with 0x55 on break
  if (!checkCmdOk()) succ = false;
  targetSaveRegisters();
  DEBLN(F("Values:"));
  DEBLN(succ);
  DEBLNF(ctx.wpc,HEX);
  DEBLNF(ctx.regs[17],HEX);
  failed += testResult(succ && ctx.wpc == 0xd8 && ctx.regs[17] == 0x91);

  gdbDebugMessagePSTR(PSTR("Test targetReset: "), testnum++);
  DWwriteIOreg(0x35, 0xFF); // MCUSR
  targetRestoreRegisters();
  targetReset(); // response is taken care of by the function itself
  targetSaveRegisters();
  failed += testResult(ctx.wpc == 0 && DWreadIOreg(0x35) == 0);
  
  if (num >= 1) {
    num = testnum;
    return failed;
  } else {
    testSummary(failed);
    gdbSendReply("OK");
    return 0;
  }
}


/****************** debugWIRE specific functions *************/

// send a break on the RESET line, check for response and calibrate 
boolean doBreak () { 
  digitalWrite(RESET, LOW);
  pinMode(RESET, INPUT);
  _delay_ms(10);
  dw.enable(false);
  ctx.bps = dw.calibrate();
  if (ctx.bps == 0) {
    DEBLN(F("No response from debugWire on sending break"));
    return false;
  }
  DEBPR(F("Speed: "));
  DEBPRF(ctx.bps, DEC);
  DEBLN(F(" bps"));
  dw.enable(true);
  dw.begin(ctx.bps);                        // Set computed bit rate
  DEBLN(F("Sending BREAK: "));
  dw.sendBreak();
  if (checkCmdOk()) {
    DEBLN(F("debugWire Enabled"));
    return true;
  } else {
    DEBLN(F("debugWIRE not enabled, is DWEN enabled?"));
  }
  return false;
}

// send a command
void sendCommand(const uint8_t *buf, uint8_t len)
{
  Serial.flush(); // wait until everything has been written in order to avoid interrupts
  dw.write(buf, len);
}

// wait for response and store in buf
unsigned int getResponse (int unsigned expected) {
  return getResponse(&buf[0], expected);
}

// wait for response and store in some data area
unsigned int getResponse (byte *data, unsigned int expected) {
  unsigned int idx = 0;
  unsigned long timeout = 0;

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
  getResponse(&tmp[0], 2);
  return ((unsigned int) tmp[0] << 8) + tmp[1];
}

// wait for a 0x55 response
boolean checkCmdOk () {
  byte tmp[1];
  byte len = getResponse(&tmp[0], 1);
  if (len == 1 && tmp[0] == 0x55) {
    return true;
  } else {
    DEBPR(F("checkCmd Error: len="));
    DEBPR(len);
    DEBPR(" tmp[0]=");
    DEBPRF(tmp[0], HEX);
    return false;
  }
}

// wait for a BREAK/0x55 response
boolean checkCmdOk2 () {
  byte tmp[2];
  byte len = getResponse(&tmp[0], 2);
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
//  to transfer data over debugWire via the DWDR register.  However, because the location of the DWDR register can vary from device
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
                    0xD0, 0x00, 0x1C,                                   // Set Start Reg number (r28(
                    0xD1, 0x00, 0x20,                                   // Set End Reg number (r31) + 1
                    0xC2, 0x05,                                         // Set repeating copy to registers via DWDR
                    0x20,                                               // Go
                    0x01, 0x01, (byte)(addr & 0xFF), (byte)(addr >> 8)};// Data written into registers r28-r31
  byte doRead[]  = {0x64,                                               // Set up for single step using loaded instruction
                    0xD2, outHigh(mcu.eearh, 31), outLow(mcu.eearh, 31), 0x23,  // out EEARH,r31  EEARH = ah  EEPROM Address MSB
                    0xD2, outHigh(mcu.eearl, 30), outLow(mcu.eearl, 30), 0x23,  // out EEARL,r30  EEARL = al  EEPROMad Address LSB
                    0xD2, outHigh(mcu.eecr, 28), outLow(mcu.eecr, 28), 0x23,    // out EECR,r28   EERE = 01 (EEPROM Read Enable)
                    0xD2, inHigh(mcu.eedr, 29), inLow(mcu.eedr, 29), 0x23,      // in  r29,EEDR   Read data from EEDR
                    0xD2, outHigh(mcu.dwdr, 29), outLow(mcu.dwdr, 29), 0x23};   // out DWDR,r29   Send data back via DWDR reg
  measureRam();
  sendCommand(setRegs, sizeof(setRegs));
  sendCommand(doRead, sizeof(doRead));
  getResponse(&retval,1);                                                       // Read data from EEPROM location
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
  byte doWrite[] = {0x64,                                                 // Set up for single step using loaded instruction
                    0xD2, outHigh(mcu.eearh, 31), outLow(mcu.eearh, 31), 0x23,    // out EEARH,r31  EEARH = ah  EEPROM Address MSB
                    0xD2, outHigh(mcu.eearl, 30), outLow(mcu.eearl, 30), 0x23,    // out EEARL,r30  EEARL = al  EEPROM Address LSB
                    0xD2, inHigh(mcu.dwdr, 30), inLow(mcu.dwdr, 30), 0x23,        // in  r30,DWDR   Get data to write via DWDR
                    val,                                                  // Data written to EEPROM location
                    0xD2, outHigh(mcu.eedr, 30), outLow(mcu.eedr, 30), 0x23,      // out EEDR,r30   EEDR = data
                    0xD2, outHigh(mcu.eecr, 28), outLow(mcu.eecr, 28), 0x23,      // out EECR,r28   EECR = 04 (EEPROM Master Program Enable)
                    0xD2, outHigh(mcu.eecr, 29), outLow(mcu.eecr, 29), 0x23};     // out EECR,r29   EECR = 02 (EEPROM Program Enable)
  measureRam();
  sendCommand(setRegs, sizeof(setRegs));
  sendCommand(doWrite, sizeof(doWrite));
}

//
//  Read len bytes from flash memory area at <addr> into data[] buffer
//
boolean DWreadFlash(unsigned int addr, byte *mem, unsigned int len) {
  // Read len bytes form flash page at <addr>
  unsigned int rsp;
  DEBPR(F("Read flash "));
  DEBPRF(addr,HEX);
  DEBPR("-");
  DEBLNF(addr+len-1, HEX);
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
  DEBPR(F("Erase: "));
  DEBLNF(addr,HEX);
  DWwriteRegister(30, addr & 0xFF); // load Z reg with addr low
  DWwriteRegister(31, addr >> 8  ); // load Z reg with addr high
  DWwriteRegister(29, 0x03); // PGERS value for SPMCSR
  DWsetWPc(mcu.bootaddr); // so that access of all of flash is possible
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
  measureRam();
  flashcnt++;
  DWwriteRegister(30, addr & 0xFF); // load Z reg with addr low
  DWwriteRegister(31, addr >> 8  ); // load Z reg with addr high
  DWwriteRegister(29, 0x05); //  PGWRT value for SPMCSR
  DWsetWPc(mcu.bootaddr); // so that access of all of flash is possible
  byte eprog[] = { 0x64, // single stepping
		    0xD2, // load into instr reg
		    outHigh(0x37, 29), // Build "out SPMCSR, r29"
		    outLow(0x37, 29), 
		    0x23,  // execute
		    0xD2, 0x95 , 0xE8, 0x33 }; // execute SPM
  sendCommand(eprog, sizeof(eprog));
  return checkCmdOk2();
  
  if (!mcu.bootaddr) { // no bootloader
    return checkCmdOk2(); // simply return
  } else { // bootloader: wait for spm to finish
    while ((DWreadSPMCSR() & 0x1F) != 0) { 
      _delay_us(100);
      DEBPR("."); // wait
    }
    return true;
  }
  return false;
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
    DWsetWPc(mcu.bootaddr);
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
  if (mcu.bootaddr) {
    DEBLN(F("DWreenableRWW"));
    DWsetWPc(mcu.bootaddr);
    DWwriteRegister(29, 0x11); //  RWWSRE value for SPMCSR
    byte errw[] = { 0x64, 0xD2,
		    outHigh(0x37, 29),       // Build "out SPMCSR, r29"
		    outLow(0x37, 29),
		    0x23,                    // execute
		    0xD2, 0x95, 0xE8, 0x23 }; // spm
    measureRam();
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
  sendCommand(sc, sizeof(sc));
  return DWreadRegister(30);
}

unsigned int DWgetWPc () {
  sendCommand((const byte[]) {0xF0}, 1);
  unsigned int pc = getWordResponse();
  return (pc - 1);
}

// get hardware breakpoint word address 
unsigned int DWgetWBp () {
  sendCommand((const byte[]) {0xF1}, 1);
  return (getWordResponse());
}

// get chip signature
unsigned int DWgetChipId () {
  sendCommand((const byte[]) {0xF3}, 1);
  return (getWordResponse()) ;
}

// set PC (word address)
void DWsetWPc (unsigned int wpc) {
  byte cmd[] = {0xD0, (byte)(wpc >> 8), (byte)(wpc & 0xFF)};
  sendCommand(cmd, sizeof(cmd));
}

// set hardware breakpoint at word address
void DWsetWBp (unsigned int wbp) {
  byte cmd[] = {0xD1, (byte)(wbp >> 8), (byte)(wbp & 0xFF)};
  sendCommand(cmd, sizeof(cmd));
}


boolean DWexecOffline(unsigned int opcode)
{
  byte cmd[] = {0xD2, (byte) (opcode >> 8), (byte) (opcode&0xFF), 0x23};
  measureRam();

  DEBPR(F("Offline exec: "));
  DEBLNF(opcode,HEX);
  if (opcode == 0x9598) {
    DEBLN(F("***Trying to execute BREAK instruction"));
    // Fatal error wil be raised by the calling routine
    return false;
  }
  sendCommand(cmd, sizeof(cmd));
  return true;
}

int testResult(bool succ)
{
  if (succ) {
    gdbDebugMessagePSTR(PSTR("  -> succeeded"), -1);
    return 0;
  } else {
    gdbDebugMessagePSTR(PSTR("  -> failed ***"), -1);
    return 1;
  }
}
 
int DWtests(int &num)
{
  int failed = 0;
  bool succ;
  int testnum;
  byte temp;

  if (num >= 1) testnum = num;
  else testnum = 1;


  gdbDebugMessagePSTR(PSTR("Test DWwriteRegister/DWreadRegister: "), testnum++);
  DWwriteRegister(0, 0x55);
  DWwriteRegister(15, 0x9F);
  DWwriteRegister(31, 0xFF);
  failed += testResult(DWreadRegister(0) == 0x55 && DWreadRegister(15) == 0x9F && DWreadRegister(31) == 0xFF);

  gdbDebugMessagePSTR(PSTR("Test DWwriteRegisters/DWreadRegisters: "), testnum++);
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

  gdbDebugMessagePSTR(PSTR("Test DWwriteIOreg/DWreadIOreg: "), testnum++);
  DWwriteIOreg(0x3F, 0x55);
  failed += testResult(DWreadIOreg(0x3F) == 0x55);

  gdbDebugMessagePSTR(PSTR("Test DWwriteSramByte/DWreadIOreg: "), testnum++);
  DWwriteSramByte(0x3F+0x20, 0x1F);
  temp = DWreadIOreg(0x3F);
  failed += testResult(temp == 0x1F);

  gdbDebugMessagePSTR(PSTR("Test DWwriteIOreg/DWreadSramByte: "), testnum++);
  DWwriteIOreg(0x3F, 0xF2);
  failed += testResult(DWreadSramByte(0x3F+0x20) == 0xF2);

  gdbDebugMessagePSTR(PSTR("Test DWwriteSramByte/DWreadSramByte: "), testnum++);
  for (byte i=0; i < 32; i++) DWwriteSramByte(0x100+i, i+1);
  succ = true;
  for (byte i=0; i < 32; i++) {
    if (DWreadSramByte(0x100+i) != i+1) {
      succ = false;
      break;
    }
  }
  failed += testResult(succ);

  gdbDebugMessagePSTR(PSTR("Test DWreadSramBytes: "), testnum++);
  for (byte i=0; i < 32; i++) membuf[i] = 0;
  DWreadSramBytes(0x100, membuf, 32);
  succ = true;
  for (byte i=0; i < 32; i++) {
    if (membuf[i] != i+1) {
      succ = false;
      break;
    }
  }
  failed += testResult(succ);

  gdbDebugMessagePSTR(PSTR("Test DWwriteEepromByte/DWreadEepromByte: "), testnum++);
  const int eeaddr = 0x15;
  succ = true;
  DWwriteEepromByte(eeaddr, 0x38);
  if (DWreadEepromByte(eeaddr) != 0x38) succ = false;
  DWwriteEepromByte(eeaddr, 0xFF);
  if (DWreadEepromByte(eeaddr) != 0xFF) succ = false;
  failed += testResult(succ);

  gdbDebugMessagePSTR(PSTR("Test DWeraseFlashPage: "), testnum++);
  const int flashaddr = 0x100;
  failed += testResult(DWeraseFlashPage(flashaddr));

  gdbDebugMessagePSTR(PSTR("Test DWreadFlash (empty page): "), testnum++);
  for (byte i=0; i < mcu.pagesz; i++) membuf[i] = 0;
  succ = true;
  DWreenableRWW();
  for (byte i=0; i < mcu.pagesz; i++) membuf[i] = 0;
  DWreadFlash(flashaddr, membuf, mcu.pagesz);
  for (byte i=0; i < mcu.pagesz; i++) {
    if (membuf[i] != 0xFF) succ = false;
  }
  failed += testResult(succ);
    

  gdbDebugMessagePSTR(PSTR("Test DWProgramFlashPage: "), testnum++);
  for (byte i=0; i < mcu.pagesz; i++) membuf[i] = 255-i;
  DWloadFlashPageBuffer(flashaddr, membuf);
  failed += testResult(DWprogramFlashPage(flashaddr));
  
  gdbDebugMessagePSTR(PSTR("Test DWloadFlashPageBuffer/DWprogramFlashPage/DWreenableRWW/DWreadFlash: "), testnum++);
  for (byte i=0; i < mcu.pagesz; i++) membuf[i] = 0;
  succ = true;
  DWreenableRWW();
  DWreadFlash(flashaddr, membuf, mcu.pagesz);
  for (byte i=0; i < mcu.pagesz; i++) {
    if (membuf[i] != 255-i) {
      succ = false;
    }
  }
  failed += testResult(succ);

  if (mcu.bootaddr != 0) {
    gdbDebugMessagePSTR(PSTR("Test DWloadFlashPageBuffer/DWprogramFlashPage/DWreenableRWW/DWreadFlash (boot section): "), testnum++);
    succ = DWeraseFlashPage(mcu.bootaddr);
    DWreenableRWW();
    if (succ) 
      succ = DWloadFlashPageBuffer(mcu.bootaddr, membuf);
    if (succ)
      succ = DWprogramFlashPage(mcu.bootaddr);
    if (succ) {
      for (byte i=0; i < mcu.pagesz; i++) membuf[i] = 0;
      DWreenableRWW();
      DWreadFlash(flashaddr, membuf, mcu.pagesz);
      for (byte i=0; i < mcu.pagesz; i++) {
	if (membuf[i] != 255-i) {
	  succ = false;
	}
      }
    }
    failed += testResult(succ);
  }

  gdbDebugMessagePSTR(PSTR("Test DWgetChipId: "), testnum++);
  failed += testResult(mcu.sig != 0 && DWgetChipId() == mcu.sig);
  
  gdbDebugMessagePSTR(PSTR("Test DWsetWPc/DWgetWPc: "), testnum++);
  const int pc = 0x3F; 
  DWsetWPc(pc);
  failed += testResult(DWgetWPc() == pc - 1);

  gdbDebugMessagePSTR(PSTR("Test DWsetWBp/DWgetWBp: "), testnum++);
  DWsetWBp(pc);
  failed += testResult(DWgetWBp() == pc);

  gdbDebugMessagePSTR(PSTR("Test DWexecOffline (eor r1,r1 at WPC=0x003F): "), testnum++);
  DWwriteIOreg(0x3F, 0); // write SREG
  DWwriteRegister(0x55, 1);
  DWsetWPc(pc);
  DWexecOffline(0x2411);
  succ = false;
  if (pc + 1 == DWgetWPc()) // PC advanced by one, then +1 for break, but this is autmatically subtracted
    if (DWreadRegister(1) == 0)  // reg 1 should be zero now
      if (DWreadIOreg(0x3F) == 0x02) // in SREG, only zero bit should be set
	succ = true;
  failed += testResult(succ);

  gdbDebugMessagePSTR(PSTR("Test DWexecOffline (rjmp 0x002E at WPC=0x0001 (word addresses)): "), testnum++);
  DWsetWPc(0x01);
  DWexecOffline(0xc02C);
  failed += testResult(DWgetWPc() == 0x2E); // = byte addr 0x005C

  if (num >= 1) {
    num = testnum;
    return failed;
  } else {
    testSummary(failed);
    gdbSendReply("OK");
    return 0;
  }
}

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

/***************************** a little bit of SPI programming ********/


void enableSpiPins () {
  measureRam();

  pinMode(SCK, OUTPUT);
  digitalWrite(SCK, HIGH);
  pinMode(MOSI, OUTPUT);
  digitalWrite(MOSI, HIGH);
  pinMode(MISO, INPUT);
}

void disableSpiPins () {
  measureRam();

  //pinMode(SCK, INPUT); - no leave it as an output
  pinMode(MOSI, INPUT);
  pinMode(MISO, INPUT);
}

byte transfer (byte val) {
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

byte ispSend (byte c1, byte c2, byte c3, byte c4) {
  transfer(c1);
  transfer(c2);
  transfer(c3);
  return transfer(c4);
}

boolean enterProgramMode () {
  // TIMSK0 &= ~_BV(OCIE0A); // disable our blink IRQ
  if (progmode) {
    DEBLN(F("Already in progmode"));
    return true;
  }
  DEBLN(F("Entering progmode"));
  byte timeout = 0;
  byte rsp;
  enableSpiPins();
  pinMode(RESET, OUTPUT);
  digitalWrite(RESET, LOW);
  _delay_ms(50);
  do {
    _delay_ms(50);
    ispSend(0xAC, 0x53, 0x00, 0x00);
    rsp = ispSend(0x30, 0x00, 0x00, 0x00);
  } while (rsp != 0x1E && ++timeout < 5);
  progmode = timeout < 5;
  if (!progmode) {
    DEBLN(F("Timeout: Chip may have DWEN bit enabled"));
    pinMode(RESET, INPUT);
  }
  return progmode;
}

void leaveProgramMode() {
  measureRam();

  disableSpiPins();
  pinMode(RESET, INPUT);
  progmode = false;
  // TIMSK0 |= _BV(OCIE0A); // reenable blink interrupt
}
  

// identify chip
unsigned int SPIgetChipId () {
  unsigned int id;
  if (enterProgramMode()) {
    id = ispSend(0x30, 0x00, 0x01, 0x00) << 8;
    id |= ispSend(0x30, 0x00, 0x02, 0x00);
    DEBPR(F("SIG:   "));
    DEBLNF(id,HEX);
    return id;
  }
  return 0;
}

boolean setMcuAttr(unsigned int id)
{
  int ix = 0;
  unsigned int sig;
  unsigned int *ptr;
  measureRam();

  while ((sig = pgm_read_word(&mcu_attr[ix*13]))) {
    if (sig == id) { // found the right mcu type
      ptr = &mcu.sig;
      for (byte f = 0; f < 13; f++) 
	*ptr++ = pgm_read_word(&mcu_attr[ix*13+f]);
      mcu.eearl = mcu.eearh - 1;
      mcu.eedr = mcu.eecr + 1;
      mcu.infovalid = true;
      DEBPR(F("Found MCU Sig: "));
      DEBLNF(id, HEX);
      return true;
    }
    ix++;
  }
  DEBPR(F("Could not determine MCU type with SIG: "));
  DEBLNF(id, HEX);
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
  DEBPR(F("RAM: "));
  DEBLN(f);
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

