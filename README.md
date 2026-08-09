<p align="center">
  <img src="docs/images/netra-banner.png" alt="NETRA" width="70%">
</p>

<div align="center">

# NETRA Device Firmware V1

### ESP32-C6 spatial-sensing firmware for the NETRA wearable safety system

[![Arduino](https://img.shields.io/badge/Arduino-00979D?logo=arduino&logoColor=white)](https://www.arduino.cc/)
[![ESP32-C6](https://img.shields.io/badge/ESP32--C6-E7352C?logo=espressif&logoColor=white)](https://www.espressif.com/en/products/socs/esp32-c6)
![C++](https://img.shields.io/badge/C%2B%2B-00599C?logo=cplusplus&logoColor=white)
![HC-SR04](https://img.shields.io/badge/Sensor-HC--SR04-00B8D9)
![MPU6050](https://img.shields.io/badge/IMU-MPU6050-7B61FF)
![Claude Community Events](https://img.shields.io/badge/Claude_Community_Events-Impact_Lab-D97757)

Ultrasonic ranging and six-axis motion telemetry, streamed as clean CSV over USB serial.

**Built for Claude Community Events @ Impact Lab.**

</div>

## Hardware

<p align="center">
  <img src="docs/images/netra-device-front.jpg" alt="NETRA device front view" width="48%">
  <img src="docs/images/netra-device-angle.jpg" alt="NETRA device angled view" width="48%">
</p>

The V1 sensor node combines:

| Component | Purpose | Interface |
|---|---|---|
| PCBCupid Glyph C6 / ESP32-C6 | Main controller and USB serial link | USB CDC |
| HC-SR04 | Obstacle distance measurement, nominally 2–400 cm | Trigger/echo pulse |
| MPU6050 | 3-axis acceleration, 3-axis angular velocity, temperature | I²C at `0x68` or `0x69` |

The MPU6050 is read directly through `Wire`; no third-party sensor library is required.

## Wearer movement tracking

NETRA does more than measure nearby obstacles. The device is worn by the user, so its MPU6050 continuously captures the wearer's three-axis acceleration and three-axis rotation. The firmware streams this motion telemetry to the companion debugger, where it can be used to detect movement, estimate gait intensity, follow orientation changes, and animate the user's motion inside a closed or indoor tracking environment.

This is relative inertial movement tracking rather than GPS-style absolute positioning. Long-term position estimation from an IMU alone will accumulate drift; the current system is designed for live movement state, posture/orientation response, and closed-environment visualization.

## Pin map

| Sensor pin | Board pin | GPIO | Direction | Notes |
|---|---|---:|---|---|
| HC-SR04 `VCC` | `5V` | — | Power | Use the board's 5 V rail |
| HC-SR04 `GND` | `GND` | — | Power | All grounds must be common |
| HC-SR04 `TRIG` | `D20` | 20 | Output | 10 µs trigger pulse |
| HC-SR04 `ECHO` | `D15` | 15 | Input | **Must pass through a voltage divider** |
| MPU6050 `VCC` | `3V3` | — | Power | Safe default for common breakout boards |
| MPU6050 `GND` | `GND` | — | Power | Common ground |
| MPU6050 `SDA` | `SDA` | 4 | I²C data | Internal address auto-detected |
| MPU6050 `SCL` | `SCL` | 5 | I²C clock | Bus runs at 100 kHz |

> [!CAUTION]
> The HC-SR04 echo output is approximately 5 V. Never connect it directly to an ESP32-C6 input. Use, for example, 1 kΩ from `ECHO` to GPIO 15 and 2 kΩ from GPIO 15 to GND to reduce the signal to approximately 3.3 V.

```text
HC-SR04 ECHO ── 1 kΩ ──┬── GPIO 15
                       │
                      2 kΩ
                       │
                      GND
```

## Build and upload

### Arduino IDE

1. Install [Arduino IDE 2](https://www.arduino.cc/en/software).
2. In Boards Manager, install **esp32 by Espressif Systems** version 3.1.0 or newer.
3. Open `Netra_Device_Firmware_V1.ino`.
4. Select **PCBCUPID GLYPH C6** or **ESP32C6 Dev Module**.
5. Enable **Tools → USB CDC On Boot** and select the device's COM port.
6. Click **Verify**, then **Upload**.
7. Open Serial Monitor at **115200 baud** after upload.

### Arduino CLI

Replace `COM6` and the FQBN when using another board definition:

```powershell
arduino-cli core update-index
arduino-cli core install esp32:esp32
arduino-cli compile --fqbn esp32:esp32:esp32c6 .
arduino-cli upload -p COM6 --fqbn esp32:esp32:esp32c6 .
```

## Mechanical enclosure and 3D printing

Printable NETRA V1 enclosure and wearable-pod models are maintained separately from the firmware source under [`mechanical/`](mechanical/). The directory contains six STL parts plus slicing, printing, dry-fit, and assembly instructions.

The [interactive assembled-system viewer](mechanical/belt_system_viewer.html) renders the printable enclosure together with the belt, controller, sensors, controls, battery, and pod components. It includes ghost-shell, part-visibility, rotate, fit, and exploded-view controls.

![NETRA belt system and 3D-printing assembly viewer](docs/images/netra-mechanical-3d-viewer.png)

```text
mechanical/stl/
├── pod_1027_left.stl
├── pod_1027_right.stl
├── v2_button_cap.stl
├── v3_knob.stl
├── v7_base.stl
└── v7_shell.stl
```

See the [mechanical and 3D-printing guide](mechanical/README.md) before printing or installing electronics.

## Serial protocol

The device publishes one row every 250 ms:

```text
distance_cm,echo_us,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps,temp_c,status
24.63,1436,0.012,-0.021,0.998,0.31,-0.18,0.07,28.41,RANGE_OK|IMU_OK
```

| Field | Unit | Meaning |
|---|---|---|
| `distance_cm` | cm | Distance calculated from the HC-SR04 echo pulse |
| `echo_us` | µs | Raw ultrasonic round-trip pulse width |
| `ax_g`…`az_g` | g | Accelerometer axes |
| `gx_dps`…`gz_dps` | °/s | Gyroscope axes |
| `temp_c` | °C | IMU die temperature |
| `status` | — | Combined range and IMU health flags |

`RANGE_TIMEOUT` indicates that no echo arrived within 30 ms. `IMU_ERROR` usually points to I²C wiring, power, or address trouble.

## Use with the NETRA debugger

Install and run the companion [Netra System Debugger V1](../Netra_System_Debugger_V1) to visualize motion, sensor health, proximity, and the live 3D digital twin.

![NETRA PySide6 3D debugger](docs/images/netra-debugger.png)

Only one application can own the serial port at a time. Close Arduino Serial Monitor before starting the debugger.

## Quick checks

- At rest, the magnitude of `(ax, ay, az)` should be close to `1 g`.
- Rotating the enclosure should change the gyroscope values.
- A flat target between 10 and 100 cm should produce a stable distance.
- If the board prints ROM download messages, tap reset and confirm **USB CDC On Boot** is enabled.

## Repository layout

```text
Netra_Device_Firmware_V1/
├── Netra_Device_Firmware_V1.ino
├── docs/images/
├── mechanical/
│   ├── README.md
│   ├── belt_system_viewer.html
│   └── stl/*.stl
├── .gitignore
└── README.md
```
