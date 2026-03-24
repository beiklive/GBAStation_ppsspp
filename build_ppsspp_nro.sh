#!/bin/bash
# Build PPSSPP NRO with Tico Overlay
# Step 1: Build PPSSPP libretro core using its own Makefile (platform=libnx)
# Step 2: Build Tico NRO (CMake) linking against the core .a

set -e

export DEVKITPRO=/opt/devkitpro
export DEVKITARM=$DEVKITPRO/devkitARM
export DEVKITPPC=$DEVKITPRO/devkitPPC
export DEVKITA64=$DEVKITPRO/devkitA64

echo "=== Step 1: Building PPSSPP libretro core ==="
cd /project/libretro

# Clean previous build
# make platform=libnx clean || true

# Build the core (this handles ffmpeg, at3, everything)
make platform=libnx -j$(nproc)

CORE_LIB="/project/libretro/ppsspp_libretro_libnx.a"
if [ ! -f "$CORE_LIB" ]; then
    echo "Error: ppsspp_libretro_libnx.a not found after build"
    ls -la *.a 2>/dev/null || true
    exit 1
fi
echo "Core built: $CORE_LIB"

echo ""
echo "=== Step 2: Building Tico NRO ==="
cd /project
rm -rf build_tico
mkdir -p build_tico
cd build_tico

cmake ../tico \
    -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/Switch.cmake" \
    -DPPSSPP_CORE_LIB="$CORE_LIB" \
    -DPPSSPP_SOURCE_DIR="/project" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_FLAGS="-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE -Os -ffunction-sections -fdata-sections -D__SWITCH__ -fno-lto -fvisibility=hidden -fomit-frame-pointer -fno-ident -fstack-protector-strong -DDISABLE_LOGGING=1" \
    -DCMAKE_CXX_FLAGS="-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE -Os -ffunction-sections -fdata-sections -D__SWITCH__ -fno-lto -fvisibility=hidden -fvisibility-inlines-hidden -fomit-frame-pointer -fno-ident -fstack-protector-strong -DDISABLE_LOGGING=1"

make -j$(nproc)

if [ -f "ppsspp_tico.nro" ]; then
    echo "======================================"
    echo "Build successful!"
    echo "Output: $(pwd)/ppsspp_tico.nro"
    echo "======================================"
else
    echo "Error: ppsspp_tico.nro not found"
    exit 1
fi
