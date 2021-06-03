#define LED 4

byte thisByte = 0;

// the setup function runs once when you press reset or power the board
void setup() {
  // initialize digital pin LED_BUILTIN as an output.
  pinMode(LED, OUTPUT);
}

// the loop function runs over and over again forever
void loop() {
  int i=10;
  digitalWrite(LED, HIGH);   // turn the LED on (HIGH is the voltage level)
  i++;
  delay(1000);                       // wait for a second
  i++;
  digitalWrite(LED, LOW);    // turn the LED off by making the voltage LOW
  i++;
  delay(1000);                       // wait for a second
  i++;
  thisByte++;
  thisByte = thisByte + i;
  delay(thisByte);
}
