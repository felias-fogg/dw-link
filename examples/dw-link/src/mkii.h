// dw-link -- layer 3: the JTAGICE mkII protocol (AVR067) over the serial line to the host
#ifndef DWLINK_MKII_H
#define DWLINK_MKII_H

void mkiiInit(void);          // open the serial line at the mkII start speed
void mkiiPoll(void);          // handle bytes from the host
void mkiiTargetStopped(void); // the running target has stopped (break from the target)

#endif
