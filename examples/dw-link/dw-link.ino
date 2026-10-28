// It should run on all ATmega328 boards and provides a hardware debugger
// for the classic ATtinys and some small ATmegas
//
// Copyright (c) 2021-2026 Bernhard Nebel
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
// Version 7 is no longer a GDB server: towards the host, it speaks the subset of the
// JTAGICE mkII protocol (Atmel AVR067) that a debugWIRE probe needs, and the GDB server
// is PyAvrOCD. Version 6, the stand-alone GDB server, lives on the master branch.
// The layers:
//   0  src/SingleWireSerial, src/dwSerial  -- the debugWIRE line
//   1  src/dwprim                          -- debugWIRE and ISP primitives
//   2  src/target                          -- target operations
//   3  src/mkii                            -- the mkII protocol to the host
// src/selftest holds the live tests of layers 1 and 2, which the host starts (vendor command).
//
// You can run it on an UNO, a Nano, or a Pro Mini.
// For the UNO, I designed a shield with different voltage levels and
// level shifters.
//
// I thought that the sketch should also work with the Leonardo-like boards
// and with the Mega board. For the former, I got stuck with the flashtest program.
// USB and tight interrupt timing do not seem to go together.
// For the latter, I experienced non-deterministic failures of unit tests, probably
// because relevant input ports are not in the I/O range and therefore the tight timing
// constraints are not satisfied.


#include "src/config.h"
#include "src/dwprim.h"
#include "src/target.h"
#include "src/mkii.h"


context ctx;
options opt;
mcutype mcu;
byte fatalerror = NO_FATAL;
boolean poweron;

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

byte ledmask, todsckmask, tmosimask, tmisomask, tsckmask;
volatile byte *ledout, *todsckmode, *tmosiout, *tmosimode, *tmisoin, *tsckout;

DEBDECLARE();

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
// block the timer0 irqs during transfers on the dw line, which needs timer1.
// The USART irqs stay on: up to v6, UCSR0B was saved and restored here with
// ~(RXCIE0|TXCIE0|UDRIE0), which clears bits 0-2 (bit numbers, not masks),
// so they were never blocked -- but the restore set UDRIE0 again after the
// TX buffer had run empty in between, and the TX irq then sent the whole
// 64-byte ring buffer a second time.
void blockIRQ(void)
{
  saveTIMSK0 = TIMSK0;
  TIMSK0 = 0;
}

void unblockIRQ(void)
{
  TIMSK0 = saveTIMSK0;
}

  
  

/******************* main ******************************/
int main(void) {
  // Arduino init
  init();
  
  // setup
  pinMode(DWLINE, INPUT);
  power(false);
  pinMode(SENSEBOARD, INPUT_PULLUP);
  ctx.levelshifting = !digitalRead(SENSEBOARD);
  mkiiInit();
  DEBINIT(DEBTX);
  DEBLN(F("\ndw-link version " VERSION));
  setupio();
  TIMSK0 = 0; // no millis interrupts
  pinMode(LEDGND, OUTPUT);
  digitalWrite(LEDGND, LOW);
  power(true); // switch target on
  _delay_ms(50); // let the power state settle

  initSession(); // initialize all critical global variables
  initOptions();

  //  DEBLN(F("Now configuereSupply"));
  DEBLN(F("Setup done"));

  // loop
  while (1) {
    if (Serial.available()) {
      mkiiPoll();
    } else if (ctx.state == RUN_STATE) {
      if (dw.available()) {
	byte cc = dw.read();
	if (cc == 0x0) { // break sent by target
	  if (expectUCalibrate()) {
	    DEBLN(F("Execution stopped"));
	    _delay_us(5); // avoid conflicts on the line
	    mkiiTargetStopped();
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

// init all global vars when a session starts
void initSession(void)
{
  DEBLN(F("initSession"));
  flashidle = true;
  hwbp = 0xFFFF;
  validpg = false;
  fatalerror = NO_FATAL;
  setSysState(NOTCONN_STATE);
  targetInitRegisters();
}

// init the options the host can set
void initOptions(void)
{
  opt.tmask = 0xDF;
  opt.readbeforewrite = true;
  opt.verifyload = true;
}


// Report a fatal error and stop everything
// error will be displayed when trying to execute.
// If checkio is set to true, we will check whether
// the connection to the target is still there.
// If not, the error is not recorded, but the connection is
// marked as not connected
// We will mark the error; the host learns about it from the
// answer to its command (from D1 on)
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

// switch the target supply of the dw-probe shield (IVSUP low = on). Switched on at once, the
// capacitors on the target side can pull the Arduino's 5 V down so far that its MCU misbehaves at
// 16 MHz, still above the brown-out level and thus without a reset (measured on a Uno without the
// recommended 100 uF across its supply rails: answers lost or garbled). So, with the shield, switch
// on softly: SOFTSTART_PULSES on-pulses of 1 us, 20 us apart, then permanently on. Between the
// pulses IVSUP is driven high, which switches the MOSFET off through 220 ohms instead of the 10 kohm
// pull-up. Measured with a Uno as target and the 100 uF in place: own supply down to 4.28 V when
// switched hard, 4.87 V with the ramp. The ramp is kept short (~4 ms, ~1.2 V/ms on a large target,
// within the datasheets' power-on slope rate of 0.01..10 V/ms), so that a target without BOD and
// with a short start-up delay runs as little as possible at a low supply voltage.
const unsigned int SOFTSTART_PULSES = 200;

void power(boolean on)
{
  volatile byte *mode = portModeRegister(digitalPinToPort(IVSUP));
  volatile byte *out = portOutputRegister(digitalPinToPort(IVSUP));
  byte mask = digitalPinToBitMask(IVSUP);

  poweron = on;
  if (!on) {
    *mode &= ~mask;                // input without pull-up: R5 switches the MOSFET off
    *out &= ~mask;
    return;
  }
  if (ctx.levelshifting) {
    *out |= mask;                  // output high: off, the gate actively driven
    *mode |= mask;
    for (unsigned int n = 0; n < SOFTSTART_PULSES; n++) softPulse(out, mask);
  }
  *out &= ~mask;                   // on
  *mode |= mask;
}

// one on-pulse of 1 us, then 20 us off
void softPulse(volatile byte *out, byte mask)
{
  byte sreg = SREG;
  cli();
  *out &= ~mask;
  _delay_us(1);
  *out |= mask;
  SREG = sreg;
  _delay_us(20);
}

// check whether connected
boolean targetOffline(void)
{
  if (ctx.state == PWRCYC_STATE || ctx.state == DWCONN_STATE ||
      ctx.state == RUN_STATE || ctx.state == LOAD_STATE) return false;
  return true;
}

