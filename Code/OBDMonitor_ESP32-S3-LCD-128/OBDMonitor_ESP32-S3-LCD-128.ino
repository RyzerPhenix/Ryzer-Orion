#include <TFT_eSPI.h>
#include "bitmaps.h"
//#include <WiFi.h>
//#include <WiFiClient.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft);

/* =========================
   COLORS
   ========================= */

#define BG TFT_BLACK
#define WHITE TFT_WHITE
#define ACCENT tft.color565(51, 195, 247)

/* =========================
   STATE
   ========================= */

enum State { BOOT, FLICKER, SWEEP, RUN_START, RUN };
volatile State state = BOOT;

/* =========================
   VALUES (live simulated)
   ========================= */

volatile float maf = 3.4;
volatile float temp = 92;
volatile float fuel = 12.8;

/* animated */
volatile float mafA = 0;
volatile float tempA = 0;
volatile float fuelA = 0;

/* =========================
   SPRITES
   ========================= */

TFT_eSprite logoSpr = TFT_eSprite(&tft);

/* =========================
   RING
   ========================= */

void drawRing(int cx, int cy, int r, float v, float maxv) {

  float start = -225;
  float end = 45;

  float p = constrain(v / maxv, 0.0, 1.0);
  float target = start + p * (end - start);

  for (float a = start; a < target; a += 2) {

    float rad = a * DEG_TO_RAD;

    int x1 = cx + cos(rad) * r;
    int y1 = cy + sin(rad) * r;

    int x2 = cx + cos(rad) * (r - 8);
    int y2 = cy + sin(rad) * (r - 8);

    spr.drawLine(x1, y1, x2, y2, ACCENT);
  }
}

/* =========================
   ICONS
   ========================= */

void drawIcons() {
  spr.pushImage(70,  180, 32, 32, (uint16_t*)MAFIcon, TFT_BLACK);
  spr.pushImage(105, 180, 32, 32, (uint16_t*)tempIcon, TFT_BLACK);
  spr.pushImage(140, 180, 32, 32, (uint16_t*)fuelIcon, TFT_BLACK);s
}

/* =========================
   UI
   ========================= */

void drawUI() {

  spr.fillSprite(BG);

  int cx = 120;
  int cy = 120;

  drawRing(cx, cy, 105, mafA, 20);
  drawRing(cx, cy, 85, tempA, 130);
  drawRing(cx, cy, 65, fuelA, 20);

  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(WHITE, BG);
  spr.setTextFont(4);

  spr.drawString(String(mafA, 1) + " g/s", cx, cy - 20);
  spr.drawString(String(tempA, 0) + " °C", cx, cy + 10);
  spr.drawString(String(fuelA, 1) + " L/100km", cx, cy + 40);

  drawIcons();

  spr.pushSprite(0, 0);
}

/* =========================
   BOOT LOGO
   ========================= */

void drawBootLogo(uint8_t alpha) {

  logoSpr.fillSprite(TFT_BLACK);

  for (int y = 0; y < 144; y++) {
    for (int x = 0; x < 256; x++) {

      uint16_t c = bootLogo[y * 256 + x];

      uint8_t r = ((c >> 11) & 0x1F) << 3;
      uint8_t g = ((c >> 5) & 0x3F) << 2;
      uint8_t b = (c & 0x1F) << 3;

      r = (r * alpha) >> 8;
      g = (g * alpha) >> 8;
      b = (b * alpha) >> 8;

      logoSpr.drawPixel(x, y, tft.color565(r, g, b));
    }
  }

  logoSpr.pushSprite(-10, 40);
}

/* =========================
   BOOT
   ========================= */

void bootAnimation() {

  tft.fillScreen(BG);

  for (int a = 0; a <= 255; a += 8) {
    drawBootLogo(a);
    delay(10);
  }

  for (int a = 255; a >= 0; a -= 8) {
    drawBootLogo(a);
    delay(10);
  }

  tft.fillScreen(BG);
  delay(300);
}

/* =========================
   FLICKER
   ========================= */

int flickerStage = 0;
unsigned long flickerT = 0;

bool flickerChance(int speed) {
  return (millis() / speed) % 2;
}

void uiFlicker() {

  spr.fillSprite(BG);

  if (millis() - flickerT > 500) {
    flickerStage++;
    flickerT = millis();
  }

  int cx = 120;
  int cy = 120;

  if (flickerStage >= 0 && flickerChance(80)) drawIcons();

  if (flickerStage >= 1 && flickerChance(60)) {
    drawRing(cx, cy, 105, mafA, 20);
    drawRing(cx, cy, 85, tempA, 130);
    drawRing(cx, cy, 65, fuelA, 20);
  }

  if (flickerStage >= 2 && flickerChance(40)) {
    spr.setTextDatum(MC_DATUM);
    spr.setTextColor(WHITE, BG);
    spr.setTextFont(4);

    spr.drawString(String(mafA, 1) + " g/s", cx, cy - 20);
    spr.drawString(String(tempA, 0) + " °C", cx, cy + 10);
    spr.drawString(String(fuelA, 1) + " L/100km", cx, cy + 40);
  }

  if (flickerStage > 3) state = SWEEP;

  spr.pushSprite(0, 0);
}

/* =========================
   SWEEP
   ========================= */

unsigned long sweepStart = 0;

float ease(float t) {
  return 0.5 - 0.5 * cos(PI * t);
}

void sweepAnimation() {

  if (sweepStart == 0) sweepStart = millis();

  float t = (millis() - sweepStart) / 3000.0;
  if (t > 1) t = 1;

  float wave = (t < 0.5)
    ? ease(t * 2)
    : ease(1 - (t - 0.5) * 2);

  float mafMin = 0, mafMax = 20;
  float tempMin = 0, tempMax = 130;
  float fuelMin = 0, fuelMax = 20;

  mafA  = mafMin  + (mafMax  - mafMin)  * wave;
  tempA = tempMin + (tempMax - tempMin) * wave;
  fuelA = fuelMin + (fuelMax - fuelMin) * wave;

  drawUI();

  if (t >= 1) {
    state = RUN_START;
  }
}

/* =========================
   RUN START (0 → REAL VALUES)
   ========================= */

void runStartAnimation() {

  static float t = 0;
  t += 0.02;
  if (t > 1) t = 1;

  float e = ease(t);

  mafA  = maf  * e;
  tempA = temp * e;
  fuelA = fuel * e;

  drawUI();

  if (t >= 1) {
    state = RUN;
  }
}

/* =========================
   DATA TASK
   ========================= */

void dataTask(void *pv) {

  while (true) {

    // smooth simulation (NOT jittery anymore)
    maf += sin(millis() * 0.001) * 0.01;
    temp += cos(millis() * 0.0012) * 0.02;
    fuel = maf * 2.7;

    vTaskDelay(20 / portTICK_PERIOD_MS);
  }
}

/* =========================
   RENDER TASK
   ========================= */

void renderTask(void *pv) {

  bootAnimation();
  state = FLICKER;

  while (true) {

    if (state == FLICKER) uiFlicker();
    else if (state == SWEEP) sweepAnimation();
    else if (state == RUN_START) runStartAnimation();
    else if (state == RUN) drawUI();

    vTaskDelay(1);
  }
}

/* =========================
   SETUP
   ========================= */

void setup() {

  tft.init();
  tft.setRotation(0);
  tft.setSwapBytes(true);

  spr.createSprite(240, 240);

  logoSpr.setColorDepth(16);
  logoSpr.createSprite(256, 144);

  xTaskCreatePinnedToCore(renderTask, "Render", 12000, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(dataTask, "Data", 8000, NULL, 1, NULL, 1);
}

/* =========================
   LOOP
   ========================= */

void loop() {
  vTaskDelay(1000);
}