// dw-link -- the table of supported MCUs; goes when the mkII device descriptor replaces it
#ifndef DWLINK_MCUINFO_H
#define DWLINK_MCUINFO_H

#if !defined(NOTINY)
extern const byte t4313mask[] PROGMEM; // setupDW switches to it for the ATtiny2313A
#endif

boolean setMcuAttr(unsigned int);

#endif
