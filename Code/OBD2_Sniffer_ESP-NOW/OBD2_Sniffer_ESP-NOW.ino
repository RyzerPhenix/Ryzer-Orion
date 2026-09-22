#include <esp32_can.h>
#include <esp_now.h>
#include <WiFi.h>

/* =========================================================
   SETTINGS
   ========================================================= */

/*
   ESP-NOW channel.

   Must match the TFT receiver.
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
   Standard OBD-II request address.
*/

#define OBD_REQUEST_ID 0x7DF


/*
   Standard OBD-II response range.

   Usually:

   0x7E8
   0x7E9
   ...
   0x7EF
*/

#define OBD_RESPONSE_MIN 0x7E8
#define OBD_RESPONSE_MAX 0x7EF


/* =========================================================
   ESP-NOW DATA
   ========================================================= */

/*
   All values required by the display.

   maf:
      grams / second

   speed:
      km/h

   rpm:
      engine RPM

   coolant:
      coolant temperature in °C
*/

typedef struct {

  float maf;

  float speed;

  float rpm;

  float coolant;

} SensorData;


SensorData sensorData;


/* =========================================================
   OBD PID DEFINITIONS
   ========================================================= */

#define PID_COOLANT 0x05
#define PID_RPM     0x0C
#define PID_SPEED   0x0D
#define PID_MAF     0x10


/* =========================================================
   TIMING
   ========================================================= */

unsigned long lastOBDRequest = 0;

const unsigned long OBD_REQUEST_INTERVAL = 100;


/*
   We request the following:

   coolant
   RPM
   speed
   MAF

   one after another.
*/

uint8_t currentPIDIndex = 0;

const uint8_t pidList[] = {

  PID_COOLANT,
  PID_RPM,
  PID_SPEED,
  PID_MAF

};

const uint8_t PID_COUNT =
  sizeof(pidList) / sizeof(pidList[0]);


/* =========================================================
   VALID DATA FLAGS
   ========================================================= */

bool coolantValid = false;
bool rpmValid = false;
bool speedValid = false;
bool mafValid = false;


/* =========================================================
   ESP-NOW SEND CALLBACK
   ========================================================= */

void onDataSent(
  const wifi_tx_info_t *info,
  esp_now_send_status_t status
) {

  if (status == ESP_NOW_SEND_SUCCESS) {

    Serial.println(
      "ESP-NOW: OK"
    );

  }
  else {

    Serial.println(
      "ESP-NOW: FAILED"
    );
  }
}


/* =========================================================
   SEND SENSOR DATA
   ========================================================= */

void sendSensorData() {

  esp_err_t result = esp_now_send(
    receiverMac,
    (uint8_t *)&sensorData,
    sizeof(sensorData)
  );


  Serial.println();
  Serial.println(
    "=============================="
  );

  Serial.println(
    "ESP-NOW SENSOR DATA"
  );

  Serial.print(
    "MAF: "
  );

  Serial.print(
    sensorData.maf,
    2
  );

  Serial.println(
    " g/s"
  );


  Serial.print(
    "Speed: "
  );

  Serial.print(
    sensorData.speed,
    0
  );

  Serial.println(
    " km/h"
  );


  Serial.print(
    "RPM: "
  );

  Serial.print(
    sensorData.rpm,
    0
  );

  Serial.println(
    " rpm"
  );


  Serial.print(
    "Coolant: "
  );

  Serial.print(
    sensorData.coolant,
    0
  );

  Serial.println(
    " C"
  );


  if (result == ESP_OK) {

    Serial.println(
      "ESP-NOW packet queued"
    );

  }
  else {

    Serial.print(
      "ESP-NOW send error: "
    );

    Serial.println(
      result
    );
  }

  Serial.println(
    "=============================="
  );
}


/* =========================================================
   SEND OBD REQUEST
   ========================================================= */

void requestPID(
  uint8_t pid
) {

  CAN_FRAME frame;

  /*
     Clear frame.
  */

  memset(
    &frame,
    0,
    sizeof(frame)
  );


  /*
     Standard 11-bit OBD request.
  */

  frame.id = OBD_REQUEST_ID;

  frame.extended = false;

  frame.rtr = 0;

  frame.length = 8;


  /*
     ISO 15765-4 single-frame request:

     Byte 0:
       Number of following bytes = 2

     Byte 1:
       Service = 01
       Show current data

     Byte 2:
       PID
  */

  frame.data.byte[0] = 0x02;

  frame.data.byte[1] = 0x01;

  frame.data.byte[2] = pid;


  /*
     Remaining bytes are zero.
  */

  frame.data.byte[3] = 0x00;
  frame.data.byte[4] = 0x00;
  frame.data.byte[5] = 0x00;
  frame.data.byte[6] = 0x00;
  frame.data.byte[7] = 0x00;


  /*
     Send CAN frame.
  */

  CAN0.sendFrame(
    frame
  );


  Serial.print(
    "OBD request PID 0x"
  );

  if (pid < 0x10) {

    Serial.print("0");
  }

  Serial.println(
    pid,
    HEX
  );
}


/* =========================================================
   CHECK OBD RESPONSE
   ========================================================= */

bool isOBDResponse(
  CAN_FRAME &frame
) {

  /*
     We only care about the standard
     diagnostic response IDs.
  */

  if (
    frame.id < OBD_RESPONSE_MIN ||
    frame.id > OBD_RESPONSE_MAX
  ) {

    return false;
  }


  /*
     We need at least 4 bytes:

     Byte 0 = length
     Byte 1 = service
     Byte 2 = PID
     Byte 3 = data
  */

  if (frame.length < 4) {

    return false;
  }


  /*
     Positive response to service 01
     is service 41.
  */

  if (frame.data.byte[1] != 0x41) {

    return false;
  }


  return true;
}


/* =========================================================
   PROCESS OBD RESPONSE
   ========================================================= */

void processOBDResponse(
  CAN_FRAME &frame
) {

  if (!isOBDResponse(frame)) {

    return;
  }


  uint8_t pid =
    frame.data.byte[2];


  /*
     ==============================
     COOLANT TEMPERATURE
     PID 05
     ==============================
  */

  if (pid == PID_COOLANT) {

    uint8_t A =
      frame.data.byte[3];

    sensorData.coolant =
      (float)A - 40.0;

    coolantValid = true;


    Serial.print(
      "COOLANT: "
    );

    Serial.print(
      sensorData.coolant,
      1
    );

    Serial.println(
      " C"
    );
  }


  /*
     ==============================
     ENGINE RPM
     PID 0C
     ==============================
  */

  else if (pid == PID_RPM) {

    uint8_t A =
      frame.data.byte[3];

    uint8_t B =
      frame.data.byte[4];


    uint16_t raw =
      ((uint16_t)A << 8) |
      B;


    sensorData.rpm =
      raw / 4.0;


    rpmValid = true;


    Serial.print(
      "RPM: "
    );

    Serial.println(
      sensorData.rpm,
      0
    );
  }


  /*
     ==============================
     VEHICLE SPEED
     PID 0D
     ==============================
  */

  else if (pid == PID_SPEED) {

    uint8_t A =
      frame.data.byte[3];


    sensorData.speed =
      (float)A;


    speedValid = true;


    Serial.print(
      "SPEED: "
    );

    Serial.print(
      sensorData.speed,
      0
    );

    Serial.println(
      " km/h"
    );
  }


  /*
     ==============================
     MASS AIR FLOW
     PID 10
     ==============================
  */

  else if (pid == PID_MAF) {

    uint8_t A =
      frame.data.byte[3];

    uint8_t B =
      frame.data.byte[4];


    uint16_t raw =
      ((uint16_t)A << 8) |
      B;


    sensorData.maf =
      raw / 100.0;


    mafValid = true;


    Serial.print(
      "MAF: "
    );

    Serial.print(
      sensorData.maf,
      2
    );

    Serial.println(
      " g/s"
    );


    /*
       Once MAF has been received,
       we have a complete sensor set.

       Send it to the display.
    */

    if (
      mafValid &&
      speedValid &&
      rpmValid &&
      coolantValid
    ) {

      sendSensorData();
    }
  }
}


/* =========================================================
   PRINT RAW CAN FRAME
   ========================================================= */

void printCANFrame(
  CAN_FRAME &frame
) {

  Serial.print(
    "CAN 0x"
  );

  Serial.print(
    frame.id,
    HEX
  );

  Serial.print(
    " ["
  );

  Serial.print(
    frame.length
  );

  Serial.print(
    "] "
  );


  for (
    int i = 0;
    i < frame.length;
    i++
  ) {

    if (i > 0) {

      Serial.print(
        ":"
      );
    }

    if (
      frame.data.byte[i] < 0x10
    ) {

      Serial.print(
        "0"
      );
    }

    Serial.print(
      frame.data.byte[i],
      HEX
    );
  }


  Serial.println();
}


/* =========================================================
   SETUP
   ========================================================= */

void setup() {

  Serial.begin(
    115200
  );

  delay(
    1000
  );


  Serial.println();

  Serial.println(
    "================================"
  );

  Serial.println(
    "ESP32 OBD-II -> ESP-NOW"
  );

  Serial.println(
    "MrDIY CAN Shield"
  );

  Serial.println(
    "================================"
  );


  /*
     Initialize sensor values.
  */

  sensorData.maf = 0.0;

  sensorData.speed = 0.0;

  sensorData.rpm = 0.0;

  sensorData.coolant = 0.0;


  /*
     ==============================
     WIFI / ESP-NOW
     ==============================
  */

  WiFi.mode(
    WIFI_STA
  );


  /*
     Force ESP-NOW to channel 1.
  */

  WiFi.setChannel(
    ESPNOW_CHANNEL,
    WIFI_SECOND_CHAN_NONE
  );


  delay(
    100
  );


  Serial.print(
    "Sender MAC: "
  );

  Serial.println(
    WiFi.macAddress()
  );


  Serial.print(
    "ESP-NOW channel: "
  );

  Serial.println(
    ESPNOW_CHANNEL
  );


  /*
     Initialize ESP-NOW.
  */

  if (
    esp_now_init() != ESP_OK
  ) {

    Serial.println(
      "ESP-NOW initialization FAILED!"
    );

    while (true) {

      delay(
        1000
      );
    }
  }


  /*
     Register ESP-NOW callback.
  */

  esp_now_register_send_cb(
    onDataSent
  );


  /*
     Add TFT receiver.
  */

  esp_now_peer_info_t peerInfo = {};

  memcpy(
    peerInfo.peer_addr,
    receiverMac,
    6
  );


  peerInfo.channel =
    ESPNOW_CHANNEL;


  peerInfo.ifidx =
    WIFI_IF_STA;


  peerInfo.encrypt =
    false;


  esp_err_t peerResult =
    esp_now_add_peer(
      &peerInfo
    );


  if (
    peerResult != ESP_OK
  ) {

    Serial.print(
      "Failed to add ESP-NOW peer: "
    );

    Serial.println(
      peerResult
    );

    while (true) {

      delay(
        1000
      );
    }
  }


  Serial.println(
    "ESP-NOW initialized."
  );


  /*
     ==============================
     CAN
     ==============================
  */

  Serial.println();

  Serial.println(
    "Initializing CAN..."
  );


  /*
     MrDIY shield v1.3+:

     RX = GPIO4
     TX = GPIO5
  */

  CAN0.setCANPins(
    CAN_RX_PIN,
    CAN_TX_PIN
  );


  /*
     Standard OBD-II CAN speed.
  */

  CAN0.begin(
    CAN_SPEED
  );


  /*
     Allow all CAN frames.
  */

  CAN0.watchFor();


  Serial.println(
    "CAN initialized at 500 kbit/s."
  );


  Serial.println();

  Serial.println(
    "Waiting for OBD-II data..."
  );

  Serial.println();

  Serial.println(
    "Requested PIDs:"
  );

  Serial.println(
    "01 05 = Coolant"
  );

  Serial.println(
    "01 0C = RPM"
  );

  Serial.println(
    "01 0D = Speed"
  );

  Serial.println(
    "01 10 = MAF"
  );

  Serial.println();
}


/* =========================================================
   LOOP
   ========================================================= */

void loop() {

  /*
     Read incoming CAN frames.
  */

  CAN_FRAME canMessage;


  while (
    CAN0.read(canMessage)
  ) {

    /*
       Process OBD response.
    */

    processOBDResponse(
      canMessage
    );
  }


  /*
     Request the next PID every
     100 milliseconds.
  */

  unsigned long now =
    millis();


  if (
    now - lastOBDRequest >=
    OBD_REQUEST_INTERVAL
  ) {

    lastOBDRequest =
      now;


    /*
       Request current PID.
    */

    requestPID(
      pidList[currentPIDIndex]
    );


    /*
       Move to next PID.
    */

    currentPIDIndex++;

    if (
      currentPIDIndex >=
      PID_COUNT
    ) {

      currentPIDIndex = 0;
    }
  }


  delay(1);
}