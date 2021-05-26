
/* Debug macros */
#ifdef DEBUG
#define DEBDECLARE() TXOnlySerial deb(DEBTX)
#define DEBINIT() deb.begin(9600)
#define DEBPR(str) deb.print(str)
#define DEBPRF(str,frm) deb.print(str,frm)
#define DEBLN(str) deb.println(str)
#define DEBLNF(str,frm) deb.println(str,frm)
#else
#define DEBDECLARE()
#define DEBINIT()
#define DEBPR(str)
#define DEBPRF(str,frm) 
#define DEBLN(str)
#define DEBLNF(str,frm) 
#endif
