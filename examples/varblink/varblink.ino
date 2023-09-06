#ifdef ARDUINO_AVR_ATTINYX5
 #define LED 4
#else
 #define LED LED_BUILTIN
#endif
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
