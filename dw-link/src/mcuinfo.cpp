// dw-link -- the table of supported MCUs, only for the self-test, which runs without a host;
// in normal operation, the mkII device descriptor provides these values
#include "config.h"
#include "mcuinfo.h"
#if defined(UNITDW) ||  defined(UNITTG) ||  defined(UNITALL)

struct mcu_info_type {
  unsigned int sig;            // two byte signature
  byte         ramsz_div_64;   // SRAM size
  boolean      rambase_low;    // base address of SRAM; low: 0x60, high: 0x100
  byte         eepromsz_div_64;// size of EEPROM
  byte         flashsz_div_1k; // size of flash memory
  byte         dwdr;           // address of DWDR register
  byte         pagesz_div_2;   // page size 
  boolean      erase4pg;       // 1 when the MCU has a 4-page erase operation
  unsigned int bootaddr;       // highest address of possible boot section  (0 if no boot support)
  byte         eecr;           // address of EECR register
  byte         eearh;          // address of EARL register (0 if none)
  boolean      avreplus;       // AVRe+ architecture
  byte         dwenbase;       // DWEN fuse base
  byte         dwenmask;       // DWEN fuse mask
  byte         bootrstbase;    // BOOTRST fuse abse
  byte         bootrstmask;    // BOOTRST fuse mask
};

// mcu infos (for all AVR mcus supporting debugWIRE)
// untested ones are marked 
	    const mcu_info_type mcu_info[] PROGMEM = {
// sig is signature, sram is sram_size/64, low is 1 iff sram starts at 0x60 instead of at 0x100,
// eep is eep_size/64, flsh is flsh_size/1024, dwdr is io-reg addr of DW reg, pg is flash_page_size/2,
// er4 is 1 iff the erase command erase 4 pages, boot is the (highest) boot sector (word-)address,
// eecr is eeprom control io-reg, eearh is eeprom adress reg high io-reg, 
// sig  sram low eep flsh dwdr  pg er4  boot    eecr eearh  +  db dm    bb bm
#if !defined(NOTINY)              
  {0x9007,  1, 1,  1,  1, 0x2E,  16, 0, 0x0000, 0x1C, 0x00, 0, 1, 0x08, 0, 0x00},

  {0x910A,  2, 1,  2,  2, 0x1f,  16, 0, 0x0000, 0x1C, 0x00, 0, 1, 0x80, 0, 0x00},
  {0x910A,  2, 1,  2,  2, 0x27,  16, 0, 0x0000, 0x1C, 0x00, 0, 1, 0x80, 0, 0x00},
  {0x920D,  4, 1,  4,  4, 0x27,  32, 0, 0x0000, 0x1C, 0x00, 0, 1, 0x80, 0, 0x00},

  {0x920C,  4, 1,  1,  4, 0x27,  32, 0, 0x0000, 0x1C, 0x00, 0, 1, 0x40, 0, 0x00},

  {0x910B,  2, 1,  2,  2, 0x27,  16, 0, 0x0000, 0x1C, 0x1F, 0, 1, 0x40, 0, 0x00},   
  {0x9207,  4, 1,  4,  4, 0x27,  32, 0, 0x0000, 0x1C, 0x1F, 0, 1, 0x40, 0, 0x00},
  {0x930C,  8, 1,  8,  8, 0x27,  32, 0, 0x0000, 0x1C, 0x1F, 0, 1, 0x40, 0, 0x00},
  
  {0x9215,  4, 0,  4,  4, 0x27,   8, 1, 0x0000, 0x1C, 0x1F, 0, 1, 0x40, 0, 0x00}, 
  {0x9315,  8, 0,  8,  8, 0x27,   8, 1, 0x0000, 0x1C, 0x1F, 0, 1, 0x40, 0, 0x00},
  
  {0x9108,  2, 1,  2,  2, 0x22,  16, 0, 0x0000, 0x1C, 0x1F, 0, 1, 0x40, 0, 0x00},
  {0x9206,  4, 1,  4,  4, 0x22,  32, 0, 0x0000, 0x1C, 0x1F, 0, 1, 0x40, 0, 0x00},
  {0x930B,  8, 1,  8,  8, 0x22,  32, 0, 0x0000, 0x1C, 0x1F, 0, 1, 0x40, 0, 0x00},
  
  {0x910C,  2, 1,  2,  2, 0x20,  16, 0, 0x0000, 0x1C, 0x1F, 0, 1, 0x40, 0, 0x00},
  {0x9208,  4, 1,  4,  4, 0x20,  32, 0, 0x0000, 0x1C, 0x1F, 0, 1, 0x40, 0, 0x00},
  {0x930D,  8, 1,  8,  8, 0x20,  32, 0, 0x0000, 0x1C, 0x1F, 0, 1, 0x40, 0, 0x00},
  
  {0x9387,  8, 0,  8,  8, 0x31,  64, 0, 0x0000, 0x1F, 0x22, 0, 1, 0x40, 0, 0x00},  
  {0x9487,  8, 0,  8, 16, 0x31,  64, 0, 0x0000, 0x1F, 0x22, 0, 1, 0x40, 0, 0x00},

  {0x9314,  8, 0,  4,  8, 0x31,  32, 0, 0x0F80, 0x1F, 0x22, 0, 1, 0x40, 2, 0x01},

  {0x9209,  4, 0,  1,  4, 0x31,  32, 0, 0x0000, 0x1F, 0x22, 0, 1, 0x40, 0, 0x00},  
  {0x9311,  8, 0,  1,  8, 0x31,  32, 0, 0x0000, 0x1F, 0x22, 0, 1, 0x40, 0, 0x00},
  
  {0x9412, 16, 0,  4, 16, 0x2E,  16, 1, 0x0000, 0x1C, 0x00, 0, 1, 0x40, 0, 0x00},
#endif

#if !defined(NOMEGA)
  {0x9205,  8, 0,  4,  4, 0x31,  32, 0, 0x0000, 0x1F, 0x22, 1, 1, 0x40, 0, 0x00},
  {0x920A,  8, 0,  4,  4, 0x31,  32, 0, 0x0000, 0x1F, 0x22, 1, 1, 0x40, 0, 0x00},
  {0x9210,  8, 0,  4,  4, 0x31,  32, 0, 0x0000, 0x1F, 0x22, 1, 1, 0x40, 0, 0x00}, // untested
  {0x930A, 16, 0,  8,  8, 0x31,  32, 0, 0x0F80, 0x1F, 0x22, 1, 1, 0x40, 2, 0x01},
  {0x930F, 16, 0,  8,  8, 0x31,  32, 0, 0x0F80, 0x1F, 0x22, 1, 1, 0x40, 2, 0x01},
  {0x9316, 16, 0,  8,  8, 0x31,  32, 0, 0x0F80, 0x1F, 0x22, 1, 1, 0x40, 2, 0x01}, // untested
  {0x9406, 16, 0,  8, 16, 0x31,  64, 0, 0x1F80, 0x1F, 0x22, 1, 1, 0x40, 2, 0x01},
  {0x940B, 16, 0,  8, 16, 0x31,  64, 0, 0x1F80, 0x1F, 0x22, 1, 1, 0x40, 2, 0x01},
  {0x9415, 16, 0,  8, 16, 0x31,  64, 0, 0x1F80, 0x1F, 0x22, 1, 1, 0x40, 2, 0x01}, 
  {0x9514, 32, 0, 16, 32, 0x31,  64, 0, 0x3F00, 0x1F, 0x22, 1, 1, 0x40, 1, 0x01},
  {0x950F, 32, 0, 16, 32, 0x31,  64, 0, 0x3F00, 0x1F, 0x22, 1, 1, 0x40, 1, 0x01},
  {0x9516, 32, 0, 16, 32, 0x31,  64, 0, 0x3F00, 0x1F, 0x22, 1, 1, 0x40, 1, 0x01},
  
  {0x9389,  8, 0,  8,  8, 0x31,  32, 0, 0x0000, 0x1F, 0x22, 1, 1, 0x80, 1, 0x01}, // untested
  {0x9489,  8, 0,  8, 16, 0x31,  64, 0, 0x0000, 0x1F, 0x22, 1, 1, 0x80, 1, 0x01}, // untested
  {0x958A, 16, 0, 16, 32, 0x31,  64, 0, 0x0000, 0x1F, 0x22, 1, 1, 0x80, 1, 0x01}, // untested

  {0x9586, 32, 0, 16, 32, 0x31,  64, 0, 0x3F00, 0x1F, 0x22, 1, 1, 0x40, 1, 0x01}, // untested
  {0x9686, 64, 0, 32, 64, 0x31, 128, 0, 0x7F00, 0x1F, 0x22, 1, 1, 0x40, 1, 0x01}, // untested
  {0x9484, 16, 0,  8, 16, 0x31,  64, 0, 0x1F00, 0x1F, 0x22, 1, 1, 0x40, 1, 0x01}, // untested
  {0x9584, 32, 0, 16, 32, 0x31,  64, 0, 0x3F00, 0x1F, 0x22, 1, 1, 0x40, 1, 0x01}, // untested
  {0x9684, 64, 0, 32, 64, 0x31, 128, 0, 0x7F00, 0x1F, 0x22, 1, 1, 0x40, 1, 0x01}, // untested
#endif

#if !defined(NO90)
  {0x9382,  8, 0,  8,  8, 0x31,  64, 0, 0x1E00, 0x1F, 0x22, 1, 1, 0x80, 1, 0x01}, // untested
  {0x9482,  8, 0,  8, 16, 0x31,  64, 0, 0x3E00, 0x1F, 0x22, 1, 1, 0x80, 1, 0x01}, 

  {0x9383,  8, 0,  8,  8, 0x31,  32, 0, 0x0F80, 0x1F, 0x22, 1, 1, 0x40, 2, 0x01}, // untested
  {0x9383,  8, 0,  8,  8, 0x31,  32, 0, 0x0F80, 0x1F, 0x22, 1, 1, 0x40, 2, 0x01}, // untested
  {0x9383,  8, 0,  8,  8, 0x31,  32, 0, 0x0F80, 0x1F, 0x22, 1, 1, 0x40, 2, 0x01}, // untested

  {0x9388,  4, 0,  8,  8, 0x31,  32, 0, 0x0F80, 0x1C, 0x1F, 1, 1, 0x40, 1, 0x01}, // untested
  {0x948B, 16, 0,  8, 16, 0x31,  64, 0, 0x1F00, 0x1C, 0x1F, 1, 1, 0x40, 1, 0x01}, // untested
  
  {0x9483, 16, 0,  8, 16, 0x31,  64, 0, 0x1F00, 0x1F, 0x22, 1, 1, 0x40, 2, 0x01}, // untested
  {0x9483, 16, 0,  8, 16, 0x31,  64, 0, 0x1F00, 0x1F, 0x22, 1, 1, 0x40, 2, 0x01}, // untested
  
  {0x9310,  8, 0,  4,  8, 0x31,  64, 0, 0x0000, 0x1C, 0x1F, 1, 0, 0x10, 0, 0x00}, // untested
  {0x940C,  8, 0,  4, 16, 0x31,  64, 0, 0x0000, 0x1C, 0x1F, 1, 0, 0x10, 0, 0x00}, // untested
  {0x940D, 16, 0,  8, 16, 0x31,  64, 0, 0x1F00, 0x1C, 0x1F, 1, 1, 0x08, 1, 0x01}, // untested
  {0x9510, 32, 0, 16, 32, 0x31,  64, 0, 0x3F00, 0x1C, 0x1F, 1, 1, 0x08, 1, 0x01}, // untested
  {0x940D, 16, 0,  8, 16, 0x31,  64, 0, 0x1F00, 0x1C, 0x1F, 1, 1, 0x08, 1, 0x01}, // untested
  {0x9510, 32, 0, 16, 32, 0x31,  64, 0, 0x3F00, 0x1C, 0x1F, 1, 1, 0x08, 1, 0x01}, // untested
  {0x9610, 64, 0, 16, 64, 0x31,  64, 0, 0x7F00, 0x1C, 0x1F, 1, 1, 0x08, 1, 0x01}, // untested
#endif
  { 0,      0, 0,  0, 0,  0,      0, 0, 0,      0,    0,    0, 0, 0x00, 0, 0x00} // last mark
};

boolean setMcuAttr(unsigned int id)
{
  int ix = 0;
  unsigned int sig;
  measureRam();

  while ((sig = pgm_read_word(&mcu_info[ix].sig))) {
    if (sig == id) { // found the right mcu type
      //DEBLN(F("MCU struct:"));
      mcu.sig = sig;
      mcu.ramsz = pgm_read_byte(&mcu_info[ix].ramsz_div_64)*64;
      mcu.rambase = (pgm_read_byte(&mcu_info[ix].rambase_low) ? 0x60 : 0x100);
      mcu.eepromsz = pgm_read_byte(&mcu_info[ix].eepromsz_div_64)*64;
      mcu.flashsz = pgm_read_byte(&mcu_info[ix].flashsz_div_1k)*1024;
      mcu.dwdr = pgm_read_byte(&mcu_info[ix].dwdr);
      mcu.pagesz = pgm_read_byte(&mcu_info[ix].pagesz_div_2)*2;
      mcu.erase4pg = pgm_read_byte(&mcu_info[ix].erase4pg);
      mcu.bootaddr = pgm_read_word(&mcu_info[ix].bootaddr);
      mcu.eecr =  pgm_read_byte(&mcu_info[ix].eecr);
      mcu.eearh =  pgm_read_byte(&mcu_info[ix].eearh);
      mcu.avreplus = pgm_read_byte(&mcu_info[ix].avreplus);
      mcu.dwenbase = pgm_read_byte(&mcu_info[ix].dwenbase);
      mcu.dwenmask = pgm_read_byte(&mcu_info[ix].dwenmask);
      mcu.bootrstbase = pgm_read_byte(&mcu_info[ix].bootrstbase);
      mcu.bootrstmask = pgm_read_byte(&mcu_info[ix].bootrstmask);
      // the remaining fields will be derived 
      mcu.eearl = mcu.eecr + 2;
      mcu.eedr = mcu.eecr + 1;
      // we treat the 4-page erase MCU as if pages were larger by a factor of 4!
      if (mcu.erase4pg) mcu.targetpgsz = mcu.pagesz*4; 
      else mcu.targetpgsz = mcu.pagesz;
      return true;
    }
    ix++;
  }
  return false;
}
#endif
