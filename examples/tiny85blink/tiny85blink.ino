#define LED 4

byte thisByte = 0;


void setup() {

  pinMode(LED, OUTPUT);
}


void loop() {
  int i=10;
  digitalWrite(LED, HIGH);   
  i++;
  delay(1000);               
  i++;
  digitalWrite(LED, LOW);    
  i++;
  delay(1000);               
  i++;
  thisByte++;
  thisByte = thisByte + i;
  delay(thisByte);
}
