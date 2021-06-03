// This is a gdbserver implementation using the debug wire protocol.
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
// NOTE: In order to enable debugWire mode, one needs to enable the DWEN fuse.
//       Use the gdb command "monitor init" to do so. Afterwards one has to power-cycle the
//       target system (if this is not supported by the hardware debugger).
//       From now on, one cannot reset the MCU using the reset line nor
//       is it possible to change fuses with ISP programming or load programs using
//       the ISP interface. The program has to be loaded using the debug load command.
//       With the gdb command "monitor stop", one can switch the MCU back to normal behavior
//       using this hardware debugger.
// Some of the code is inspired  by
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
// Version 0.5
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
// Version 0.6
//   - added support for ATtiny828 and ATtiny43
//   - issue an error when a byte is escaped although it should not have been instead
//     of silently ignoring it
//   - added gdbRemoveAllBreakpoints in order to avoid leaving active bps before reset etc.
//   - changed the number of entries of bp from MaXBREAKS*2+1 to one less, because we now
//     refuse to acknowledge every extra BP above the allowed number
//   - detach function now really detaches, i.e., continues execution on the target and leaves it alone.
//   - 

//#define DEBUG // for debugging the debugger!
#define VERSION "0.6"
#define FREERAM

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
#include "OnePinSerial.h"
#ifdef DEBUG
#include <TXOnlySerial.h> // only needed for (meta-)debuging
#endif
#include "debug.h" // some (meta-)debug macros

// some size restrictions
#define MAXBUF 255
#define MAXBREAK 32 // maximum of active breakpoints (we need double as many!)

// clock rates 
#define DEBUG_BAUD    115200 // communcation speed with the host
#define DWIRE_RATE    (1000000 / 128) // Set default baud rate (1 MHz / 128) 

// signals
#define SIGINT  2      // Interrupt  - user interrupted the program (UART ISR) 
#define SIGILL  4      // Illegal instruction
#define SIGTRAP 5      // Trace trap  - stopped on a breakpoint
#define SIGABRT 6

// some masks to interpret memory addresses
#define MEM_SPACE_MASK 0x00FF0000 // mask to detect what memory area is meant
#define FLASH_OFFSET   0x00000000 // flash is addressed starting from 07
#define SRAM_OFFSET    0x00800000 // RAM address from GBD is (real addresss + 0x00800000)
#define EEPROM_OFFSET  0x00810000 // EEPROM address from GBD is (real addresss + 0x00810000)

// some GDB variables
struct breakpoint
{
  bool used:1;      // bp is in use
  bool active:1;    // breakpoint is active
  bool inflash:1;   // breakpoint is in flash memory
  bool hw:1;        // breakpoint is a hardware breakpoint
  unsigned int waddr; // word addressing! 
  unsigned int opcode; // opcode that has been replaced by BREAK
} bp[MAXBREAK*2];
int bpcnt = 0;

unsigned int hwbp = 0xFFFF; // the one hardware breakpoint (word address)
unsigned int lasthwbp = 0xFFFF; // address we stopped last

struct context {
  unsigned int wpc; // pc (using word addresses)
  unsigned int sp; // stack pointer
  byte sreg;    // status reg
  byte regs[32]; // general purpose regs
  boolean running:1;    // whether target is running
  boolean targetcon:1;  // whether target is connected
  boolean clobbered:1;  // some of the regs are clobbered (0-1, 28-31)
  boolean saved:1; // all of the regs have been saved 
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
  unsigned int cckdiv8;
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

// som statistics
long flashcnt = 0; // number of flash writes 
#ifdef FREERAM
int freeram = 2048; // minimal amount of free memory (only if enabled)
#define measureRam() freeRamMin()
#else
#define measureRam()
#endif

// communcation interface to target
OnePinSerial  debugWire(RESET);
boolean       reportTimeout = true;   // If true, report read timeout errors
boolean       progmode = false;
char          rpt[16];                // Repeat command buffer
unsigned int  timeOutDelay;           // Timeout delay (based on baud rate)
byte lastsignal = 0;

// use LED to signal system state
// LED off = not connected to target system
// LED flashing every half second = power-cycle target in order to enable debugWire
// LED blinking every 1/10 second = could not connect to target board
// LED constantly on = connected to target and target is halted
// Led blinks every 1/3 second = target is running
volatile enum {INIT_STATE, NOTCONN_STATE, PWRCYC_STATE, ERROR_STATE, CONN_STATE, RUN_STATE} state = INIT_STATE;
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

DEBDECLARE();

/******************* setup & loop ******************************/
void setup(void) {
  DEBINIT();
  debugWire.begin(DWIRE_RATE);    
  setTimeoutDelay(DWIRE_RATE); 
  debugWire.enable(true);
  Serial.begin(DEBUG_BAUD);
  ctx.running = false;
  ctx.targetcon = false;
  mcu.infovalid = false;
  pinMode(LED_BUILTIN, OUTPUT);
  OCR0A = 0x80; // interrupt every msec between the millis interrupts
  TIMSK0 |= _BV(OCIE0A);
}


void loop(void) {
  if (Serial.available()) {
      gdbHandleCmd();
  } else if (ctx.running) {
    if (debugWire.available()) {
      byte cc = debugWire.read();
      if (cc == 0x55) { // breakpoint reached
	ctx.running = false;
	state = CONN_STATE;
	delay(5); // we need that in order to avoid conflicts on the line
	gdbSendState(SIGTRAP);
      }
    }
  }
}

/****************** Interrupt blink routine *********************/

ISR(TIMER0_COMPA_vect)
{
  static int cnt, laststate;

  if (state == INIT_STATE) {
    cnt = 0;
    state = NOTCONN_STATE;
    laststate = INIT_STATE;
  }
  if (laststate != state) {
    laststate = state;
    cnt = ontimes[laststate];
  }
  if (cnt == ontimes[state] && cnt) 
    digitalWrite(LED_BUILTIN, HIGH);
  else if (cnt == -offtimes[state])
    cnt = ontimes[state] + 1;
  else if (cnt == -1)
    digitalWrite(LED_BUILTIN, LOW);
  cnt--;
    
}

/****************** gdbserver routines **************************/


// handle command from from client
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
      if (checkCmdOk()) {
	ctx.running = false;
	gdbSendState(SIGINT);
	state = CONN_STATE;
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
  switch (*buff) {
  case '?':               /* last signal */
    gdbSendReply((lastsignal == SIGTRAP ? "S05" : "S02"));  /* signal # 5 is SIGTRAP */
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
    state = NOTCONN_STATE;
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
    ctx.running = true;
    state = RUN_STATE;
    gdbContinue();       /* start executuion on target and continue with ctx.running = true */
    break;
  case 's':               /* step */
    ctx.running = true;
    state = RUN_STATE;
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
    if (memcmp_P(buff, (void *)PSTR("qSupported"), 10) == 0) 
	gdbSendPSTR((const char *)PSTR("PacketSize=FF;swbreak+")); 
    else if (memcmp_P(buf, (void *)PSTR("qRcmd,73746f70"), 14) == 0)   /* stop */
      gdbStop();
    else if  (memcmp_P(buf, (void *)PSTR("qRcmd,696e6974"), 14) == 0)     /* init */
      gdbConnect();
    else if (memcmp_P(buf, (void *)PSTR("qRcmd,666c617368636f756e74"), 26) == 0) /* flashcount */
      gdbReportFlashCount();
    else if (memcmp_P(buf, (void *)PSTR("qRcmd,72616d"), 12) == 0)    /* ram */
      gdbReportRamUsage();
    else if  (memcmp_P(buf, (void *)PSTR("qRcmd,74657374"), 14) == 0)    /* test */
      if (testFlashWrite(0)) gdbSendReply("OK");
      else gdbSendReply("E05");
    else if (memcmp_P(buf, (void *)PSTR("qC"), 2) == 0)
      /* current thread is always 1 */
      gdbSendReply("QC01");
    else if (memcmp_P(buf, (void *)PSTR("qfThreadInfo"), 12) == 0)
      /* always 1 thread*/
      gdbSendReply("m1");
    else if (memcmp_P(buf, (void *)PSTR("qsThreadInfo"), 12) == 0)
      /* send end of list */
      gdbSendReply("l");
    else if (memcmp_P(buf, (void *)PSTR("qRcmd,7265736574"), 16) == 0) { /* reset */
      gdbRemoveAllBreakpoints();
      gdbUpdateBreakpoints();  // update breakpoints in memory before reset
      gdbReset();
      gdbSendReply("OK");      
      break;
    } else 
      gdbSendReply("");  /* not supported */
    break;
  default:
    gdbSendReply("");  /* not supported */
    break;
  }
}

boolean testFlashWrite(unsigned int addr) {
  unsigned int len = 104;
  for (byte i = 0; i < len; i++) {
    membuf[i] = i;
  }
  return targetWriteFlash(addr+0x20, membuf, len);
}

// report on how many flash pages have been written
void gdbReportFlashCount(void)
{
  gdbDebugMessagePSTR(PSTR("Number of flash write operations: "), flashcnt);
  delay(5);
  flushInput();
  gdbSendReply("OK");
}

void gdbReportRamUsage(void)
{
#ifdef FREERAM
  gdbDebugMessagePSTR(PSTR("Minimal number of free RAM bytes: "), freeram);
  delay(5);
  flushInput();
  gdbSendReply("OK");
#else
  gdbSendReply("");
#endif
}


// try to enable debugWire
// this might imply that the user has to power-cycle the target system
boolean gdbConnect(void)
{
  int retry = 0;
  byte b;

  switch (targetConnect()) {
  case -1: // for some reason, we cannot connect;
    state = ERROR_STATE;
    flushInput();
    gdbSendReply("E05");
    ctx.targetcon = false;
    return false;
    break;
  case 1: // everything OK
    state = CONN_STATE;
    ctx.targetcon = true;
    gdbDebugMessagePSTR(PSTR("debugWire still enabled"),-1);
    gdbReset();
    flushInput();
    gdbSendReply("OK");
    return true;
    break;
  case 0: // we have changed the fuse and need to powercycle
    state = PWRCYC_STATE;
    while (retry < 60) {
      if ((retry++)%10 == 0 && retry > 0) {
	do {
	  flushInput();
	  gdbDebugMessagePSTR(PSTR("Please power-cycle the target system"),-1);
	  b = gdbReadByte();
	} while (b == '-');
      }
      delay(1000);
      if (doBreak()) {
	state = CONN_STATE;
	gdbReset();
	flushInput();
	gdbDebugMessagePSTR(PSTR("debugWire is now enabled"),-1);
	delay(100);
	flushInput();
	gdbSendReply("OK");
	ctx.targetcon = true;
	return true;
      }
    }
  }
  state = ERROR_STATE;
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
    state = NOTCONN_STATE;
  } else
    gdbSendReply("E05");
}

// issue reset on target
void gdbReset(void)
{
  targetReset();
  gdbInitRegisters();
}

void gdbStep(void)
{
  DEBLN(F("Start step operation"));
  targetRestoreClobberedRegisters();
#if 0 // using now error message as a reply to the step commend
  if (bpcnt > MAXBREAK) {
    setWPc(ctx.wpc+1); // set PC!
    DEBLN(F("Too many bps"));
    return;
  }
#endif
  if (!gdbStepOverBP(true)) { // only if not already stepped over BP
    targetRestoreClobberedRegisters();
    targetStep();
  }
}

void gdbContinue(void)
{
  DEBLN(F("Start continue operation"));
  targetRestoreClobberedRegisters();
  gdbStepOverBP(false);    // either step over or remove BREAK from flash
  gdbUpdateBreakpoints();  // update breakpoints in memory
  targetRestoreClobberedRegisters();
  targetContinue();
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
      if (twoByteInstr(bp[bpix].opcode) && noJumpInstr(bp[bpix].opcode)) { // a two byte instruction
	DEBLN(F("Two byte instr / noJump"));
	targetRestoreClobberedRegisters(); // set all regs
	execsucc = execOffline(bp[bpix].opcode); // execute the instruction offline
	if (onlyonestep || !execsucc) { // if we do not continue
	  ctx.running = false; // mark that we are not executing any longer
	  state = CONN_STATE;
	  gdbSendState(execsucc ? SIGTRAP : SIGILL); // and signal that to GDB
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
	  targetRestoreClobberedRegisters(); // set all regs
	  targetStep();         // step over the 4byte/call instr
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

  // reassign HW BP if used twice and if  possible 
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
	    DEBLN(F("Saved BREAK instruction in bp struct! This SHOULDN'T happen!"));
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
      if (bpcnt >= MAXBREAK) {
	DEBPR(F("Too many BPs. Will not insert: "));
	DEBLNF(byteflashaddr,HEX);
	gdbSendReply("E05");
	return;
      }
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

/* insert bp, flash addr is in bytes */
void gdbInsertBreakpoint(unsigned int waddr)
{
  int i;

  // if bp is already there, but not active, then activate
  i = gdbFindBreakpoint(waddr);
  if (i >= 0) { // existing bp
    if (bp[i].active) return; // should not happen!
    bp[i].active = true;
    bpcnt++;
    DEBPR(F("New recycled BP: "));
    DEBPRF(waddr*2,HEX);
    DEBPR(F(" / now active: "));
    DEBLN(bpcnt);
    return;
  }
  // if we try to set too many bps, return
  if (bpcnt > MAXBREAK) return;
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
  // we should never end up here, because there should be a free slot!
}

// inactivate a bp
void gdbRemoveBreakpoint(unsigned int waddr)
{
  int i;
  i = gdbFindBreakpoint(waddr);
  if (i < 0) return; // not found
  DEBPR(F("Remove BP: "));
  DEBLNF(bp[i].waddr*2,HEX);
  bp[i].active = false;
  if (bp[i].hw) { // a HW BP can be freed
    bp[i].waddr = 0;
    bp[i].used = false;
    bp[i].hw = false;
    hwbp = 0xFFFF;
    DEBLN(F("HW BP inactivated"));
  } else if (!bp[i].inflash) { // a SW BP not in flash can be freed
    bp[i].waddr = 0;
    bp[i].used = false;
    DEBLN(F("SW BP inactivated"));
  }
  bpcnt--;
  DEBPR(F("BP removed: "));
  DEBPRF(waddr*2,HEX);
  DEBPR(F(" / now active: "));
  DEBLN(bpcnt);
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
    DEBLN(F("Inconsistent bpcnt number: "));
    DEBPR(cnt);
    DEBLN(F(" BPs removed!"));
  }
  bpcnt = 0;
  lasthwbp = 0xFFFF;
}

void gdbInitRegisters(void)
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

  targetGetAllRegisters();
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
  
  /* receive SREG as 32 register */
  ctx.sreg = hex2nib(*buff++) << 4;
  ctx.sreg |= hex2nib(*buff++);
  
  /* receive SP as 33 register */
  ctx.sp  = hex2nib(*buff++) << 4;
  ctx.sp |= hex2nib(*buff++);
  ctx.sp |= hex2nib(*buff++) << 12;
  ctx.sp |= hex2nib(*buff++) << 8;
  /* receive PC as 34 register
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
  targetSetAllRegisters();
  gdbSendReply("OK");
}

// read out some of the memory and send to it GDB
void gdbReadMemory(const byte *buff)
{
  unsigned long addr, sz, flag;
  byte i, b;
  boolean succ;

  if (targetoffline()) return;

  buff += parseHex(buff, &addr);
  /* skip 'xxx,' */
  parseHex(buff + 1, &sz);
  
  if (sz > 127) { // should not happen because we required packet length = 255:
    gdbSendReply("E05");
    return;
  }

  flag = addr & MEM_SPACE_MASK;
  addr &= ~MEM_SPACE_MASK;
  if (flag == SRAM_OFFSET) succ = targetReadSram(addr, membuf, sz);
  else if (flag == FLASH_OFFSET) succ = targetReadFlash(addr, membuf, sz);
  else if (flag == EEPROM_OFFSET) succ = targetReadEeprom(addr, membuf, sz);
  else {
    gdbSendReply("E05");
    return;
  }
  if (succ) {
    for (i = 0; i < sz; ++i) {
      b = membuf[i];
      buf[i*2 + 0] = nib2hex(b >> 4);
      buf[i*2 + 1] = nib2hex(b & 0xf);
    }
  } else {
    gdbSendReply("E05");
    return;
  }
  buffill = sz * 2;
  gdbSendBuff(buf, buffill);
}

// write to target memory
static void gdbWriteMemory(const byte *buff)
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
    return;
  }
  
  flag = addr & MEM_SPACE_MASK;
  addr &= ~MEM_SPACE_MASK;
  if (flag == SRAM_OFFSET) targetWriteSram(addr, membuf, memsz);
  else if (flag == FLASH_OFFSET) targetWriteFlash(addr, membuf, memsz);
  else if (flag == EEPROM_OFFSET) targetWriteEeprom(addr, membuf, memsz);
  else {
    gdbSendReply("E05");
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
  targetGetAllControlRegisters();
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

// enable DebugWire
// returns -1 if we cannot connect
// returns 0 if we need to powercycle
// returns 1 if we are connected 
int targetConnect(void)
{
  if (ctx.targetcon) return 1;
  if (doBreak()) return (setMcuAttr(DWgetChipId()) ? 1 : -1);
  if (!enterProgramMode()) {
    DEBLN(F("Neither in debugWire nor ISP mode"));
    return -1;
  }
  if (!setMcuAttr(SPIgetChipId())) return -1;
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

boolean targetStop(void)
{
  if (doBreak()) {
    if (setMcuAttr(DWgetChipId())) {
      debugWire.sendCmd((const byte[]) {0x06}, 1); // leave debugWireMode
      delay(50);
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
  return false;
}

void targetBreak(void)
{
  debugWire.sendBreak();
}

void targetContinue(void)
{
  measureRam();

  DEBPR(F("Continue at (byte adress) "));
  DEBLNF(ctx.wpc*2,HEX);
  delay(10);
  if (hwbp != 0xFFFF) {
    setWBp(hwbp);
    debugWire.sendCmd((const byte []) { 0x61 }, 1);
    setWBp(hwbp);
  } else {
    debugWire.sendCmd((const byte []) { 0x60 }, 1);
  }
  byte cmd[] = { 0xD0, (byte)(ctx.wpc>>8), (byte)(ctx.wpc), 0x30};
  debugWire.sendCmd(cmd, sizeof(cmd));
}

void targetStep(void)
{
  measureRam();

  DEBPR(F("Single step at (byte address):"));
  DEBLNF(ctx.wpc*2,HEX);
  delay(5);
  byte cmd[] = {0x60, 0xD0, (byte)(ctx.wpc>>8), (byte)(ctx.wpc), 0x31};
  debugWire.sendCmd(cmd, sizeof(cmd));
}

boolean targetReset(void)
{
  debugWire.sendCmd((const byte[]) {0x07}, 1);
  if (checkCmdOk2()) {
    DEBLN(F("RESET successful"));
    ctx.wpc = 0;
    return true;
  } else {
    DEBLN(F("RESET failed"));
    return false;
  }
}

boolean targetReadFlashPage(unsigned int addr)
{
  boolean succ = true;
  DEBPR(F("Reading flash page starting at: "));
  DEBLNF(addr,HEX);
  if (!validpg || (lastpg != (addr & ~(mcu.pagesz-1)))) {
    succ = targetReadFlash(addr & ~(mcu.pagesz-1), page, mcu.pagesz);
    if (succ) {
      lastpg = addr & ~(mcu.pagesz-1);
      validpg = true;
    } else {
      validpg = false;
    }
  } else {
    DEBPR(F("using cached page at "));
    DEBLNF(lastpg,HEX);
  }
  return succ;
}

boolean targetReadFlash(unsigned int addr, byte *mem, unsigned int len)
{
  return readFlash(addr, mem, len);
}

boolean targetReadSram(unsigned int addr, byte *mem, unsigned int len)
{
  return readSRamBytes(addr, mem, len);
}

boolean targetReadEeprom(unsigned int addr, byte *mem, unsigned int len)
{
  for (unsigned int i=0; i < len; i++) {
    mem[i] = readEepromByte(addr++);
  }
  return true;
}

boolean targetWriteFlashPage(unsigned int addr, byte *mem)
{
  DEBPR(F("Write flash ... "));
  DEBPRF(addr, HEX);
  DEBPR("-");
  DEBPRF(addr+mcu.pagesz-1,HEX);
  DEBPR(":");
  reenableRWW();
  // read old page contents
  if (!targetReadFlashPage(addr)) {
    DEBLN(F(" read error"));
    return false;
  }
  // check whether something changed
  DEBPR(F("Check for change: "));
  if (memcmp(mem, page, mcu.pagesz) == 0) {
    DEBLN(F("unchanged"));
    return true;
  }
  DEBLN(F("changed"));
  
  // check whether we need to erase the page
  boolean dirty = false;
  for (byte i=0; i < mcu.pagesz; i++) 
    if (~page[i] & mem[i]) {
      dirty = true;;
      break;
    }

  validpg = false;
  
  // erase page when dirty
  if (dirty) {
    DEBPR(F(" erasing ..."));
    if (!eraseFlashPage(addr)) {
      DEBLN(F(" not possible"));
      reenableRWW();
      return false;
    } else {
      DEBPR(F(" will overwrite ..."));
    }
    
    // maybe the new page is also empty?
    memset(page, 0xFF, mcu.pagesz);
    if (memcmp(mem, page, mcu.pagesz) == 0) {
      DEBLN(" nothing to write");
      reenableRWW();
      validpg = true;
      return true;
    }
  }
  if (!loadFlashBuffer(addr, mem)) {
    DEBPR(F(" cannot load page buffer "));
    reenableRWW();
    return false;
  } else {
    DEBPR(F(" flash buffer loaded"));
  }
    
  boolean succ = programFlashPage(addr);
  reenableRWW();
  if (succ) {
    memcpy(page, mem, mcu.pagesz);
    validpg = true;
    DEBLN(F(" page flashed"));
  } else {
    DEBLN(F(" flashing unsuccessful"));
  }
  return succ;
}

boolean targetWriteFlash(unsigned int addr, byte *mem, unsigned int len)
{
  unsigned int pageoffmsk = mcu.pagesz-1;
  unsigned int pagebasemsk = ~pageoffmsk;
  unsigned int partbase = addr & pagebasemsk;
  unsigned int partoffset = addr & pageoffmsk;
  unsigned int partlen = min(mcu.pagesz-partoffset, len);
  boolean succ = true;

  if (len == 0) return true;

  if (addr & pageoffmsk)  { // mem starts in the middle of a page
    succ &= targetReadFlashPage(partbase);
    memcpy(newpage, page, mcu.pagesz);
    memcpy(newpage + partoffset, mem, partlen);
    succ &= targetWriteFlashPage(partbase, newpage);
    addr += partlen;
    mem += partlen;
    len -= partlen;
  }

  // now write whole pages
  while (len >= mcu.pagesz) {
    succ &= targetWriteFlashPage(addr, mem);
    addr += mcu.pagesz;
    mem += mcu.pagesz;
    len -= mcu.pagesz;
  }

  // write remaining partial page (if any)
  if (len) {
    succ &= targetReadFlashPage(addr);
    memcpy(newpage, page, mcu.pagesz);
    memcpy(newpage, mem, len);
    succ &= targetWriteFlashPage(addr, newpage);
  }
  return succ;
}

boolean targetWriteSram(unsigned int addr, byte *mem, unsigned int len)
{
  for (unsigned int i=0; i < len; i++) 
    writeSRamByte(addr+i, mem[i]);
  return true;
}

boolean targetWriteEeprom(unsigned int addr, byte *mem, unsigned int len)
{
  for (unsigned int i=0; i < len; i++) 
    writeEepromByte(addr+i, mem[i]);
  return true;
}

void targetGetAllControlRegisters(void)
{
  //  DEBPR(F("ctx.clobbered="));
  //  DEBLN(ctx.clobbered);
  //  DEBLN(F("Get Ctrl Regs"));
  ctx.wpc = getWPc();
  DEBPR(F("PC (byte address) after stop: "));
  DEBLNF(ctx.wpc*2,HEX);
  ctx.sreg = readSRamByte(0x3F + 0x20);
  // DEBPR(F("SREG: "));
  // DEBLNF(ctx.sreg,BIN);
  ctx.sp = readSRamByte(0x3D + 0x20);
  // DEBPR(F("SPL: "));
  // DEBLNF(ctx.sp,HEX);
  if (mcu.ramsz > 256) ctx.sp |= readSRamByte(0x3E + 0x20) << 8;
  // DEBPR(F("SPH: "));
  // DEBLNF((ctx.sp>>8),HEX);
}

void targetGetAllRegisters(void)
{
  byte r;
  //  DEBLN(F("Get ALL regs"));
  if (!ctx.targetcon) {
    // DEBLN(F("not connected yet"));
    gdbInitRegisters();
    ctx.saved = true;
  }
  if (ctx.saved) {
    // DEBLN(F("Already done"));
    return; // no need to read them twice
  }
#ifdef NEWREGCODE
  readRegisters(&ctx.regs[0]);
#else
  if (ctx.clobbered) {
    /// DEBLN(F("Only 2-27"));
  }
  for (r = (ctx.clobbered ? 2 : 0); r < (ctx.clobbered ? 28 : 32); r++) 
    ctx.regs[r] = readRegister(r);
#endif
#if 0 // we never have to read the ctrl regs, since we do that immediately after each stop
  ctx.wpc = getWPc();
  DEBPR(F("PC (word address): "));
  DEBLNF(ctx.wpc,HEX);
  ctx.sreg = readSRamByte(0x3F + 0x20);
  DEBPR(F("SREG: "));
  DEBLNF(ctx.sreg,BIN);
  ctx.sp = readSRamByte(0x3D + 0x20);
  DEBPR(F("SPL: "));
  DEBLNF(ctx.sp,HEX);
  if (mcu.ramsz > 256) ctx.sp |= readSRamByte(0x3E + 0x20) << 8;
  DEBPR(F("SPH: "));
  DEBLNF((ctx.sp>>8),HEX);
#endif
  ctx.saved = true;
}

void targetSetAllRegisters(void)
{
  byte r;
  // DEBLN(F("Set all regs"));
  // DEBPR(F("SPL: "));
  // DEBLNF((ctx.sp&0xFF),HEX);
  writeSRamByte(0x3D + 0x20, (ctx.sp&0xFF));
  // DEBPR(F("SPH: "));
  // DEBLNF((ctx.sp>>8),HEX);
  if (mcu.ramsz > 256) writeSRamByte(0x3E + 0x20, (ctx.sp>>8)&0xFF);
  // DEBPR(F("SREG: "));
  // DEBLNF(ctx.sreg,BIN);
  writeSRamByte(0x3F + 0x20, ctx.sreg);
#ifdef NEWREGCODE
  writeRegisters(&ctx.regs[0]);
#else
  for (r = 0; r < 32; r++) // needs to be done AFTER calling writeSRamByte
    writeRegister(r, ctx.regs[r]);
#endif
  ctx.clobbered = false;
  ctx.saved = true;
}

void targetRestoreClobberedRegisters(void)
{
  byte r;
  ctx.saved = false;
  if (!ctx.clobbered) return;
  // DEBLN(F("Restore clobbered"));
  for (r = 0; r < 2 ; r++) 
    writeRegister(r, ctx.regs[r]);
  for (r = 28; r < 32; r++) 
    writeRegister(r, ctx.regs[r]);
  ctx.clobbered = false;
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

// check whether the instr doe not change the PC
boolean noJumpInstr(unsigned int opcode)
{
  measureRam();

  if (((opcode & 0b1111100000000000) == (unsigned int)0b1111000000000000) || // Branch
      ((opcode & 0b1110000000000000) == (unsigned int)0b1100000000000000) || // RJMP/RCALL
      ((opcode & 0b1111111011111111) == (unsigned int)0b1001010000001001) || // IJMP/ICALL
      ((opcode & 0b1111111111101111) == (unsigned int)0b1001010100001000))   // RET/RETI
    return false;
  return true;
}

/****************** dwbug wire specific functions *************/

// send a break on the RESET line, check for response and calibrate 
boolean doBreak () { 
  digitalWrite(RESET, LOW);
  pinMode(RESET, INPUT);
  delay(10);
  debugWire.enable(false);
  unsigned long baud = measureBaud();
  if (baud == 0) {
    DEBLN(F("No response from debugWire on sending break"));
    return false;
  }
  DEBPR(F("Speed: "));
  DEBPRF(baud, DEC);
  DEBLN(F(" bps"));
  debugWire.enable(true);
  debugWire.begin(baud);                            // Set computed baud rate
  setTimeoutDelay(DWIRE_RATE);                      // Set timeout based on baud rate
  DEBLN(F("Sending BREAK: "));
  debugWire.sendBreak();
  if (checkCmdOk()) {
    DEBLN(F("debugWire Enabled"));
    return true;
  } else {
    DEBLN(F("debugWire not enabled, is DWEN enabled?"));
  }
  return false;
}

// Measure debugWire baud rate by sending BREAK commands and measuring pulse length
unsigned long measureBaud(void)
{
#ifndef OLDCALIBRATE
  // We use TIMER1 for measuring the number of ticks between falling
  // edges and will wait for the fifth falling edge, which will give us the
  // length of a byte in intervals of 1/16us = 62.5ns. 
  // We do not use interrupts, but use polling to capture event times and overflows.
  // Important: here we need to use Arduino pin 8 as the reset line for the target (or as an additional input)
  unsigned long capture, overflow = 0;
  unsigned long stamp[5];
  byte edgecnt = 0;
  const unsigned long timeout = 50000UL*16UL; // 50 msec is max
  

  TCCR1A = 0;                                   // normal operation
  TCCR1B = _BV(ICNC1) | _BV(CS10);              // input filter, falling edge, no prescaler (=16MHz)
  TIMSK1 = 0;                                   // no Timer1 interrupts
  overflow = 0;
  
  debugWire.sendBreak();                        // send break
  delayMicroseconds(10);                        // give some leeway

  TCNT1 = 0;                                    // clear counter
  TIFR1  = _BV(ICF1) | _BV(TOV1);               // clear capture and interrupt flags
  while (edgecnt < 5 && (overflow << 16) + TCNT1 < timeout) { // wait for fifth edge or timeout
    if (TIFR1 & _BV(ICF1)) { // input capture
      capture = ICR1;
      if ((TIFR1 & _BV(TOV1)) && capture < 30000) // if additionally overflow and low count, we missed the overflow
	stamp[edgecnt++] = capture + ((overflow+1) << 16); 
      else
	stamp[edgecnt++] = capture + ((overflow) << 16); // if the capture value is very high, the overflow must have occured later than the capture
      TIFR1 |= _BV(ICF1); // clear capture flag
    }
    if (TIFR1 & _BV(TOV1)) { // after an overflow has occured
      overflow++;            // count it
      TIFR1 |= _BV(TOV1);    // and clear flag
    }
  }
  for (byte i=0; i < 5; i++) { DEBPR(stamp[i]); DEBPR(":"); DEBLN(i>0 ? stamp[i] - stamp[i-1] : 0);  }
  if (edgecnt < 5) return 0;  // if there were less than 5 edge, we did not see a 'U'
  return 8UL*16000000UL/(stamp[4]-stamp[0]); // we measured the length of 8 bits with 16MHz ticks
#else
  // We use pulseIn in order to measure the high and low intervals. Turns out
  // that in particular for high clock rates this is not very reliable.
  uint8_t oldSREG = SREG;
  unsigned int pulselen[5];
  byte i, off;
  unsigned long pulse = 0;

  for (byte rep=0; rep < 4; rep++) {
    debugWire.sendBreak();
    cli();                      // turn off interrupts for timing
    delayMicroseconds(20);
    for (i = 0; i < 5; i++) {   // sometimes the first measurement seems to be wrong
      pulselen[i] = pulseIn(RESET, HIGH, 20000); // so we take the first 5 intervalls
    }
    if (pulselen[0] == 0) {
      SREG = oldSREG;             // turn interrupts back on
      return 0;
    }
    DEBLN(pulselen[0]);    DEBLN(pulselen[1]);    DEBLN(pulselen[2]);    DEBLN(pulselen[3]);    DEBLN(pulselen[4]);
    sei();
    if (pulselen[4] == 0) off = 0; // last one is empty, add the 4 first pulses
    else off = 1; // otherwise ignore first pulse and add up the 4 last ones
    for (i = 0; i < 4; i++) pulse += pulselen[off + i];
    delay(2); 
    debugWire.sendBreak();
    delayMicroseconds(20); 
    cli();
    for (i = 0; i < 4; i++) {
      pulse += pulseIn(RESET, LOW, 20000);
    }
    DEBLN(pulse);
    SREG = oldSREG;             // turn interrupts back on
    return 8000000/pulse;       // we measured 8 bit intervalls by 1us ticks
  }
#endif
}


byte getResponse (int unsigned expected) {
  return getResponse(&buf[0], expected);
}

byte getResponse (byte *data, unsigned int expected) {
  unsigned int idx = 0;
  byte timeout = 0;

  do {
    if (debugWire.available()) {
      data[idx++] = debugWire.read();
      timeout = 0;
      if (expected > 0 && idx == expected) {
        return expected;
      }
    } else {
      delayMicroseconds(timeOutDelay);
      timeout++;
    }
  } while (timeout < 50 && idx < sizeof(buf));
  if (reportTimeout) {
    DEBPR(F("Timeout: received: "));
    DEBPR(idx);
    DEBPR(F(" expected: "));
    DEBLN(expected);
  }
  buffill = idx;
  return idx;
}

void setTimeoutDelay (unsigned int rate) {

  timeOutDelay = F_CPU / rate;
}

unsigned int getWordResponse () {
  byte tmp[2];
  getResponse(&tmp[0], 2);
  return ((unsigned int) tmp[0] << 8) + tmp[1];
}

boolean checkCmdOk () {
  byte tmp[2];
  byte rsp = getResponse(&tmp[0], 1);
  if (rsp == 1 && tmp[0] == 0x55) {
    return true;
  } else {
    return false;
  }
}

boolean checkCmdOk2 () {
  byte tmp[2];
  if (getResponse(&tmp[0], 2) == 2 && tmp[0] == 0x00 && tmp[1] == 0x55) {
    return true;
  } else {
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

void saveTempRegisters(void)
{
  measureRam();

  //  DEBPR(F("Check clobbered: "));
  //  DEBLN(ctx.clobbered);
  if (ctx.clobbered) return; // the registers have already been saved
  ctx.clobbered = true;
  //  DEBPR(F("Check saved: "));
  //  DEBLN(ctx.saved);
  if (ctx.saved) return; // nothing to do
  // DEBLN(F("Save temporary regs"));
  for (byte r=0; r < 32; r++)
    if (r < 2 || r > 27)
      ctx.regs[r] = readRegister(r);
}

#ifdef NEWREGCODE
// Write all registers 
void writeRegisters(byte *regs)
{
  byte wrRegs[] = {0x66,              // read/write
		   0xD0, 0x00, 0x00,  // start reg
		   0xD1, 0x00, 0x20,  // end reg
		   0xC2, 0x05,        // write registers
		   20 };              // go
  debugWire.sendCmd(wrRegs,  sizeof(wrRegs));
  debugWire.sendCmd(regs, 32);
}
#endif

// Set register <reg> by building and executing an "in <reg>,DWDR" instruction via the CMD_SET_INSTR register
void writeRegister (byte reg, byte val) {
  byte wrReg[] = {0x64,                                               // Set up for single step using loaded instruction
                  0xD2, inHigh(mcu.dwdr, reg), inLow(mcu.dwdr, reg), 0x23,    // Build "in reg,DWDR" instruction
                  val};                                               // Write value to register via DWDR
  measureRam();

  debugWire.sendCmd(wrReg,  sizeof(wrReg));
}

#ifdef NEWREGCODE
// Read all registers
void readRegisters (byte *regs)
{
  byte rdRegs[] = {0x66,
		   0xD0, 0x00, 0x00, // start reg
		   0xD1, 0x00, 0x20, // end reg
		   0xC2, 0x01,       // read registers
		   0x20 };            // start
  measureRam();

  debugWire.sendCmd(rdRegs,  sizeof(rdRegs));
  getResponse(regs, 32);               // Get value sent as response
}
#endif

// Read register <reg> by building and executing an "out DWDR,<reg>" instruction via the CMD_SET_INSTR register
byte readRegister (byte reg) {
  byte res;
  byte rdReg[] = {0x64,                                               // Set up for single step using loaded instruction
                  0xD2, outHigh(mcu.dwdr, reg), outLow(mcu.dwdr, reg),        // Build "out DWDR, reg" instruction
                  0x23};                                              // Execute loaded instruction
  measureRam();

  debugWire.sendCmd(rdReg,  sizeof(rdReg));
  getResponse(&res, 1);                                                     // Get value sent as response
  return res;
}

// Write one byte to SRAM address space using an SRAM-based value for <addr>, not an I/O address
void writeSRamByte (unsigned int addr, byte val) {
  saveTempRegisters();
  byte wrSRam[] = {0x66,                                              // Set up for read/write using repeating simulated instructions
                   0xD0, 0x00, 0x1E,                                  // Set Start Reg number (r30)
                   0xD1, 0x00, 0x20,                                  // Set End Reg number (r31) + 1
                   0xC2, 0x05,                                        // Set repeating copy to registers via DWDR
                   0x20,                                              // Go
		   (byte)(addr & 0xFF), (byte)(addr >> 8),            // r31:r30 (Z) = addr
                   0xD0, 0x00, 0x01,
                   0xD1, 0x00, 0x03,
                   0xC2, 0x04,                                        // Set simulated "in r?,DWDR; st Z+,r?" insrtuctions
                   0x20,                                              // Go
                   val};
  measureRam();

  debugWire.sendCmd(wrSRam, sizeof(wrSRam));
}

// Read one byte from SRAM address space using an SRAM-based value for <addr>, not an I/O address
byte readSRamByte (unsigned int addr) {
  byte res;
  byte rdSRam[] = {0x66,                                              // Set up for read/write using repeating simulated instructions
                   0xD0, 0x00, 0x1E,                                  // Set Start Reg number (r30)
                   0xD1, 0x00, 0x20,                                  // Set End Reg number (r31) + 1
                   0xC2, 0x05,                                        // Set repeating copy to registers via DWDR
                   0x20,                                              // Go
                   (byte)(addr & 0xFF), (byte)(addr >> 8),            // r31:r30 (Z) = addr
                   0xD0, 0x00, 0x00,                                  // 
                   0xD1, 0x00, 0x02,                                  // 
                   0xC2, 0x00,                                        // Set simulated "ld r?,Z+; out DWDR,r?" insrtuctions
                   0x20};                                             // Go
  saveTempRegisters();
  debugWire.sendCmd(rdSRam, sizeof(rdSRam));
  getResponse(&res,1);
  return res;
}

// Read <len> bytes from SRAM address space into buf[] using an SRAM-based value for <addr>, not an I/O address
// Note: can't read addresses that correspond to  r28-31 (Y & Z Regs) because Z is used for transfer (not sure why Y is clobbered) 
boolean readSRamBytes (unsigned int addr, byte *mem, byte len) {
  unsigned int len2 = len * 2;
  byte rsp;
  saveTempRegisters();
  reportTimeout = false;
  for (byte ii = 0; ii < 4; ii++) {
    byte rdSRam[] = {0x66,                                            // Set up for read/write using repeating simulated instructions
                     0xD0, 0x00, 0x1E,                                // Set Start Reg number (r30)
                     0xD1, 0x00, 0x20,                                // Set End Reg number (r31) + 1
                     0xC2, 0x05,                                      // Set repeating copy to registers via DWDR
                     0x20,                                            // Go
                     (byte)(addr & 0xFF), (byte)(addr >> 8),          // r31:r30 (Z) = addr
                     0xD0, 0x00, 0x00,                                // 
                     0xD1, (byte)(len2 >> 8), (byte)(len2 & 0xFF),    // Set repeat count = len * 2
                     0xC2, 0x00,                                      // Set simulated "ld r?,Z+; out DWDR,r?" instructions
                     0x20};                                           // Go
    debugWire.sendCmd(rdSRam, sizeof(rdSRam));
    rsp = getResponse(mem, len);
    if (rsp == len) {
      break;
    } else {
      // Wait and retry read
      delay(5);
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

byte readEepromByte (unsigned int addr) {
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
  saveTempRegisters();
  debugWire.sendCmd(setRegs, sizeof(setRegs));
  debugWire.sendCmd(doRead, sizeof(doRead));
  getResponse(&retval,1);                                                       // Read data from EEPROM location
  return retval;
}

//   
//   Write one byte to EEPROM
//   

void writeEepromByte (unsigned int addr, byte val) {
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
  saveTempRegisters();
  debugWire.sendCmd(setRegs, sizeof(setRegs));
  debugWire.sendCmd(doWrite, sizeof(doWrite));
}

//
//  Read len bytes from flash memory area at <addr> into data[] buffer
//
boolean readFlash (unsigned int addr, byte *mem, unsigned int len) {
  // Read len bytes form flash page at <addr>
  byte rsp;
  DEBPR(F("Read flash "));
  DEBPRF(addr,HEX);
  DEBPR("-");
  DEBLNF(addr+len-1, HEX);
  saveTempRegisters();
  reportTimeout = false;
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
    debugWire.sendCmd(rdFlash, sizeof(rdFlash));
    rsp = getResponse(mem, len);                                         // Read len bytes
     if (rsp ==len) {
      break;
    } else {
      // Wait and retry read
      delay(5);
    }
  }
  reportTimeout = true;
  return rsp==len;
}

// erase entire flash page
boolean eraseFlashPage(unsigned int addr) {
  saveTempRegisters();
  writeRegister(30, addr & 0xFF); // load Z reg with addr low
  writeRegister(31, addr >> 8  ); // load Z reg with addr high
  writeRegister(29, 0x03); // PGERS value for SPMCSR
  setWPc(mcu.bootaddr>>1); // so that access of all of flash is possible
  byte eflash[] = { 0x64, // single stepping
		    0xD2, // load into instr reg
		    outHigh(0x37, 29), // Build "out SPMCSR, r29"
		    outLow(0x37, 29), 
		    0x23,  // execute
		    0xD2, 0x95 , 0xE8, 0x33 }; // execute SPM
  debugWire.sendCmd(eflash, sizeof(eflash));
  return checkCmdOk2();
}
		    
// now move the page from temp memory to flash
boolean programFlashPage(unsigned int addr)
{
  flashcnt++;
  saveTempRegisters();
  writeRegister(30, addr & 0xFF); // load Z reg with addr low
  writeRegister(31, addr >> 8  ); // load Z reg with addr high
  writeRegister(29, 0x05); //  PGWRT value for SPMCSR
  setWPc(mcu.bootaddr>>1); // so that access of all of flash is possible
  byte eprog[] = { 0x64, // single stepping
		    0xD2, // load into instr reg
		    outHigh(0x37, 29), // Build "out SPMCSR, r29"
		    outLow(0x37, 29), 
		    0x23,  // execute
		    0xD2, 0x95 , 0xE8, 0x33 }; // execute SPM
  debugWire.sendCmd(eprog, sizeof(eprog));
  if (!mcu.bootaddr) { // no bootloader
    return checkCmdOk2(); // simply return
  } else { // bootloader: wait for spm to finish
    while ((readSPMCSR() & 0x1F) != 0) { 
      delayMicroseconds(100);
      DEBPR("."); // wait
    }
    return true;
  }
}

// load bytes into temp memory
boolean loadFlashBuffer(unsigned int addr, byte *mem)
{
  saveTempRegisters();
  writeRegister(30, addr & 0xFF); // load Z reg with addr low
  writeRegister(31, addr >> 8  ); // load Z reg with addr high
  writeRegister(29, 0x01); //  SPMEN value for SPMCSR
  setWPc(mcu.bootaddr>>1);
  byte ix = 0;
  while (ix < mcu.pagesz) {
    writeRegister(0, mem[ix++]);               // load next word
    writeRegister(1, mem[ix++]);
    byte eload[] = { 0x64, 0xD2,
		     outHigh(0x37, 29),       // Build "out SPMCSR, r29"
		     outLow(0x37, 29),
		     0x23,                    // execute
		     0xD2, 0x95, 0xE8, 0x23, // spm
		     0xD2, 0x96, 0x32, 0x23, // addiw Z,2
    };
    debugWire.sendCmd(eload, sizeof(eload));
  }
  return true;
}

boolean reenableRWW(void)
{
  saveTempRegisters();
  if (mcu.bootaddr) {
    setWPc(mcu.bootaddr>>1);
    writeRegister(29, 0x11); //  RWWSRE value for SPMCSR
    byte errw[] = { 0x64, 0xD2,
		    outHigh(0x37, 29),       // Build "out SPMCSR, r29"
		    outLow(0x37, 29),
		    0x23,                    // execute
		    0xD2, 0x95, 0xE8, 0x23 }; // spm
    debugWire.sendCmd(errw, sizeof(errw));
    return checkCmdOk2();
  }
  return true;
}

byte readSPMCSR(void)
{
  saveTempRegisters();
  byte sc[] = { 0x64, 0xD2,        // setup for single step and load instr reg 
		inHigh(0x37, 30),  // build "in 30, SPMCSR"
		inLow(0x37, 30),
		0x23 };             // execute
  debugWire.sendCmd(sc, sizeof(sc));
  return readRegister(30);
}

unsigned int getWPc () {
  debugWire.sendCmd((const byte[]) {0xF0}, 1);
  unsigned int pc = getWordResponse();
  return (pc - 1);
}

// get hardware breakpoint word address 
unsigned int getWBp () {
  debugWire.sendCmd((const byte[]) {0xF1}, 1);
  return (getWordResponse() - 1);
}

// get chip signature
unsigned int DWgetChipId () {
  debugWire.sendCmd((const byte[]) {0xF3}, 1);
  return (getWordResponse()) ;
}

// set PC (word address)
void setWPc (unsigned int wpc) {
  byte cmd[] = {0xD0, (byte)(wpc >> 8), (byte)(wpc & 0xFF)};
  debugWire.sendCmd(cmd, sizeof(cmd));
}

// set hardware breakpoint at word address
void setWBp (unsigned int wbp) {
  byte cmd[] = {0xD1, (byte)(wbp >> 8), (byte)(wbp & 0xFF)};
  debugWire.sendCmd(cmd, sizeof(cmd));
}


boolean execOffline(unsigned int opcode)
{
  byte cmd[] = {0xD2, (byte) (opcode >> 8), (byte) (opcode&0xFF), 0x23};
  measureRam();

  DEBPR(F("Offline exec: "));
  DEBLNF(opcode,HEX);
  if (opcode == 0x9598) return false;
  debugWire.sendCmd(cmd, sizeof(cmd));
  setWPc(ctx.wpc+2); // and set it higher by two - it will be decremented after execution
  return true;
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

  pinMode(SCK, INPUT);
  pinMode(MOSI, INPUT);
  pinMode(MISO, INPUT);
}

byte transfer (byte val) {
  measureRam();

  for (byte ii = 0; ii < 8; ++ii) {
    digitalWrite(MOSI, (val & 0x80) ? HIGH : LOW);
    digitalWrite(SCK, HIGH);
    delayMicroseconds(4);
    val = (val << 1) + digitalRead(MISO);
    digitalWrite(SCK, LOW); // slow pulse
    delayMicroseconds(4);
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
  TIMSK0 &= ~_BV(OCIE0A); // disable our blink IRQ
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
  delay(50);
  do {
    delay(50);
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

  if (!progmode)
    return;
  disableSpiPins();
  pinMode(RESET, INPUT);
  progmode = false;
  TIMSK0 |= _BV(OCIE0A); // reenable blink interrupt
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

#ifdef FREERAM
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
