# Hook of the Reaper — Linux Build Guide

## Dependencies

Install the following before building:

**Arch / CachyOS / Manjaro:**
```bash
sudo pacman -S cmake qt6-base qt6-serialport qt6-multimedia hidapi
```

**Ubuntu 22.04+ / Debian:**
```bash
sudo apt install cmake qt6-base-dev qt6-serialport-dev qt6-multimedia-dev libhidapi-dev libhidapi-libusb0
```

**Fedora:**
```bash
sudo dnf install cmake qt6-qtbase-devel qt6-qtserialport-devel qt6-qtmultimedia-devel hidapi-devel
```

---

## Pre-Build Code Edit (Required)

`COMDeviceList/LightGun.cpp` contains a Windows-only obfuscation header that must be patched before compiling. Open the file and make these two changes at lines 4–5:

```cpp
// Line 4 — comment out:
//#include "../../saeicmrterta.h"

// Line 5 — add:
#define ARIEMCTORIRLK ""
```

Without this edit, the compiler will error looking for a header that does not exist on Linux.

---

## Building

Standard CMake build:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

The binary is placed at `build/HookOfTheReaper`.

---

## System Setup (Run Once)

Before launching for the first time, run the setup script from the repo root:

```bash
bash setup_linux.sh
```

This requires `sudo` and makes the following system changes:

### Step 1 — Serial port group membership

Adds your user to the `uucp` group so serial ports (`/dev/ttyUSB*`, `/dev/ttyACM*`) are accessible without root. You must **log out and back in** (or run `newgrp uucp`) for this to take effect.

### Step 2 — `option` driver for 3A-3H Retro Shooter 2

The Retro Shooter 2 (USB IDs `0483:5751`) has a non-compliant CDC ACM USB descriptor. Linux's standard `cdc_acm` driver rejects it, so the device never appears as a serial port.

The setup script configures the `option` driver (originally a GSM modem driver, but permissive enough to accept any USB serial device) to claim this device instead. It:
- Creates `/etc/modules-load.d/hotr.conf` to load the `option` module at boot
- Registers both VID/PID pairs (`0483:5750` for Retro Shooter 1, `0483:5751` for Retro Shooter 2) with the `option` driver immediately for the current session

After this, the gun appears as `/dev/ttyUSBx` when plugged in.

### Step 3 — udev rules for USB access without root

Creates `/etc/udev/rules.d/99-hotr.rules` granting read/write access to the following devices for the `uucp` group:

| Device | VID:PID |
|--------|---------|
| Alien USB Light Gun | `04B4:6870` |
| AimTrak Light Gun | `D209:1601` |
| Custom USB Light Gun | `1B4F:9206` |
| ALED Strip Controller | `CAFE:6920` |
| 3A-3H Retro Shooter 1 | `0483:5750` |
| 3A-3H Retro Shooter 2 | `0483:5751` |

The udev rules for the Retro Shooter guns also run `modprobe option` and re-register the VID/PID with the `option` driver on each plug-in event, so the device works correctly even after the system is rebooted or the driver module is reloaded.

### Step 4 — Reload udev

Runs `udevadm control --reload-rules` and `udevadm trigger` so the new rules take effect immediately without a reboot.

### Step 5 — Copy data directory

Copies `data/` from the repo root to `build/data/` so the binary can find its configuration files, default light gun profiles, and sounds.

> **Note:** If you run this before building, the `build/` directory won't exist yet. Build first, then either re-run the script or copy manually:
> ```bash
> cp -r data build/data
> ```

---

## Running

```bash
./build/HookOfTheReaper
```

The `data/` directory must be in the same directory as the binary. HOTR reads and writes configuration there (`data/settings.hor`, `data/lightguns.hor`, etc.).

---

## Building an AppImage

An AppImage bundles the binary and all Qt libraries into a single portable executable that runs on any Linux distribution with a compatible glibc.

### Option A — Native build (matches your system's glibc)

```bash
cmake --build build --target appimage
```

This downloads `linuxdeploy` and its Qt plugin automatically on first run, then produces `HookOfTheReaper-x86_64.AppImage` in the repo root.

### Option B — Docker build (glibc 2.35 / Ubuntu 22.04)

For maximum compatibility with older systems, build inside a Docker container:

```bash
bash make_appimage_docker.sh
```

Requires Docker or Podman. Builds against Ubuntu 22.04's glibc 2.35, so the AppImage runs on any distribution with glibc 2.35 or newer.

### Running the AppImage

```bash
chmod +x HookOfTheReaper-x86_64.AppImage
./HookOfTheReaper-x86_64.AppImage
```

The AppImage automatically sets `QT_QPA_PLATFORM=xcb` (unless you've already set it), so no environment variables need to be exported manually.

The `data/` directory must be in the **same folder as the AppImage file**. On first run, HOTR will look there for configuration. If the AppImage directory is not writable (e.g. on a read-only filesystem), HOTR falls back to `~/.HookOfTheReaper/data/`.



---

## Configuration Files

| Scenario | Config location |
|----------|----------------|
| Normal build (`./build/HookOfTheReaper`) | `build/data/` |
| AppImage (writable directory) | `<AppImage directory>/data/` |
| AppImage (read-only filesystem) | `~/.HookOfTheReaper/data/` |

To reset configuration, delete or clear the relevant `data/` directory. HOTR will regenerate defaults on next launch.

---

## Troubleshooting

**Serial port not appearing in dropdown**
- Confirm you are in the `uucp` group: `groups $USER`
- If not listed, re-run `setup_linux.sh` and log out/in
- Check the device is detected: `lsusb` and `ls /dev/ttyUSB*`

**Retro Shooter 2 not appearing as a serial port**
- Confirm the `option` driver is loaded: `lsmod | grep option`
- If missing: `sudo modprobe option && sudo sh -c 'echo "0483 5751" > /sys/bus/usb-serial/drivers/option1/new_id'`
- Unplug and replug the gun

**USB HID gun not accessible (permission denied)**
- Re-run `setup_linux.sh` to install/update the udev rules
- Reload rules: `sudo udevadm control --reload-rules && sudo udevadm trigger`
- Unplug and replug the device

**AppImage fails with `xcb` platform error**
- This should be handled automatically by the AppRun script. If it still occurs: `export QT_QPA_PLATFORM=xcb` before running

**No sound**
- Ensure PulseAudio or PipeWire is running
- The AppImage bundles Qt Multimedia but not audio backends — the host system must provide them
