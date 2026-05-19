# ESP32 Macro Photography Rail Controller

A high-performance, ESP32-based automated macro photography rail controller. This project integrates seamless stepper motor control, a modern Web Bluetooth (BLE) user interface, and direct WiFi communication with Sony cameras (Sony Remote API) to fully automate focus stacking.

## Features

- **Precision Stepper Control**: Moves the camera rail with micrometer precision. Supports micro-stepping configuration (1/8th by default).
- **Automated Focus Stacking**: Calculates the exact depth of field and step size required based on your lens's Magnification and Aperture (or Numerical Aperture).
- **Direct Sony Camera Integration**: 
  - Connects directly to the Sony camera's WiFi access point (e.g., ILCE-7RM2, ILCE-6300).
  - Automatically configures the camera for stacking (sets minimum ISO).
  - Triggers the shutter via the Sony Remote API.
- **Ultra-Optimized Timing**: Overlaps the rail movement with the camera's internal processing/SD-card save time, reducing the delay between shots to just ~0.8s.
- **Robust Connection Handling**: If the camera turns off or the WiFi connection drops during a stack, the ESP32 automatically pauses the sequence and resumes flawlessly once the camera is reconnected.
- **Modern Web Interface**: 
  - Hosted directly on the ESP32 via LittleFS.
  - Uses Web Bluetooth (BLE) to communicate with the ESP32, allowing control from any modern browser (Chrome/Edge on PC, Android, or Mac) without needing a native app.
  - Real-time telemetry: Progress bar, Elapsed Time, Estimated Time of Arrival (ETA), and Total Photos.
- **OTA Updates**: Supports Over-The-Air firmware and filesystem updates via PlatformIO or through the hidden `/update` web page.

## Hardware Requirements

- **Microcontroller**: ESP32 (e.g., DOIT ESP32 DEVKIT V1)
- **Stepper Driver**: A4988, DRV8825, or TMC2208/2209
- **Actuator**: A motorized macro rail (e.g., standard NEMA 17 stepper motor on a lead screw)
- **Camera**: Sony mirrorless camera supporting the "Smart Remote Control" app / Sony Camera API.

### Default Pinout
| Function | ESP32 Pin |
|----------|-----------|
| STEP     | GPIO 16   |
| DIR      | GPIO 4    |
| MS1      | GPIO 18   |
| MS2      | GPIO 27   |
| ENABLE   | GPIO 25   |

## Software Architecture

1. **Backend (C++)**: 
   - Uses `NimBLE-Arduino` for lightweight and fast Bluetooth Low Energy communication.
   - Built on the Arduino Framework using PlatformIO.
   - Manages motor bit-banging and non-blocking HTTP requests.
2. **Frontend (HTML/JS/CSS)**:
   - Stored in the `data/` folder and uploaded to the ESP32's `LittleFS` partition.
   - Single-page application using modern dark-mode aesthetics.
   - Connects to the ESP32 via the Web Bluetooth API.

## Installation & Setup

1. **Clone the repository** and open the project in [PlatformIO](https://platformio.org/).
2. **Build and Upload the Firmware**:
   - Connect the ESP32 via USB.
   - Click **Upload** in PlatformIO.
3. **Upload the Web Interface**:
   - In PlatformIO, go to the Project Tasks menu -> `esp32doit-devkit-v1` -> `Platform` -> **Upload Filesystem Image**.
   - *Note: This is mandatory, otherwise the web interface will not load!*
4. **Connect to the ESP32**:
   - Power up the ESP32. It will broadcast a WiFi network named `ESP32_Rail_Setup`.
   - Connect to this network with your phone or PC.
   - Open a browser and navigate to `http://192.168.4.1/`.

## Usage Instructions

1. Turn on your Sony camera and launch the "Smart Remote Control" app (or enable "Control with Smartphone").
2. Open the ESP32 Web Interface (`http://192.168.4.1/`).
3. Click the **Connect BLE** button in the web interface to establish a Bluetooth link with the rail.
4. Input your Sony camera's WiFi password in the setup panel if it's your first time.
5. Define your start and end positions using the movement controls.
6. Enter your lens settings (Magnification, Aperture/NA).
7. Click **Start**! The system will connect to the camera, configure the ISO, and execute the stack autonomously.

## OTA (Over-The-Air) Updates

Once the initial flash is done via USB, you can update the system wirelessly:
- **Via PlatformIO**: Ensure your computer is connected to the `ESP32_Rail_Setup` WiFi. PlatformIO will automatically detect it and flash over WiFi.
- **Via Web Browser**: Go to `http://192.168.4.1/update` and upload your `.bin` files (both `firmware.bin` and `littlefs.bin` are supported).
