// PCA9685 hobby-servo oscillator for Wemos D1 mini (ESP8266).
// Sweeps several servo channels together between their min and max positions
// as a triangle wave, one full cycle every 4 seconds (2 s rising, 2 s falling).
//
// PCA9685 outputs are 12-bit (0-4095). At a 50 Hz servo frame each count is
// ~4.88 us (20 ms / 4096), so the standard 1-2 ms hobby-servo range is:
//   PWM_MIN = 205 counts  ~= 1.0 ms pulse  (one end of travel)
//   PWM_MAX = 410 counts  ~= 2.0 ms pulse  (other end of travel)
//
// I2C: SDA = D2 (GPIO4), SCL = D1 (GPIO5). Open Serial Monitor at 9600 baud.

#include <Wire.h>

const uint8_t  PCA9685_ADDR = 0x40;
const uint8_t  CHANNELS[]    = {12, 13, 14, 15};   // outputs to sweep together
const uint8_t  NUM_CHANNELS  = sizeof(CHANNELS) / sizeof(CHANNELS[0]);

const uint16_t PWM_MIN   = 205;         // ~1.0 ms pulse at 50 Hz
const uint16_t PWM_MAX   = 410;         // ~2.0 ms pulse at 50 Hz
const uint32_t PERIOD_MS = 4000;        // one full min->max->min cycle

// PCA9685 registers
const uint8_t MODE1     = 0x00;
const uint8_t PRESCALE  = 0xFE;
const uint8_t LED0_ON_L = 0x06;         // channel n base = LED0_ON_L + 4*n

void writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(PCA9685_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

uint8_t readReg(uint8_t reg) {
  Wire.beginTransmission(PCA9685_ADDR);
  Wire.write(reg);
  Wire.endTransmission();
  Wire.requestFrom((int)PCA9685_ADDR, 1);
  return Wire.read();
}

// on/off are 12-bit tick positions within the 4096-step PWM frame.
void setPWM(uint8_t channel, uint16_t on, uint16_t off) {
  Wire.beginTransmission(PCA9685_ADDR);
  Wire.write(LED0_ON_L + 4 * channel);
  Wire.write(on & 0xFF);
  Wire.write(on >> 8);
  Wire.write(off & 0xFF);
  Wire.write(off >> 8);
  Wire.endTransmission();
}

void setFrequency(float hz) {
  // prescale = round(25 MHz / (4096 * hz)) - 1
  uint8_t prescale = (uint8_t)(25000000.0 / (4096.0 * hz) - 1.0 + 0.5);
  uint8_t oldmode = readReg(MODE1);
  writeReg(MODE1, (oldmode & 0x7F) | 0x10);  // enter sleep to change prescale
  writeReg(PRESCALE, prescale);
  writeReg(MODE1, oldmode);                  // wake
  delay(5);
  writeReg(MODE1, oldmode | 0xA0);           // RESTART + auto-increment (AI)
}

void setup() {
  Serial.begin(9600);
  Serial.println();
  Serial.println("PCA9685 channel oscillator");

  Wire.begin(D2, D1);

  writeReg(MODE1, 0x00);   // reset to known state
  delay(5);
  setFrequency(50.0);      // 50 Hz frame for hobby servos

  Serial.print("Oscillating channels 12-15: min=");
  Serial.print(PWM_MIN);
  Serial.print(" max=");
  Serial.print(PWM_MAX);
  Serial.print(" period=");
  Serial.print(PERIOD_MS);
  Serial.println(" ms");
}

void loop() {
  static uint32_t lastPrint = 0;

  uint32_t t = millis() % PERIOD_MS;
  uint16_t value;
  if (t < PERIOD_MS / 2) {                                   // rising half
    value = PWM_MIN + (uint32_t)(PWM_MAX - PWM_MIN) * t / (PERIOD_MS / 2);
  } else {                                                   // falling half
    value = PWM_MAX - (uint32_t)(PWM_MAX - PWM_MIN) * (t - PERIOD_MS / 2) / (PERIOD_MS / 2);
  }

  for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
    setPWM(CHANNELS[i], 0, value);
  }

  if (millis() - lastPrint >= 250) {   // report progress without flooding serial
    lastPrint = millis();
    Serial.print("ch12-15 = ");
    Serial.println(value);
  }

  delay(10);
}
