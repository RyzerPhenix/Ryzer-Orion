# Ryzer-Orion

A modular automotive OBD display system based on the **Waveshare ESP32-S3-LCD-1.28 Round Display**.

The project is designed around a central OBD adapter that reads vehicle data and distributes it to one or more display modules. The display modules provide a clean, animated circular dashboard interface for displaying live vehicle parameters.

The current display firmware uses simulated values while the **ESP-NOW communication layer is being implemented**. The intended architecture allows multiple display modules to receive data from the same OBD adapter without requiring a separate OBD connection for every display.

---

## Overview

The project consists of two main components:

1. **OBD Adapter**

   * Connects to the vehicle's OBD-II port.
   * Reads vehicle parameters through the OBD interface.
   * Parses the received OBD responses.
   * Transmits the processed data wirelessly using **ESP-NOW**.

2. **Display Modules**

   * Based on the Waveshare ESP32-S3 Round Display.
   * Receive vehicle data wirelessly from the OBD adapter.
   * Display the data using an animated circular dashboard.
   * Multiple displays can receive data from the same OBD adapter.

### Current Data

The display UI currently contains three values:

| Parameter           | Display   | Current Source |
| ------------------- | --------- | -------------- |
| Mass Air Flow       | `g/s`     | Simulated      |
| Coolant Temperature | `°C`      | Simulated      |
| Fuel Consumption    | `L/100km` | Simulated      |

The simulated values are used during UI development. They can later be replaced by the corresponding values received through ESP-NOW.

---

# Hardware

## Display Module

The current display module is based on the:

**Waveshare ESP32-S3 Round Display**

The display provides the circular 240 × 240 pixel interface used by the dashboard.

### Display Module BOM

| Component                        | Quantity | Description                                         |
| -------------------------------- | -------: | --------------------------------------------------- |
| Waveshare ESP32-S3 Round Display |        1 | Main display/controller                             |
| USB-C cable                      |        1 | Programming and power                               |
| Computer                         |        1 | Arduino IDE / development                           |
| —                                |        — | Additional hardware as required by the installation |

The exact hardware required may vary depending on how the display is mounted and powered in the vehicle.

---

## Display Module Build

1. Connect the Waveshare ESP32-S3 Round Display to your computer via USB-C.
2. Install the required Arduino ESP32 board support.
3. Install the required libraries.
4. Configure `TFT_eSPI` for the Waveshare display.
5. Add the project bitmap assets.
6. Compile and upload the firmware.
7. Power the display from a suitable USB or automotive power source for vehicle installation.

The display firmware is currently designed around a **240 × 240 pixel** display area.

---

# OBD Adapter

## Hardware

<!-- TODO: Add OBD adapter hardware information -->

---

## BOM

<!-- TODO: Add OBD adapter BOM -->

---

## Build

<!-- TODO: Add OBD adapter build instructions -->

---

## Software

<!-- TODO: Add OBD adapter software information -->

---

## OBD Data Processing

<!-- TODO: Add information about OBD-II communication, PID requests and response parsing -->

---

## ESP-NOW

The OBD adapter will act as the wireless data source for the display modules.

After obtaining the required vehicle parameters, the adapter will package the data and transmit it using **ESP-NOW**.

---

# Software

## Display Module Code

The display firmware is written for the **ESP32-S3** using the Arduino framework.

The application is divided into several functional parts:

### Display Initialization

The display is initialized using `TFT_eSPI`:

```cpp
tft.init();
tft.setRotation(0);
tft.setSwapBytes(true);
```

Two sprites are used:

* A `240 × 240` sprite for the main dashboard.
* A `256 × 144` sprite for the boot logo.

Sprites allow the dashboard to be rendered off-screen before being pushed to the physical display. This reduces visible drawing artifacts during animation.

---

## Dashboard Rendering

The main UI consists of three circular rings.

Each ring represents one vehicle parameter:

* MAF
* Coolant temperature
* Fuel consumption

The rings use a common angular range from approximately `-225°` to `45°`.

The displayed value determines how far around the ring the accent line is rendered.

For example:

```cpp
drawRing(cx, cy, 105, mafA, 20);
```

The three rings use different radii:

```text
MAF             105 px
Temperature      85 px
Fuel             65 px
```

---

## Icons

Three 32 × 32 pixel icons are displayed below the dashboard values.

```cpp
MAFIcon
tempIcon
fuelIcon
```

These bitmap assets are stored in:

```text
bitmaps.h
```

The icons are rendered directly into the main sprite.

---

## Boot Animation

When the display starts, the boot logo is displayed using a fade-in/fade-out animation.

The logo is stored as a 256 × 144 RGB565 bitmap:

```cpp
bootLogo
```

The firmware modifies the RGB channels according to an alpha value to create the fade effect.

The boot sequence is:

```text
Black screen
     ↓
Logo fade in
     ↓
Logo fade out
     ↓
Black screen
     ↓
Dashboard initialization
```

---

## UI Startup Animation

After the boot logo, the dashboard starts through several animation states.

The firmware uses the following state machine:

```text
BOOT
  ↓
FLICKER
  ↓
SWEEP
  ↓
RUN_START
  ↓
RUN
```

### Flicker

The individual UI components appear progressively.

The icons, rings and text use different flicker frequencies to create a digital startup effect.

### Sweep

The three rings perform a synchronized ping-pong sweep.

The values move:

```text
0 → maximum → 0
```

using an easing function.

### Run Start

After the sweep, the displayed values smoothly transition from zero to their current values.

```text
0 → current value
```

This prevents the dashboard from suddenly jumping from the animation to the live values.

### Run

Once the startup sequence is complete, the dashboard continuously displays the current values.

---

## Simulation

The current firmware uses simulated vehicle values.

Initial values are:

```cpp
volatile float maf = 3.4;
volatile float temp = 92;
volatile float fuel = 12.8;
```

The data task continuously modifies these values to create a smooth running simulation:

```cpp
maf += sin(millis() * 0.001) * 0.01;
temp += cos(millis() * 0.0012) * 0.02;
fuel = maf * 2.7;
```

The simulation is intended for UI development and testing.

Once ESP-NOW communication is fully integrated, these values will be replaced with data received from the OBD adapter.

---

## FreeRTOS

The firmware uses FreeRTOS tasks provided by the ESP32 Arduino framework.

Two tasks are used:

### Render Task

The render task is pinned to **Core 0**.

```cpp
xTaskCreatePinnedToCore(
  renderTask,
  "Render",
  12000,
  NULL,
  1,
  NULL,
  0
);
```

Its responsibilities include:

* Boot animation
* Flicker animation
* Ring animation
* UI rendering
* Display updates

### Data Task

The data task is pinned to **Core 1**.

```cpp
xTaskCreatePinnedToCore(
  dataTask,
  "Data",
  8000,
  NULL,
  1,
  NULL,
  1
);
```

Its current responsibility is generating simulated vehicle data.

In the final implementation, this task can be used for processing incoming ESP-NOW data and updating the display values.

---

# Dependencies

The display firmware currently requires:

### Arduino ESP32 Core

Install the ESP32 board support package through the Arduino IDE Board Manager.

The project targets:

```text
ESP32-S3
```

### TFT_eSPI

The display graphics are implemented using:

```cpp
#include <TFT_eSPI.h>
```

The `TFT_eSPI` configuration must match the Waveshare ESP32-S3 Round Display hardware.

The correct display driver, GPIO assignments and SPI configuration are required for the display to initialize correctly.

### Project Bitmap Assets

The firmware also requires:

```cpp
#include "bitmaps.h"
```

This file contains the graphical assets used by the interface, including:

* Boot logo
* MAF icon
* Temperature icon
* Fuel icon

---

# Settings

## TFT_eSPI

Before compiling the display firmware, configure `TFT_eSPI` for the Waveshare ESP32-S3 Round Display.

The configuration must provide the correct:

* Display driver
* SPI pins
* Chip select
* Data/command pin
* Reset pin
* Backlight configuration
* Display resolution
* SPI frequency

The project uses:

```text
240 × 240 pixels
```

and:

```cpp
tft.setRotation(0);
tft.setSwapBytes(true);
```

The exact TFT_eSPI setup depends on the TFT_eSPI version and the Waveshare board configuration being used.

---

## ESP-NOW Settings

ESP-NOW is being implemented as the communication layer between the OBD adapter and the display modules.

The planned communication architecture is:

```text
                 ┌──────────────────┐
                 │   OBD Adapter    │
                 │                  │
                 │  OBD-II Reader  │
                 │       ↓          │
                 │   Data Parser   │
                 │       ↓          │
                 │    ESP-NOW      │
                 └────────┬─────────┘
                          │
             ┌────────────┼────────────┐
             │            │            │
             ▼            ▼            ▼
       ┌──────────┐ ┌──────────┐ ┌──────────┐
       │ Display  │ │ Display  │ │ Display  │
       │ Module 1 │ │ Module 2 │ │ Module 3 │
       └──────────┘ └──────────┘ └──────────┘
```

The OBD adapter acts as the transmitter/source while the display modules act as receivers.

---

# Expansion

## Multiple Display Modules

One of the main goals of the project is to allow **multiple display modules to use the same OBD adapter**.

Instead of every display establishing its own OBD connection, the OBD adapter reads the vehicle data once and distributes the processed data to multiple ESP32-based displays.

For example:

```text
                    Vehicle
                       │
                       │ OBD-II
                       ▼
              ┌─────────────────┐
              │   OBD Adapter   │
              │                 │
              │ OBD-II Reader  │
              │      +          │
              │   ESP-NOW TX    │
              └────────┬────────┘
                       │
              ESP-NOW broadcast/
                multi-peer data
                       │
        ┌──────────────┼──────────────┐
        │              │              │
        ▼              ▼              ▼
   ┌─────────┐    ┌─────────┐    ┌─────────┐
   │ Display │    │ Display │    │ Display │
   │    1    │    │    2    │    │    3    │
   └─────────┘    └─────────┘    └─────────┘
```

### Advantages

This architecture provides several benefits:

* Only one OBD connection is required.
* Multiple displays can show the same vehicle data.
* Display modules remain relatively simple.
* Additional displays can be added without adding additional OBD adapters.
* The OBD adapter can act as a central vehicle-data gateway.

---

## ESP-NOW Network

The intended ESP-NOW implementation will allow the OBD adapter to distribute a common data packet to multiple display modules.

A future data packet could contain values such as:

```text
MAF
Coolant temperature
Fuel consumption
RPM
Vehicle speed
Throttle position
Engine load
Intake air temperature
```

The exact packet structure is still being developed.

Each display module can then decide which values it wants to display.

For example:

```text
Display 1
─────────
MAF
Coolant temperature
Fuel consumption


Display 2
─────────
RPM
Vehicle speed
Throttle position


Display 3
─────────
Engine load
Intake temperature
Fuel consumption
```

This makes the system scalable without requiring changes to the OBD connection itself.

---

# Project Structure

A possible project structure is:

```text
OBD-Monitor/
│
├── Display/
│   ├── OBDMonitor.ino
│   └── bitmaps.h
│
├── OBD-Adapter/
│   └── ...
│
└── README.md
```

The `Display` directory contains the firmware for the Waveshare ESP32-S3 display modules.

The `OBD-Adapter` directory will contain the OBD adapter firmware and related code.

---

# Development Status

| Component                | Status                   |
| ------------------------ | ------------------------ |
| ESP32-S3 display         | Working                  |
| Circular dashboard       | Working                  |
| Boot animation           | Working                  |
| Flicker animation        | Working                  |
| Ring sweep               | Working                  |
| Value startup animation  | Working                  |
| Simulated data           | Working                  |
| OBD adapter              | In development           |
| OBD data parsing         | In development           |
| ESP-NOW transmission     | In development           |
| Multiple display support | Planned / in development |

---

# License

Add the project license here.

<!-- TODO: Add license information -->
