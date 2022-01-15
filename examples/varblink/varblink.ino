#ifndef LED_BUILTIN
 #define LED 4
#else
 #define LED LED_BUILTIN
#endif
byte thisByte = 0;
void setup() {
  pinMode(LED, OUTPUT);
}

void loop() {
  int i=random(100);
  digitalWrite(LED, HIGH);  
  delay(1000);              
  digitalWrite(LED, LOW);              
  thisByte++;
  thisByte = thisByte + i;
  delay(100+thisByte);
}
