#include <TFT_eSPI.h>
#include "bitmaps.h"

#include <WiFi.h>
#include <esp_now.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft);

/* =========================
   ESP-NOW SETTINGS
   ========================= */

#define ESPNOW_CHANNEL 1

/* =========================
   COLORS
   ========================= */

#define BG TFT_BLACK
#define WHITE TFT_WHITE
#define ACCENT tft.color565(51, 195, 247)

/* =========================
   STATE
   ========================= */

enum State {
  BOOT,
  FLICKER,
  SWEEP,
  RUN_START,
  RUN
};

volatile State state = BOOT;

/* =========================
   VALUES
   ========================= */

/*
   Latest received values.
*/

volatile float maf = 0.0;
volatile float speed = 0.0;
volatile float rpm = 0.0;
volatile float temp = 0.0;
volatile float fuel = 0.0;

/* =========================
   ANIMATED VALUES
   ========================= */

volatile float mafA = 0.0;
volatile float tempA = 0.0;
volatile float fuelA = 0.0;

/* =========================
   ESP-NOW DATA
   ========================= */

/*
   IMPORTANT:

   This structure must be identical
   to the sender structure.

   Sender:

     float maf;
     float speed;
     float rpm;
     float coolant;
*/

typedef struct {

  float maf;
  float speed;
  float rpm;
  float coolant;

} SensorData;

/*
   Latest received targets.
*/

volatile float mafTarget = 0.0;
volatile float speedTarget = 0.0;
volatile float rpmTarget = 0.0;
volatile float tempTarget = 0.0;

/*
   Becomes true when the first valid
   ESP-NOW packet has arrived.
*/

volatile bool mafReceived = false;

/* =========================
   FUEL CONSUMPTION SETTINGS
   ========================= */

/*
   Assumed stoichiometric
   air/fuel ratio for gasoline.

   14.7 : 1
*/

const float AIR_FUEL_RATIO = 14.7;

/*
   Approximate gasoline density.

   0.745 kg/L = 745 g/L
*/

const float FUEL_DENSITY_KG_PER_L = 0.745;

/*
   Fuel consumption is calculated
   once every five seconds.
*/

const unsigned long FUEL_CALC_INTERVAL = 5000;

/*
   MAF averaging variables.

   The data task samples MAF every
   approximately 20 ms.
*/

float mafSum = 0.0;
unsigned long mafSampleCount = 0;

/*
   Distance accumulation.

   Instead of assuming a fixed speed,
   we integrate the actual received
   vehicle speed over the five-second
   measurement period.
*/

float distanceSumKm = 0.0;
unsigned long speedSampleCount = 0;

/*
   Timer for five-second calculation.
*/

unsigned long lastFuelCalculation = 0;

/* =========================
   SPRITES
   ========================= */

TFT_eSprite logoSpr = TFT_eSprite(&tft);

/* =========================
   ESP-NOW RECEIVE CALLBACK
   ========================= */

void onDataRecv(const esp_now_recv_info_t *info,
                const uint8_t *data,
                int len) {

  /*
     Make sure the packet has exactly
     the size we expect.
  */

  if (len != sizeof(SensorData)) {

    Serial.print("ESP-NOW: Wrong packet size: ");
    Serial.print(len);
    Serial.print(" expected: ");
    Serial.println(sizeof(SensorData));

    return;
  }

  /*
     Copy the received packet into
     a local structure.
  */

  SensorData incoming;

  memcpy(&incoming, data, sizeof(incoming));

  /*
     Store the latest values.

     The render task will use these
     values for the display.
  */

  mafTarget = incoming.maf;
  speedTarget = incoming.speed;
  rpmTarget = incoming.rpm;
  tempTarget = incoming.coolant;

  /*
     Copy the latest values into the
     general variables as well.
  */

  maf = incoming.maf;
  speed = incoming.speed;
  rpm = incoming.rpm;
  temp = incoming.coolant;

  /*
     First valid packet received.
  */

  mafReceived = true;

  /*
     Debug output.
  */

  Serial.print("ESP-NOW RX -> ");

  Serial.print("MAF: ");
  Serial.print(incoming.maf, 2);
  Serial.print(" g/s");

  Serial.print(" | Speed: ");
  Serial.print(incoming.speed, 1);
  Serial.print(" km/h");

  Serial.print(" | RPM: ");
  Serial.print(incoming.rpm, 0);

  Serial.print(" | Coolant: ");
  Serial.print(incoming.coolant, 1);
  Serial.println(" C");
}

/* =========================
   ESP-NOW SETUP
   ========================= */

void setupESPNow() {

  /*
     ESP-NOW uses the WiFi radio.
  */

  WiFi.mode(WIFI_STA);

  /*
     Force the receiver to the same
     channel as the sender.
  */

  WiFi.setChannel(
    ESPNOW_CHANNEL,
    WIFI_SECOND_CHAN_NONE
  );

  delay(100);

  Serial.println();
  Serial.println("================================");
  Serial.println("ESP-NOW RECEIVER");
  Serial.println("================================");

  Serial.print("Receiver MAC: ");
  Serial.println(WiFi.macAddress());

  Serial.print("WiFi channel: ");
  Serial.println(ESPNOW_CHANNEL);

  /*
     Initialize ESP-NOW.
  */

  if (esp_now_init() != ESP_OK) {

    Serial.println("ESP-NOW initialization FAILED!");

    while (true) {
      delay(1000);
    }
  }

  /*
     Register receive callback.
  */

  esp_now_register_recv_cb(onDataRecv);

  Serial.println("ESP-NOW receiver ready");
  Serial.println("Waiting for OBD data...");
  Serial.println();
}

/* =========================
   FUEL CALCULATION
   ========================= */

void calculateFuelConsumption() {

  /*
     We need MAF samples.
  */

  if (mafSampleCount == 0) {

    return;
  }

  /*
     Calculate average MAF over the
     previous five-second period.

     MAF = grams of air / second
  */

  float averageMAF =
    mafSum / (float)mafSampleCount;

  /*
     Calculate average vehicle speed.

     Speed is sampled at approximately
     the same interval as MAF.
  */

  float averageSpeed = 0.0;

  if (speedSampleCount > 0) {

    averageSpeed =
      speedSampleCount > 0
      ? distanceSumKm /
        (FUEL_CALC_INTERVAL / 3600000.0)
      : 0.0;
  }

  /*
     Save the distance before resetting
     the accumulation variables.
  */

  float distanceKm = distanceSumKm;

  /*
     Reset the averaging values
     for the next five-second period.
  */

  mafSum = 0.0;
  mafSampleCount = 0;

  distanceSumKm = 0.0;
  speedSampleCount = 0;

  /*
     Convert air mass to fuel mass.

     Example:

       5 g/s air
       / 14.7
       = 0.340 g/s fuel
  */

  float fuelMassPerSecond =
    averageMAF / AIR_FUEL_RATIO;

  /*
     Convert grams to kilograms.
  */

  float fuelMassKgPerSecond =
    fuelMassPerSecond / 1000.0;

  /*
     Convert fuel mass to litres.

     litres =
       kilograms / kg-per-litre
  */

  float fuelLitresPerSecond =
    fuelMassKgPerSecond /
    FUEL_DENSITY_KG_PER_L;

  /*
     Calculate fuel used during the
     five-second measurement period.
  */

  float fuelLitres =
    fuelLitresPerSecond *
    (FUEL_CALC_INTERVAL / 1000.0);

  /*
     Calculate L/100 km.

     If the car is stationary there is
     no meaningful L/100 km value.

     In that case we keep the previous
     fuel value instead of producing
     infinity or a huge number.
  */

  if (distanceKm > 0.0001) {

    fuel =
      (fuelLitres / distanceKm) * 100.0;
  }

  /*
     Debug output.
  */

  Serial.println();
  Serial.println("------------------------------");

  Serial.print("Average MAF: ");
  Serial.print(averageMAF, 2);
  Serial.println(" g/s");

  Serial.print("Average speed: ");
  Serial.print(averageSpeed, 1);
  Serial.println(" km/h");

  Serial.print("Distance: ");
  Serial.print(distanceKm, 4);
  Serial.println(" km");

  Serial.print("Fuel used: ");
  Serial.print(fuelLitres, 4);
  Serial.println(" L");

  if (distanceKm > 0.0001) {

    Serial.print("Consumption: ");
    Serial.print(fuel, 2);
    Serial.println(" L/100km");

  } else {

    Serial.println("Consumption: vehicle stationary");
  }

  Serial.print("Coolant: ");
  Serial.print(tempTarget, 1);
  Serial.println(" C");

  Serial.print("RPM: ");
  Serial.print(rpmTarget, 0);
  Serial.println(" rpm");

  Serial.println("------------------------------");
}

/* =========================
   RING
   ========================= */

void drawRing(
  int cx,
  int cy,
  int r,
  float v,
  float maxv
) {

  float start = -225;
  float end = 45;

  float p =
    constrain(v / maxv, 0.0, 1.0);

  float target =
    start + p * (end - start);

  for (
    float a = start;
    a < target;
    a += 2
  ) {

    float rad =
      a * DEG_TO_RAD;

    int x1 =
      cx + cos(rad) * r;

    int y1 =
      cy + sin(rad) * r;

    int x2 =
      cx + cos(rad) * (r - 8);

    int y2 =
      cy + sin(rad) * (r - 8);

    spr.drawLine(
      x1,
      y1,
      x2,
      y2,
      ACCENT
    );
  }
}

/* =========================
   ICONS
   ========================= */

void drawIcons() {

  spr.pushImage(
    70,
    180,
    32,
    32,
    (uint16_t*)MAFIcon,
    TFT_BLACK
  );

  spr.pushImage(
    105,
    180,
    32,
    32,
    (uint16_t*)tempIcon,
    TFT_BLACK
  );

  spr.pushImage(
    140,
    180,
    32,
    32,
    (uint16_t*)fuelIcon,
    TFT_BLACK
  );
}

/* =========================
   UI
   ========================= */

void drawUI() {

  spr.fillSprite(BG);

  int cx = 120;
  int cy = 120;

  /*
     MAF ring
  */

  drawRing(
    cx,
    cy,
    105,
    mafA,
    20
  );

  /*
     Coolant temperature ring
  */

  drawRing(
    cx,
    cy,
    85,
    tempA,
    130
  );

  /*
     Fuel consumption ring
  */

  drawRing(
    cx,
    cy,
    65,
    fuelA,
    20
  );

  /*
     Main values.
  */

  spr.setTextDatum(MC_DATUM);

  spr.setTextColor(
    WHITE,
    BG
  );

  spr.setTextFont(4);

  /*
     MAF
  */

  spr.drawString(
    String(mafA, 1) + " g/s",
    cx,
    cy - 20
  );

  /*
     Coolant temperature
  */

  spr.drawString(
    String(tempA, 0) + " °C",
    cx,
    cy + 10
  );

  /*
     Fuel consumption
  */

  spr.drawString(
    String(fuelA, 1) + " L/100km",
    cx,
    cy + 40
  );

  /*
     Icons.
  */

  drawIcons();

  /*
     Push complete sprite to TFT.
  */

  spr.pushSprite(
    0,
    0
  );
}

/* =========================
   BOOT LOGO
   ========================= */

void drawBootLogo(uint8_t alpha) {

  logoSpr.fillSprite(TFT_BLACK);

  for (int y = 0; y < 144; y++) {

    for (int x = 0; x < 256; x++) {

      uint16_t c =
        bootLogo[y * 256 + x];

      uint8_t r =
        ((c >> 11) & 0x1F) << 3;

      uint8_t g =
        ((c >> 5) & 0x3F) << 2;

      uint8_t b =
        (c & 0x1F) << 3;

      r =
        (r * alpha) >> 8;

      g =
        (g * alpha) >> 8;

      b =
        (b * alpha) >> 8;

      logoSpr.drawPixel(
        x,
        y,
        tft.color565(
          r,
          g,
          b
        )
      );
    }
  }

  logoSpr.pushSprite(
    -10,
    40
  );
}

/* =========================
   BOOT
   ========================= */

void bootAnimation() {

  tft.fillScreen(BG);

  /*
     Fade in.
  */

  for (
    int a = 0;
    a <= 255;
    a += 8
  ) {

    drawBootLogo(a);

    delay(10);
  }

  /*
     Fade out.
  */

  for (
    int a = 255;
    a >= 0;
    a -= 8
  ) {

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

bool flickerChance(int speedValue) {

  return (millis() / speedValue) % 2;
}

void uiFlicker() {

  spr.fillSprite(BG);

  if (
    millis() - flickerT > 500
  ) {

    flickerStage++;

    flickerT = millis();
  }

  int cx = 120;
  int cy = 120;

  /*
     Icons.
  */

  if (
    flickerStage >= 0 &&
    flickerChance(80)
  ) {

    drawIcons();
  }

  /*
     Rings.
  */

  if (
    flickerStage >= 1 &&
    flickerChance(60)
  ) {

    drawRing(
      cx,
      cy,
      105,
      mafA,
      20
    );

    drawRing(
      cx,
      cy,
      85,
      tempA,
      130
    );

    drawRing(
      cx,
      cy,
      65,
      fuelA,
      20
    );
  }

  /*
     Text.
  */

  if (
    flickerStage >= 2 &&
    flickerChance(40)
  ) {

    spr.setTextDatum(MC_DATUM);

    spr.setTextColor(
      WHITE,
      BG
    );

    spr.setTextFont(4);

    spr.drawString(
      String(mafA, 1) + " g/s",
      cx,
      cy - 20
    );

    spr.drawString(
      String(tempA, 0) + " °C",
      cx,
      cy + 10
    );

    spr.drawString(
      String(fuelA, 1) + " L/100km",
      cx,
      cy + 40
    );
  }

  /*
     Move to sweep.
  */

  if (flickerStage > 3) {

    state = SWEEP;
  }

  spr.pushSprite(
    0,
    0
  );
}

/* =========================
   SWEEP
   ========================= */

unsigned long sweepStart = 0;

float ease(float t) {

  return 0.5 -
         0.5 * cos(PI * t);
}

void sweepAnimation() {

  if (sweepStart == 0) {

    sweepStart = millis();
  }

  /*
     Three second complete sweep:

       0 → 100 → 0
  */

  float t =
    (millis() - sweepStart) /
    3000.0;

  if (t > 1) {

    t = 1;
  }

  float wave =
    (t < 0.5)
    ? ease(t * 2)
    : ease(
        1 - (t - 0.5) * 2
      );

  float mafMin = 0;
  float mafMax = 20;

  float tempMin = 0;
  float tempMax = 130;

  float fuelMin = 0;
  float fuelMax = 20;

  /*
     MAF sweep.
  */

  mafA =
    mafMin +
    (mafMax - mafMin) * wave;

  /*
     Temperature sweep.
  */

  tempA =
    tempMin +
    (tempMax - tempMin) * wave;

  /*
     Fuel sweep.
  */

  fuelA =
    fuelMin +
    (fuelMax - fuelMin) * wave;

  drawUI();

  if (t >= 1) {

    state = RUN_START;
  }
}

/* =========================
   RUN START
   ========================= */

void runStartAnimation() {

  /*
     Wait until the first real
     ESP-NOW packet arrives.

     The sweep has already finished,
     so the display remains at zero.
  */

  if (!mafReceived) {

    mafA = 0;
    tempA = 0;
    fuelA = 0;

    drawUI();

    return;
  }

  /*
     Startup animation progress.
  */

  static float t = 0;

  t += 0.02;

  if (t > 1) {

    t = 1;
  }

  float e = ease(t);

  /*
     MAF:
       0 → real MAF
  */

  mafA =
    mafTarget * e;

  /*
     Coolant:
       0 → real coolant temperature
  */

  tempA =
    tempTarget * e;

  /*
     Temporary startup consumption.

     Use the actual current vehicle
     speed instead of the old fixed
     50 km/h assumption.
  */

  float startupConsumption = 0.0;

  if (speedTarget > 0.1) {

    /*
       Fuel mass per second.
    */

    float startupFuelMassPerSecond =
      mafTarget /
      AIR_FUEL_RATIO;

    /*
       Convert to kg/s.
    */

    float startupFuelKgPerSecond =
      startupFuelMassPerSecond /
      1000.0;

    /*
       Convert to litres/s.
    */

    float startupFuelLitresPerSecond =
      startupFuelKgPerSecond /
      FUEL_DENSITY_KG_PER_L;

    /*
       L/100 km:

       fuel L/s
       /
       speed km/s
       × 100
    */

    startupConsumption =
      (
        startupFuelLitresPerSecond /
        (speedTarget / 3600.0)
      ) * 100.0;
  }

  /*
     Animate temporary fuel value.
  */

  fuelA =
    startupConsumption * e;

  drawUI();

  if (t >= 1) {

    state = RUN;
  }
}

/* =========================
   DATA TASK
   ========================= */

void dataTask(void *pv) {

  /*
     Start the five-second timer.
  */

  lastFuelCalculation =
    millis();

  while (true) {

    /*
       Keep general values synchronized
       with the latest received packet.
    */

    maf =
      mafTarget;

    speed =
      speedTarget;

    rpm =
      rpmTarget;

    temp =
      tempTarget;

    /*
       Add MAF to the five-second
       averaging period.
    */

    if (mafReceived) {

      mafSum +=
        mafTarget;

      mafSampleCount++;

      /*
         Integrate actual vehicle speed.

         speed is in km/h.

         20 ms =
           0.020 seconds

         distance =
           speed × time

         Since speed is km/h:

           distance km =
             speed / 3600 × seconds
      */

      const float SAMPLE_TIME_SECONDS =
        0.020;

      distanceSumKm +=
        speedTarget *
        (SAMPLE_TIME_SECONDS / 3600.0);

      speedSampleCount++;
    }

    /*
       Check the five-second timer.
    */

    unsigned long now =
      millis();

    if (
      now - lastFuelCalculation >=
      FUEL_CALC_INTERVAL
    ) {

      lastFuelCalculation =
        now;

      calculateFuelConsumption();
    }

    /*
       20 ms data sampling.
    */

    vTaskDelay(
      20 / portTICK_PERIOD_MS
    );
  }
}

/* =========================
   RENDER TASK
   ========================= */

void renderTask(void *pv) {

  /*
     Run boot animation first.
  */

  bootAnimation();

  /*
     Start flicker sequence.
  */

  state = FLICKER;

  while (true) {

    if (state == FLICKER) {

      uiFlicker();
    }

    else if (state == SWEEP) {

      sweepAnimation();
    }

    else if (state == RUN_START) {

      runStartAnimation();
    }

    else if (state == RUN) {

      /*
         Smooth MAF display toward
         the latest received value.
      */

      mafA +=
        (mafTarget - mafA) *
        0.08;


      /*
         Smooth fuel display toward
         the latest calculated
         consumption.
      */

      fuelA +=
        (fuel - fuelA) *
        0.08;


      /*
         Smooth coolant temperature
         toward the actual OBD value.
      */

      tempA +=
        (tempTarget - tempA) *
        0.05;


      /*
         Draw the complete UI.
      */

      drawUI();
    }

    /*
       Give the CPU to the other task.
    */

    vTaskDelay(1);
  }
}

/* =========================
   SETUP
   ========================= */

void setup() {

  Serial.begin(115200);

  /*
     TFT
  */

  tft.init();

  tft.setRotation(0);

  tft.setSwapBytes(true);

  /*
     Main UI sprite
  */

  spr.createSprite(
    240,
    240
  );

  /*
     Boot logo sprite
  */

  logoSpr.setColorDepth(16);

  logoSpr.createSprite(
    256,
    144
  );

  /*
     ESP-NOW
  */

  setupESPNow();

  /*
     Render task
  */

  xTaskCreatePinnedToCore(
    renderTask,
    "Render",
    12000,
    NULL,
    1,
    NULL,
    0
  );

  /*
     Data task
  */

  xTaskCreatePinnedToCore(
    dataTask,
    "Data",
    8000,
    NULL,
    1,
    NULL,
    1
  );
}

/* =========================
   LOOP
   ========================= */

void loop() {

  /*
     Everything is handled by
     the FreeRTOS tasks.
  */

  vTaskDelay(
    1000 / portTICK_PERIOD_MS
  );
}