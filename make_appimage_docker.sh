#!/bin/bash
#
# Build HookOfTheReaper AppImage inside Ubuntu 22.04.
#
# This avoids linking against the much newer glibc/libstdc++ found on
# Arch/CachyOS and produces an AppImage suitable for older distributions
# such as Batocera 43.
#
# Requires:
#   Docker OR Podman
#
# Usage:
#   chmod +x make_appimage_docker.sh
#   ./make_appimage_docker.sh
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

IMAGE_NAME="hotr-appimage-builder"
BUILD_DIR="build_appimage"
OUTPUT="HookOfTheReaper-x86_64.AppImage"

# ------------------------------------------------------------
# Find Docker or Podman
# ------------------------------------------------------------

CONTAINER_CMD=""

if command -v docker >/dev/null 2>&1; then
    CONTAINER_CMD="docker"
elif command -v podman >/dev/null 2>&1; then
    CONTAINER_CMD="podman"
else
    echo "ERROR: Docker or Podman is required."
    echo
    echo "On Arch/CachyOS you can install Podman with:"
    echo "  sudo pacman -S podman"
    exit 1
fi

echo "============================================"
echo " HookOfTheReaper AppImage Builder"
echo "============================================"
echo
echo "Container runtime: $CONTAINER_CMD"
echo "Build image:       $IMAGE_NAME"
echo "Output:            $OUTPUT"
echo

# ------------------------------------------------------------
# Verify required source files exist
# ------------------------------------------------------------

if [ ! -f "$SCRIPT_DIR/CMakeLists.txt" ]; then
    echo "ERROR: CMakeLists.txt not found."
    echo "Run this script from the HookOfTheReaper repo root."
    exit 1
fi

if [ ! -f "$SCRIPT_DIR/Dockerfile.appimage" ]; then
    echo "ERROR: Dockerfile.appimage not found."
    exit 1
fi

if [ ! -f "$SCRIPT_DIR/HookOfTheReaper.desktop" ]; then
    echo "ERROR: HookOfTheReaper.desktop not found."
    exit 1
fi

# ------------------------------------------------------------
# Build/update container image
# ------------------------------------------------------------

echo "[1/4] Building Ubuntu 22.04 build environment..."

"$CONTAINER_CMD" build \
    --pull \
    -f "$SCRIPT_DIR/Dockerfile.appimage" \
    -t "$IMAGE_NAME" \
    "$SCRIPT_DIR"

echo
echo "[2/4] Compiling HOTR and building AppImage..."

# -i is important because the build script is supplied through stdin.
#
# We run with the host user's UID/GID so generated files don't become
# root-owned on the host.
"$CONTAINER_CMD" run \
    --rm \
    -i \
    --user "$(id -u):$(id -g)" \
    -e HOME=/tmp/hotr-home \
    -v "$SCRIPT_DIR:/src" \
    -w /src \
    "$IMAGE_NAME" \
    bash <<'CONTAINER_EOF'

set -euo pipefail

BUILD_DIR="build_appimage"
OUTPUT="HookOfTheReaper-x86_64.AppImage"

echo
echo "============================================"
echo " Container environment"
echo "============================================"

echo -n "glibc: "
ldd --version | sed -n '1p'

echo -n "gcc:   "
g++ --version | sed -n '1p'

echo -n "cmake: "
cmake --version | sed -n '1p'

echo "qmake:"
qmake6 --version

# ------------------------------------------------------------
# Clean previous build
# ------------------------------------------------------------

echo "Cleaning previous build..."

rm -rf "$BUILD_DIR"
rm -f "$OUTPUT"

# Remove linuxdeploy binaries from previous builds if you want CMake
# to fetch fresh copies every time.
#
# Comment these two lines out if you'd rather cache them.
rm -f linuxdeploy-x86_64.AppImage
rm -f linuxdeploy-plugin-qt-x86_64.AppImage

# ------------------------------------------------------------
# Configure
# ------------------------------------------------------------

echo
echo "============================================"
echo " Configuring"
echo "============================================"

cmake \
    -S . \
    -B "$BUILD_DIR" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr

# ------------------------------------------------------------
# Build your existing CMake 'appimage' target
# ------------------------------------------------------------

echo
echo "============================================"
echo " Building AppImage"
echo "============================================"

cmake \
    --build "$BUILD_DIR" \
    --target appimage \
    -j"$(nproc)"

# ------------------------------------------------------------
# Verify output
# ------------------------------------------------------------

if [ ! -f "$OUTPUT" ]; then
    echo
    echo "ERROR: AppImage was not created."
    exit 1
fi

chmod +x "$OUTPUT"

echo
echo "============================================"
echo " AppImage created"
echo "============================================"

ls -lh "$OUTPUT"

# ------------------------------------------------------------
# Extract AppImage for validation
# ------------------------------------------------------------

echo
echo "Extracting AppImage for validation..."

rm -rf /tmp/hotr-appimage-check

mkdir -p /tmp/hotr-appimage-check
cd /tmp/hotr-appimage-check

APPIMAGE_EXTRACT_AND_RUN=1 "/src/$OUTPUT" \
    --appimage-extract >/dev/null

if [ ! -d squashfs-root ]; then
    echo "WARNING: Could not extract AppImage for validation."
    exit 0
fi

# ------------------------------------------------------------
# Verify key Qt pieces
# ------------------------------------------------------------

echo
echo "============================================"
echo " Checking bundled Qt libraries"
echo "============================================"

for LIB in \
    "libQt6Core.so.6" \
    "libQt6Gui.so.6" \
    "libQt6Widgets.so.6" \
    "libQt6Network.so.6" \
    "libQt6SerialPort.so.6" \
    "libQt6Multimedia.so.6"
do
    RESULT="$(find squashfs-root -name "$LIB" -print -quit)"

    if [ -n "$RESULT" ]; then
        echo "[OK] $LIB"
        echo "     $RESULT"
    else
        echo "[MISSING] $LIB"
    fi
done

echo
echo "Qt xcb platform plugin:"

QXCB="$(find squashfs-root -name 'libqxcb.so' -print -quit)"

if [ -n "$QXCB" ]; then
    echo "[OK] $QXCB"
else
    echo "[WARNING] libqxcb.so not found."
fi

# ------------------------------------------------------------
# Check missing dependencies inside AppImage
# ------------------------------------------------------------

echo
echo "============================================"
echo " Checking HOTR binary dependencies"
echo "============================================"

HOTR_BINARY="$(find squashfs-root -path '*/usr/bin/HookOfTheReaper' -print -quit)"

if [ -n "$HOTR_BINARY" ]; then
    LD_LIBRARY_PATH="$(pwd)/squashfs-root/usr/lib:$(pwd)/squashfs-root/usr/lib/x86_64-linux-gnu" \
        ldd "$HOTR_BINARY" || true
else
    echo "ERROR: HookOfTheReaper binary wasn't found inside AppImage."
fi

# ------------------------------------------------------------
# Determine highest GLIBC requirement
# ------------------------------------------------------------

echo
echo "============================================"
echo " Highest GLIBC requirements"
echo "============================================"

find squashfs-root -type f -print0 |
while IFS= read -r -d '' FILE; do
    if file "$FILE" | grep -q 'ELF'; then
        objdump -T "$FILE" 2>/dev/null |
            grep -o 'GLIBC_[0-9.]*' || true
    fi
done |
sort -Vu |
tail -20

# ------------------------------------------------------------
# Determine highest GLIBCXX requirement
# ------------------------------------------------------------

echo
echo "============================================"
echo " Highest GLIBCXX requirements"
echo "============================================"

find squashfs-root -type f -print0 |
while IFS= read -r -d '' FILE; do
    if file "$FILE" | grep -q 'ELF'; then
        strings "$FILE" 2>/dev/null |
            grep -o 'GLIBCXX_[0-9.]*' || true
    fi
done |
sort -Vu |
tail -20

# ------------------------------------------------------------
# Determine highest CXXABI requirement
# ------------------------------------------------------------

echo
echo "============================================"
echo " Highest CXXABI requirements"
echo "============================================"

find squashfs-root -type f -print0 |
while IFS= read -r -d '' FILE; do
    if file "$FILE" | grep -q 'ELF'; then
        strings "$FILE" 2>/dev/null |
            grep -o 'CXXABI_[0-9.]*' || true
    fi
done |
sort -Vu |
tail -20

echo
echo "Validation complete."

CONTAINER_EOF

# ------------------------------------------------------------
# Host-side final check
# ------------------------------------------------------------

echo
echo "[3/4] Checking output..."

if [ ! -f "$SCRIPT_DIR/$OUTPUT" ]; then
    echo
    echo "ERROR: Build finished but $OUTPUT does not exist."
    exit 1
fi

chmod +x "$SCRIPT_DIR/$OUTPUT"

echo
echo "[4/4] Done."
echo
echo "============================================"
echo " Build successful"
echo "============================================"
echo
ls -lh "$SCRIPT_DIR/$OUTPUT"
echo
echo "Output:"
echo "  $SCRIPT_DIR/$OUTPUT"
echo
echo "For Batocera:"
echo "  1. Replace your existing HOTR AppImage with this file."
echo "  2. chmod +x $OUTPUT"
echo "  3. Keep your HOTR data directory in the expected location."
echo