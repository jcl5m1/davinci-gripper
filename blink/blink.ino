// Simple blink test for Wemos D1 mini (ESP8266)
// Verifies the board and toolchain are working.
// Note: the onboard LED (LED_BUILTIN, GPIO2) is active-LOW.

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
  Serial.println();
  Serial.println("Blink test running on Wemos D1 mini");
}

void loop() {
  digitalWrite(LED_BUILTIN, LOW);   // LED on
  Serial.println("LED ON");
  delay(500);
  digitalWrite(LED_BUILTIN, HIGH);  // LED off
  Serial.println("LED OFF");
  delay(500);
}
