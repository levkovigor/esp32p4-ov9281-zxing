# ESP32-P4 Barcode Scanner (OV9281 MIPI-CSI)

![Front Side](images/front_side.png)
![Back Side](images/back_side.png)

High-performance barcode and QR code scanner based on the ESP32-P4 and the OV9281 (Global Shutter) sensor. The firmware is optimized for high-speed motion capture with 100 FPS support and ultra-short exposure.

## Key Features
- **Sensor**: OV9281 (MIPI-CSI, 640x400, Global Shutter).
- **Speed**: 100 FPS.
- **Exposure**: Minimum (1 line, ~25-30 µs) to eliminate motion blur.
- **Algorithm**: ZXing C++ with optimization (Box Average downsampling to 320x200).
- **Architecture**: Multi-core task distribution (CPU 0 for video & display, CPU 1 for scanning).

---

## Hardware Setup

### Camera
You can use an OV9281 camera and its ribbon cable designed for the Raspberry Pi. This specific project uses the OV9281 camera module from **INNO-MAKER**.

### Screen Connections
For the screen to work properly, additional jumper connections are required between the ESP32-P4 header and the screen:
- **5V** to **5V**
- **GND** to **GND**
- **3V3** to **LCD_RST**

### Power and UART
- **Power**: Supply power via the **USB POWER-IN** port.
- **UART**: Use the **USB-UART** port for serial communication.

A sample UART log can be found here: [esp32-uart-log.txt](esp32-uart-log.txt)

---

## Environment Setup from Scratch

### 1. Download ESP-IDF
1. Download the **ESP-IDF Tools Installer** (v5.3 or newer recommended) from the [official website](https://dl.espressif.com/dl/esp-idf/).
2. Run the installer and follow the instructions. It is recommended to install to `C:\Espressif`.

### 2. Prepare PowerShell
Open PowerShell and run the command to activate the environment (adjust the path if necessary):
```powershell
. C:\Espressif\tools\Microsoft.v5.3.PowerShell_profile.ps1
```

---

## Building and Flashing

To ensure correct operation after configuration changes (FPS, resolution), it is recommended to perform a full clean build:

```powershell
# 1. Clean old configs and build artifacts
rm sdkconfig
rm -r build

# 2. Set the target platform
idf.py set-target esp32p4

# 3. Build, flash, and start the monitor
idf.py build flash monitor
```

---

## Technical Details
- **Firmware Version**: 19.0 (Limit Speed).
- **Data Output**: Barcode coordinates and decoded data are printed to the console in the following format:
  `deltaX:deltaY:angle:distX:distY:data`
- **Display**: Uses LVGL Canvas for real-time 640x400 video streaming.

## Troubleshooting
- **Black Screen**: Check the MIPI-CSI ribbon cable connection.
- **Task WDT Error**: If the system resets, ensure that performance optimization is enabled in `sdkconfig` (`CONFIG_COMPILER_OPTIMIZATION_PERF=y`).
