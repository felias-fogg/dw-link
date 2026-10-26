// dw-link -- live tests of layers 1 and 2 on the real target (UNITDW, UNITTG builds)
#include "config.h"
#include "dwprim.h"
#include "target.h"
#include "mcuinfo.h"
#include "selftest.h"

/*****************************************************************************************************/
/******************************************** Unit tests *********************************************/
/*****************************************************************************************************/



/********************************* General setup and reporting functions ******************************/
#if defined(UNITDW) ||  defined(UNITTG) ||  defined(UNITALL)

byte testbuf[40]; // test data, and the test code on its way to the target

// run all unit tests, once after reset; the target must already be in debugWIRE mode
// (the results go to the serial line in plain text, at MKII_BPS)
void alltests(void)
{
  int failed = 0;
  int testnum = 1;

  testMessage(PSTR("\ndw-link " VERSION " live tests"), -1);
  if (!targetDWConnect(false)) {
    testMessage(PSTR("*** No debugWIRE connection, error: "), fatalerror);
    return;
  }
  if (!setMcuAttr(mcu.sig)) {             // without a host, the device data come from the table
    testMessage(PSTR("*** MCU not in the table: "), mcu.sig);
    return;
  }
  if (stuckAtOneOrCap()) {                // needs the flash size from the table
    testMessage(PSTR("*** PC stuck at one or capacitive load, error: "), fatalerror);
    return;
  }
  setupDW();
  testMessage(PSTR("MCU signature: "), mcu.sig);
  testMessage(PSTR("debugWIRE bps: "), ctx.bps);
#if UNITDW
  failed += DWtests(testnum);
#endif
#if UNITTG
  failed += targetTests(testnum);
#endif
  testSummary(failed);
}

// give a summary of the test batch
void testSummary(int failed)
{
  if (failed) {
    testMessage(PSTR("\n... some live tests failed:"), failed);
  } else {
    testMessage(PSTR("\n...live tests successfully finished"), -1);
  }
}

int testResult(boolean succ)
{
  if (succ) {
    testMessage(PSTR("  -> succeeded"), -1);
    return 0;
  } else {
    testMessage(PSTR("  -> failed ***"), -1);
    return 1;
  }
}

// print a message from PROGMEM, followed by num if num is not negative
void testMessage(const char pstr[], long num)
{
  Serial.print((const __FlashStringHelper *)pstr);
  if (num >= 0) Serial.print(num);
  Serial.println();
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
 memcpy_P(testbuf, testcode, sizeof(testcode));
 if (mcu.rambase == 0x60) { // small ATtinys with only little memory
   testbuf[12] = 0x60; // use address 0x0060 instead of 0x0100
   testbuf[13] = 0x00;
   testbuf[16] = 0x60;
   testbuf[17] = 0x00;
 }
 targetWriteFlash(0x1aa, testbuf, sizeof(testcode));
 targetFlushFlashProg();
}


/********************************* Target specific tests ******************************************/

int targetTests(int &num) {
  int failed = 0;
  boolean succ;
  int testnum;
  unsigned int i;
  long lastflashcnt;

  if (targetOffline()) {
    return 0;
  }

  if (num >= 1) testnum = num;
  else testnum = 1;

  // write a (target-size) flash page (only check that no fatal error)
  testMessage(PSTR("targetWriteFlashPage: "), testnum++);
  const int flashaddr = 0x80;
  fatalerror = NO_FATAL;
  setSysState(DWCONN_STATE);
  DWeraseFlashPage(flashaddr);
  DWreenableRWW();
  validpg = false;
  for (i=0; i < mcu.targetpgsz; i++) page[i] = 0;
  for (i=0; i < mcu.targetpgsz; i++) newpage[i] = i;
  targetWriteFlashPage(flashaddr);
  lastflashcnt = flashcnt;
  failed += testResult(fatalerror == NO_FATAL);

  // write same page again (since cache is valid, should not happen)
  testMessage(PSTR("targetWriteFlashPage (check vaildpg): "), testnum++);
  fatalerror = NO_FATAL; setSysState(DWCONN_STATE);
  targetWriteFlashPage(flashaddr);
  failed += testResult(fatalerror == NO_FATAL && lastflashcnt == flashcnt);
  
  // write same page again (cache valid flag cleared), but since contents is tha same, do not write
  testMessage(PSTR("targetWriteFlashPage (check contents): "), testnum++);
  fatalerror = NO_FATAL; setSysState(DWCONN_STATE);
  validpg = false;
  targetWriteFlashPage(flashaddr);
  failed += testResult(fatalerror == NO_FATAL && lastflashcnt == flashcnt);

  // try to write a cache page at an address that is not at a page boundary -> fatal error
  testMessage(PSTR("targetWriteFlashPage (addr error): "), testnum++);
  fatalerror = NO_FATAL; setSysState(DWCONN_STATE);
  targetWriteFlashPage(flashaddr+2);
  failed += testResult(fatalerror != NO_FATAL && lastflashcnt == flashcnt);

  // read page (should be done from cache)
  testMessage(PSTR("targetReadFlashPage (from cache): "), testnum++);
  fatalerror = NO_FATAL; setSysState(DWCONN_STATE);
  page[0] = 0x11; // mark first cell in order to see whether things get reloaded
  targetReadFlashPage(flashaddr);
  failed += testResult(fatalerror == NO_FATAL && page[0] == 0x11);

  // read page (force cache to be invalid and read from flash)
  testMessage(PSTR("targetReadFlashPage: "), testnum++);
  fatalerror = NO_FATAL; setSysState(DWCONN_STATE);
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
  testMessage(PSTR("targetRestoreRegisters/targetSaveRegisters: "), testnum++);
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
  testMessage(PSTR("targetInitRegisters: "), testnum++);
  targetInitRegisters();
  failed += testResult(ctx.wpc == 0 && ctx.saved == true); // this is the only requirement!

  testMessage(PSTR("targetWriteEeprom/targetReadEeprom: "), testnum++);
  DEBLN(F("targetEEPROM"));
  succ = true;
  const int eeaddr = 0x15;
  testbuf[0] = 0x30;
  testbuf[1] = 0x45;
  testbuf[2] = 0x67;
  targetWriteEeprom(eeaddr, testbuf, 3);
  targetReadEeprom(eeaddr, &testbuf[3], 3);
  for (i = 0; i <3; i++)
    if (testbuf[i] != testbuf[i+3]) succ = false;
  DEBLNF(testbuf[0],HEX);
  DEBLNF(testbuf[1],HEX);
  DEBLNF(testbuf[2],HEX);
  DEBLNF(testbuf[3],HEX);
  DEBLNF(testbuf[4],HEX);
  DEBLNF(testbuf[5],HEX);
  for (i = 0; i < 6; i++) testbuf[i] = 0xFF;
  targetWriteEeprom(eeaddr, testbuf, 3);
  targetReadEeprom(eeaddr, &testbuf[3], 3);
  for (i = 0; i <3; i++)
    if (testbuf[3+i] != 0xFF) succ = false;
  DEBLNF(testbuf[0],HEX);
  DEBLNF(testbuf[1],HEX);
  DEBLNF(testbuf[2],HEX);
  DEBLNF(testbuf[3],HEX);
  DEBLNF(testbuf[4],HEX);
  DEBLNF(testbuf[5],HEX);
  failed += testResult(succ);

  testMessage(PSTR("targetWriteSram/targetReadSram: "), testnum++);
  succ = true;
  const int ramaddr = mcu.rambase;
  testbuf[0] = 0x31;
  testbuf[1] = 0x46;
  testbuf[2] = 0x68;
  targetWriteSram(ramaddr, testbuf, 3);
  targetReadSram(ramaddr, &testbuf[3], 3);
  for (i = 0; i <3; i++)
    if (testbuf[i] != testbuf[i+3]) succ = false;
  for (i = 0; i < 6; i++) testbuf[i] = 0xFF;
  targetWriteSram(ramaddr, testbuf, 3);
  for (i = 0; i < 3; i++) testbuf[i] = 0;
  targetReadSram(ramaddr, testbuf, 3);
  for (i = 0; i <3; i++)
    if (testbuf[i] != 0xFF) succ = false;
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


  testMessage(PSTR("targetStep (ldi r18,0x49): "), testnum++);
  succ = true;
  targetInitRegisters();
  ctx.wpc = 0xd9; // ldi instruction
  ctx.sp = mcu.ramsz+mcu.rambase-1; // SP to upper limit of RAM
  targetRestoreRegisters(); 
  targetStep();
  if (!expectBreakAndU())
    succ = false;
  targetSaveRegisters();
  failed += testResult(succ && ctx.wpc == 0xda && ctx.regs[18] == 0x49);

  testMessage(PSTR("targetStep (rcall): "), testnum++);
  succ = true;
  ctx.wpc = 0xde; // rcall instruction
  targetRestoreRegisters();
  targetStep(); // one step leads to Break+0x55
  if (!expectBreakAndU()) succ = false;  
  targetSaveRegisters();
  failed += testResult(succ && ctx.wpc == 0xe4);
  testMessage(PSTR("Test targetContinue/targetBreak: "), testnum++);
  succ = true;
  hwbp = 0xFFFF;
  targetRestoreRegisters();
  targetContinue(0xFFFF);
  targetBreak(); // DW responds with 0x55 on break
  if (!expectUCalibrate()) succ = false;
  targetSaveRegisters();
  failed += testResult(succ && ctx.wpc == 0xd8 && ctx.regs[17] == 0x91);

  testMessage(PSTR("targetReset: "), testnum++);
  ctx.sreg= 0xFF; // SREG
  DEBPR(F("SREG before: ")); DEBLNF(ctx.sreg,HEX);
  DEBPR(F("WPC before:  ")); DEBLNF(ctx.wpc,HEX);
  targetRestoreRegisters();
  targetReset(); // response is taken care of by the function itself
  targetSaveRegisters();
  DEBPR(F("SREG after: ")); DEBLNF(ctx.sreg,HEX);
  DEBPR(F("WPC after:  ")); DEBLNF(ctx.wpc,HEX);
  failed += testResult((ctx.wpc & 0x7F) == 0 && ctx.sreg == 0); // PC can be set to boot area!

  if (num >= 1) {
    num = testnum;
    return failed;
  } else {
    testSummary(failed);
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
    return 0;
  }

  if (num >= 1) testnum = num;
  else testnum = 1;

  // write and read 3 registers
  testMessage(PSTR("DWwriteRegister/DWreadRegister: "), testnum++);
  DWwriteRegister(0, 0x55);
  DWwriteRegister(15, 0x9F);
  DWwriteRegister(31, 0xFF);
  failed += testResult(DWreadRegister(0, false) == 0x55 && DWreadRegister(15, false) == 0x9F && DWreadRegister(31, false) == 0xFF);

  // write registers in one go and read them in one go (much faster than writing/reading individually) 
  testMessage(PSTR("DWwriteRegisters/DWreadRegisters: "), testnum++);
  for (byte i=0; i < 32; i++) testbuf[i] = i*2+1;
  DWwriteRegisters(testbuf);
  for (byte i=0; i < 32; i++) testbuf[i] = 0;
  DWreadRegisters(testbuf);
  succ = true;
  for (byte i=0; i < 32; i++) {
    if (testbuf[i] != i*2+1) {
      succ = false;
      break;
    }
  }
  failed += testResult(succ);

  // write to and read from an IO reg (0x3F = SREG)
  testMessage(PSTR("DWwriteIOreg/DWreadIOreg: "), testnum++);
  DWwriteIOreg(0x3F, 0x55);
  failed += testResult(DWreadIOreg(0x3F) == 0x55);

  // write into (lower) sram and read it back from corresponding IO reag 
  testMessage(PSTR("DWwriteSramByte/DWreadIOreg: "), testnum++);
  DWwriteSramByte(0x3F+0x20, 0x1F);
  temp = DWreadIOreg(0x3F);
  failed += testResult(temp == 0x1F);

  // write into IO reg and read it from the ocrresponding sram addr
  testMessage(PSTR("DWwriteIOreg/DWreadSramByte: "), testnum++);
  DWwriteIOreg(0x3F, 0xF2);
  failed += testResult(DWreadSramByte(0x3F+0x20) == 0xF2);

  // write a number of bytes to sram and read them again byte by byte
  testMessage(PSTR("DWwriteSramByte/DWreadSramByte: "), testnum++);
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
  testMessage(PSTR("DWreadSram (bulk): "), testnum++);
  for (byte i=0; i < 32; i++) testbuf[i] = 0;
  DWreadSramBytes(mcu.rambase, testbuf, 32);
  succ = true;
  for (byte i=0; i < 32; i++) {
    if (testbuf[i] != i+1) {
      succ = false;
      break;
    }
  }
  failed += testResult(succ);

  // write to EEPROM (addr 0x15) and read from it
  testMessage(PSTR("DWwriteEepromByte/DWreadEepromByte: "), testnum++);
  const int eeaddr = 0x15;
  succ = true;
  DWwriteEepromByte(eeaddr, 0x38);
  if (DWreadEepromByte(eeaddr) != 0x38) succ = false;
  DWwriteEepromByte(eeaddr, 0xFF);
  if (DWreadEepromByte(eeaddr) != 0xFF) succ = false;
  failed += testResult(succ);
  
  // erase flash page (check only for errors)
  testMessage(PSTR("DWeraseFlashPage: "), testnum++);
  const int flashaddr = 0x100;
  DWeraseFlashPage(flashaddr);
  failed += testResult(fatalerror == NO_FATAL);
  fatalerror = NO_FATAL;

  // read the freshly cleared flash page
  testMessage(PSTR("DWreadFlash (empty page): "), testnum++);
  for (i=0; i < mcu.pagesz; i++) newpage[i] = 0;
  succ = true;
  DWreenableRWW();
  DWreadFlash(flashaddr, newpage, mcu.pagesz);
  for (i=0; i < mcu.pagesz; i++) {
    if (newpage[i] != 0xFF) succ = false;
  }
  failed += testResult(succ);
    
  // program one flash page (only check for error code returns)
  testMessage(PSTR("DWloadFlashPage/DWprogramFlashPage: "), testnum++);
  for (i=0; i < mcu.pagesz; i++) newpage[i] = 255-i;
  DWloadFlashPageBuffer(flashaddr, newpage);
  DWprogramFlashPage(flashaddr);
  failed += testResult(fatalerror == NO_FATAL);
  fatalerror = NO_FATAL;

  // now try to read the freshly flashed page
  testMessage(PSTR("DWreenableRWW/DWreadFlash: "), testnum++);
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
    testMessage(PSTR("DWFlash prog. in boot section: "), testnum++);
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
	DEBLN(newpage[i]);
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
  testMessage(PSTR("DWgetChipId: "), testnum++);
  failed += testResult(mcu.sig != 0 && (DWgetChipId() == mcu.sig ||
					(mcu.sig == 0x9514 && DWgetChipId() == 0x950F) || // imposter 328P!
					(mcu.sig == 0x9406 && DWgetChipId() == 0x940B) || // imposter 168PA!
					(mcu.sig == 0x930A && DWgetChipId() == 0x930F) || // imposter 88PA!
					(mcu.sig == 0x9205 && DWgetChipId() == 0x920A))); // imposter 48PA!
  
  // Set/get PC (word address)
  testMessage(PSTR("DWsetWPc/DWgetWPc: "), testnum++);
  unsigned int pc = 0x3F; 
  DWsetWPc(pc);
  unsigned int newpc = DWgetWPc(true);
  DEBLNF(newpc, HEX);
  failed += testResult(newpc == pc - 1);

  /*
  // Set/get hardware breakpoint
  testMessage(PSTR("Test DWsetWBp/DWgetWBp: "), testnum++);
  DWsetWBp(pc);
  failed += testResult(DWgetWBp() == pc);
  */
  
  // execute one instruction offline
  testMessage(PSTR("DWexecOffline (eor r1,r1 at WPC=0x013F): "), testnum++);
  DWwriteIOreg(0x3F, 0); // write SREG
  DWwriteRegister(1, 0x55);
  pc = 0x13F;
  DWsetWPc(pc);
  DWexecOffline(0x2411); // specify opcode as MSB LSB (bigendian!)
  succ = false;
  if (pc + 1 == DWgetWPc(true)) // PC advanced by one, then +1 for break, but this is autmatically subtracted
    if (DWreadRegister(1, false) == 0)  // reg 1 should be zero now
      if (DWreadIOreg(0x3F) == 0x02) // in SREG, only zero bit should be set
	succ = true;
  failed += testResult(succ);

  // execute MUL offline
  testMessage(PSTR("DWexecOffline (mul r16, r16 at WPC=0x013F): "), testnum++);
  DWwriteRegister(16, 5);
  DWwriteRegister(1, 0x55);
  DWwriteRegister(0, 0x55);
  DWsetWPc(pc);
  DWexecOffline(0x9F00); // specify opcode as MSB LSB (bigendian!)
  newpc = DWgetWPc(true);
  succ = false;
  DEBPR(F("reg 1:")); DEBLN(DWreadRegister(1, false));
  DEBPR(F("reg 0:")); DEBLN(DWreadRegister(0, false));
  DEBLN(mcu.avreplus);
  DEBLNF(newpc,HEX);
  if (pc+1 == newpc) // PC advanced by one, then +1 for break, but this is autmatically subtracted
    succ = ((DWreadRegister(0, false) == 25 && DWreadRegister(1, false) == 0 && mcu.avreplus) ||
	    (DWreadRegister(0, false) == 0x55 && DWreadRegister(0, false) == 0x55 && !mcu.avreplus));
  failed += testResult(succ);

  // execute a rjmp instruction offline 
  testMessage(PSTR("DWexecOffline (rjmp 0x002E at WPC=0x0001): "), testnum++);
  DWsetWPc(0x01);
  DWexecOffline(0xc02C);
  failed += testResult(DWgetWPc(true) == 0x2E); // = byte addr 0x005C

  
  if (num >= 1) {
    num = testnum;
    return failed;
  } else {
    testSummary(failed);
    return 0;
  }
}
#endif // some unit tests enabled
