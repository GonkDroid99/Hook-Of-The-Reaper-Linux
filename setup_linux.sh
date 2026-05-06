#!/bin/bash
# Hook of the Reaper — Linux system setup
# Run once before launching the application for the first time.
# Requires sudo.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "=== Hook of the Reaper Linux Setup ==="

# --- Group membership for serial port access ---
echo "[1/5] Adding $USER to uucp group (serial port access)..."
sudo usermod -aG uucp "$USER"
echo "      Done. You must log out and back in (or run 'newgrp uucp') for this to take effect."

# --- Load option serial driver for 3A-3H Retro Shooter 2 ---
# The gun's CDC ACM USB descriptor is non-compliant; Linux's cdc_acm driver rejects it.
# The 'option' GSM modem driver is permissive and successfully claims the data interface.
echo "[2/5] Configuring option serial driver for 3A-3H Retro Shooter 2..."
sudo tee /etc/modules-load.d/hotr.conf > /dev/null << 'EOF'
# Load GSM modem serial driver at boot — used for 3A-3H Retro Shooter 2
option
EOF

# Register Retro Shooter 1 (Verify these IDs with lsusb)
sudo sh -c 'echo "0483 5750" > /sys/bus/usb-serial/drivers/option1/new_id' || true

# Register Retro Shooter 2
sudo sh -c 'echo "0483 5751" > /sys/bus/usb-serial/drivers/option1/new_id' || true
echo "      Done."

# --- udev rules for USB device access without root ---
echo "[3/5] Installing udev rules..."
sudo tee /etc/udev/rules.d/99-hotr.rules > /dev/null << 'EOF'
# Hook of the Reaper — USB device access rules

# Alien USB Light Gun (VID 0x04B4, PID 0x6870)
SUBSYSTEM=="usb",    ATTRS{idVendor}=="04b4", ATTRS{idProduct}=="6870", MODE="0666", GROUP="uucp"
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="04b4", ATTRS{idProduct}=="6870", MODE="0666", GROUP="uucp"

# AimTrak Light Gun (VID 0xD209, PID 0x1601)
SUBSYSTEM=="usb",    ATTRS{idVendor}=="d209", ATTRS{idProduct}=="1601", MODE="0666", GROUP="uucp"
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="d209", ATTRS{idProduct}=="1601", MODE="0666", GROUP="uucp"

# Custom USB Light Gun (VID 0x1B4F, PID 0x9206)
SUBSYSTEM=="usb",    ATTRS{idVendor}=="1b4f", ATTRS{idProduct}=="9206", MODE="0666", GROUP="uucp"
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="1b4f", ATTRS{idProduct}=="9206", MODE="0666", GROUP="uucp"

# ALED Strip Controller (VID 0xCAFE, PID 0x6920)
SUBSYSTEM=="usb",    ATTRS{idVendor}=="cafe", ATTRS{idProduct}=="6920", MODE="0666", GROUP="uucp"
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="cafe", ATTRS{idProduct}=="6920", MODE="0666", GROUP="uucp"

# 3A-3H Retro Shooter 2 (VID 0x0483, PID 0x5751)
# The gun's CDC ACM descriptor is non-compliant; cdc_acm rejects it.
# The 'option' driver is used instead — it claims the data interface as ttyUSBx.
# On plug-in, re-register the VID/PID with option in case the module reloaded.
# Multiple guns of the same model are handled automatically — each appears as a
# separate ttyUSBx. Linux also auto-creates stable /dev/serial/by-path/ symlinks
# per physical USB port, which HOTR uses in its port dropdown.



# 3A-3H Retro Shooter 1 (VID 0x0483, PID 0x5750)
SUBSYSTEM=="usb",    ATTRS{idVendor}=="0483", ATTRS{idProduct}=="5750", MODE="0666", GROUP="uucp"
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="5750", MODE="0666", GROUP="uucp"
SUBSYSTEM=="tty",    ATTRS{idVendor}=="0483", ATTRS{idProduct}=="5750", MODE="0666", GROUP="uucp"
ACTION=="add", SUBSYSTEM=="usb", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="5750", RUN+="/bin/sh -c 'modprobe -q option; echo 0483 5750 > /sys/bus/usb-serial/drivers/option1/new_id'"

# 3A-3H Retro Shooter 2 (VID 0x0483, PID 0x5751)
SUBSYSTEM=="usb",    ATTRS{idVendor}=="0483", ATTRS{idProduct}=="5751", MODE="0666", GROUP="uucp"
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="5751", MODE="0666", GROUP="uucp"
SUBSYSTEM=="tty",    ATTRS{idVendor}=="0483", ATTRS{idProduct}=="5751", MODE="0666", GROUP="uucp"
ACTION=="add", SUBSYSTEM=="usb", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="5751", RUN+="/bin/sh -c 'modprobe -q option; echo 0483 5751 > /sys/bus/usb-serial/drivers/option1/new_id'"
EOF
echo "      Written to /etc/udev/rules.d/99-hotr.rules"

# --- Reload udev ---
echo "[4/5] Reloading udev rules..."
sudo udevadm control --reload-rules
sudo udevadm trigger
echo "      Done."

# --- Copy data directory next to the binary ---
echo "[5/5] Copying data/ directory to build/..."
if [ ! -d "$SCRIPT_DIR/build" ]; then
    echo "      WARNING: build/ directory not found. Build the project first, then re-run this step:"
    echo "        cp -r \"$SCRIPT_DIR/data\" \"$SCRIPT_DIR/build/data\""
else
    cp -r "$SCRIPT_DIR/data" "$SCRIPT_DIR/build/data"
    echo "      Done. Copied to $SCRIPT_DIR/build/data"
fi

echo ""
echo "=== Setup complete ==="
echo "Retro Shooter 2 guns appear automatically in the HOTR port dropdown when plugged in."
echo "HOTR shows /dev/serial/by-path/ entries (stable per USB port) — plug P1 and P2 into"
echo "the same USB ports each session and HOTR will find them consistently, no extra setup."
echo "Note: serial port access requires a new login session to take effect."
