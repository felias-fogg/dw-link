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
 private:
  struct  {
    uint16_t oneAndAHalfBitDelay;
    uint16_t bitDelay;
    uint16_t endOfByte;
    uint8_t setICfalling, setICrising, setCTC;
  } _speedData[2];


 public:
  // public methods
  dwSerial(void);
  unsigned long calibrate(void);
  void sendBreak();
  size_t sendCmd(const uint8_t  *buf, uint8_t len);
  size_t write(const uint8_t  *buf, uint8_t len);
  void enable(bool);
};


#endif
