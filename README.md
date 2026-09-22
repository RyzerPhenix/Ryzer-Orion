# Ryzer-Orion

A modular automotive OBD display system based on the [**Waveshare ESP32-S3-LCD-1.28 Round Display**](https://docs.waveshare.com/ESP32-S3-LCD-1.28).

The project is designed around a central OBD adapter that reads vehicle data and distributes it to one or more display modules. The display modules provide a clean, animated circular dashboard interface for displaying live vehicle parameters.

The system uses [**ESP-NOW**](https://docs.arduino.cc/tutorials/nano-esp32/esp-now/) to transmit processed vehicle data from the OBD adapter to one or more display modules. This architecture allows multiple displays to receive data from the same OBD adapter without requiring a separate OBD connection for every display.

## Overview

The project consists of two main components:

1. **OBD Adapter**

   * Connects to the vehicle's OBD-II port.
   * Reads vehicle parameters through the OBD interface.
   * Parses the received OBD responses.
   * Processes the vehicle data.
   * Transmits the processed data wirelessly using **ESP-NOW**.

2. **Display Modules**

   * Receive live vehicle data wirelessly from the OBD adapter.
   * Display the data using an animated circular dashboard.
   * Multiple displays can receive data from the same OBD adapter.

## Data

The display UI can display 3 Data Values at once. In the example those are:

| Parameter           | Display   | Source                            |
| ------------------- | --------- | --------------------------------- |
| Mass Air Flow       | `g/s`     | OBD-II                            |
| Oil Temperature     | `°C`      | OBD-II                            |
| Fuel Consumption    | `L/100km` | Calculated from live vehicle data |

The values displayed by the dashboard are received from the OBD adapter through ESP-NOW.

## Hardware

### Display Module

The display module is based on the [**Waveshare ESP32-S3-LCD-1.28 Round Display**](https://www.waveshare.com/esp32-s3-lcd-1.28.htm).

The display provides the circular 240 × 240 pixel interface used by the dashboard.

#### BOM

| Component                                                                           | Quantity | Description                                         |
| ----------------------------------------------------------------------------------- | -------: | --------------------------------------------------- |
| [Waveshare ESP32-S3 Round Display](https://www.waveshare.com/esp32-s3-lcd-1.28.htm) |        1 | Main display/microcontroller                        |
| 3D-Printed Case                                                                     |        1 | Case                                                |
| USB-C cable                                                                         |        1 | Programming and power                               |
| Computer                                                                            |        1 | Arduino IDE                                         |


#### Build

1. Connect the Waveshare ESP32-S3 Round Display to your computer via USB-C.
2. Install the required Arduino ESP32 board support:
  - Go to File --> Prefferences and insert `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json` und Additional boards manager URLs.
  - Then install it using the Arduino IDEs boards manager.
4. Install the required libraries:
  - Download all libraries as a zip file.
  - Go to Sketch --> Include Library --> Add .ZIP Library... and select the library to include.
8. Compile and upload the firmware:
  - Board: ESP32S3 Dev Module
  - Set Flash Parameters:
    ![Flash Parameters](https://github.com/RyzerPhenix/Ryzer-Orion/blob/main/Pictures/Waveshare_ESP32-S3-LCD-128_Arduino_IDE_settings.png)
  - Port: Select your COM-Port
9. Power the display from a suitable USB or automotive power source for vehicle installation.


### OBD Adapter

#### BOM

| Component                                                              | Quantity | Description                                         |
| ---------------------------------------------------------------------- | -------: | --------------------------------------------------- |
| [ESP32 CAN Bus Shield](https://store.mrdiy.ca/p/esp32-can-bus-shield/) |        1 | OBD2 ESP32 carrier board                            |
| ESP32 DevKit V1 (!ONLY V1 WORKS)                                       |        1 | Microcontroller                                     |
| 3D-Printed Case                                                        |        1 | Case                                                |
| USB-C cable                                                            |        1 | Programming and power                               |
| Computer                                                               |        1 | Arduino IDE                                         |

#### Build

## Software
