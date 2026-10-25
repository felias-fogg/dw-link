// dw-link -- layer 1: debugWIRE and ISP primitives
#include "config.h"
#include "dwprim.h"

const byte maxspeedexp = 4; // corresponds to a factor of 16
const byte speedcmd[] PROGMEM = { 0x83, 0x82, 0x81, 0x80, 0xA0, 0xA1 };
unsigned long speedlimit = SPEEDLIMIT;

// some statistics
long timeoutcnt = 0; // counter for DW read timeouts
long flashcnt = 0; // number of flash writes 

// communcation interface to target
dwSerial      dw;

/****************** debugWIRE specific functions *************/

// send a break on the RESET line, check for response and calibrate 
boolean doBreak (boolean doprinterror) {
  measureRam();
  (void)doprinterror;

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
    reportFatalError(CONNERR_CAPACITIVE_LOAD, false);  // if not, we have a capacitive load on the RESET line
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
  byte wrRegs[] = {(byte)(0x66&opt.tmask),              // read/write
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
  byte wrReg[] = {(byte)(0x64&opt.tmask),                                  // Set up for single step using loaded instruction
                  0xD2, inHigh(mcu.dwdr, reg), inLow(mcu.dwdr, reg), 0x23, // Build "in reg,DWDR" instruction
                  val};                                                    // Write value to register via DWDR
  measureRam();

  dw.sendCmd(wrReg,  sizeof(wrReg));
}

// Read all registers
void DWreadRegisters (byte *regs)
{
  int response;
  byte rdRegs[] = {(byte)(0x66&opt.tmask),
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

// Read register <reg> by building and executing an "out DWDR,<reg>" instruction
// via the CMD_SET_INSTR register.
// This function is also used to check whether we have the right DWDR address.
byte DWreadRegister (byte reg, bool checkdwdr) {
  int response;
  byte res = 0;
  byte rdReg[] = {(byte)(0x64&opt.tmask),                               // Set up for single step using loaded instruction
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
  byte wrSram[] = {(byte)(0x66&opt.tmask),                           // Set up for read/write 
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
  byte wrIOreg[] = {(byte)(0x64&opt.tmask),                                // Set up for single step using loaded instruction
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
  byte rdSram[] = {(byte)(0x66&opt.tmask),                            // Set up for read/write 
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
  byte rdIOreg[] = {(byte)(0x64&opt.tmask),                            // Set up for single step using loaded instruction
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
  byte rdSram[] = {(byte)(0x66&opt.tmask),                          // Set up for read/write using 
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
  byte setRegs[] = {(byte)(0x66&opt.tmask),                                      // Set up for read/write 
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
  dw.sendCmd((byte)(0x64&opt.tmask));                                   // Set up for single step using loaded instruction
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
  byte setRegs[] = {(byte)(0x66&opt.tmask),                                       // Set up for read/write 
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
  byte rdFlash[] = {(byte)(0x66&opt.tmask),                             // Set up for read/write
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
  byte eflash[] = { (byte)(0x64&opt.tmask), // single stepping
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
  byte eprog[] = { (byte)(0x64&opt.tmask), // single stepping
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
  byte eload[] = { (byte)(0x64&opt.tmask), 0xD2,
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
  byte errw[] = { (byte)(0x64&opt.tmask), 0xD2,
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
  byte sc[] = { (byte)(0x64&opt.tmask), 0xD2,        // setup for single step and load instr reg 
		inHigh(0x37, 30),  // build "in 30, SPMCSR"
		inLow(0x37, 30),
		0x23 };             // execute
  measureRam();
  DWflushInput();
  dw.sendCmd(sc, sizeof(sc));
  return DWreadRegister(30, false);
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
byte ispReadFuse(FuseByte fuse)
{
  switch (fuse) {
  case LowFuse: return ispSend(0x50, 0x00, 0x00, 0x00, true);
  case HighFuse: return ispSend(0x58, 0x08, 0x00, 0x00, true);
  case ExtendedFuse: ispSend(0x50, 0x08, 0x00, 0x00, true);
  }
  return 0;
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
