// dw-link -- layer 3: the JTAGICE mkII protocol (AVR067) over the serial line to the host
//
// D0: only the skeleton. Frames, CRC, sign-on, parameters and the baud rate switch come in D1.
#include "config.h"
#include "mkii.h"

// a serial mkII starts at 19200 bps; the host switches with PAR_BAUD_RATE (AVR067)
void mkiiInit(void)
{
  Serial.begin(MKII_BPS);
}

// D0: discard everything
void mkiiPoll(void)
{
  while (Serial.available()) Serial.read();
}

// D0: nothing to tell the host yet; from D3 on, EVT_BREAK
void mkiiTargetStopped(void)
{
  setSysState(DWCONN_STATE);
}
