// dw-link -- layer 3: the JTAGICE mkII protocol (AVR067) over the serial line to the host
//
// A frame is MESSAGE_START, sequence number (2 bytes), body size (4), TOKEN, body, CRC (2),
// everything LSB first. The answer to a command carries the command's sequence number,
// an event carries 0xFFFF. Frames with a bad header or CRC are dropped without an answer.
//
// D1: sign-on, sign-off, GET_SYNC, parameters, baud rate switch. Everything else is
// answered with RSP_ILLEGAL_COMMAND.
#include <avr/eeprom.h>
#include "config.h"
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
const byte CMND_GET_SYNC = 0x0F;

// responses
const byte RSP_OK = 0x80;
const byte RSP_PARAMETER = 0x81;
const byte RSP_SIGN_ON = 0x86;
const byte RSP_FAILED = 0xA0;
const byte RSP_ILLEGAL_PARAMETER = 0xA1;
const byte RSP_ILLEGAL_EMULATOR_MODE = 0xA4;
const byte RSP_ILLEGAL_VALUE = 0xA6;
const byte RSP_ILLEGAL_COMMAND = 0xAA;

// parameters
const byte PAR_HW_VERSION = 0x01;
const byte PAR_FW_VERSION = 0x02;
const byte PAR_EMULATOR_MODE = 0x03;
const byte PAR_BAUD_RATE = 0x05;
const byte PAR_OCD_VTARGET = 0x06;
const byte PAR_TIMERS_RUNNING = 0x09;

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

static uint16_t crcUpdate(uint16_t crc, byte b);
static int readByte(void);
static void sendFrame(unsigned int seq, const byte *body, unsigned int len);
static unsigned int dispatch(unsigned int len);
static unsigned int setParameter(unsigned int len);
static unsigned int getParameter(void);
static unsigned int signOn(void);
static uint16_t vtargetMillivolt(void);
static void makeSerialNumber(void);

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
    if (value == EMULATOR_MODE_DEBUGWIRE || value == EMULATOR_MODE_UNKNOWN || value == EMULATOR_MODE_SPI)
      emulatormode = value; // D2: switching to debugWIRE syncs with the target
    else
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
