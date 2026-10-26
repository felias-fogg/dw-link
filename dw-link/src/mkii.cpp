// dw-link -- layer 3: the JTAGICE mkII protocol (AVR067) over the serial line to the host
//
// A frame is MESSAGE_START, sequence number (2 bytes), body size (4), TOKEN, body, CRC (2),
// everything LSB first. The answer to a command carries the command's sequence number,
// an event carries 0xFFFF. Frames with a bad header or CRC are dropped without an answer.
//
// D1: sign-on, sign-off, GET_SYNC, parameters, baud rate switch.
// D2: debugWIRE mode, device descriptor, FORCED_STOP with EVT_BREAK, READ_PC, READ_MEMORY.
// Everything else is answered with RSP_ILLEGAL_COMMAND.
#include <avr/eeprom.h>
#include "config.h"
#include "dwprim.h"
#include "target.h"
#include "mkii.h"

// framing
const byte MESSAGE_START = 0x1B;
const byte TOKEN = 0x0E;
const unsigned int EVENT_SEQUENCE = 0xFFFF;
#define MKII_MAXBODY 300 // the device descriptor (1+298) and a 256-byte page write (10+256) fit

// commands
const byte CMND_SIGN_OFF = 0x00;
const byte CMND_GET_SIGN_ON = 0x01;
const byte CMND_SET_PARAMETER = 0x02;
const byte CMND_GET_PARAMETER = 0x03;
const byte CMND_READ_MEMORY = 0x05;
const byte CMND_READ_PC = 0x07;
const byte CMND_FORCED_STOP = 0x0A;
const byte CMND_SET_DEVICE_DESCRIPTOR = 0x0C;
const byte CMND_SET_DEVICE_CONTEXT = 0x36; // not in AVR067; avrdude: CMND_SET_XMEGA_PARAMS (see setContext)
const byte CMND_GET_SYNC = 0x0F;

// responses
const byte RSP_OK = 0x80;
const byte RSP_PARAMETER = 0x81;
const byte RSP_MEMORY = 0x82;
const byte RSP_PC = 0x84;
const byte RSP_SIGN_ON = 0x86;
const byte RSP_FAILED = 0xA0;
const byte RSP_ILLEGAL_PARAMETER = 0xA1;
const byte RSP_ILLEGAL_MEMORY_TYPE = 0xA2;
const byte RSP_ILLEGAL_MEMORY_RANGE = 0xA3;
const byte RSP_ILLEGAL_EMULATOR_MODE = 0xA4;
const byte RSP_ILLEGAL_MCU_STATE = 0xA5;
const byte RSP_ILLEGAL_VALUE = 0xA6;
const byte RSP_ILLEGAL_COMMAND = 0xAA;
const byte RSP_DEBUGWIRE_SYNC_FAILED = 0xAC;

// events
const byte EVT_BREAK = 0xE0;
const byte BREAK_CAUSE_STOP = 0x00; // after stop, step, reset, run to (measured on the mkII)

// parameters
const byte PAR_HW_VERSION = 0x01;
const byte PAR_FW_VERSION = 0x02;
const byte PAR_EMULATOR_MODE = 0x03;
const byte PAR_BAUD_RATE = 0x05;
const byte PAR_OCD_VTARGET = 0x06;
const byte PAR_TIMERS_RUNNING = 0x09;
const byte PAR_MCU_STATE = 0x1A;
const byte PAR_TARGET_SIGNATURE = 0x1D;

// memory types
const byte MEMTYPE_SRAM = 0x20; // registers, I/O, extended I/O, SRAM by address
const byte MEMTYPE_EEPROM = 0x22;
const byte MEMTYPE_SPM = 0xA0;
const byte MEMTYPE_FLASH_PAGE = 0xB0;
const byte MEMTYPE_EEPROM_PAGE = 0xB1;
const byte MEMTYPE_SIGNATURE = 0xB4;

// emulator modes
const byte EMULATOR_MODE_DEBUGWIRE = 0x00;
const byte EMULATOR_MODE_UNKNOWN = 0x02;
const byte EMULATOR_MODE_SPI = 0x03;

// PAR_BAUD_RATE codes 1..8 (AVR067)
const unsigned long baudrates[] PROGMEM = { 2400, 4800, 9600, 19200, 38400, 57600, 115200, 14400 };

const char IDENT[] PROGMEM = "dw-link"; // the device ID in the sign-on, how the host recognizes dw-link

// the serial number in the sign-on: random, made once and kept at the end of the EEPROM
const byte SERIAL_MAGIC = 0xD7;
const unsigned int SERIAL_ADDR = E2END - 6; // magic byte, then 6 bytes

byte mkbuf[MKII_MAXBODY]; // body of the received command, then of the answer
byte serialno[6];
byte emulatormode = EMULATOR_MODE_UNKNOWN;
byte baudcode = 4; // 19200
boolean breakevent; // an EVT_BREAK follows the answer

static uint16_t crcUpdate(uint16_t crc, byte b);
static int readByte(void);
static void sendFrame(unsigned int seq, const byte *body, unsigned int len);
static unsigned int dispatch(unsigned int len);
static unsigned int setParameter(unsigned int len);
static unsigned int getParameter(void);
static unsigned int signOn(void);
static uint16_t vtargetMillivolt(void);
static void makeSerialNumber(void);
static byte enterDebugWire(void);
static unsigned int setDescriptor(unsigned int len);
static unsigned int setContext(unsigned int len);
static byte finishDescription(void);
static unsigned int forcedStop(void);
static unsigned int readPc(void);
static unsigned int readMemory(unsigned int len);
static unsigned int notStopped(void);
static unsigned long le32(const byte *p);
static unsigned int currentPc(void);

// a serial mkII starts at 19200 bps; the host switches with PAR_BAUD_RATE (AVR067)
void mkiiInit(void)
{
  makeSerialNumber();
  Serial.begin(MKII_BPS);
}

// a byte has arrived: read the frame it starts and answer it
void mkiiPoll(void)
{
  byte header[8];
  int c;
  unsigned int seq;
  unsigned long size;
  uint16_t crc;

  if ((c = Serial.read()) != MESSAGE_START) return; // not the start of a frame: skip
  header[0] = MESSAGE_START;
  for (byte i = 1; i < 8; i++) {
    if ((c = readByte()) < 0) return;
    header[i] = c;
  }
  seq = header[1] | ((unsigned int)header[2] << 8);
  size = header[3] | ((unsigned long)header[4] << 8) | ((unsigned long)header[5] << 16) |
    ((unsigned long)header[6] << 24);
  if (header[7] != TOKEN || size == 0 || size > MKII_MAXBODY) {
    DEBPR(F("mkII: bad header, size ")); DEBLN(size);
    return;
  }
  for (unsigned int i = 0; i < size; i++) {
    if ((c = readByte()) < 0) return;
    mkbuf[i] = c;
  }
  crc = 0xFFFF;
  for (byte i = 0; i < 8; i++) crc = crcUpdate(crc, header[i]);
  for (unsigned int i = 0; i < size; i++) crc = crcUpdate(crc, mkbuf[i]);
  for (byte i = 0; i < 2; i++) {
    if ((c = readByte()) < 0) return;
    if ((byte)c != (byte)(crc >> (8*i))) {
      DEBLN(F("mkII: CRC error"));
      return;
    }
  }
  byte cmd = mkbuf[0];
  byte arg = (size > 2 ? mkbuf[2] : 0);
  unsigned int answer = dispatch(size);
  sendFrame(seq, mkbuf, answer);
  if (breakevent) {
    breakevent = false;
    unsigned int pc = currentPc();
    byte event[6] = { EVT_BREAK, (byte)pc, (byte)(pc >> 8), 0, 0, BREAK_CAUSE_STOP };
    sendFrame(EVENT_SEQUENCE, event, sizeof(event));
  }
  if (cmd == CMND_SET_PARAMETER && mkbuf[0] == RSP_OK && size > 2 && mkbuf[1] == PAR_BAUD_RATE) {
    Serial.flush(); // the answer still goes out at the old speed
    Serial.begin(pgm_read_dword(&baudrates[arg-1]));
    baudcode = arg;
  } else if (cmd == CMND_SIGN_OFF) {
    // AVR067 names 19200 only as the power-on setting; going back to it at sign-off is
    // dw-link's choice, so that the next session finds it even without a reopened port
    Serial.flush();
    Serial.begin(MKII_BPS);
    baudcode = 4;
    emulatormode = EMULATOR_MODE_UNKNOWN;
  }
}

// D0: nothing to tell the host yet; from D3 on, EVT_BREAK
void mkiiTargetStopped(void)
{
  setSysState(DWCONN_STATE);
}

// CRC of AVR067: reflected CCITT polynomial 0x8408, start value 0xFFFF, no final xor
static uint16_t crcUpdate(uint16_t crc, byte b)
{
  crc ^= b;
  for (byte i = 0; i < 8; i++)
    crc = (crc & 1) ? (crc >> 1) ^ 0x8408 : crc >> 1;
  return crc;
}

// next byte of a frame, -1 if none comes within about 200 ms
static int readByte(void)
{
  unsigned int timeout = 20000;
  while (!Serial.available()) {
    if (--timeout == 0) {
      DEBLN(F("mkII: timeout in frame"));
      return -1;
    }
    _delay_us(10);
  }
  return Serial.read();
}

static void sendFrame(unsigned int seq, const byte *body, unsigned int len)
{
  byte header[8] = { MESSAGE_START, (byte)seq, (byte)(seq >> 8),
    (byte)len, (byte)(len >> 8), 0, 0, TOKEN };
  uint16_t crc = 0xFFFF;
  for (byte i = 0; i < 8; i++) {
    crc = crcUpdate(crc, header[i]);
    Serial.write(header[i]);
  }
  for (unsigned int i = 0; i < len; i++) {
    crc = crcUpdate(crc, body[i]);
    Serial.write(body[i]);
  }
  Serial.write((byte)crc);
  Serial.write((byte)(crc >> 8));
}

// execute the command in mkbuf[0..len-1], put the answer into mkbuf, return its length
static unsigned int dispatch(unsigned int len)
{
  switch (mkbuf[0]) {
  case CMND_GET_SIGN_ON:
    return signOn();
  case CMND_SIGN_OFF:
  case CMND_GET_SYNC:
    mkbuf[0] = RSP_OK;
    return 1;
  case CMND_SET_PARAMETER:
    return setParameter(len);
  case CMND_GET_PARAMETER:
    if (len < 2) break;
    return getParameter();
  case CMND_SET_DEVICE_DESCRIPTOR:
    return setDescriptor(len);
  case CMND_SET_DEVICE_CONTEXT:
    return setContext(len);
  case CMND_FORCED_STOP:
    return forcedStop();
  case CMND_READ_PC:
    return readPc();
  case CMND_READ_MEMORY:
    if (len < 10) break;
    return readMemory(len);
  }
  mkbuf[0] = RSP_ILLEGAL_COMMAND;
  return 1;
}

// mkbuf[1] is the parameter, mkbuf[2..] its value; on RSP_OK, mkbuf[1] keeps the parameter
static unsigned int setParameter(unsigned int len)
{
  byte value = mkbuf[2];
  byte answer = RSP_OK;

  if (len < 3) {
    mkbuf[0] = RSP_FAILED;
    return 1;
  }
  switch (mkbuf[1]) {
  case PAR_EMULATOR_MODE:
    if (value == EMULATOR_MODE_DEBUGWIRE)
      answer = enterDebugWire();
    else if (value == EMULATOR_MODE_UNKNOWN || value == EMULATOR_MODE_SPI) {
      emulatormode = value;             // D4: SPI
      setSysState(NOTCONN_STATE);
    } else
      answer = RSP_ILLEGAL_EMULATOR_MODE;
    break;
  case PAR_BAUD_RATE:
    if (value < 1 || value > 8 || pgm_read_dword(&baudrates[value-1]) > MAXHOSTBPS)
      answer = RSP_ILLEGAL_VALUE;
    break;
  case PAR_TIMERS_RUNNING:
    opt.tmask = (value ? 0xDF : 0xFF);
    break;
  default:
    answer = RSP_ILLEGAL_PARAMETER;
    break;
  }
  mkbuf[0] = answer;
  return 1;
}

// mkbuf[1] is the parameter
static unsigned int getParameter(void)
{
  byte board = (ctx.levelshifting ? 1 : 0); // hardware version: 0 = plain Arduino, 1 = dw-probe shield
  uint16_t mv;

  switch (mkbuf[1]) {
  case PAR_HW_VERSION:
    mkbuf[1] = board;
    mkbuf[2] = board;
    mkbuf[0] = RSP_PARAMETER;
    return 3;
  case PAR_FW_VERSION:
    mkbuf[1] = VERSION_MINOR;
    mkbuf[2] = VERSION_MAJOR;
    mkbuf[3] = VERSION_MINOR;
    mkbuf[4] = VERSION_MAJOR;
    mkbuf[0] = RSP_PARAMETER;
    return 5;
  case PAR_EMULATOR_MODE:
    mkbuf[1] = emulatormode;
    mkbuf[0] = RSP_PARAMETER;
    return 2;
  case PAR_BAUD_RATE:
    mkbuf[1] = baudcode;
    mkbuf[0] = RSP_PARAMETER;
    return 2;
  case PAR_OCD_VTARGET:
    mv = vtargetMillivolt();
    mkbuf[1] = (byte)mv;
    mkbuf[2] = (byte)(mv >> 8);
    mkbuf[0] = RSP_PARAMETER;
    return 3;
  case PAR_TIMERS_RUNNING:
    mkbuf[1] = (opt.tmask == 0xDF ? 1 : 0);
    mkbuf[0] = RSP_PARAMETER;
    return 2;
  case PAR_MCU_STATE:
    mkbuf[1] = (ctx.state == RUN_STATE ? 1 : (ctx.state == PROG_STATE ? 2 : 0));
    mkbuf[0] = RSP_PARAMETER;
    return 2;
  case PAR_TARGET_SIGNATURE:                  // in debugWIRE mode, LSB first (measured on the mkII)
    if (emulatormode != EMULATOR_MODE_DEBUGWIRE || mcu.sig == 0) {
      mkbuf[0] = RSP_ILLEGAL_EMULATOR_MODE;
      return 1;
    }
    mkbuf[1] = (byte)mcu.sig;
    mkbuf[2] = (byte)(mcu.sig >> 8);
    mkbuf[0] = RSP_PARAMETER;
    return 3;
  }
  mkbuf[0] = RSP_ILLEGAL_PARAMETER;
  return 1;
}

// RSP_SIGN_ON (AVR067): protocol version, then boot loader version, firmware version
// (minor, major) and hardware version of the main and the second MCU, which dw-link
// reports alike, the serial number and the device ID
static unsigned int signOn(void)
{
  byte board = (ctx.levelshifting ? 1 : 0);
  unsigned int len;

  mkbuf[0] = RSP_SIGN_ON;
  mkbuf[1] = 1; // communications protocol version
  for (byte m = 0; m < 2; m++) {
    mkbuf[2+4*m] = 0xFF; // no boot loader
    mkbuf[3+4*m] = VERSION_MINOR;
    mkbuf[4+4*m] = VERSION_MAJOR;
    mkbuf[5+4*m] = board;
  }
  memcpy(&mkbuf[10], serialno, 6);
  strcpy_P((char *)&mkbuf[16], IDENT);
  len = 16 + strlen_P(IDENT) + 1;
  return len;
}

// the target voltage: dw-link's own supply, measured against the 1.1 V bandgap (+-10 %),
// on the assumption that the target hangs on the same supply; 0 if the RESET line is low,
// i.e., the target is not powered
static uint16_t vtargetMillivolt(void)
{
  uint16_t adc;

  if (digitalRead(DWLINE) == LOW) return 0;
  ADMUX = _BV(REFS0) | 0x0E; // AVcc as reference, measure the bandgap
  _delay_ms(2);              // let the input settle
  ADCSRA |= _BV(ADEN) | _BV(ADSC);
  while (ADCSRA & _BV(ADSC));
  adc = ADC;
  if (adc == 0) return 0;
  return (uint16_t)(1125300UL / adc); // 1100 mV * 1023 / adc
}

// read the serial number from the EEPROM; on the first start, make one up and store it
static void makeSerialNumber(void)
{
  if (eeprom_read_byte((const uint8_t *)SERIAL_ADDR) != SERIAL_MAGIC) {
    // noise source: the low bits of repeated conversions of the bandgap,
    // mixed with timer 1, which runs freely
    ADMUX = _BV(REFS0) | 0x0E;
    ADCSRA |= _BV(ADEN);
    for (byte i = 0; i < 6; i++) {
      byte r = TCNT1L;
      for (byte j = 0; j < 8; j++) {
        ADCSRA |= _BV(ADSC);
        while (ADCSRA & _BV(ADSC));
        r = (r << 1 | r >> 7) ^ ADCL ^ TCNT1L;
        (void)ADCH;
      }
      eeprom_write_byte((uint8_t *)(SERIAL_ADDR + 1 + i), r);
    }
    eeprom_write_byte((uint8_t *)SERIAL_ADDR, SERIAL_MAGIC);
  }
  for (byte i = 0; i < 6; i++)
    serialno[i] = eeprom_read_byte((const uint8_t *)(SERIAL_ADDR + 1 + i));
}

/****************** D2: debugWIRE mode, device descriptor, stop, reads ******************/

static unsigned long le32(const byte *p)
{
  return p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

// the PC as a word address: the saved one, or, before the registers are saved, the target's
static unsigned int currentPc(void)
{
  if (ctx.saved) return ctx.wpc;
  return DWgetWPc(true);
}

// PAR_EMULATOR_MODE debugWIRE: sync with the target and read its signature. Unlike the mkII,
// which lets the target run on, dw-link leaves it stopped: reading registers needs DWDR, which
// only the device descriptor brings, and avrdude and PyAvrOCD send FORCED_STOP next anyway.
static byte enterDebugWire(void)
{
  fatalerror = NO_FATAL;
  setSysState(NOTCONN_STATE);
  emulatormode = EMULATOR_MODE_UNKNOWN;
  mcu.described = false;
  if (!targetDWConnect(false)) {
    fatalerror = NO_FATAL;              // the host learns about it from the answer
    setSysState(NOTCONN_STATE);
    return RSP_DEBUGWIRE_SYNC_FAILED;
  }
  ctx.saved = false;                    // nothing read from the target yet
  emulatormode = EMULATOR_MODE_DEBUGWIRE;
  setSysState(DWCONN_STATE);
  return RSP_OK;
}

// CMND_SET_DEVICE_DESCRIPTOR: the device data dw-link needs, field layout as avrdude sends it
// (PyAvrOCD: jtag2.DESCRIPTOR_TAIL). Then, on a stopped target, what needs DWDR: the check of
// the PC, telling the ATtiny2313 from the 2313A, and the first save of the registers.
static unsigned int setDescriptor(unsigned int len)
{
  const byte *d = &mkbuf[1];
  unsigned int op0, op1;

  mkbuf[0] = RSP_FAILED;
  if (len < 1 + 298) return 1;
  memcpy(mcu.readio, d, 8);               // ucReadIO: 0x20-0x5F
  memcpy(&mcu.readio[8], d + 32, 52);     // ucReadExtIO: from 0x60 on
  mcu.dwdr = d[240] ? d[240] : d[283];    // in debugWIRE, the IDR is the DWDR; ucDWDRAddress else
  mcu.pagesz = d[243] | (d[244] << 8);
  mcu.extiotop = d[250] | (d[251] << 8);
  mcu.flashsz = le32(d + 252);
  mcu.bootaddr = d[286] | (d[287] << 8);  // uiStartSmallestBootLoaderSection (words), 0: no boot section
  mcu.eecr = (d[296] | (d[297] << 8)) - 0x20; // EECRAddress is a memory address
  mcu.eedr = mcu.eecr + 1;
  mcu.eearl = mcu.eecr + 2;
  mcu.eearh = 0;
  // ucEepromInst begins with 'out EEARH,r31' and 'out EEARL,r30', high byte first;
  // the same address twice means that there is no EEARH
  op0 = ((unsigned int)d[256] << 8) | d[257];
  op1 = ((unsigned int)d[258] << 8) | d[259];
  if ((op0 & 0xF800) == 0xB800 && (op1 & 0xF800) == 0xB800) {
    byte a0 = ((op0 >> 5) & 0x30) | (op0 & 0x0F);
    byte a1 = ((op1 >> 5) & 0x30) | (op1 & 0x0F);
    mcu.eearl = a1;
    if (a0 != a1) mcu.eearh = a0;
  }
  mcu.targetpgsz = mcu.pagesz * (mcu.erase4pg ? 4 : 1);
  mkbuf[0] = finishDescription();
  return 1;
}

// what follows the device data, however they came: a plausibility check, and, on a stopped target,
// what needs DWDR -- the check of the PC, telling the ATtiny2313 from the 2313A, and the first save
// of the registers
static byte finishDescription(void)
{
  if (mcu.pagesz == 0 || mcu.targetpgsz > MAXPAGESIZE || mcu.flashsz == 0) return RSP_FAILED;
  mcu.described = true;
  if (emulatormode == EMULATOR_MODE_DEBUGWIRE && ctx.state == DWCONN_STATE && !ctx.saved) {
    if (stuckAtOneOrCap()) {
      mcu.described = false;
      return RSP_FAILED;
    }
    if (mcu.sig == 0x910A) {              // ATtiny2313 and 2313A share the signature, not DWDR
      mcu.dwdr = 0x1F;
      if (!DWreadRegister(1, true)) mcu.dwdr = 0x27;
    }
    targetSaveRegisters();
  }
  return RSP_OK;
}

// CMND 0x36: the device context as EDBG gets it, sent by Microchip Studio 7 to the mkII instead of
// the descriptor (captured 2026-10-25; firmware 7.27 accepts it). Unlike the descriptor, it carries
// the number of page buffers per flash block, i.e., the 4-page erase.
// Format: 0x36, section, length (2 bytes, high byte first), data. Sections:
//   0x02  device data, 31 bytes, the 'TINY' layout of EDBG (PyAvrOCD: xavr8target.py)
//   0x80  read mask for I/O 0x20-0x5F (8 bytes)     0x88  write mask for the same
//   0x81  read mask for I/O from 0x60 on            0x89  write mask for the same
// The write masks are not needed: writes always reach the target.
static unsigned int setContext(unsigned int len)
{
  const byte *d = &mkbuf[4];
  unsigned int n;
  byte buffers;

  mkbuf[0] = RSP_FAILED;
  if (len < 4) return 1;
  n = ((unsigned int)mkbuf[2] << 8) | mkbuf[3];
  if (len < 4 + n) return 1;
  switch (mkbuf[1]) {
  case 0x02:
    if (n < 31) return 1;
    mcu.pagesz = d[0] | (d[1] << 8);
    mcu.flashsz = le32(d + 2);
    mcu.bootaddr = le32(d + 10) < mcu.flashsz ? le32(d + 10) / 2 : 0; // boot base, bytes -> words;
                                                // outside the flash (Studio for the t1634): none
    mcu.extiotop = (d[14] | (d[15] << 8)) > 0x60 ? (d[14] | (d[15] << 8)) - 1 : 0; // up to the SRAM base
    buffers = d[20] ? d[20] : 1;          // page buffers per flash block
    mcu.erase4pg = (buffers == 4);
    mcu.dwdr = d[24];                     // OCD module address
    mcu.eearh = (d[25] != d[26] ? d[25] : 0); // EEARH = EEARL: there is no EEARH
    mcu.eearl = d[26];
    mcu.eecr = d[27];
    mcu.eedr = d[28];
    mcu.targetpgsz = mcu.pagesz * buffers;
    mkbuf[0] = finishDescription();
    return 1;
  case 0x80:
    memcpy(mcu.readio, d, n < 8 ? n : 8);
    break;
  case 0x81:
    memcpy(&mcu.readio[8], d, n < sizeof(mcu.readio) - 8 ? n : sizeof(mcu.readio) - 8);
    break;
  case 0x88:
  case 0x89:
    break;
  default:
    mkbuf[0] = RSP_ILLEGAL_PARAMETER;
    return 1;
  }
  mkbuf[0] = RSP_OK;
  return 1;
}

// CMND_FORCED_STOP: RSP_OK, then EVT_BREAK -- also when the target is stopped already, as the
// mkII does it (measured)
static unsigned int forcedStop(void)
{
  if (emulatormode != EMULATOR_MODE_DEBUGWIRE || ctx.state == NOTCONN_STATE || ctx.state == ERROR_STATE) {
    mkbuf[0] = RSP_ILLEGAL_EMULATOR_MODE;
    return 1;
  }
  if (ctx.state == RUN_STATE) {
    targetBreak();
    if (!expectUCalibrate()) {
      mkbuf[0] = RSP_FAILED;
      return 1;
    }
    setSysState(DWCONN_STATE);
    ctx.saved = false;
    if (mcu.described) targetSaveRegisters();
  }
  breakevent = true;
  mkbuf[0] = RSP_OK;
  return 1;
}

// 0 if the target is in debugWIRE mode and stopped, else the length of the refusal in mkbuf
static unsigned int notStopped(void)
{
  if (emulatormode != EMULATOR_MODE_DEBUGWIRE || ctx.state == NOTCONN_STATE || ctx.state == ERROR_STATE) {
    mkbuf[0] = RSP_ILLEGAL_EMULATOR_MODE;
    return 1;
  }
  if (ctx.state != DWCONN_STATE) {
    mkbuf[0] = RSP_ILLEGAL_MCU_STATE;
    mkbuf[1] = (ctx.state == RUN_STATE ? 1 : 2); // RUNNING, PROGRAMMING
    return 2;
  }
  if (mcu.described && !ctx.saved) targetSaveRegisters();
  return 0;
}

// CMND_READ_PC: RSP_PC with the word address
static unsigned int readPc(void)
{
  unsigned int n = notStopped();
  unsigned int pc;

  if (n) return n;
  pc = currentPc();
  mkbuf[0] = RSP_PC;
  mkbuf[1] = (byte)pc;
  mkbuf[2] = (byte)(pc >> 8);
  mkbuf[3] = 0;
  mkbuf[4] = 0;
  return 5;
}

// CMND_READ_MEMORY: memory type, byte count, start address (byte address for all types)
static unsigned int readMemory(unsigned int len)
{
  byte memtype = mkbuf[1];
  unsigned long count = le32(&mkbuf[2]);
  unsigned long addr = le32(&mkbuf[6]);
  unsigned int n = notStopped();
  byte sigbytes[3] = { 0x1E, (byte)(mcu.sig >> 8), (byte)mcu.sig };

  (void)len;
  if (n) return n;
  if (count == 0 || count > MKII_MAXBODY - 1) {
    mkbuf[0] = RSP_ILLEGAL_MEMORY_RANGE;
    return 1;
  }
  if (memtype != MEMTYPE_SIGNATURE && !mcu.described) {
    mkbuf[0] = RSP_FAILED;
    return 1;
  }
  switch (memtype) {
  case MEMTYPE_SRAM:
    if (addr + count > 0x10000UL) {
      mkbuf[0] = RSP_ILLEGAL_MEMORY_RANGE;
      return 1;
    }
    targetReadSram(addr, &mkbuf[1], count);
    break;
  case MEMTYPE_EEPROM:
  case MEMTYPE_EEPROM_PAGE:
    targetReadEeprom(addr, &mkbuf[1], count);
    break;
  case MEMTYPE_SPM:
  case MEMTYPE_FLASH_PAGE:                // unlike the mkII, any start address works
    if (addr + count > mcu.flashsz) {
      mkbuf[0] = RSP_ILLEGAL_MEMORY_RANGE;
      return 1;
    }
    targetReadFlash(addr, &mkbuf[1], count);
    break;
  case MEMTYPE_SIGNATURE:
    if (addr + count > 3) {
      mkbuf[0] = RSP_ILLEGAL_MEMORY_RANGE;
      return 1;
    }
    for (byte i = 0; i < count; i++) mkbuf[1+i] = sigbytes[addr+i];
    break;
  default:
    mkbuf[0] = RSP_ILLEGAL_MEMORY_TYPE;
    return 1;
  }
  if (fatalerror) {
    mkbuf[0] = RSP_FAILED;
    return 1;
  }
  mkbuf[0] = RSP_MEMORY;
  return 1 + count;
}
