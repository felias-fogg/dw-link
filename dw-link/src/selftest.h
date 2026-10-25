// dw-link -- live tests of layers 1 and 2 on the real target (UNITDW, UNITTG builds)
#ifndef DWLINK_SELFTEST_H
#define DWLINK_SELFTEST_H

void alltests(void);
void testSummary(int);
int testResult(boolean);
void testMessage(const char *, long);
void setupTestCode(void);
int targetTests(int &);
int DWtests(int &);

#endif
