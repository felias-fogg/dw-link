#include <Arduino.h>

#define LED SCK

byte thisByte = 0;
void setup() {
  pinMode(LED, OUTPUT);
}

void loop() {
  int i=digitalRead(1)+20;
  digitalWrite(LED, HIGH);  
  delay(1000);              
  digitalWrite(LED, LOW);              
  thisByte++;
  thisByte = thisByte + i;
  delay(100+thisByte);
}
