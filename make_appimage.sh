#!/bin/bash
# Builds HookOfTheReaper and packages it as a portable AppImage.
# Run from the repo root. Requires: cmake, Qt6, hidapi-libusb.
#
# The resulting HookOfTheReaper-x86_64.AppImage is self-contained — copy it
# plus the data/ directory to any Linux x86_64 machine (including Batocera)
# and it will run without any additional dependencies.

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

LINUXDEPLOY="$SCRIPT_DIR/linuxdeploy-x86_64.AppImage"
LINUXDEPLOY_QT="$SCRIPT_DIR/linuxdeploy-plugin-qt-x86_64.AppImage"

# --- Download linuxdeploy tools if not present ---
if [ ! -f "$LINUXDEPLOY" ]; then
    echo "[1/4] Downloading linuxdeploy..."
    wget -q --show-progress -O "$LINUXDEPLOY" \
        "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
    chmod +x "$LINUXDEPLOY"
else
    echo "[1/4] linuxdeploy already present."
fi

if [ ! -f "$LINUXDEPLOY_QT" ]; then
    echo "      Downloading linuxdeploy-plugin-qt..."
    wget -q --show-progress -O "$LINUXDEPLOY_QT" \
        "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage"
    chmod +x "$LINUXDEPLOY_QT"
fi

# --- Build ---
echo "[2/4] Building HookOfTheReaper..."
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr > /dev/null
cmake --build build -j"$(nproc)"

# --- Assemble AppDir ---
echo "[3/4] Assembling AppDir..."
rm -rf AppDir
mkdir -p AppDir/usr/bin
mkdir -p AppDir/usr/share/applications
mkdir -p AppDir/usr/share/icons/hicolor/256x256/apps

cp build/HookOfTheReaper AppDir/usr/bin/HookOfTheReaper
cp data/icons/hOTRIcon256.png AppDir/usr/share/icons/hicolor/256x256/apps/HookOfTheReaper.png
cp HookOfTheReaper.desktop AppDir/usr/share/applications/HookOfTheReaper.desktop

# Write a custom AppRun so Qt uses xcb and the binary is launched correctly.
# linuxdeploy won't overwrite an existing AppRun.
cat > AppDir/AppRun << 'APPRUN_EOF'
#!/bin/bash
HERE="$(dirname "$(readlink -f "$0")")"
export PATH="$HERE/usr/bin:$PATH"
export LD_LIBRARY_PATH="$HERE/usr/lib:$LD_LIBRARY_PATH"
export QT_PLUGIN_PATH="$HERE/usr/plugins"
export QML2_IMPORT_PATH="$HERE/usr/qml"
# Force xcb so the app doesn't try Wayland/unknown platforms inside AppImage
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}"
exec "$HERE/usr/bin/HookOfTheReaper" "$@"
APPRUN_EOF
chmod +x AppDir/AppRun

# --- Bundle with linuxdeploy ---
echo "[4/4] Bundling Qt and all libraries into AppImage..."

# Find qmake — prefer qmake6
QMAKE_BIN=$(command -v qmake6 2>/dev/null || command -v qmake 2>/dev/null || echo "")
if [ -z "$QMAKE_BIN" ]; then
    echo "ERROR: qmake not found. Install Qt6 development tools."
    exit 1
fi

# NO_STRIP=1: linuxdeploy's bundled strip is too old for CachyOS's RELR relocations
QMAKE="$QMAKE_BIN" \
OUTPUT="HookOfTheReaper-x86_64.AppImage" \
NO_STRIP=1 \
"$LINUXDEPLOY" \
    --appdir AppDir \
    --plugin qt \
    --output appimage \
    --desktop-file HookOfTheReaper.desktop \
    --icon-file data/icons/hOTRIcon256.png

echo ""
echo "=== Done ==="
echo "AppImage: $SCRIPT_DIR/HookOfTheReaper-x86_64.AppImage"
echo ""
echo "To deploy to Batocera (or any Linux x86_64 machine):"
echo "  1. Copy HookOfTheReaper-x86_64.AppImage to the target machine"
echo "  2. Copy the data/ directory alongside it (same folder)"
echo "  3. chmod +x HookOfTheReaper-x86_64.AppImage"
echo "  4. Run: ./HookOfTheReaper-x86_64.AppImage"
echo ""
echo "The data/ directory stores your gun configs and settings — keep it with the AppImage."
