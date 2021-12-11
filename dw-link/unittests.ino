
/********************************* General setup and reporting functions ******************************************/

// run all unit tests
void alltests(void)
{
  int failed = 0;
  int testnum = 1;

  if (targetOffline()) {
    gdbSendReply("E00");
    return;
  }
  
  failed += DWtests(testnum);
  failed += targetTests(testnum);
  failed += gdbTests(testnum);

  testSummary(failed);
  gdbSendReply("OK");
}

// give a summary of the test batch
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
 memcpy_P(membuf, testcode, sizeof(testcode));
 if (mcu.rambase == 0x60) { // small ATtinys with only little memory
   membuf[12] = 0x60; // use address 0x0060 instead of 0x0100
   membuf[13] = 0x00;
   membuf[16] = 0x60;
   membuf[17] = 0x00;
 }
 targetWriteFlash(0x1aa, membuf, sizeof(testcode));
}

/********************************* GDB interface function specific tests ******************************************/


int gdbTests(int &num) {
  int failed = 0;
  bool succ;
  int testnum;
  unsigned int oldsp;

  if (targetOffline()) {
    if (num == 0) gdbSendReply("E00");
    return 0;
  }
  
  if (num >= 1) testnum = num;
  else testnum = 1;

  /* We do not test the I/O functions. They have been taken over from avr_gdb and
   * seem to work quite robustly. We only test the breakpoint and execution functions.
   * The tests build on each other, so do not rearrange.
   */
  
  setupTestCode(); // insert the test code into memory
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

  // insert 4 BPs (one of it is a duplicate) 
  gdbDebugMessagePSTR(PSTR("Test gdbInsertBreakpoint: "), testnum++);
  gdbInsertBreakpoint(0xe4);
  gdbInsertBreakpoint(0xd5);
  gdbInsertBreakpoint(0xda);
  gdbInsertBreakpoint(0xd5);
  failed += testResult(bpcnt == 3 && bpused == 3 && hwbp == 0xda && bp[0].waddr == 0xe4
		       && bp[1].waddr == 0xd5 && bp[2].waddr == 0xda && bp[2].hw);

  // will insert two software breakpoints and the most recent one is a hardware breakpoint
  gdbDebugMessagePSTR(PSTR("Test gdbUpdateBreakpoints: "), testnum++);
  gdbUpdateBreakpoints(false);
  failed += testResult(bp[0].inflash && bp[0].used && bp[0].active  && bp[1].inflash
		       && bp[1].opcode == 0 && !bp[2].inflash && bp[0].opcode == 0xe911
		       && targetReadFlashWord(0xe4*2) == 0x9598
		       && targetReadFlashWord(0xd5*2) == 0x9598
		       && targetReadFlashWord(0xda*2) == 0x9320);

  // remove all breakpoints (the software breakpoints will still be in flash memory)
  gdbDebugMessagePSTR(PSTR("Test gdbRemoveBreakpoints: "), testnum++);
  gdbRemoveBreakpoint(0xd5);
  gdbRemoveBreakpoint(0xe4);
  gdbRemoveBreakpoint(0xda);
  gdbRemoveBreakpoint(0xd5);
  gdbRemoveBreakpoint(0xd5);
  failed += testResult(bpcnt == 0 && bpused == 3 && hwbp == 0xda && bp[0].inflash && bp[0].used
		       && !bp[0].active && bp[1].inflash && bp[1].used && !bp[1].active
		       && !bp[2].inflash && bp[2].used && !bp[2].active && bp[2].hw);

  // insert two new breakpoints: they will 'steal' the hardware breakpoint from the former breakpoint entry 2
  gdbDebugMessagePSTR(PSTR("Test gdbInsertBreakpoint (with old inactive BPs): "), testnum++);
  gdbInsertBreakpoint(0xe0); // CALL instruction
  gdbInsertBreakpoint(0xd6); // second NOP instruction, should become hwbp!
  failed += testResult(bpcnt == 2 && bpused == 5 && hwbp == 0xd6 && !bp[2].inflash && bp[2].used
		       && !bp[2].active && !bp[2].hw  && !bp[3].inflash && bp[3].used 
		       && bp[3].active && !bp[3].hw && bp[3].waddr == 0xe0
		       && !bp[4].inflash && bp[4].used 
		       && bp[4].active && bp[4].hw && bp[4].waddr == 0xd6);
  
  // "reinsert" two of the former breakpoints (in particular the former hardware breakpoint)
  // Then call gdbUpdateBreakpoints: the former hardware breakpoint now is a software breakpoint and therefore
  // a BREAK instruction is inserted at this point
  // All in all: 3 active BPs
  gdbDebugMessagePSTR(PSTR("Test gdbUpdateBreakpoint (after reinserting 2 of 3 inactive BPs): "), testnum++);
  gdbInsertBreakpoint(0xe4);
  gdbInsertBreakpoint(0xda);
  gdbUpdateBreakpoints(false);
  failed += testResult(bpcnt == 4 && bpused == 4 && hwbp == 0xd6 && bp[0].inflash
		       && bp[0].used && bp[0].active && bp[0].inflash && bp[0].waddr == 0xe4
		       && !bp[1].used && !bp[1].active &&  targetReadFlashWord(0xd5*2) == 0
		       && bp[2].inflash && bp[2].used && bp[2].active && !bp[2].hw
		          && bp[2].waddr == 0xda && targetReadFlashWord(0xda*2) == 0x9598
		       && bp[3].inflash && bp[3].used && bp[3].active && !bp[3].hw
		          && bp[3].waddr == 0xe0 && targetReadFlashWord(0xe0*2) == 0x9598
		       && !bp[4].inflash && bp[4].used && bp[4].active && bp[4].hw && bp[4].waddr == 0xd6 && hwbp == 0xd6);

  // execute starting at 0xd5 (word address) with a NOP and run to the hardware breakpoint (next instruction)
  gdbDebugMessagePSTR(PSTR("Test gdbContinue (with HWBP): "), testnum++);
  targetInitRegisters();
  ctx.sp = mcu.ramsz+mcu.rambase-1;
  ctx.wpc = 0xd5;
  gdbContinue();
  succ = expectBreakAndU();
  if (!succ) {
    targetBreak();
    expectUCalibrate();
  }
  targetSaveRegisters();
  failed += testResult(succ && ctx.wpc == 0xd6);

  // execute starting at 0xdc (an RCALL instruction) and stop at the software breakpoint at 0xe4
  gdbDebugMessagePSTR(PSTR("Test gdbContinue (with software breakpoint): "), testnum++);
  ctx.wpc = 0xdc;
  oldsp = ctx.sp;
  gdbContinue();
  succ = expectBreakAndU();
  if (!succ) {
    targetBreak();
    expectUCalibrate();
  }
  targetSaveRegisters();
  targetReadSram(ctx.sp+1,membuf,2); // return addr
  failed += testResult(succ && ctx.wpc == 0xe4 && ctx.sp == oldsp - 2
		       && (membuf[0]<<8)+membuf[1] == 0xDF);
  
  // remove the first 3 breakpoints from being active (they are still marked as used and the BREAK
  // instruction is still in flash)
  gdbDebugMessagePSTR(PSTR("Test gdbRemoveBreakpoint (3): "), testnum++);
  gdbRemoveBreakpoint(0xe4);
  gdbRemoveBreakpoint(0xda);
  gdbRemoveBreakpoint(0xd6);
  failed += testResult(bpcnt == 1 && bpused == 4 && hwbp == 0xd6 && bp[0].used && !bp[0].active 
		       && !bp[1].used && bp[2].used && !bp[2].active && bp[3].used && bp[3].active
		       && bp[4].used && !bp[4].active);

  // perform  a single step at location 0xda at which a BREAK instruction has been inserted,
  // replacing the first word of a STS __,r18 instruction; execution happens using
  // simulation.
  gdbDebugMessagePSTR(PSTR("Test gdbStep on 4-byte instruction (STS) hidden by BREAK: "), testnum++);
  //DEBLN(F("Test simulated write:"));
  unsigned int sramaddr = (mcu.rambase == 0x60 ? 0x60 : 0x100);
  ctx.regs[18] = 0x42;
  ctx.wpc = 0xda;
  membuf[0] = 0xFF;
  targetWriteSram(sramaddr, membuf, 1);
  targetReadSram(sramaddr, membuf, 1);
  membuf[0] = 0;
  //DEBLNF(membuf[0],HEX);
  gdbStep();
  targetReadSram(sramaddr, membuf, 1);
  //DEBLNF(membuf[0],HEX);
  //DEBLNF(ctx.wpc,HEX);
  failed += testResult(membuf[0] == 0x42 && ctx.wpc == 0xdc);

  // perform a single stop at location 0xe5 at which a BREAK instruction has been inserted,
  // replacing a "ldi r17, 0x91" instruction
  // execution is done via offline execution in debugWIRE
  gdbDebugMessagePSTR(PSTR("Test gdbStep on 2-byte instr LDI r17,0x91 hidden by BREAK: "), testnum++);
  ctx.regs[17] = 0xFF;
  ctx.wpc = 0xe4;
  gdbStep();
  //DEBLNF(ctx.regs[17],HEX);
  //DEBLNF(ctx.wpc,HEX);
  failed += testResult(ctx.wpc == 0xe5 && ctx.regs[17] == 0x91);

  // perform a single step at location 0xe5 on instruction RET
  gdbDebugMessagePSTR(PSTR("Test gdbStep on normal instruction RET (2-byte): "), testnum++);
  ctx.wpc = 0xe5;
  oldsp = ctx.sp;
  gdbStep();
  failed += testResult(ctx.sp == oldsp + 2 && ctx.wpc == 0xdf);

  // perform single step at location 0xdc on the instruction LDS r16, 0x100
  gdbDebugMessagePSTR(PSTR("Test gdbStep on normal instruction (4-byte): "), testnum++);
  ctx.regs[16] = 0;
  ctx.wpc = 0xdc;
  gdbStep();
  failed += testResult(ctx.regs[16] == 0x42 && ctx.wpc == 0xde);

  // check the "BREAK hiding" feature by loading part of the flash memory and
  // replacing BREAKs with the original instructions in the buffer to be sent to gdb
  gdbDebugMessagePSTR(PSTR("Test gdbHideBREAKs: "), testnum++);
  targetReadFlash(0x1ad, membuf, 0x1C); // from 0x1ad (uneven) to 0x1e4 (even)
  succ = (membuf[0x1ad-0x1ad] == 0x00 && membuf[0x1b4-0x1ad] == 0x98
	  && membuf[0x1b4-0x1ad+1] == 0x95 && membuf[0x1c8-0x1ad] == 0x98);
  //DEBLNF(membuf[0x1ad-0x1ad],HEX);
  //DEBLNF(membuf[0x1b4-0x1ad],HEX);
  //DEBLNF(membuf[0x1b4-0x1ad+1],HEX);
  //DEBLNF(membuf[0x1c8-0x1ad],HEX);
  //DEBLN();
  gdbHideBREAKs(0x1ad, membuf, 0x1C);
  //DEBLNF(membuf[0x1ad-0x1ad],HEX);
  //DEBLNF(membuf[0x1b4-0x1ad],HEX);
  //DEBLNF(membuf[0x1b4-0x1ad+1],HEX);
  //DEBLNF(membuf[0x1c8-0x1ad],HEX);
  //DEBLN();
  failed += testResult(succ && membuf[0x1ad-0x1ad] == 0x00 && membuf[0x1b4-0x1ad] == 0x20
		       && membuf[0x1b4-0x1ad+1] == 0x93 && membuf[0x1c8-0x1ad] == 0x11);

  // cleanup
  gdbDebugMessagePSTR(PSTR("Test delete BPs and BP update: "), testnum++);
  gdbRemoveBreakpoint(0xe0);
  gdbUpdateBreakpoints(false);
  failed += testResult(bpcnt == 0 && bpused == 0 && hwbp == 0xFFFF);

  // test the illegal opcode detector for single step
  gdbDebugMessagePSTR(PSTR("Test gdbStep on illegal instruction 0x0001: "), testnum++);
  ctx.wpc = 0xe6;
  byte sig = gdbStep();
  //DEBLNF(ctx.wpc,HEX);
  //DEBLN(sig);
  failed += testResult(sig == SIGILL && ctx.wpc == 0xe6);
  
  // test the illegal opcode detector for continue
  gdbDebugMessagePSTR(PSTR("Test gdbContinue on illegal instruction 0x0001: "), testnum++);
  targetInitRegisters();
  ctx.sp = mcu.ramsz+mcu.rambase-1;
  ctx.wpc = 0xe6;
  sig = gdbContinue();
  succ = ctx.saved && sig == SIGILL; 
  if (sig == 0) {
    targetBreak();
    expectUCalibrate();
  }
  targetSaveRegisters();
  failed += testResult(succ && ctx.wpc == 0xe6);
  
  setSysState(CONN_STATE);
  if (num >= 1) {
    num = testnum;
    return failed;
  } else {
    testSummary(failed);
    gdbSendReply("OK");
    return 0;
  }
}

/********************************* Target specific tests ******************************************/

int targetTests(int &num) {
  int failed = 0;
  bool succ;
  int testnum;
  byte i;
  long lastflashcnt;

  if (targetOffline()) {
    if (num == 0) gdbSendReply("E00");
    return 0;
  }

  if (num >= 1) testnum = num;
  else testnum = 1;

  // write a (target-size) flash page (only check that no fatal error)
  gdbDebugMessagePSTR(PSTR("Test targetWriteFlashPage: "), testnum++);
  const int flashaddr = 0x80;
  fatalerror = NO_FATAL; setSysState(CONN_STATE);
  DWeraseFlashPage(flashaddr);
  DWreenableRWW();
  validpg = false;
  for (i=0; i < mcu.targetpgsz; i++) page[i] = 0;
  for (i=0; i < mcu.targetpgsz; i++) membuf[i] = i;
  targetWriteFlashPage(flashaddr, membuf);
  lastflashcnt = flashcnt;
  failed += testResult(fatalerror == NO_FATAL);

  // write same page again (since cache is valid, should not happen)
  gdbDebugMessagePSTR(PSTR("Test targetWriteFlashPage (no 2nd write when vaildpg): "), testnum++);
  fatalerror = NO_FATAL; setSysState(CONN_STATE);
  targetWriteFlashPage(flashaddr, membuf);
  failed += testResult(fatalerror == NO_FATAL && lastflashcnt == flashcnt);
  
  // write same page again (cache valid flag cleared), but since contents is tha same, do not write
  gdbDebugMessagePSTR(PSTR("Test targetWriteFlashPage (no 2nd write when same contents): "), testnum++);
  fatalerror = NO_FATAL; setSysState(CONN_STATE);
  validpg = false;
  targetWriteFlashPage(flashaddr, membuf);
  failed += testResult(fatalerror == NO_FATAL && lastflashcnt == flashcnt);

  // try to write a cache page at an address that is not at a page boundary -> fatal error
  gdbDebugMessagePSTR(PSTR("Test targetWriteFlashPage (addr error): "), testnum++);
  fatalerror = NO_FATAL; setSysState(CONN_STATE);
  targetWriteFlashPage(flashaddr+2, membuf);
  failed += testResult(fatalerror != NO_FATAL && lastflashcnt == flashcnt);

  // read page (should be done from cache)
  gdbDebugMessagePSTR(PSTR("Test targetReadFlashPage (from cache): "), testnum++);
  fatalerror = NO_FATAL; setSysState(CONN_STATE);
  page[0] = 0x11; // mark first cell in order to see whether things get reloaded
  targetReadFlashPage(flashaddr);
  failed += testResult(fatalerror == NO_FATAL && page[0] == 0x11);

  // read page (force cache to be invalid and read from flash)
  gdbDebugMessagePSTR(PSTR("Test targetReadFlashPage (from flash memory): "), testnum++);
  fatalerror = NO_FATAL; setSysState(CONN_STATE);
  for (i=0; i < mcu.targetpgsz; i++) page[i] = 0;
  validpg = false;
  succ = true;
  targetReadFlashPage(flashaddr);
  for (i=0; i < mcu.targetpgsz; i++) {
    if (page[i] != i) succ = false;
  }
  failed += testResult(fatalerror == NO_FATAL && succ);

  // write and read two bytes to/from flash
  gdbDebugMessagePSTR(PSTR("Test targetReadFlash/targetWriteFlash (read bytes from flash - not chache!): "), testnum++);
  fatalerror = NO_FATAL; setSysState(CONN_STATE);
  membuf[0] = 22; 
  membuf[1] = 33;
  targetWriteFlash(flashaddr+2, membuf, 2);
  membuf[0] = 0;
  membuf[1] = 0;
  for (i=0; i < mcu.targetpgsz; i++) page[i] = 0;
  validpg = false;
  targetReadFlash(flashaddr+2, membuf, 2);
  failed += testResult(fatalerror == NO_FATAL && membuf[0] == 22 && membuf[1] == 33);

  // restore registers (send to target) and save them (read from target)
  gdbDebugMessagePSTR(PSTR("Test targetRestoreRegisters/targetSaveRegisters: "), testnum++);
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
  gdbDebugMessagePSTR(PSTR("Test targetInitRegisters: "), testnum++);
  targetInitRegisters();
  failed += testResult(ctx.wpc == 0 && ctx.saved == true); // this is the only requirement!

  gdbDebugMessagePSTR(PSTR("Test targetWriteEeprom/targetReadEeprom: "), testnum++);
  DEBLN(F("targetEEPROM"));
  succ = true;
  const int eeaddr = 0x15;
  membuf[0] = 0x30;
  membuf[1] = 0x45;
  membuf[2] = 0x67;
  targetWriteEeprom(eeaddr, membuf, 3);
  targetReadEeprom(eeaddr, &membuf[3], 3);
  for (i = 0; i <3; i++)
    if (membuf[i] != membuf[i+3]) succ = false;
  DEBLNF(membuf[0],HEX);
  DEBLNF(membuf[1],HEX);
  DEBLNF(membuf[2],HEX);
  DEBLNF(membuf[3],HEX);
  DEBLNF(membuf[4],HEX);
  DEBLNF(membuf[5],HEX);
  for (i = 0; i < 6; i++) membuf[i] = 0xFF;
  targetWriteEeprom(eeaddr, membuf, 3);
  targetReadEeprom(eeaddr, &membuf[3], 3);
  for (i = 0; i <3; i++)
    if (membuf[3+i] != 0xFF) succ = false;
  DEBLNF(membuf[0],HEX);
  DEBLNF(membuf[1],HEX);
  DEBLNF(membuf[2],HEX);
  DEBLNF(membuf[3],HEX);
  DEBLNF(membuf[4],HEX);
  DEBLNF(membuf[5],HEX);
  failed += testResult(succ);

  gdbDebugMessagePSTR(PSTR("Test targetWriteSram/targetReadSram: "), testnum++);
  succ = true;
  const int ramaddr = mcu.rambase;
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


  gdbDebugMessagePSTR(PSTR("Test targetStep (ldi r18,0x49): "), testnum++);
  succ = true;
  targetInitRegisters();
  ctx.wpc = 0xd9; // ldi instruction
  ctx.sp = mcu.ramsz+mcu.rambase-1; // SP to upper limit of RAM
  targetRestoreRegisters(); 
  targetStep();
  if (!expectBreakAndU()) succ = false;
  targetSaveRegisters();
  failed += testResult(succ && ctx.wpc == 0xda && ctx.regs[18] == 0x49);

  gdbDebugMessagePSTR(PSTR("Test targetStep (rcall): "), testnum++);
  succ = true;
  ctx.wpc = 0xde; // rcall instruction
  targetRestoreRegisters();
  targetStep(); // one step leads to Break+0x55
  if (!expectBreakAndU()) succ = false;
  targetSaveRegisters();
  failed += testResult(succ && ctx.wpc == 0xe4);

  gdbDebugMessagePSTR(PSTR("Test targetContinue/targetBreak: "), testnum++);
  succ = true;
  hwbp = 0xFFFF;
  targetRestoreRegisters();
  targetContinue();
  targetBreak(); // DW responds with 0x55 on break
  if (!expectUCalibrate()) succ = false;
  targetSaveRegisters();
  failed += testResult(succ && ctx.wpc == 0xd8 && ctx.regs[17] == 0x91);

  gdbDebugMessagePSTR(PSTR("Test targetReset: "), testnum++);
  DWwriteIOreg(0x3F, 0xFF); // SREG
  DEBPR(F("SREG before: ")); DEBLNF(DWreadIOreg(0x3F),HEX);
  targetRestoreRegisters();
  targetReset(); // response is taken care of by the function itself
  targetSaveRegisters();
  DEBPR(F("SREG after: ")); DEBLNF(DWreadIOreg(0x3F),HEX);
  failed += testResult(ctx.wpc == 0 && DWreadIOreg(0x3F) == 0);

  gdbDebugMessagePSTR(PSTR("Test targetIllegalOpcode (mul r16, r16): "), testnum++);
  failed += testResult(targetIllegalOpcode(0x9F00) == !mcu.avreplus);

  gdbDebugMessagePSTR(PSTR("Test targetIllegalOpcode (jmp ...): "), testnum++);
  failed += testResult(targetIllegalOpcode(0x940C) == (mcu.flashsz <= 8192));
  
  
  if (num >= 1) {
    num = testnum;
    return failed;
  } else {
    testSummary(failed);
    gdbSendReply("OK");
    return 0;
  }
}

/********************************* debugWIRE specific tests ******************************************/

int DWtests(int &num)
{
  int failed = 0;
  bool succ;
  int testnum;
  byte temp;

  if (targetOffline()) {
    if (num == 0) gdbSendReply("E00");
    return 0;
  }

  if (num >= 1) testnum = num;
  else testnum = 1;

  // write and read 3 registers
  gdbDebugMessagePSTR(PSTR("Test DWwriteRegister/DWreadRegister: "), testnum++);
  DWwriteRegister(0, 0x55);
  DWwriteRegister(15, 0x9F);
  DWwriteRegister(31, 0xFF);
  failed += testResult(DWreadRegister(0) == 0x55 && DWreadRegister(15) == 0x9F && DWreadRegister(31) == 0xFF);

  
  // write registers in one go and read them in one go (much faster than writing/reading individually) 
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

  // write to and read from an IO reg (0x3F = SREG)
  gdbDebugMessagePSTR(PSTR("Test DWwriteIOreg/DWreadIOreg: "), testnum++);
  DWwriteIOreg(0x3F, 0x55);
  failed += testResult(DWreadIOreg(0x3F) == 0x55);

  // write into (lower) sram and read it back from corresponding IO reag 
  gdbDebugMessagePSTR(PSTR("Test DWwriteSramByte/DWreadIOreg: "), testnum++);
  DWwriteSramByte(0x3F+0x20, 0x1F);
  temp = DWreadIOreg(0x3F);
  failed += testResult(temp == 0x1F);

  // write into IO reg and read it from the ocrresponding sram addr
  gdbDebugMessagePSTR(PSTR("Test DWwriteIOreg/DWreadSramByte: "), testnum++);
  DWwriteIOreg(0x3F, 0xF2);
  failed += testResult(DWreadSramByte(0x3F+0x20) == 0xF2);

  // write a number of bytes to sram and read them again byte by byte
  gdbDebugMessagePSTR(PSTR("Test DWwriteSramByte/DWreadSramByte: "), testnum++);
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
  gdbDebugMessagePSTR(PSTR("Test DWreadSram (bulk reading): "), testnum++);
  for (byte i=0; i < 32; i++) membuf[i] = 0;
  DWreadSramBytes(mcu.rambase, membuf, 32);
  succ = true;
  for (byte i=0; i < 32; i++) {
    if (membuf[i] != i+1) {
      succ = false;
      break;
    }
  }
  failed += testResult(succ);

  // write to EEPROM (addr 0x15) and read from it
  gdbDebugMessagePSTR(PSTR("Test DWwriteEepromByte/DWreadEepromByte: "), testnum++);
  const int eeaddr = 0x15;
  succ = true;
  DWwriteEepromByte(eeaddr, 0x38);
  if (DWreadEepromByte(eeaddr) != 0x38) succ = false;
  DWwriteEepromByte(eeaddr, 0xFF);
  if (DWreadEepromByte(eeaddr) != 0xFF) succ = false;
  failed += testResult(succ);
  
  // erase flash page (check only for errors)
  gdbDebugMessagePSTR(PSTR("Test DWeraseFlashPage: "), testnum++);
  const int flashaddr = 0x100;
  failed += testResult(DWeraseFlashPage(flashaddr));

  // read the freshly cleared flash page
  gdbDebugMessagePSTR(PSTR("Test DWreadFlash (empty page): "), testnum++);
  for (byte i=0; i < mcu.pagesz; i++) membuf[i] = 0;
  succ = true;
  DWreenableRWW();
  DWreadFlash(flashaddr, membuf, mcu.pagesz);
  for (byte i=0; i < mcu.pagesz; i++) {
    if (membuf[i] != 0xFF) succ = false;
  }
  failed += testResult(succ);
    
  // program one flash page (only check for error code returns)
  gdbDebugMessagePSTR(PSTR("Test DWloadFlashPage/DWprogramFlashPage: "), testnum++);
  for (byte i=0; i < mcu.pagesz; i++) membuf[i] = 255-i;
  DWloadFlashPageBuffer(flashaddr, membuf);
  failed += testResult(DWprogramFlashPage(flashaddr));

  // now try to read the freshly flashed page
  gdbDebugMessagePSTR(PSTR("Test DWreenableRWW/DWreadFlash: "), testnum++);
  for (byte i=0; i < mcu.pagesz; i++) membuf[i] = 0;
  succ = true;
  DWreenableRWW();
  DWreadFlash(flashaddr, membuf, mcu.pagesz);
  DEBLN(F("Read Flash:"));
  for (byte i=0; i < mcu.pagesz; i++) {
    DEBLNF(membuf[i],HEX);
    if (membuf[i] != 255-i) {
      succ = false;
    }
  }
  failed += testResult(succ);

  // if a device with boot sector, try everything immediately after each other in the boot area 
  if (mcu.bootaddr != 0) {
    for (byte i=0; i < mcu.pagesz; i++) membuf[i] = 255-i;
    gdbDebugMessagePSTR(PSTR("Test DWloadFlashPageBuffer/DWprogramFlashPage/DWreenableRWW/DWreadFlash (boot section): "), testnum++);
    succ = DWeraseFlashPage(mcu.bootaddr);
    if (succ) {
      //DEBLN(F("erase successful"));
      DWreenableRWW();
      succ = DWloadFlashPageBuffer(mcu.bootaddr, membuf);
      //for (byte i=0; i < mcu.pagesz; i++) DEBLN(membuf[i]);
    }
    if (succ) {
      //DEBLN(F("load temp successful"));
      succ = DWprogramFlashPage(mcu.bootaddr);
    }
    if (succ) {
      DWreenableRWW();
      DEBLN(F("program successful"));
      for (byte i=0; i < mcu.pagesz; i++) membuf[i] = 0;
      DWreadFlash(mcu.bootaddr, membuf, mcu.pagesz);
      for (byte i=0; i < mcu.pagesz; i++) {
	DEBLN(membuf[i]);
	if (membuf[i] != 255-i) {
	  DEBPR(F("Now wrong!"));
	  succ = false;
	}
      }
    }
    failed += testResult(succ);
  }

  // get chip id
  gdbDebugMessagePSTR(PSTR("Test DWgetChipId: "), testnum++);
  failed += testResult(mcu.sig != 0 && DWgetChipId() == mcu.sig);

  // Set/get PC (word address)
  gdbDebugMessagePSTR(PSTR("Test DWsetWPc/DWgetWPc: "), testnum++);
  const int pc = 0x3F; 
  DWsetWPc(pc);
  failed += testResult(DWgetWPc() == pc - 1);

  // Set/get hardware breakpoint
  gdbDebugMessagePSTR(PSTR("Test DWsetWBp/DWgetWBp: "), testnum++);
  DWsetWBp(pc);
  failed += testResult(DWgetWBp() == pc);

  // execute one instruction offline
  gdbDebugMessagePSTR(PSTR("Test DWexecOffline (eor r1,r1 at WPC=0x003F): "), testnum++);
  DWwriteIOreg(0x3F, 0); // write SREG
  DWwriteRegister(1, 0x55);
  DWsetWPc(pc);
  DWexecOffline(0x2411); // specify opcode as MSB LSB (bigendian!)
  succ = false;
  if (pc + 1 == DWgetWPc()) // PC advanced by one, then +1 for break, but this is autmatically subtracted
    if (DWreadRegister(1) == 0)  // reg 1 should be zero now
      if (DWreadIOreg(0x3F) == 0x02) // in SREG, only zero bit should be set
	succ = true;
  failed += testResult(succ);

  // execute MUL offline
  gdbDebugMessagePSTR(PSTR("Test DWexecOffline (mul r16, r16 at WPC=0x003F): "), testnum++);
  DWwriteRegister(16, 5);
  DWwriteRegister(1, 0x55);
  DWwriteRegister(0, 0x55);
  DWsetWPc(pc);
  DWexecOffline(0x9F00); // specify opcode as MSB LSB (bigendian!)
  int newpc = DWgetWPc();
  succ = false;
  DEBPR(F("reg 1:")); DEBLN(DWreadRegister(1));
  DEBPR(F("reg 0:")); DEBLN(DWreadRegister(0));
  DEBLN(mcu.avreplus);
  DEBLNF(newpc,HEX);
  if (0x0040 == newpc) // PC advanced by one, then +1 for break, but this is autmatically subtracted
    succ = ((DWreadRegister(0) == 25 && DWreadRegister(1) == 0 && mcu.avreplus) ||
	    (DWreadRegister(0) == 0x55 && DWreadRegister(0) == 0x55 && !mcu.avreplus));
  failed += testResult(succ);

  // execute a rjmp instruction offline 
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
