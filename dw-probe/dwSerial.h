/*
 * dwSerial.h -- debugWIRE serial interface (based on SingleWireSerial)
 */

#ifndef dwSerial_h
#define dwSerial_h

#include <inttypes.h>
#include <SingleWireSerial.h>

/******************************************************************************
* Definitions
******************************************************************************/

class dwSerial : public SingleWireSerial
{
public:
  // public methods
  dwSerial(void);
  unsigned long calibrate();
  void sendBreak();
  size_t sendCmd(const uint8_t  *buf, uint8_t len);
  size_t write(const uint8_t  *buf, uint8_t len);
  void enable(bool);
};


#endif
