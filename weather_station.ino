// Weather station + bubble level
// Parts: UNO R4 WiFi, DHT11 3-pin module, 0.96" OLED (SSD1306), push button,
//        GY-6500 (MPU-9250/6500) motion board
// Optional: BMP180 barometer (detected automatically if ever added)
// Lines marked "CHANGED" or "NEW" differ from the original sketch.
//
// Button cycles screens:
//   0 = summary   1 = pressure graph (only if BMP180 present)
//   2 = temp graph   3 = humidity graph   4 = bubble level (NEW)
//
// Level screen: keep the breadboard flat on the desk when you power it on.
// That starting position is treated as "level".

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_BMP085.h>
#include <DHT.h>

#define DHTPIN 11
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

int butPin = 2;
int butVal = 0;
int butValOld = 0;
int butCount = 0;
int butRem = 0;
unsigned long lastPress = 0;  // NEW: for debouncing
const int NUM_SCREENS = 5;    // NEW: added the bubble-level screen

float tempF = 70;  // CHANGED: start at a sensible value
float tempC;
float hum = 50;
unsigned long hReadTime = 0;  // CHANGED: millis() timers should be unsigned long
unsigned long hReadInt = 2000;

unsigned long lastUpdate = 0;  // CHANGED
unsigned long updateInterval = 600;

float rawXdata[128];
int normXdata[128];

float rawYdataP[128];
int normYdataP[128];
float rawYdataH[128];
int normYdataH[128];
float rawYdataT[128];
int normYdataT[128];

float xMin = 0;
float xMax = 127;

float yMinP = 28;
float yMaxP = 31;
float yMinH = 30;
float yMaxH = 90;
float yMinT = 60;
float yMaxT = 90;

float Pt = 14;
float Pb = 63;
float Pl = 0;
float Pr = 127;

int numPoints = 128;

Adafruit_BMP085 BP;
bool BMPconnected = false;

float BarPress = 30;
float BarPressNow = 30;
float alt = 330;  // CHANGED: set this to YOUR elevation in metres

// NEW: motion sensor (MPU-6050/6500/9250 all share these registers)
byte mpuAddr = 0x68;
bool MPUconnected = false;
float pitch = 0;
float roll = 0;
float pitch0 = 0;  // NEW: angles measured at power-on (= "level")
float roll0 = 0;

int screenWidth = 128;
int screenHeight = 64;
int oledReset = -1;
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 oled(screenWidth, screenHeight, &Wire, oledReset);

void setup() {
  pinMode(butPin, INPUT_PULLUP);
  Serial.begin(9600);
  Wire.begin();

  // CHANGED: start the screen FIRST and report if it isn't found
  if (!oled.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("OLED not found at 0x3C - check GND, VCC, SCL, SDA wires");
  } else {
    Serial.println("OLED found");
  }
  oled.clearDisplay();
  oled.setTextColor(WHITE);
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.println("Starting...");
  oled.display();

  BMPconnected = BP.begin();
  if (BMPconnected) {
    Serial.println("BMP180 found and Connected");
  } else {
    Serial.println("BMP180 not found - pressure off, temperature from DHT11");
  }

  // NEW: wake the motion sensor (tries both possible addresses)
  MPUconnected = mpuBegin(0x68) || mpuBegin(0x69);
  if (MPUconnected) {
    Serial.print("Motion sensor found at 0x");
    Serial.println(mpuAddr, HEX);
    // NEW: the board stands upright in the breadboard, so measure its
    // starting angle and treat that as level
    for (int i = 0; i < 50; i = i + 1) {
      mpuReadTilt();
      delay(10);
    }
    pitch0 = pitch;
    roll0 = roll;
  } else {
    Serial.println("Motion sensor not found - level screen disabled");
  }

  for (int i = 0; i < numPoints; i = i + 1) {
    rawXdata[i] = i;
  }
  dht.begin();
  delay(2000);

  // NEW: fill the graph history with the first reading so graphs
  // don't start with a jump from zero
  float h0 = dht.readHumidity();
  float t0 = dht.readTemperature(true);
  if (!isnan(h0)) hum = h0;
  if (!isnan(t0) && !BMPconnected) tempF = t0;
  for (int i = 0; i < numPoints; i = i + 1) {
    rawYdataP[i] = BarPress;
    rawYdataT[i] = tempF;
    rawYdataH[i] = hum;
  }
}

void loop() {
  // CHANGED: only talk to the barometer if it's actually there
  if (BMPconnected) {
    BarPressNow = normPress(alt);
    BarPress = .95 * BarPress + .05 * BarPressNow;
    tempC = BP.readTemperature();
    tempF = tempC * 9 / 5 + 32;
  }

  if (millis() - hReadTime > hReadInt) {
    float h = dht.readHumidity();
    float t = dht.readTemperature(true);  // true = Fahrenheit
    // CHANGED: ignore failed readings instead of showing "nan"
    if (isnan(h) || isnan(t)) {
      Serial.println("DHT11 read failed - check S to pin 11, + and - wires");
    } else {
      hum = h;
      if (!BMPconnected) {
        tempF = t;  // CHANGED: DHT11 supplies temperature when no BMP180
      }
      Serial.print("Temp F: ");
      Serial.print(tempF);
      Serial.print("   Hum %: ");
      Serial.println(hum);
    }
    hReadTime = millis();
  }

  if (millis() - lastUpdate >= updateInterval) {
    for (int i = 0; i < numPoints - 1; i = i + 1) {
      rawYdataP[i] = rawYdataP[i + 1];
      rawYdataT[i] = rawYdataT[i + 1];
      rawYdataH[i] = rawYdataH[i + 1];
    }
    lastUpdate = millis();
    rawYdataP[numPoints - 1] = BarPress;
    rawYdataT[numPoints - 1] = tempF;
    rawYdataH[numPoints - 1] = hum;
  }

  butVal = digitalRead(butPin);
  // CHANGED: ignore extra "presses" from contact bounce within 200 ms
  if (butVal == 0 && butValOld == 1 && millis() - lastPress > 200) {
    lastPress = millis();
    butCount = butCount + 1;
    butRem = butCount % NUM_SCREENS;
    // NEW: skip screens for sensors that aren't connected
    if (butRem == 1 && !BMPconnected) {
      butCount = butCount + 1;
      butRem = butCount % NUM_SCREENS;
    }
    if (butRem == 4 && !MPUconnected) {
      butCount = butCount + 1;
      butRem = butCount % NUM_SCREENS;
    }
  }
  butValOld = butVal;

  if (butRem == 0) {
    oled.clearDisplay();
    oled.setCursor(0, 0);
    oled.setTextColor(WHITE);
    oled.setTextSize(2);
    oled.print(tempF);
    oled.println(" F");
    oled.print(hum);
    oled.println(" %Hum");
    if (BMPconnected) {
      oled.print(BarPress);
      oled.println(" InHG");
    }
    oled.display();
  }
  if (butRem == 1) {
    oled.clearDisplay();
    oled.setCursor(0, 0);
    oled.setTextColor(WHITE);
    oled.setTextSize(2);
    oled.print(BarPress);
    oled.setTextSize(1);
    oled.println(" InHG");
    normDataP();
    plotPress();
    oled.display();
  }
  if (butRem == 2) {
    oled.clearDisplay();
    oled.setCursor(0, 0);
    oled.setTextColor(WHITE);
    oled.setTextSize(2);
    oled.print(tempF);
    oled.setTextSize(1);
    oled.println(" F");
    normDataT();
    plotTemp();
    oled.display();
  }
  if (butRem == 3) {
    oled.clearDisplay();
    oled.setCursor(0, 0);
    oled.setTextColor(WHITE);
    oled.setTextSize(2);
    oled.print(hum);
    oled.setTextSize(1);
    oled.println(" %HUM");
    normDataH();
    plotHum();
    oled.display();
  }
  if (butRem == 4) {  // NEW: bubble level
    mpuReadTilt();
    drawLevel();
  }
}

// ---------- NEW: motion sensor ----------

// Wake the sensor at the given address. Returns true if it answered.
bool mpuBegin(byte addr) {
  Wire.beginTransmission(addr);
  Wire.write(0x6B);  // PWR_MGMT_1 register
  Wire.write(0x00);  // 0 = wake up (it starts asleep)
  if (Wire.endTransmission() == 0) {
    mpuAddr = addr;
    return true;
  }
  return false;
}

// Read the accelerometer and turn it into tilt angles in degrees.
void mpuReadTilt() {
  Wire.beginTransmission(mpuAddr);
  Wire.write(0x3B);  // ACCEL_XOUT_H: first of 6 accelerometer bytes
  if (Wire.endTransmission(false) != 0) return;
  Wire.requestFrom(mpuAddr, (uint8_t)6);
  if (Wire.available() < 6) return;

  int16_t ax = readWord();
  int16_t ay = readWord();
  int16_t az = readWord();

  // Gravity always points down, so its direction in the sensor's frame gives the tilt.
  float newRoll = atan2((float)ay, (float)az) * 180.0 / PI;
  float newPitch = atan2(-(float)ax, sqrt((float)ay * ay + (float)az * az)) * 180.0 / PI;

  // Smooth out jitter (same idea as the pressure smoothing above)
  roll = 0.8 * roll + 0.2 * newRoll;
  pitch = 0.8 * pitch + 0.2 * newPitch;
}

// Two bytes, high byte first -> one signed 16-bit number.
int16_t readWord() {
  uint8_t hi = Wire.read();
  uint8_t lo = Wire.read();
  return (int16_t)((hi << 8) | lo);
}

void drawLevel() {
  int cx = 64;  // centre of the level circle
  int cy = 38;
  int r = 24;  // circle radius in pixels

  // NEW: tilt relative to the power-on position
  float dp = pitch - pitch0;
  float dr = roll - roll0;

  oled.clearDisplay();
  oled.setTextColor(WHITE);
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.print("P:");
  oled.print(dp, 1);
  oled.print(" R:");
  oled.print(dr, 1);

  oled.drawCircle(cx, cy, r, WHITE);
  oled.drawLine(cx - 4, cy, cx + 4, cy, WHITE);  // centre crosshair
  oled.drawLine(cx, cy - 4, cx, cy + 4, WHITE);

  // 1 pixel per degree, clamped to stay inside the circle
  int bx = cx + constrain((int)dr, -(r - 5), r - 5);
  int by = cy + constrain((int)dp, -(r - 5), r - 5);
  oled.fillCircle(bx, by, 5, WHITE);

  if (abs(dp) < 2 && abs(dr) < 2) {
    oled.setCursor(96, 56);
    oled.print("LEVEL");
  }
  oled.display();
}

// ---------- original functions ----------

// Sea-level-corrected pressure in inches of mercury. elev is in metres.
float normPress(float elev) {
  float BPRaw;
  float BPNorm;
  float T0 = 288.15;
  float L = .0065;
  float R = 8.3144598;
  float M = .0289644;
  float g = 9.8;
  float c = M * g / (R * L);
  BPRaw = BP.readPressure() * .0002953;
  BPNorm = BPRaw / pow(1 - L * elev / T0, c);
  return BPNorm;
}

void normDataP() {
  for (int i = 0; i < numPoints; i = i + 1) {
    normYdataP[i] = (Pb - Pt) / (yMinP - yMaxP) * (rawYdataP[i] - yMaxP) + Pt;
    normXdata[i] = (Pr - Pl) / (xMax - xMin) * (rawXdata[i] - xMin) + Pl;
  }
}
void normDataT() {
  for (int i = 0; i < numPoints; i = i + 1) {
    normYdataT[i] = (Pb - Pt) / (yMinT - yMaxT) * (rawYdataT[i] - yMaxT) + Pt;
    normXdata[i] = (Pr - Pl) / (xMax - xMin) * (rawXdata[i] - xMin) + Pl;
  }
}
void normDataH() {
  for (int i = 0; i < numPoints; i = i + 1) {
    normYdataH[i] = (Pb - Pt) / (yMinH - yMaxH) * (rawYdataH[i] - yMaxH) + Pt;
    normXdata[i] = (Pr - Pl) / (xMax - xMin) * (rawXdata[i] - xMin) + Pl;
  }
}

void plotPress() {  // plot pressure
  oled.drawLine(Pl, Pt, Pl, Pb, WHITE);
  oled.drawLine(Pl, Pb, Pr, Pb, WHITE);
  int MID = (Pb - Pt) / (yMinP - yMaxP) * (29.92 - yMaxP) + Pt;
  oled.drawLine(Pl, MID, Pr, MID, WHITE);
  for (int i = 0; i < numPoints - 1; i = i + 1) {
    oled.drawLine(normXdata[i], normYdataP[i], normXdata[i + 1], normYdataP[i + 1], WHITE);
  }
}
void plotTemp() {  // plot temperature
  oled.drawLine(Pl, Pt, Pl, Pb, WHITE);
  oled.drawLine(Pl, Pb, Pr, Pb, WHITE);
  for (int i = 0; i < numPoints - 1; i = i + 1) {
    oled.drawLine(normXdata[i], normYdataT[i], normXdata[i + 1], normYdataT[i + 1], WHITE);
  }
}
void plotHum() {  // plot humidity
  oled.drawLine(Pl, Pt, Pl, Pb, WHITE);
  oled.drawLine(Pl, Pb, Pr, Pb, WHITE);
  for (int i = 0; i < numPoints - 1; i = i + 1) {
    oled.drawLine(normXdata[i], normYdataH[i], normXdata[i + 1], normYdataH[i + 1], WHITE);
  }
}
