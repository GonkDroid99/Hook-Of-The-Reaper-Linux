#!/bin/bash
# Builds a portable HookOfTheReaper AppImage inside a Docker container.
# This produces a binary compatible with older glibc (Ubuntu 22.04 = glibc 2.35),
# which is required for Batocera and other non-rolling Linux distributions.
#
# Requirements: Docker installed and running.
# Run from the repo root.

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE="hotr-appimage-builder"
OUTPUT="HookOfTheReaper-x86_64.AppImage"

# Prefer Docker; fall back to Podman
DOCKER_CMD=$(command -v docker 2>/dev/null || command -v podman 2>/dev/null || true)
if [ -z "$DOCKER_CMD" ]; then
    echo "ERROR: Neither Docker nor Podman found."
    echo "Install with: sudo pacman -S podman"
    exit 1
fi
echo "Using: $DOCKER_CMD"

echo "=== Building $OUTPUT (Ubuntu 22.04 container) ==="

# Build the container image if not already built
if ! "$DOCKER_CMD" image inspect "$IMAGE" &>/dev/null; then
    echo "[1/3] Building container image (first time only, ~5 minutes)..."
    "$DOCKER_CMD" build -f "$SCRIPT_DIR/Dockerfile.appimage" -t "$IMAGE" "$SCRIPT_DIR"
else
    echo "[1/3] Container image already exists, skipping build."
fi

# Run the build inside the container
echo "[2/3] Compiling and packaging inside container..."
"$DOCKER_CMD" run --rm \
    --privileged \
    -v "$SCRIPT_DIR:/src" \
    -w /src \
    "$IMAGE" \
    bash -c '
        set -e

        # Build
        cmake -B build_docker -DCMAKE_BUILD_TYPE=Release > /dev/null
        cmake --build build_docker -j$(nproc)

        # Assemble AppDir
        rm -rf AppDir
        mkdir -p AppDir/usr/bin
        mkdir -p AppDir/usr/share/applications
        mkdir -p AppDir/usr/share/icons/hicolor/256x256/apps

        cp build_docker/HookOfTheReaper AppDir/usr/bin/HookOfTheReaper
        cp data/icons/hOTRIcon256.png AppDir/usr/share/icons/hicolor/256x256/apps/HookOfTheReaper.png
        cp HookOfTheReaper.desktop AppDir/usr/share/applications/HookOfTheReaper.desktop

        # Custom AppRun: force xcb platform so the app works without QT_QPA_PLATFORM set.
        # linuxdeploy will not overwrite an existing AppRun.
        cat > AppDir/AppRun << '"'"'APPRUN_EOF'"'"'
#!/bin/bash
HERE="$(dirname "$(readlink -f "$0")")"
export PATH="$HERE/usr/bin:$PATH"
export LD_LIBRARY_PATH="$HERE/usr/lib:$LD_LIBRARY_PATH"
export QT_PLUGIN_PATH="$HERE/usr/plugins"
export QML2_IMPORT_PATH="$HERE/usr/qml"
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}"
exec "$HERE/usr/bin/HookOfTheReaper" "$@"
APPRUN_EOF
        chmod +x AppDir/AppRun

        # Bundle with linuxdeploy.
        # X11/xcb display libs are excluded — they must come from the host system
        # because they need to match the X server version exactly.
        APPIMAGE_EXTRACT_AND_RUN=1 \
        QMAKE=/usr/bin/qmake6 \
        OUTPUT=HookOfTheReaper-x86_64.AppImage \
        /usr/local/bin/linuxdeploy \
            --appdir AppDir \
            --plugin qt \
            --output appimage \
            --desktop-file HookOfTheReaper.desktop \
            --icon-file data/icons/hOTRIcon256.png \
            --exclude-library libX11.so.6 \
            --exclude-library libXau.so.6 \
            --exclude-library libXdmcp.so.6 \
            --exclude-library libxcb.so.1 \
            --exclude-library "libxcb-*.so.*" \
            --exclude-library libxkbcommon.so.0 \
            --exclude-library libxkbcommon-x11.so.0 \
            --exclude-library libX11-xcb.so.1
    '

echo "[3/3] Done."
echo ""
echo "AppImage: $SCRIPT_DIR/$OUTPUT"
echo ""
echo "To deploy to Batocera:"
echo "  1. Copy $OUTPUT to Batocera (e.g. to /userdata/roms/ports/)"
echo "  2. Copy the data/ directory alongside it (same folder)"
echo "  3. chmod +x $OUTPUT"
echo "  4. Run: ./$OUTPUT"
echo ""
echo "The data/ folder stores your gun configs — keep it next to the AppImage."
