# TrailCurrent Torrent

<p align="center">
  <img src="DOCS/images/torrent_main.png" alt="TrailCurrent Torrent" width="600">
</p>

CAN-controlled 8-channel PWM power distribution module for vehicle lighting and accessory control with OTA firmware update capability. Part of the [TrailCurrent](https://trailcurrent.com) open-source vehicle platform.

## Hardware Overview

- **Microcontroller:** ESP32 (WROOM32)
- **Function:** 8-channel PWM lighting/accessory controller with CAN bus interface
- **Key Features:**
  - 8 independent MOSFET-driven PWM outputs (0-255 brightness)
  - CAN bus communication at 500 kbps
  - Individual and master on/off/brightness control
  - Animated light sequences (startup, interior, exterior)
  - Over-the-air (OTA) firmware updates via WiFi
  - WiFi credentials provisioned dynamically over CAN bus
  - Hierarchical PCB schematic design (5 sheets)

## Hardware Requirements

### Components

- **Microcontroller:** ESP32 development board
- **CAN Transceiver:** Vehicle CAN bus interface (TX: GPIO 15, RX: GPIO 13)
- **MOSFET Drivers:** 8 channels for PWM output switching
- **DIP Switches:** Configuration switches

### Pin Connections

**PWM Outputs:**

| GPIO | Function |
|------|----------|
| 32 | Output 1 |
| 33 | Output 2 |
| 26 | Output 3 |
| 14 | Output 4 |
| 4 | Output 5 |
| 17 | Output 6 |
| 19 | Output 7 |
| 23 | Output 8 |

### KiCAD Library Dependencies

This project uses the consolidated [TrailCurrentKiCADLibraries](https://github.com/trailcurrentoss/TrailCurrentKiCADLibraries).

**Setup:**

```bash
# Clone the library
git clone git@github.com:trailcurrentoss/TrailCurrentKiCADLibraries.git

# Set environment variables (add to ~/.bashrc or ~/.zshrc)
export TRAILCURRENT_SYMBOL_DIR="/path/to/TrailCurrentKiCADLibraries/symbols"
export TRAILCURRENT_FOOTPRINT_DIR="/path/to/TrailCurrentKiCADLibraries/footprints"
export TRAILCURRENT_3DMODEL_DIR="/path/to/TrailCurrentKiCADLibraries/3d_models"
```

See [KICAD_ENVIRONMENT_SETUP.md](https://github.com/trailcurrentoss/TrailCurrentKiCADLibraries/blob/main/docs/KICAD_ENVIRONMENT_SETUP.md) in the library repository for detailed setup instructions.

## Opening the Project

1. **Set up environment variables** (see Library Dependencies above)
2. **Open KiCAD:**
   ```bash
   kicad EDA/trailcurrent-torrent.kicad_pro
   ```
3. **Verify libraries load** - All symbol and footprint libraries should resolve without errors
4. **View 3D models** - Open PCB and press `Alt+3` to view the 3D visualization

### Schematic Sheets

The design uses a hierarchical schematic with dedicated sheets:
- **Root** - Top-level connections
- **Power** - Power distribution and regulation
- **CAN** - CAN bus transceiver interface
- **MCU** - ESP32 microcontroller and support circuits
- **MOSFETs** - 8-channel MOSFET driver outputs
- **DIP Switch** - Configuration switches

## Firmware

See `src/` directory for PlatformIO-based firmware.

**Setup:**
```bash
# Install PlatformIO (if not already installed)
pip install platformio

# Build firmware
pio run

# Upload to board (serial)
pio run -t upload

# Upload via OTA (after initial flash)
pio run -t upload --upload-port esp32-DEVICE_ID
```

### Firmware Dependencies

This firmware depends on the following public libraries:

- **[OtaUpdateLibraryWROOM32](https://github.com/trailcurrentoss/OtaUpdateLibraryWROOM32)** (v0.0.1) - Over-the-air firmware update functionality
- **[TwaiTaskBasedLibraryWROOM32](https://github.com/trailcurrentoss/TwaiTaskBasedLibraryWROOM32)** (v0.0.1) - CAN bus communication interface
- **[ESP32ArduinoDebugLibrary](https://github.com/trailcurrentoss/ESP32ArduinoDebugLibrary)** (v2.0.0) - Debug macro system with compile-time removal

All dependencies are automatically resolved by PlatformIO during the build process.

### Serial Debugging

Debug output is controlled by the `DEBUG` build flag in `platformio.ini`:

```ini
build_flags = -DDEBUG=1   ; Enable debug output (default)
; build_flags = -DDEBUG=0  ; Disable — all debug calls compile away to zero overhead
```

When enabled, debug macros (`debugln()`, `debugf()`, `debug_tag()`, etc.) output to Serial at 115200 baud. When disabled (`DEBUG=0`), all debug code is completely removed at compile time with no performance or flash size impact.

**WiFi Credentials:**
- WiFi credentials are provisioned dynamically via CAN bus (Message ID 0x01)
- Credentials are stored in NVS (non-volatile storage) and persist across reboots
- For standalone testing, credentials can be set manually in firmware

### CAN Bus Protocol

**Receive (Bus to Module):**

| CAN ID | Description |
|--------|-------------|
| 0x00 | OTA update trigger (MAC-based device targeting) |
| 0x01 | WiFi credential provisioning (SSID/password via CAN) |
| 0x18 | Toggle channel on/off (byte 0 = channel 0-7, 8=all on, 9=all off) |
| 0x21 | Set brightness (byte 0 = channel, byte 1 = PWM value 0-255) |
| 0x1E | Trigger light sequence (interior/exterior animations) |

**Transmit (Module to Bus):**

| CAN ID | Description |
|--------|-------------|
| 0x1B | Status report - current PWM values for all 8 channels (8 bytes) |

## Manufacturing

- **PCB Files:** Ready for fabrication via standard PCB services (JLCPCB, OSH Park, etc.)
- **BOM Generation:** Export BOM from KiCAD schematic (Tools > Generate BOM)
- **JLCPCB Assembly:** See [BOM_ASSEMBLY_WORKFLOW.md](https://github.com/trailcurrentoss/TrailCurrentKiCADLibraries/blob/main/tools/BOM_ASSEMBLY_WORKFLOW.md) for detailed assembly workflow

## Project Structure

```
├── EDA/                          # KiCAD hardware design files
│   ├── trailcurrent-torrent.kicad_pro
│   ├── trailcurrent-torrent.kicad_sch  # Root schematic
│   ├── can.kicad_sch             # CAN subsystem
│   ├── mcu.kicad_sch             # MCU subsystem
│   ├── power.kicad_sch           # Power subsystem
│   ├── mosfets.kicad_sch         # 8-channel MOSFET drivers
│   ├── dip_switch.kicad_sch      # Configuration switches
│   └── trailcurrent-torrent.kicad_pcb  # PCB layout
├── CAD/                          # FreeCAD case design and 3D models
│   ├── trailcurrent-torrent-case.FCStd
│   └── TrailCurrentTorrent.3mf
├── src/                          # Firmware source
│   ├── main.cpp                  # Main application
│   ├── globals.h                 # Pin definitions
│   ├── canHelper.h               # CAN message handling
│   ├── lightSequences.h          # Startup and animated light sequences
│   └── wifiConfig.h              # NVS WiFi credential storage
├── data/
│   └── partitions.csv            # ESP32 flash partition layout
└── platformio.ini                # Build configuration
```

## License

MIT License - See LICENSE file for details.

This is open source hardware. You are free to use, modify, and distribute these designs under the terms of the MIT license.

## Contributing

Improvements and contributions are welcome! Please submit issues or pull requests.

## Support

For questions about:
- **KiCAD setup:** See [KICAD_ENVIRONMENT_SETUP.md](https://github.com/trailcurrentoss/TrailCurrentKiCADLibraries/blob/main/docs/KICAD_ENVIRONMENT_SETUP.md)
- **Assembly workflow:** See [BOM_ASSEMBLY_WORKFLOW.md](https://github.com/trailcurrentoss/TrailCurrentKiCADLibraries/blob/main/tools/BOM_ASSEMBLY_WORKFLOW.md)
