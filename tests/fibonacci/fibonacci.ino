// Program intended to be used for testing dw-link

#define LED SCK // use always the SCK pin so one can see something flashing.
#define MAXDEPTH 7 // that is OK for the ATtiny13

int callcnt;
int memo[MAXDEPTH];
int lookups;

int fib(int n)
{
  callcnt++;
  if (n <= 2) return 1;
  else return fib(n-1) + fib(n-2);
}

int mfib(int n)
{
  int res;
  
  callcnt++;
  if (n <= 2) return 1;
  else if (memo[n]) {
    lookups++;
    return memo[n];
  } else {
    res = mfib(n-1) + mfib(n-2);
    memo[n] = res;
    return res;
  }
}

void setup()
{
#ifdef Serial
  Serial.begin(9600);
#endif
  pinMode(LED, OUTPUT);
#ifdef  LED_BUILTIN
  pinMode(LED_BUILTIN, OUTPUT);
#endif
}

void loop(void)
{
  int result, arg;
  byte j;

  arg = MAXDEPTH;
  /* ordinary recursive call */
  callcnt = 0;
  result = fib(arg);
#ifdef Serial
  Serial.print(F("fib("));
  Serial.print(arg);
  Serial.print(F(")="));
  Serial.println(result);
  Serial.print(F(" Calls="));
  Serial.println(callcnt);
#endif
  ledControl(true);
  delay(result);
  ledControl(false);
  delay(300);
  ledControl(true);
  delay(callcnt);
  ledControl(false);
  delay(500);

  /* memoizing */
  callcnt = 0;
  for (j=0; j < MAXDEPTH; j++) memo[j] = 0;
  result = mfib(arg);
#ifdef Serial
  Serial.print(F("fib("));
  Serial.print(arg);
  Serial.print(F(")="));
  Serial.println(result);
  Serial.print(F(" Calls="));
  Serial.println(callcnt);
  Serial.print(F(" Lookups="));
#endif
  ledControl(true);
  delay(result);
  ledControl(false);
  delay(1000);
  ledControl(true);
  delay(callcnt);
  ledControl(false);
  delay(2000);
}

inline void ledControl(boolean on)
{
  digitalWrite(LED, on);
#ifdef LED_BUILTIN
  digitalWrite(LED_BUILTIN, on);
#endif
}
