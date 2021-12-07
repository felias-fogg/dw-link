// Program intended to be used for testing dw-link

#include <stdio.h>
#include <string.h>
#include "src/picoUART.h"
#include "src/pu_print.h"

#define MAXDEPTH 20

unsigned long memo[MAXDEPTH];
unsigned long lookups;
unsigned long callcnt;

char *convnum(unsigned long num)
{
  static char numstr[11];
  unsigned long div = 100000000UL;
  boolean nodigit = true;
  byte i = 0;

  while (div != 0) {
    if (num/div == 0 && nodigit && div != 1) div /= 10;
    else {
      numstr[i++] = num/div + '0';
      num %= div;
      div /= 10;
      nodigit = false;
    }
  }
  numstr[i] = '\0';
  return numstr;
}

unsigned long fib(unsigned long n)
{
  callcnt++;
  if (n <= 1) return n;
  else return fib(n-1) + fib(n-2);
}

unsigned long mfib(unsigned long n)
{
  unsigned long res;
  
  callcnt++;
  if (n <= 1) return n;
  else if (memo[n]) {
    lookups++;
    return memo[n];
  } else {
    res = mfib(n-1) + mfib(n-2);
    memo[n] = res;
    return res;
  }
}

int main(void)
{
  prints_P(PSTR("\n\n\rCalling fib and mfib with increasing parameters\n\r"));
  for (byte i=0; i <= MAXDEPTH; i++) {
    prints_P(PSTR("fib("));
    prints(convnum(i));
    prints_P(PSTR(")="));
    callcnt = 0;
    prints(convnum(fib(i)));
    prints_P(PSTR(" Calls="));
    prints(convnum(callcnt));
    prints_P(PSTR(" mfib("));
    prints(convnum(i));
    prints_P(PSTR(")="));
    callcnt = 0;
    for (byte j=0; j < MAXDEPTH; j++) memo[j] = 0;
    prints(convnum(mfib(i)));
    prints_P(PSTR(" Calls="));
    prints(convnum(callcnt));
    prints_P(PSTR(" Lookups="));
    prints(convnum(lookups));
    prints_P(PSTR("\n\r"));
  }
  
}
