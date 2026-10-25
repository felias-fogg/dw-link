// dw-link -- layer 1: debugWIRE and ISP primitives
#ifndef DWLINK_DWPRIM_H
#define DWLINK_DWPRIM_H

enum FuseByte { LowFuse=0, HighFuse=1, ExtendedFuse=2 };

extern dwSerial dw;    // communcation interface to target
extern long timeoutcnt; // counter for DW read timeouts
extern long flashcnt;   // number of flash writes

boolean doBreak(boolean doprinterror = true);
boolean expectUCalibrate(void);
boolean expectBreakAndU(void);
unsigned int getResponse(byte *, unsigned int);
unsigned int getWordResponse(byte);
void DWsetSpeed(byte);
byte outHigh(byte, byte);
byte outLow(byte, byte);
byte inHigh(byte, byte);
byte inLow(byte, byte);
void DWwriteRegisters(byte *);
void DWwriteRegister(byte, byte);
void DWreadRegisters(byte *);
byte DWreadRegister(byte, bool);
void DWwriteSramByte(unsigned int, byte);
void DWwriteIOreg(byte, byte);
byte DWreadSramByte(unsigned int);
byte DWreadIOreg(byte);
void DWreadSramBytes(unsigned int, byte *, byte);
byte DWreadEepromByte(unsigned int);
void DWwriteEepromByte(unsigned int, byte);
void DWreadFlash(unsigned int, byte *, unsigned int);
void DWeraseFlashPage(unsigned int);
void DWprogramFlashPage(unsigned int);
void DWloadFlashPageBuffer(unsigned int, byte *);
void DWreenableRWW(void);
byte DWreadSPMCSR(void);
unsigned int DWgetWPc(boolean);
unsigned int DWgetChipId(void);
void DWsetWPc(unsigned int);
void DWsetWBp(unsigned int);
void DWexecOffline(unsigned int);
byte DWflushInput(void);

void enableSpiPins(void);
void disableSpiPins(void);
byte ispTransfer(byte, boolean);
void ispDelay(boolean);
byte ispSend(byte, byte, byte, byte, boolean);
boolean enterProgramMode(void);
void leaveProgramMode(void);
unsigned int ispGetChipId(void);
byte ispReadFuse(FuseByte);
boolean ispProgramFuse(FuseByte, byte, byte);
boolean ispEraseFlash(void);
boolean ispLocked(void);

#endif
