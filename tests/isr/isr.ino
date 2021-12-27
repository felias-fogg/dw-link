#include <Arduino.h>
#if defined(__AVR_ATtiny13__)
#define IRQPIN 1
#elif defined(__AVR_ATtiny2313__) ||  defined(__AVR_ATtiny4313__) || defined(__AVR_ATtiny2313A__) 
#define IRQPIN 4
#elif defined(__AVR_ATtiny43U__)
#define IRQPIN 7
#elif defined(__AVR_ATtiny24__) || defined(__AVR_ATtiny44__) || defined(__AVR_ATtiny84__)
#define IRQPIN 8
#elif defined(__AVR_ATtiny441__) || defined(__AVR_ATtiny841__)
#define IRQPIN 1
#elif defined(__AVR_ATtiny25__) || defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny85__)
#define IRQPIN 2
#elif defined(__AVR_ATtiny261__) || defined(__AVR_ATtiny461__) || defined(__AVR_ATtiny861__)
#define IRQPIN 14
#elif defined(__AVR_ATtiny87__) || defined(__AVR_ATtiny167__)
#define IRQPIN 14
#elif defined(__AVR_ATtiny48__) || defined(__AVR_ATtiny88__)
#define IRQPIN 2
#elif defined(__AVR_ATtiny828__)
#define IRQPIN 17
#elif defined(__AVR_ATtiny1634__)
#define IRQPIN 11
#elif defined(__AVR_ATmega48__) || defined(__AVR_ATmega48A__) || \
  defined(__AVR_ATmega48P__) || defined(__AVR_ATmega48PA__) || defined(__AVR_ATmega48PB__) || \
  defined(__AVR_ATmega88__) || defined(__AVR_ATmega88A__) ||  \
  defined(__AVR_ATmega88P__) || defined(__AVR_ATmega88PA__) || defined(__AVR_ATmega88PB__) || \
  defined(__AVR_ATmega168__) || defined(__AVR_ATmega168A__) ||  \
  defined(__AVR_ATmega168P__) || defined(__AVR_ATmega168PA__) || defined(__AVR_ATmega168PB__) || \
  defined(__AVR_ATmega328__) || defined(__AVR_ATmega328P__) || defined(__AVR_ATmega328PB__)
#define IRQPIN 2
#else
#error "MCU not supported"
#endif

volatile int irqcount = 0;
volatile int outsidecount = 0;

void setup()
{
#if FLASHEND >= 4095
  Serial.begin(9600);
  Serial.println(F("Startup ..."));
  delay(300);
#endif
  pinMode(IRQPIN, OUTPUT);
  digitalWrite(IRQPIN, HIGH);
  attachInterrupt(0, irqserver, LOW);
}

void loop()
{
  outsidecount++;
  digitalWrite(IRQPIN,LOW);
  outsidecount++;
  delay(1);
  outsidecount++;
  digitalWrite(IRQPIN,HIGH);
#if FLASHEND >= 4095
  Serial.print(outsidecount);
  Serial.print(" / ");
  Serial.println(irqcount);
#else
  delay(outsidecount+irqcount/1000);
#endif
  delay(100);
}

void irqserver()
{
  irqcount++;
}
