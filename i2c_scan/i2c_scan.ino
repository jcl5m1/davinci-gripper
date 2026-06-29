// I2C bus scanner for Wemos D1 mini (ESP8266).
// Connect via USB and open the Serial Monitor at 9600 baud.
// Default I2C pins on the Wemos D1 mini: SDA = D2 (GPIO4), SCL = D1 (GPIO5).

#include <Wire.h>

void setup() {
  Serial.begin(9600);
  Serial.println();
  Serial.println("I2C scanner running on Wemos D1 mini");

  // Wire.begin(SDA, SCL). D2/D1 are the board defaults; pass explicitly to be clear.
  Wire.begin(D2, D1);
}

void loop() {
  byte count = 0;

  Serial.println("Scanning I2C bus (0x01 - 0x7E)...");

  for (byte address = 1; address < 127; address++) {
    // A device ACKs its address with endTransmission() returning 0.
    Wire.beginTransmission(address);
    byte error = Wire.endTransmission();

    if (error == 0) {
      Serial.print("  Found device at 0x");
      if (address < 16) Serial.print("0");
      Serial.print(address, HEX);
      Serial.println();
      count++;
    } else if (error == 4) {
      Serial.print("  Unknown error at 0x");
      if (address < 16) Serial.print("0");
      Serial.println(address, HEX);
    }
  }

  if (count == 0) {
    Serial.println("No I2C devices found.");
  } else {
    Serial.print("Done. ");
    Serial.print(count);
    Serial.println(" device(s) found.");
  }

  Serial.println();
  delay(5000);  // Rescan every 5 seconds.
}
