// dw-link -- the self-test: live tests of layers 1 and 2 on the connected target, started by the
// host with the vendor command SELFTEST (mkii.cpp)
#include "config.h"
#include "dwprim.h"
#include "target.h"
#include "selftest.h"

static byte testbuf[40];          // test data, and the test code on its way to the target
static byte testcount;            // tests run so far
static unsigned long testfailed;  // bit i set: test i+1 failed

static void DWtests(void);
static void targetTests(void);
static void setupTestCode(void);

// start of the SRAM, from the device data the host sent
static unsigned int rambase(void)
{
  return (mcu.extiotop ? mcu.extiotop + 1 : 0x60);
}

static void testResult(boolean succ)
{
  if (!succ && testcount < 32) testfailed |= 1UL << testcount;
  testcount++;
}

// run all tests on the target, which must be stopped in debugWIRE mode with its device data known.
// They overwrite flash at 0x80, 0x100 and 0x1aa (and the first page of the boot section, if there is
// one), and leave the target stopped after a reset. Returns the number of tests; failed gets the
// failed ones as a bit mask (bit i: test i+1).
byte selfTest(unsigned long &failed)
{
  testcount = 0;
  testfailed = 0;
  setupDW();
  DWtests();
  targetTests();
  validpg = false;
  fatalerror = NO_FATAL;           // the tests provoke errors on purpose
  setSysState(DWCONN_STATE);
  failed = testfailed;
  return testcount;
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
 


static void setupTestCode(void)
{
  // execution related functions: setup test code in target
 memcpy_P(testbuf, testcode, sizeof(testcode));
 if (rambase() == 0x60) { // small ATtinys with only little memory
   testbuf[12] = 0x60; // use address 0x0060 instead of 0x0100
   testbuf[13] = 0x00;
   testbuf[16] = 0x60;
   testbuf[17] = 0x00;
 }
 targetWriteFlash(0x1aa, testbuf, sizeof(testcode));
 targetFlushFlashProg();
}


/********************************* Target specific tests ******************************************/

static void targetTests(void)
{
  boolean succ;
  unsigned int i;
  long lastflashcnt;


  // write a (target-size) flash page (only check that no fatal error)
  // test: targetWriteFlashPage
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
  testResult(fatalerror == NO_FATAL);

  // write same page again (since cache is valid, should not happen)
  // test: targetWriteFlashPage (check vaildpg)
  fatalerror = NO_FATAL; setSysState(DWCONN_STATE);
  targetWriteFlashPage(flashaddr);
  testResult(fatalerror == NO_FATAL && lastflashcnt == flashcnt);
  
  // write same page again (cache valid flag cleared), but since contents is tha same, do not write
  // test: targetWriteFlashPage (check contents)
  fatalerror = NO_FATAL; setSysState(DWCONN_STATE);
  validpg = false;
  targetWriteFlashPage(flashaddr);
  testResult(fatalerror == NO_FATAL && lastflashcnt == flashcnt);

  // try to write a cache page at an address that is not at a page boundary -> fatal error
  // test: targetWriteFlashPage (addr error)
  fatalerror = NO_FATAL; setSysState(DWCONN_STATE);
  targetWriteFlashPage(flashaddr+2);
  testResult(fatalerror != NO_FATAL && lastflashcnt == flashcnt);

  // read page (should be done from cache)
  // test: targetReadFlashPage (from cache)
  fatalerror = NO_FATAL; setSysState(DWCONN_STATE);
  page[0] = 0x11; // mark first cell in order to see whether things get reloaded
  targetReadFlashPage(flashaddr);
  testResult(fatalerror == NO_FATAL && page[0] == 0x11);

  // read page (force cache to be invalid and read from flash)
  // test: targetReadFlashPage
  fatalerror = NO_FATAL; setSysState(DWCONN_STATE);
  for (i=0; i < mcu.targetpgsz; i++) page[i] = 0;
  validpg = false;
  succ = true;
  targetReadFlashPage(flashaddr);
  for (i=0; i < mcu.targetpgsz; i++) {
    if (page[i] != i) succ = false;
  }
  testResult(fatalerror == NO_FATAL && succ);
  fatalerror = NO_FATAL;	

  // restore registers (send to target) and save them (read from target)
  // test: targetRestoreRegisters/targetSaveRegisters
  unsigned int spinit = rambase() + 0x3F;
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
  testResult(succ);

  // test ergister init procedure
  // test: targetInitRegisters
  targetInitRegisters();
  testResult(ctx.wpc == 0 && ctx.saved == true); // this is the only requirement!

  // test: targetWriteEeprom/targetReadEeprom
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
  testResult(succ);

  // test: targetWriteSram/targetReadSram
  succ = true;
  const int ramaddr = rambase();
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
  testResult(succ);

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


  // test: targetStep (ldi r18,0x49)
  succ = true;
  targetInitRegisters();
  ctx.wpc = 0xd9; // ldi instruction
  ctx.sp = rambase() + 0x3F; // SP into the first 64 bytes of SRAM
  targetRestoreRegisters(); 
  targetStep();
  if (!expectBreakAndU())
    succ = false;
  targetSaveRegisters();
  testResult(succ && ctx.wpc == 0xda && ctx.regs[18] == 0x49);

  // test: targetStep (rcall)
  succ = true;
  ctx.wpc = 0xde; // rcall instruction
  targetRestoreRegisters();
  targetStep(); // one step leads to Break+0x55
  if (!expectBreakAndU()) succ = false;  
  targetSaveRegisters();
  testResult(succ && ctx.wpc == 0xe4);
  // test: Test targetContinue/targetBreak
  succ = true;
  hwbp = 0xFFFF;
  targetRestoreRegisters();
  targetContinue(0xFFFF);
  targetBreak(); // DW responds with 0x55 on break
  if (!expectUCalibrate()) succ = false;
  targetSaveRegisters();
  testResult(succ && ctx.wpc == 0xd8 && ctx.regs[17] == 0x91);

  // test: targetReset
  ctx.sreg= 0xFF; // SREG
  DEBPR(F("SREG before: ")); DEBLNF(ctx.sreg,HEX);
  DEBPR(F("WPC before:  ")); DEBLNF(ctx.wpc,HEX);
  targetRestoreRegisters();
  targetReset(); // response is taken care of by the function itself
  targetSaveRegisters();
  DEBPR(F("SREG after: ")); DEBLNF(ctx.sreg,HEX);
  DEBPR(F("WPC after:  ")); DEBLNF(ctx.wpc,HEX);
  testResult((ctx.wpc & 0x7F) == 0 && ctx.sreg == 0); // PC can be set to boot area!

}

/********************************* debugWIRE specific tests ******************************************/

static void DWtests(void)
{
  boolean succ;
  byte temp;
  unsigned int i;


  // write and read 3 registers
  // test: DWwriteRegister/DWreadRegister
  DWwriteRegister(0, 0x55);
  DWwriteRegister(15, 0x9F);
  DWwriteRegister(31, 0xFF);
  testResult(DWreadRegister(0, false) == 0x55 && DWreadRegister(15, false) == 0x9F && DWreadRegister(31, false) == 0xFF);

  // write registers in one go and read them in one go (much faster than writing/reading individually) 
  // test: DWwriteRegisters/DWreadRegisters
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
  testResult(succ);

  // write to and read from an IO reg (0x3F = SREG)
  // test: DWwriteIOreg/DWreadIOreg
  DWwriteIOreg(0x3F, 0x55);
  testResult(DWreadIOreg(0x3F) == 0x55);

  // write into (lower) sram and read it back from corresponding IO reag 
  // test: DWwriteSramByte/DWreadIOreg
  DWwriteSramByte(0x3F+0x20, 0x1F);
  temp = DWreadIOreg(0x3F);
  testResult(temp == 0x1F);

  // write into IO reg and read it from the ocrresponding sram addr
  // test: DWwriteIOreg/DWreadSramByte
  DWwriteIOreg(0x3F, 0xF2);
  testResult(DWreadSramByte(0x3F+0x20) == 0xF2);

  // write a number of bytes to sram and read them again byte by byte
  // test: DWwriteSramByte/DWreadSramByte
  for (byte i=0; i < 32; i++) DWwriteSramByte(rambase()+i, i+1);
  succ = true;
  for (byte i=0; i < 32; i++) {
    if (DWreadSramByte(rambase()+i) != i+1) {
      succ = false;
      break;
    }
  }
  testResult(succ);

  // sram bulk reading
  // test: DWreadSram (bulk)
  for (byte i=0; i < 32; i++) testbuf[i] = 0;
  DWreadSramBytes(rambase(), testbuf, 32);
  succ = true;
  for (byte i=0; i < 32; i++) {
    if (testbuf[i] != i+1) {
      succ = false;
      break;
    }
  }
  testResult(succ);

  // write to EEPROM (addr 0x15) and read from it
  // test: DWwriteEepromByte/DWreadEepromByte
  const int eeaddr = 0x15;
  succ = true;
  DWwriteEepromByte(eeaddr, 0x38);
  if (DWreadEepromByte(eeaddr) != 0x38) succ = false;
  DWwriteEepromByte(eeaddr, 0xFF);
  if (DWreadEepromByte(eeaddr) != 0xFF) succ = false;
  testResult(succ);
  
  // erase flash page (check only for errors)
  // test: DWeraseFlashPage
  const int flashaddr = 0x100;
  DWeraseFlashPage(flashaddr);
  testResult(fatalerror == NO_FATAL);
  fatalerror = NO_FATAL;

  // read the freshly cleared flash page
  // test: DWreadFlash (empty page)
  for (i=0; i < mcu.pagesz; i++) newpage[i] = 0;
  succ = true;
  DWreenableRWW();
  DWreadFlash(flashaddr, newpage, mcu.pagesz);
  for (i=0; i < mcu.pagesz; i++) {
    if (newpage[i] != 0xFF) succ = false;
  }
  testResult(succ);
    
  // program one flash page (only check for error code returns)
  // test: DWloadFlashPage/DWprogramFlashPage
  for (i=0; i < mcu.pagesz; i++) newpage[i] = 255-i;
  DWloadFlashPageBuffer(flashaddr, newpage);
  DWprogramFlashPage(flashaddr);
  testResult(fatalerror == NO_FATAL);
  fatalerror = NO_FATAL;

  // now try to read the freshly flashed page
  // test: DWreenableRWW/DWreadFlash
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
  testResult(succ);

  // if a device with boot sector, try everything immediately after each other in the boot area 
  if (mcu.bootaddr != 0) {
    unsigned int bootbyte = mcu.bootaddr << 1; // bootaddr is a word address, these take bytes
    for (i=0; i < mcu.pagesz; i++) newpage[i] = 255-i;
    // test: DWFlash prog. in boot section
    DWeraseFlashPage(bootbyte);
    succ = (fatalerror == NO_FATAL);
    if (succ) {
      //DEBLN(F("erase successful"));
      DWreenableRWW();
      DWloadFlashPageBuffer(bootbyte, newpage);
      succ = (fatalerror == NO_FATAL);
      //for (i=0; i < mcu.pagesz; i++) DEBLN(newpage[i]);
    }
    if (succ) {
      //DEBLN(F("load temp successful"));
      DWprogramFlashPage(bootbyte);
      succ = (fatalerror == NO_FATAL);
    }
    if (succ) {
      DWreenableRWW();
      DEBLN(F("program successful"));
      for (i=0; i < mcu.pagesz; i++) newpage[i] = 0;
      DWreadFlash(bootbyte, newpage, mcu.pagesz);
      for (i=0; i < mcu.pagesz; i++) {
	DEBLN(newpage[i]);
	if (newpage[i] != 255-i) {
	  DEBPR(F("Now wrong!"));
	  succ = false;
	}
      }
    }
    testResult(succ);
  }
  fatalerror = NO_FATAL;


  // get chip id
  // test: DWgetChipId
  testResult(mcu.sig != 0 && (DWgetChipId() == mcu.sig ||
					(mcu.sig == 0x9514 && DWgetChipId() == 0x950F) || // imposter 328P!
					(mcu.sig == 0x9406 && DWgetChipId() == 0x940B) || // imposter 168PA!
					(mcu.sig == 0x930A && DWgetChipId() == 0x930F) || // imposter 88PA!
					(mcu.sig == 0x9205 && DWgetChipId() == 0x920A))); // imposter 48PA!
  
  // Set/get PC (word address)
  // test: DWsetWPc/DWgetWPc
  unsigned int pc = 0x3F; 
  DWsetWPc(pc);
  unsigned int newpc = DWgetWPc(true);
  DEBLNF(newpc, HEX);
  testResult(newpc == pc - 1);

  /*
  // Set/get hardware breakpoint
  // test: Test DWsetWBp/DWgetWBp
  DWsetWBp(pc);
  testResult(DWgetWBp() == pc);
  */
  
  // execute one instruction offline
  // test: DWexecOffline (eor r1,r1 at WPC=0x013F)
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
  testResult(succ);

  // execute MUL offline
  // test: DWexecOffline (mul r16, r16 at WPC=0x013F)
  DWwriteRegister(16, 5);
  DWwriteRegister(1, 0x55);
  DWwriteRegister(0, 0x55);
  DWsetWPc(pc);
  DWexecOffline(0x9F00); // specify opcode as MSB LSB (bigendian!)
  newpc = DWgetWPc(true);
  succ = false;
  DEBPR(F("reg 1:")); DEBLN(DWreadRegister(1, false));
  DEBPR(F("reg 0:")); DEBLN(DWreadRegister(0, false));
  DEBLNF(newpc,HEX);
  if (pc+1 == newpc) // PC advanced by one, then +1 for break, but this is autmatically subtracted
    succ = ((DWreadRegister(0, false) == 25 && DWreadRegister(1, false) == 0) || // MUL exists (AVRe+)
	    (DWreadRegister(0, false) == 0x55 && DWreadRegister(1, false) == 0x55));   // or is ignored
  testResult(succ);

  // execute a rjmp instruction offline 
  // test: DWexecOffline (rjmp 0x002E at WPC=0x0001)
  DWsetWPc(0x01);
  DWexecOffline(0xc02C);
  testResult(DWgetWPc(true) == 0x2E); // = byte addr 0x005C

  
}
