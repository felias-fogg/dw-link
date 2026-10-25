// dw-link -- layer 2: target operations, built on the primitives of layer 1
#include "config.h"
#include "dwprim.h"
#include "mcuinfo.h"
#include "target.h"

unsigned int hwbp = 0xFFFF; // the one hardware breakpoint (word address)

byte newpage[MAXPAGESIZE]; // one page of flash to program
byte page[MAXPAGESIZE]; // cached page contents - never overwrite it in the program! 
unsigned int lastpg; // address of the cached page
boolean validpg = false; // if cached page contents is valid
boolean flashidle; // flash programming is not active
unsigned int flashpageaddr; // current page to be programmed next

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
      reportFatalError(CONNERR_STUCKAT1_PC, false);
    } else {
      reportFatalError(CONNERR_CAPACITIVE_LOAD, false);
    }
    return true;
#endif
  }
  return false;
}

// setup everything after having entered DW mode
void setupDW(void)
{
  setSysState(DWCONN_STATE);
  targetReset(); 
  targetInitRegisters();
  // Now fix DWDR address if necessary.
  // This is a very special case, where the DWDR address
  // is not uniquely determined by the chip signature.
#if !defined(NOTINY)
  if (mcu.dwdr == 0x1F) { // DWDR address on the ATTiny2313 (and only on that MCU!)
    if (!DWreadRegister(1, true)) { // read register unsuccessful, must be a different address
      mcu.dwdr = 0x27;
      mcu.maskregs = t4313mask;
    }
  }
#endif
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
  _delay_ms(700);
  power(true);
  _delay_ms(700);
  if (targetDWConnect(false)) {
    setSysState(DWCONN_STATE);
    return true;
  }
  return false;
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
// skip write-only I/O registers
// special cases: general regs and SREG+SP
void targetReadSram(unsigned int addr, byte *mem, unsigned int len)
{
  unsigned int offset = 0;
  const byte *mask = mcu.maskregs;

  measureRam();

  while (offset < len) {
    if (addr + offset > 0xFF)
      mem[offset] = DWreadSramByte(addr + offset);
    else if (addr + offset < 0x20)
      mem[offset] = ctx.regs[addr + offset];
    else if (addr + offset == 0x5d) 
      mem[offset] = ctx.sp & 0xFF;
    else if (addr + offset == 0x5e)
      mem[offset] = (ctx.sp >> 8) & 0xFF;
    else if (addr + offset == 0x5f)
      mem[offset] = ctx.sreg;
    else {
      if (writeOnlyReg(addr + offset, mask))
        mem[offset] = 0;
      else
        mem[offset] = DWreadSramByte(addr + offset);
    }
    offset++;
  }
}

boolean writeOnlyReg(unsigned int addr, const byte *mask)
{
  byte mask_reg;

  measureRam();
  if (addr > 0xFF) return false;
  while ((mask_reg = pgm_read_byte(mask++))) {
    if (mask_reg == addr) return true; 
    if (mask_reg > addr) return false;
  }
  return false;
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
  if (opt.readbeforewrite) {
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

  if (opt.verifyload) {
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
void targetFlushFlashProg(void)
{
  targetWriteFlash(0, NULL, 0);
}

// write SRAM chunk
// skip read-only I/O registers
// special cases: general regs and SREG+SP
void targetWriteSram(unsigned int addr, byte *mem, unsigned int len)
{
  unsigned int offset = 0;
  const byte *mask = mcu.maskregs;

  measureRam();

  while (offset < len) {
    if (addr + offset > 0xFF)
      DWwriteSramByte(addr + offset, mem[offset]);
    else if (addr + offset < 0x20)
      ctx.regs[addr + offset] =  mem[offset];
    else if (addr + offset == 0x5d) 
      ctx.sp = (ctx.sp & 0xFF00) | mem[offset];
    else if (addr + offset == 0x5e)
      ctx.sp = (ctx.sp & 0x00FF) | (((unsigned int)mem[offset]) << 8);
    else if (addr + offset == 0x5f)
      ctx.sreg = mem[offset];
    else {
      if (!readOnlyReg(addr + offset, mask))
        DWwriteSramByte(addr + offset, mem[offset]);
    }
    offset++;
  }
}

boolean readOnlyReg(unsigned int addr, const byte *mask)
{
  measureRam();
  if (addr > 0xFF) return false;
  while (pgm_read_byte(mask++)); // read until we are beyond the first 0 in the mask array
  return writeOnlyReg(addr, mask); // then it 
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
// if runto == 0xFFFF, then consider hwbp as a possible target (if != 0xFFFF)
// otherwise the runto parameter has precedence 
void targetContinue(unsigned int runto)
{
  measureRam();

  if (hwbp != 0xFFFF || runto != 0xFFFF) {
    dw.sendCmd((byte)(0x61&opt.tmask));
    DWsetWBp(runto != 0xFFFF ? runto : hwbp);
  } else {
    dw.sendCmd((byte)(0x60&opt.tmask));
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
  byte cmd[] = {(byte)(0x60&opt.tmask), 0xD0, (byte)(ctx.wpc>>8), (byte)(ctx.wpc), 0x31};
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
