// dw-link -- layer 2: target operations, built on the primitives of layer 1
#ifndef DWLINK_TARGET_H
#define DWLINK_TARGET_H

extern unsigned int hwbp;          // the one hardware breakpoint (word address)
extern byte newpage[MAXPAGESIZE];  // one page of flash to program
extern byte page[MAXPAGESIZE];     // cached page contents
extern unsigned int lastpg;        // address of the cached page
extern boolean validpg;            // if cached page contents is valid
extern boolean flashidle;          // flash programming is not active
extern unsigned int flashpageaddr; // current page to be programmed next

boolean stuckAtOneOrCap(void);
void setupDW(void);
boolean autoPowerCycle(void);
boolean targetDWConnect(boolean);
void targetReadFlashPage(unsigned int);
unsigned int targetReadFlashWord(unsigned int);
void targetReadFlash(unsigned int, byte *, unsigned int);
void targetReadSram(unsigned int, byte *, unsigned int);
boolean writeOnlyReg(unsigned int, const byte *);
void targetReadEeprom(unsigned int, byte *, unsigned int);
void targetWriteFlashPage(unsigned int);
void targetWriteFlash(unsigned int, byte *, unsigned int);
void targetFlushFlashProg(void);
void targetWriteSram(unsigned int, byte *, unsigned int);
boolean readOnlyReg(unsigned int, const byte *);
void targetWriteEeprom(unsigned int, byte *, unsigned int);
void targetInitRegisters(void);
void targetSaveRegisters(void);
void targetRestoreRegisters(void);
void targetBreak(void);
void targetContinue(unsigned int);
void targetStep(void);
boolean targetReset(void);

#endif
