// PCA9685 gripper web controller for Wemos D1 mini (ESP8266).
// Connects to WiFi and serves a page of sliders that drive the gripper servos
// on PCA9685 outputs 12-15 over the 1-2 ms hobby-servo range.
//
// Channel map:
//   ch 12 = Roll          (direct)
//   ch 15 = Wrist Pitch   (direct)
//   ch 13 = Left Jaw      (mixed)
//   ch 14 = Right Jaw     (mixed)
//
// The two jaw channels are driven by a differential mix instead of directly:
//   Wrist Yaw = common base value for both jaws
//   Jaw Open  = differential value between the jaws
//   ch 13 (Left Jaw)  = yaw + open
//   ch 14 (Right Jaw) = yaw - open
//
// PCA9685 outputs are 12-bit (0-4095). At a 50 Hz frame each count is ~4.88 us,
// so 0.75-2.25 ms maps to 154 (0.75 ms) .. 461 (2.25 ms).
//
// I2C: SDA = D2 (GPIO4), SCL = D1 (GPIO5).
// Open Serial Monitor at 9600 baud to see the assigned IP address.

#include <Wire.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include "secrets.h"   // defines WIFI_SSID and WIFI_PASS (gitignored; see secrets.h.example)

const char* MDNS_HOST = "gripper";   // reachable at http://gripper.local/

const uint8_t PCA9685_ADDR = 0x40;

const uint8_t CH_ROLL        = 12;
const uint8_t CH_LEFT_JAW    = 13;
const uint8_t CH_RIGHT_JAW   = 14;
const uint8_t CH_WRIST_PITCH = 15;

const uint16_t PWM_MIN  = 154;                         // ~0.75 ms pulse at 50 Hz
const uint16_t PWM_MAX  = 461;                         // ~2.25 ms pulse at 50 Hz
const uint16_t PWM_MID  = (PWM_MIN + PWM_MAX) / 2;     // 307, neutral pulse

// Roll, pitch and yaw are commanded as signed offsets from neutral: 0 = center,
// +/-POS_MAX = full travel each way. The offset is added to PWM_MID before output.
const int      POS_MAX  = (PWM_MAX - PWM_MIN) / 2;     // 153, max offset each way

const int      OPEN_MAX = (PWM_MAX - PWM_MIN) / 2;     // 153, max symmetric spread
const int      OPEN_MIN = -15;                         // negative = jaws over-close

// Mechanical coupling: moving wrist pitch off neutral drags the yaw bias along.
// yaw bias = PITCH_YAW_COUPLING * pitch offset.
const float    PITCH_YAW_COUPLING = 0.5f;

// Mixer state (all signed offsets from neutral; 0 = centered).
int yawVal   = 0;
int openVal  = 0;
int pitchVal = 0;   // tracked so it can bias the yaw mix

// PCA9685 registers
const uint8_t MODE1     = 0x00;
const uint8_t PRESCALE  = 0xFE;
const uint8_t LED0_ON_L = 0x06;   // channel n base = LED0_ON_L + 4*n

ESP8266WebServer server(80);

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
  uint8_t prescale = (uint8_t)(25000000.0 / (4096.0 * hz) - 1.0 + 0.5);
  uint8_t oldmode = readReg(MODE1);
  writeReg(MODE1, (oldmode & 0x7F) | 0x10);  // sleep to change prescale
  writeReg(PRESCALE, prescale);
  writeReg(MODE1, oldmode);                  // wake
  delay(5);
  writeReg(MODE1, oldmode | 0xA0);           // RESTART + auto-increment
}

int clampPWM(int v) {
  if (v < PWM_MIN) v = PWM_MIN;
  if (v > PWM_MAX) v = PWM_MAX;
  return v;
}

int clampOffset(int v) {
  if (v < -POS_MAX) v = -POS_MAX;
  if (v > POS_MAX) v = POS_MAX;
  return v;
}

// Apply the differential mix to the two jaw channels, including the pitch->yaw
// coupling compensation. yawVal/pitchVal/openVal are signed offsets from neutral.
void updateJaws() {
  int yawBias = (int)lround(pitchVal * PITCH_YAW_COUPLING);
  int base = PWM_MID + yawVal + yawBias;
  setPWM(CH_LEFT_JAW,  0, clampPWM(base + openVal));  // ch 13 = yaw + bias + open
  setPWM(CH_RIGHT_JAW, 0, clampPWM(base - openVal));  // ch 14 = yaw + bias - open
}

// Web page: one slider per control, each posts /set?id=<name>&val=<n> on input.
const char PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Gripper Control</title>
<style>
  body { font-family: sans-serif; max-width: 480px; margin: 24px auto; padding: 0 16px; }
  h1 { font-size: 1.3rem; }
  .row { margin: 18px 0; }
  label { display: block; font-weight: bold; margin-bottom: 6px; }
  input[type=range] { width: 100%; }
  .val { font-family: monospace; color: #555; font-weight: normal; }
  button { font-size: 1rem; padding: 10px 18px; margin-top: 12px; cursor: pointer; }
</style></head><body>
<h1>da Vinci Gripper Control</h1>
<div id="sliders"></div>
<button id="reset">Reset to neutral</button>
<script>
const CTRLS=[
  {id:'roll',  label:'Roll (ch 12)',               min:-%POS%, max:%POS%, value:0, reset:0},
  {id:'pitch', label:'Wrist Pitch (ch 15)',        min:-%POS%, max:%POS%, value:0, reset:0},
  {id:'yaw',   label:'Wrist Yaw (jaws base)',      min:-%POS%, max:%POS%, value:0, reset:0},
  {id:'open',  label:'Jaw Open (jaws spread)',     min:%OPEN_MIN%, max:%OPEN_MAX%, value:0, reset:%OPEN_MIN%},
];
const box=document.getElementById('sliders'), els={};
function apply(c,val){ const {s,v}=els[c.id]; s.value=val; v.textContent=val;
  fetch(`/set?id=${c.id}&val=${val}`); }
CTRLS.forEach(c=>{
  const row=document.createElement('div'); row.className='row';
  row.innerHTML=`<label>${c.label} <span class="val" id="v_${c.id}">${c.value}</span></label>`+
    `<input type="range" min="${c.min}" max="${c.max}" value="${c.value}" id="s_${c.id}">`;
  box.appendChild(row);
  const s=row.querySelector('#s_'+c.id), v=row.querySelector('#v_'+c.id);
  els[c.id]={s,v};
  s.addEventListener('input',()=>{ v.textContent=s.value;
    fetch(`/set?id=${c.id}&val=${s.value}`); });
});
document.getElementById('reset').addEventListener('click',()=>{
  CTRLS.forEach(c=>apply(c,c.reset));
});
</script>
</body></html>
)rawliteral";

void handleRoot() {
  String page = FPSTR(PAGE);
  page.replace("%POS%", String(POS_MAX));
  page.replace("%OPEN_MIN%", String(OPEN_MIN));
  page.replace("%OPEN_MAX%", String(OPEN_MAX));
  server.send(200, "text/html", page);
}

void handleSet() {
  if (!server.hasArg("id") || !server.hasArg("val")) {
    server.send(400, "text/plain", "missing id or val");
    return;
  }
  String id = server.arg("id");
  int val = server.arg("val").toInt();

  if (id == "roll") {
    setPWM(CH_ROLL, 0, clampPWM(PWM_MID + clampOffset(val)));
  } else if (id == "pitch") {
    pitchVal = clampOffset(val);
    setPWM(CH_WRIST_PITCH, 0, clampPWM(PWM_MID + pitchVal));
    updateJaws();   // pitch couples into the yaw bias
  } else if (id == "yaw") {
    yawVal = clampOffset(val);
    updateJaws();
  } else if (id == "open") {
    if (val < OPEN_MIN) val = OPEN_MIN;
    if (val > OPEN_MAX) val = OPEN_MAX;
    openVal = val;
    updateJaws();
  } else {
    server.send(400, "text/plain", "bad id");
    return;
  }
  server.send(200, "text/plain", "ok");
}

void setup() {
  Serial.begin(9600);
  Serial.println();
  Serial.println("PCA9685 gripper web controller");

  Wire.begin(D2, D1);
  writeReg(MODE1, 0x00);
  delay(5);
  setFrequency(50.0);

  // Neutral pose at startup: all offsets 0 -> every servo at PWM_MID.
  setPWM(CH_ROLL, 0, PWM_MID);
  setPWM(CH_WRIST_PITCH, 0, PWM_MID);
  updateJaws();   // yaw=0, pitch=0, open=0 -> both jaws at PWM_MID

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to ");
  Serial.print(WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected. IP http://");
  Serial.print(WiFi.localIP());
  Serial.println("/");

  if (MDNS.begin(MDNS_HOST)) {
    MDNS.addService("http", "tcp", 80);
    Serial.print("Also reachable at http://");
    Serial.print(MDNS_HOST);
    Serial.println(".local/");
  }

  server.on("/", handleRoot);
  server.on("/set", handleSet);
  server.begin();
}

void loop() {
  MDNS.update();
  server.handleClient();
}
