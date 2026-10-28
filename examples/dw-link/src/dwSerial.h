/*
 * dwSerial.h -- debugWIRE serial interface (based on SingleWireSerial)
 */
#ifndef dwSerial_h
#define dwSerial_h

#include <inttypes.h>
#include "SingleWireSerial.h"

/******************************************************************************
* Definitions
******************************************************************************/

class dwSerial : public SingleWireSerial
{
#if 0 // apparently unused
 private:
  struct  {
    uint16_t oneAndAHalfBitDelay;
    uint16_t bitDelay;
    uint16_t endOfByte;
    uint8_t setICfalling, setICrising, setCTC;
  } _speedData[2];
#endif

 public:
  // public methods
  dwSerial(void);
  unsigned long calibrate(void);
  void sendBreak();
  size_t sendCmd(const uint8_t  *buf, uint8_t len, bool fastReturn = false);
  size_t sendCmd(uint8_t cmd, bool fastReturn = false);
  void enable(bool);
};


#endif
