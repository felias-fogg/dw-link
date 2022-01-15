
/* Debug macros */
#if TXODEBUG
#define DEBDECLARE() TXOnlySerial deb(255)
#define DEBINIT(p) deb.setTX(p); deb.begin(57600)
#define DEBPR(str) deb.print(str)
#define DEBPRF(str,frm) deb.print(str,frm)
#define DEBLN(str) deb.println(str)
#define DEBLNF(str,frm) deb.println(str,frm)
#else
#define DEBDECLARE()
#define DEBINIT(p)
#define DEBPR(str)
#define DEBPRF(str,frm) 
#define DEBLN(str)
#define DEBLNF(str,frm) 
#endif
