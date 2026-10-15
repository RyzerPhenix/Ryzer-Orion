#include <esp32_can.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_sleep.h>

/* =========================================================
   SETTINGS
   ========================================================= */

/*
   ESP-NOW channel.

   MUST match the TFT receiver.
*/

#define ESPNOW_CHANNEL 1


/*
   TFT RECEIVER MAC ADDRESS
*/

uint8_t receiverMac[] = {
  0x84,
  0xFC,
  0xE6,
  0x51,
  0x87,
  0x10
};


/* =========================================================
   BATTERY VOLTAGE / AUTOMATIC DEEP SLEEP
   MrDIY v1.3b: GPIO32 <- R11 1M / R12 270k divider.
   These are STARTING thresholds, NOT verified ignition states.
   Measure against a multimeter before relying on this feature.
   ========================================================= */
#define BATTERY_ADC_PIN 32
const float BATTERY_DIVIDER_MULTIPLIER = (1000000.0f + 270000.0f) / 270000.0f;
const float BATTERY_CALIBRATION = 1.000f; // Adjust against multimeter
const float ENGINE_RUNNING_VOLTAGE = 13.40f; // Charging detected
const float ENGINE_OFF_VOLTAGE = 13.00f;     // Sustained low voltage -> sleep
const unsigned long VOLTAGE_CHECK_INTERVAL_MS = 2000;
const unsigned long LOW_VOLTAGE_DELAY_MS = 90000;
const uint64_t SLEEP_INTERVAL_US = 30ULL * 1000000ULL;

unsigned long lastVoltageCheck = 0;
unsigned long lowVoltageSince = 0;
bool haveLowVoltageTimer = false;
bool chargingDetected = false;

float readBatteryVoltage() {
  // ADC1 works while the ESP32 WiFi radio is running.
  // Use calibrated Arduino core millivolt conversion; still verify externally.
  uint32_t totalMv = 0;
  for (int i = 0; i < 16; ++i) {
    totalMv += analogReadMilliVolts(BATTERY_ADC_PIN);
    delay(2);
  }
  float adcVoltage = (totalMv / 16.0f) / 1000.0f;
  return adcVoltage * BATTERY_DIVIDER_MULTIPLIER * BATTERY_CALIBRATION;
}

void enterBatterySleep(float measuredVoltage) {
  Serial.printf("[SLEEP] Battery %.2f V. Sleeping for 30 seconds.\n", measuredVoltage);
  Serial.println("[SLEEP] ESP-NOW and CAN polling stop until timer wake.");
  Serial.flush();
  esp_now_deinit();
  WiFi.mode(WIFI_OFF);
  esp_sleep_enable_timer_wakeup(SLEEP_INTERVAL_US);
  esp_deep_sleep_start();
}

void checkBatteryDuringOperation(unsigned long now) {
  if (now - lastVoltageCheck < VOLTAGE_CHECK_INTERVAL_MS) return;
  lastVoltageCheck = now;
  float v = readBatteryVoltage();
  Serial.printf("[BATTERY] %.2f V | Charging threshold %.2f V | Sleep threshold %.2f V\n",
                v, ENGINE_RUNNING_VOLTAGE, ENGINE_OFF_VOLTAGE);

  // Hysteresis: voltages in the middle keep the previous state/timer.
  if (v >= ENGINE_RUNNING_VOLTAGE) {
    chargingDetected = true;
    haveLowVoltageTimer = false;
    Serial.println("[POWER] Charging voltage detected. Stay awake.");
  } else if (v < ENGINE_OFF_VOLTAGE) {
    if (!haveLowVoltageTimer) {
      lowVoltageSince = now;
      haveLowVoltageTimer = true;
      Serial.println("[POWER] Low battery voltage. Starting 90-second countdown.");
    } else {
      unsigned long elapsed = now - lowVoltageSince;
      Serial.printf("[POWER] Low voltage for %lu / %lu seconds.\n",
                    elapsed / 1000UL, LOW_VOLTAGE_DELAY_MS / 1000UL);
      if (elapsed >= LOW_VOLTAGE_DELAY_MS) enterBatterySleep(v);
    }
  } else {
    Serial.println("[POWER] Between thresholds; retaining previous state.");
  }
}

/* LIVE CAN/OBD MODE: no synthetic/test readings are transmitted. */

/*
   MrDIY CAN Shield v1.3+

   RX = GPIO 4
   TX = GPIO 5

   IMPORTANT:

   Shield v1.0 / v1.1 / v1.2:

   RX = GPIO 5
   TX = GPIO 4
*/

#define CAN_RX_PIN GPIO_NUM_4
#define CAN_TX_PIN GPIO_NUM_5


/*
   Standard OBD-II CAN speed.
*/

#define CAN_SPEED 500000


/*
   Standard functional OBD-II request address.
*/

#define OBD_REQUEST_ID 0x7DF


/*
   Standard OBD-II response range:

   0x7E8 - 0x7EF
*/

#define OBD_RESPONSE_MIN 0x7E8
#define OBD_RESPONSE_MAX 0x7EF


/* =========================================================
   OBD PID DEFINITIONS
   ========================================================= */

#define PID_COOLANT 0x05
#define PID_RPM 0x0C
#define PID_SPEED 0x0D
#define PID_MAF 0x10


/* =========================================================
   SENSOR VALUES
   ========================================================= */

/*
   These are the REAL values received from the car.
*/

float coolant = 0.0;
float rpm = 0.0;
float speed = 0.0;
float maf = 0.0;


/* =========================================================
   ESP-NOW DATA STRUCTURE
   ========================================================= */

/*
   IMPORTANT:

   The receiver must use the EXACT SAME structure
   and variable order.

   4 floats = 16 bytes
*/

typedef struct {

  float maf;
  float speed;
  float rpm;
  float coolant;

} SensorData;


SensorData sensorData;


/* =========================================================
   TIMING
   ========================================================= */

unsigned long lastOBDRequest = 0;

const unsigned long OBD_REQUEST_INTERVAL = 100;

/* =========================================================
   LIVE TRANSMISSION TIMING AND DATA FRESHNESS
   ========================================================= */
const unsigned long ESPNOW_SEND_INTERVAL = 500;
const unsigned long OBD_STALE_TIMEOUT = 3000;
const unsigned long STATUS_INTERVAL = 5000;
unsigned long lastESPNowSend = 0;
unsigned long lastStatus = 0;
unsigned long coolantUpdated = 0;
unsigned long rpmUpdated = 0;
unsigned long speedUpdated = 0;
unsigned long mafUpdated = 0;
unsigned long obdResponseCount = 0;

/*
   PIDs are requested one after another:

   Coolant
   RPM
   Speed
   MAF
*/

const uint8_t pidList[] = {

  PID_COOLANT,
  PID_RPM,
  PID_SPEED,
  PID_MAF

};

const uint8_t PID_COUNT =
  sizeof(pidList) / sizeof(pidList[0]);

uint8_t currentPIDIndex = 0;


/* =========================================================
   VALID DATA FLAGS
   ========================================================= */

/*
   These tell us whether the ECU has actually returned
   each value at least once.
*/

bool coolantValid = false;
bool rpmValid = false;
bool speedValid = false;
bool mafValid = false;


/* =========================================================
   ESP-NOW SEND CALLBACK
   ========================================================= */

void onDataSent(
  const wifi_tx_info_t *info,
  esp_now_send_status_t status) {

  if (status == ESP_NOW_SEND_SUCCESS) {

    Serial.println(
      "ESP-NOW: DELIVERY OK");

  } else {

    Serial.println(
      "ESP-NOW: DELIVERY FAILED");
  }
}


/* =========================================================
   SEND SENSOR DATA VIA ESP-NOW
   ========================================================= */

void sendSensorData() {

  /* ########################################################
     ACTUAL LIVE CAN -> ESP-NOW DATA TRANSFER

     These values are populated exclusively by processOBDResponse()
     after decoding actual ECU replies. NO TEST DATA IS USED.

     The loop calls this function only when ALL four values
     have been received recently (within OBD_STALE_TIMEOUT).
     ######################################################## */
  sensorData.maf = maf;
  sensorData.speed = speed;
  sensorData.rpm = rpm;
  sensorData.coolant = coolant;

  /*
     =====================================================
     ACTUAL ESP-NOW SEND COMMAND
     =====================================================

     THIS LINE ACTUALLY TRANSMITS THE DATA.
  */

  esp_err_t result = esp_now_send(
    receiverMac,
    (uint8_t *)&sensorData,
    sizeof(sensorData));


  /*
     Print exactly what we attempted to send.
  */

  Serial.println();
  Serial.println(
    "================================");

  Serial.println("ESP-NOW SEND: LIVE CAN/OBD DATA");

  Serial.println(
    "================================");


  Serial.print("MAF:     ");

  Serial.print(
    sensorData.maf,
    2);

  Serial.println(" g/s");


  Serial.print("Speed:   ");

  Serial.print(
    sensorData.speed,
    0);

  Serial.println(" km/h");


  Serial.print("RPM:     ");

  Serial.print(
    sensorData.rpm,
    0);

  Serial.println(" rpm");


  Serial.print("Coolant: ");

  Serial.print(
    sensorData.coolant,
    1);

  Serial.println(" C");


  Serial.println(
    "--------------------------------");


  if (result == ESP_OK) {

    Serial.println(
      "ESP-NOW packet queued.");

  } else {

    Serial.print(
      "ESP-NOW send error: ");

    Serial.println(
      result);
  }


  Serial.println(
    "================================");

  Serial.println();
}


/* =========================================================
   SEND OBD REQUEST
   ========================================================= */

void requestPID(uint8_t pid) {

  CAN_FRAME frame;


  /*
     Clear complete CAN frame.
  */

  memset(
    &frame,
    0,
    sizeof(frame));


  /*
     Standard 11-bit OBD-II request.
  */

  frame.id = OBD_REQUEST_ID;

  frame.extended = false;

  frame.rtr = 0;

  frame.length = 8;


  /*
     ISO 15765-4 single-frame request.

     Byte 0 = number of following bytes
     Byte 1 = service 01 (current data)
     Byte 2 = requested PID
  */

  frame.data.byte[0] = 0x02;
  frame.data.byte[1] = 0x01;
  frame.data.byte[2] = pid;

  frame.data.byte[3] = 0x00;
  frame.data.byte[4] = 0x00;
  frame.data.byte[5] = 0x00;
  frame.data.byte[6] = 0x00;
  frame.data.byte[7] = 0x00;


  /*
     Send request.
  */

  CAN0.sendFrame(frame);


  Serial.print("OBD Request: 01 ");

  if (pid < 0x10) {

    Serial.print("0");
  }

  Serial.println(
    pid,
    HEX);
}


/* =========================================================
   CHECK IF FRAME IS AN OBD RESPONSE
   ========================================================= */

bool isOBDResponse(CAN_FRAME &frame) {

  /*
     Standard ECU OBD response IDs
     are 0x7E8 through 0x7EF.
  */

  if (
    frame.id < OBD_RESPONSE_MIN || frame.id > OBD_RESPONSE_MAX) {

    return false;
  }


  /*
     At least four bytes are needed.
  */

  if (frame.length < 4) {

    return false;
  }


  /*
     Positive response to Service 01
     is Service 41.
  */

  if (frame.data.byte[1] != 0x41) {

    return false;
  }


  return true;
}


/* =========================================================
   PROCESS OBD RESPONSE
   ========================================================= */

void processOBDResponse(CAN_FRAME &frame) {

  if (!isOBDResponse(frame)) {

    return;
  }


  uint8_t pid =
    frame.data.byte[2];


  /*
     =====================================================
     COOLANT TEMPERATURE
     PID 05

     Temperature = A - 40

     Unit: C
     =====================================================
  */

  if (pid == PID_COOLANT) {

    uint8_t A =
      frame.data.byte[3];


    coolant =
      (float)A - 40.0;


    coolantValid = true;
    coolantUpdated = millis();
    obdResponseCount++;


    Serial.print("Coolant: ");

    Serial.print(
      coolant,
      1);

    Serial.println(" C");
  }


  /*
     =====================================================
     ENGINE RPM
     PID 0C

     RPM = ((A * 256) + B) / 4
     =====================================================
  */

  else if (pid == PID_RPM) {

    if (frame.length < 5) {

      return;
    }


    uint8_t A =
      frame.data.byte[3];

    uint8_t B =
      frame.data.byte[4];


    uint16_t raw =
      ((uint16_t)A << 8) | B;


    rpm =
      raw / 4.0;


    rpmValid = true;
    rpmUpdated = millis();
    obdResponseCount++;


    Serial.print("RPM: ");

    Serial.println(
      rpm,
      0);
  }


  /*
     =====================================================
     VEHICLE SPEED
     PID 0D

     Speed = A

     Unit: km/h
     =====================================================
  */

  else if (pid == PID_SPEED) {

    uint8_t A =
      frame.data.byte[3];


    speed =
      (float)A;


    speedValid = true;
    speedUpdated = millis();
    obdResponseCount++;


    Serial.print("Speed: ");

    Serial.print(
      speed,
      0);

    Serial.println(" km/h");
  }


  /*
     =====================================================
     MASS AIR FLOW
     PID 10

     MAF = ((A * 256) + B) / 100

     Unit: g/s
     =====================================================
  */

  else if (pid == PID_MAF) {

    if (frame.length < 5) {

      return;
    }


    uint8_t A =
      frame.data.byte[3];

    uint8_t B =
      frame.data.byte[4];


    uint16_t raw =
      ((uint16_t)A << 8) | B;


    maf =
      raw / 100.0;


    mafValid = true;
    mafUpdated = millis();
    obdResponseCount++;


    Serial.print("MAF: ");

    Serial.print(
      maf,
      2);

    Serial.println(" g/s");


    // Transmission is handled periodically in loop(), not here.
    // This avoids sending incomplete or stale groups of values.
  }
}


/* =========================================================
   PRINT ALL REAL CAN VALUES
   ========================================================= */

void printAllValues() {

  Serial.println();
  Serial.println(
    "================================");

  Serial.println(
    "REAL OBD-II VALUES");

  Serial.println(
    "================================");


  Serial.print("Coolant: ");

  Serial.print(
    coolant,
    1);

  Serial.println(" C");


  Serial.print("RPM:     ");

  Serial.print(
    rpm,
    0);

  Serial.println(" rpm");


  Serial.print("Speed:   ");

  Serial.print(
    speed,
    0);

  Serial.println(" km/h");


  Serial.print("MAF:     ");

  Serial.print(
    maf,
    2);

  Serial.println(" g/s");


  Serial.println(
    "================================");

  Serial.println();
}


/* =========================================================
   SETUP
   ========================================================= */

void setup() {

  Serial.begin(
    115200);

  delay(2000);

  // Initialize voltage measurement BEFORE initializing WiFi and CAN.
  pinMode(BATTERY_ADC_PIN, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db);
  float bootVoltage = readBatteryVoltage();
  Serial.printf("[BATTERY] Boot voltage: %.2f V (ADC GPIO32)\n", bootVoltage);
  esp_sleep_wakeup_cause_t wakeReason = esp_sleep_get_wakeup_cause();
  if (wakeReason == ESP_SLEEP_WAKEUP_TIMER) {
    Serial.println("[WAKE] Timer wakeup. Checking battery before WiFi/CAN startup.");
    if (bootVoltage < ENGINE_RUNNING_VOLTAGE) {
      Serial.println("[WAKE] No charging voltage detected; back to sleep.");
      Serial.println("[WAKE] Note: ignition ON without alternator charging is not detectable.");
      Serial.flush();
      esp_sleep_enable_timer_wakeup(SLEEP_INTERVAL_US);
      esp_deep_sleep_start();
    }
    Serial.println("[WAKE] Charging voltage detected. Starting full operation.");
  } else {
    Serial.println("[BOOT] Power-on/reset. Starting normal operation with 90-second low-voltage grace period.");
  }
  lastVoltageCheck = millis();
  if (bootVoltage < ENGINE_OFF_VOLTAGE) {
    lowVoltageSince = millis();
    haveLowVoltageTimer = true;
  }
  Serial.println();

  Serial.println(
    "================================");

  Serial.println(
    "ESP32 OBD-II -> ESP-NOW");

  Serial.println(
    "MrDIY CAN Shield");

  Serial.println(
    "================================");

  Serial.println();


  /*
     =====================================================
     WIFI / ESP-NOW INITIALIZATION
     =====================================================
  */

  Serial.println(
    "Initializing ESP-NOW...");


  WiFi.mode(
    WIFI_STA);


  /*
     Force WiFi / ESP-NOW onto the same channel
     as the TFT receiver.
  */

  WiFi.setChannel(
    ESPNOW_CHANNEL,
    WIFI_SECOND_CHAN_NONE);


  delay(
    100);


  Serial.print(
    "Sender MAC: ");

  Serial.println(
    WiFi.macAddress());


  Serial.print(
    "ESP-NOW channel: ");

  Serial.println(
    ESPNOW_CHANNEL);


  /*
     Initialize ESP-NOW.
  */

  if (
    esp_now_init() != ESP_OK) {

    Serial.println(
      "ESP-NOW initialization FAILED!");

    while (true) {

      delay(
        1000);
    }
  }


  /*
     Register send callback.
  */

  esp_now_register_send_cb(
    onDataSent);


  /*
     Add TFT receiver as ESP-NOW peer.
  */

  esp_now_peer_info_t peerInfo = {};


  memcpy(
    peerInfo.peer_addr,
    receiverMac,
    6);


  peerInfo.channel =
    ESPNOW_CHANNEL;


  peerInfo.ifidx =
    WIFI_IF_STA;


  peerInfo.encrypt =
    false;


  esp_err_t peerResult =
    esp_now_add_peer(
      &peerInfo);


  if (
    peerResult != ESP_OK) {

    Serial.print(
      "Failed to add ESP-NOW peer: ");

    Serial.println(
      peerResult);


    while (true) {

      delay(
        1000);
    }
  }


  Serial.println(
    "ESP-NOW initialized.");

  Serial.println();


  Serial.println("ESP-NOW LIVE CAN MODE ACTIVE (NO TEST DATA)");
  Serial.println("Transmit every 500 ms when ALL PIDs are fresh.");
  Serial.println("Freshness timeout: 3000 ms per PID.");
  Serial.println();

  /*
     =====================================================
     CAN INITIALIZATION
     =====================================================
  */

  Serial.println(
    "Initializing CAN...");


  CAN0.setCANPins(
    CAN_RX_PIN,
    CAN_TX_PIN);


  CAN0.begin(
    CAN_SPEED);


  CAN0.watchFor();


  Serial.println(
    "CAN initialized at 500 kbit/s.");

  Serial.println();


  Serial.println(
    "Requested OBD-II PIDs:");

  Serial.println(
    "01 05 = Coolant Temperature");

  Serial.println(
    "01 0C = Engine RPM");

  Serial.println(
    "01 0D = Vehicle Speed");

  Serial.println(
    "01 10 = Mass Air Flow");

  Serial.println();

  Serial.println(
    "Waiting for ECU...");

  Serial.println();
}


/* =========================================================
   LOOP
   ========================================================= */

void loop() {

  /*
     =====================================================
     READ CAN FRAMES
     =====================================================
  */

  CAN_FRAME canMessage;


  while (
    CAN0.read(canMessage)) {

    processOBDResponse(
      canMessage);
  }


  /*
     =====================================================
     REQUEST NEXT PID
     =====================================================
  */

  unsigned long now =
    millis();

  checkBatteryDuringOperation(now);

  if (
    now - lastOBDRequest >= OBD_REQUEST_INTERVAL) {

    lastOBDRequest =
      now;


    uint8_t requestedPID =
      pidList[currentPIDIndex];


    requestPID(
      requestedPID);


    currentPIDIndex++;


    /*
       After all four requests have been made,
       print the REAL CAN values.

       Note that ESP-NOW test mode does NOT affect
       these values.
    */

    if (
      currentPIDIndex >= PID_COUNT) {

      currentPIDIndex = 0;

      printAllValues();
    }
  }


  /* =========================================================
     LIVE SEND: require every PID to have arrived recently.
     Keep reporting why transmission is suppressed.
     ========================================================= */
  if (now - lastESPNowSend >= ESPNOW_SEND_INTERVAL) {
    lastESPNowSend = now;
    bool allValid = coolantValid && rpmValid && speedValid && mafValid;
    bool allFresh = allValid &&
      (now - coolantUpdated <= OBD_STALE_TIMEOUT) &&
      (now - rpmUpdated <= OBD_STALE_TIMEOUT) &&
      (now - speedUpdated <= OBD_STALE_TIMEOUT) &&
      (now - mafUpdated <= OBD_STALE_TIMEOUT);

    if (allFresh) {
      sendSensorData();
    }
  }

  if (now - lastStatus >= STATUS_INTERVAL) {
    lastStatus = now;
    Serial.println("[CAN STATUS] ECU OBD responses received: " + String(obdResponseCount));
    Serial.printf("[CAN STATUS] Coolant: %s | RPM: %s | Speed: %s | MAF: %s\n",
      !coolantValid ? "MISSING" : ((now - coolantUpdated > OBD_STALE_TIMEOUT) ? "STALE" : "OK"),
      !rpmValid ? "MISSING" : ((now - rpmUpdated > OBD_STALE_TIMEOUT) ? "STALE" : "OK"),
      !speedValid ? "MISSING" : ((now - speedUpdated > OBD_STALE_TIMEOUT) ? "STALE" : "OK"),
      !mafValid ? "MISSING" : ((now - mafUpdated > OBD_STALE_TIMEOUT) ? "STALE" : "OK"));
    Serial.println("[ESP-NOW STATUS] No packet sent unless all four PIDs are fresh.");
  }

  delay(1);
}
