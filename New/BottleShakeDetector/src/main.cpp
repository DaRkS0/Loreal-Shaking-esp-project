#include <Arduino.h>

// put function declarations here:
int myFunction(int, int);

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
 
}

void loop() {
  // put your main code here, to run repeatedly:
   delay(1000);
  Serial.println("\n--- ESP32 Bottle Shake Detector ---");
}

// put function definitions here:
int myFunction(int x, int y) {
  return x + y;
} 