#!/bin/bash

set -e

GENERAL="\
   --enable-cross-compile \
   --arch=aarch64 \
   --cross-prefix=aarch64-none-elf- \
   --target-os=horizon"

MODULES="\
   --disable-filters \
   --disable-programs \
   --disable-network \
   --disable-avfilter \
   --disable-postproc \
   --disable-encoders \
   --disable-protocols \
   --disable-hwaccels \
   --disable-doc"

VIDEO_DECODERS="\
   --enable-decoder=h264 \
   --enable-decoder=mpeg4 \
   --enable-decoder=mpeg2video \
   --enable-decoder=mjpeg \
   --enable-decoder=mjpegb"

AUDIO_DECODERS="\
    --enable-decoder=aac \
    --enable-decoder=aac_latm \
    --enable-decoder=atrac3 \
    --enable-decoder=atrac3p \
    --enable-decoder=mp3 \
    --enable-decoder=pcm_s16le \
    --enable-decoder=pcm_s8"
  
DEMUXERS="\
    --enable-demuxer=h264 \
    --enable-demuxer=m4v \
    --enable-demuxer=mpegvideo \
    --enable-demuxer=mpegps \
    --enable-demuxer=mp3 \
    --enable-demuxer=avi \
    --enable-demuxer=aac \
    --enable-demuxer=pmp \
    --enable-demuxer=oma \
    --enable-demuxer=pcm_s16le \
    --enable-demuxer=pcm_s8 \
    --enable-demuxer=wav"

VIDEO_ENCODERS="\
	  --enable-encoder=huffyuv \
	  --enable-encoder=ffv1 \
	  --enable-encoder=mjpeg"

AUDIO_ENCODERS="\
	  --enable-encoder=pcm_s16le"

MUXERS="\
  	  --enable-muxer=avi"


PARSERS="\
    --enable-parser=h264 \
    --enable-parser=mpeg4video \
    --enable-parser=mpegaudio \
    --enable-parser=mpegvideo \
    --enable-parser=aac \
    --enable-parser=aac_latm"

if [ -f /opt/devkitpro/switchvars.sh ]; then
   source /opt/devkitpro/switchvars.sh
else
   export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
   export DEVKITA64="${DEVKITA64:-${DEVKITPRO}/devkitA64}"
   export PORTLIBS_PREFIX="${PORTLIBS_PREFIX:-${DEVKITPRO}/portlibs/switch}"
   export PATH="${DEVKITA64}/bin:${DEVKITPRO}/tools/bin:${PATH}"
fi

FFMPEG_SWITCH_PREFIX="${FFMPEG_SWITCH_PREFIX:-$(pwd)/switch_build}"
mkdir -p "${FFMPEG_SWITCH_PREFIX}"
export TMPDIR="$(pwd)/../.codex_tmp/msys_tmp"
export TMP="${TMPDIR}"
export TEMP="${TMPDIR}"
mkdir -p "${TMPDIR}"

# GitHub checkouts can lose the executable mode of FFmpeg's helper scripts.
# The generated Makefiles invoke version.sh directly, so include it explicitly.
find . -type f \( -name '*.sh' -o -name 'version.sh' \) -exec chmod +x {} +

bash ./configure \
--prefix="${FFMPEG_SWITCH_PREFIX}" \
${GENERAL} \
--extra-cflags='-g -D__SWITCH__ -D_GNU_SOURCE -O3 -march=armv8-a -mtune=cortex-a57 -mtp=soft -fPIE -pie -ffunction-sections -fdata-sections -ftls-model=local-exec' \
--extra-cxxflags='-g -D__SWITCH__ -D_GNU_SOURCE -O3 -march=armv8-a -mtune=cortex-a57 -mtp=soft -fPIE -pie -ffunction-sections -fdata-sections -ftls-model=local-exec' \
--extra-ldflags='-g -fPIE -pie -L${PORTLIBS_PREFIX}/lib -L${DEVKITPRO}/libnx/lib ' \
--disable-shared \
--enable-static \
--enable-zlib \
--disable-runtime-cpudetect \
--disable-everything \
${MODULES} \
${VIDEO_DECODERS} \
${AUDIO_DECODERS} \
${VIDEO_ENCODERS} \
${AUDIO_ENCODERS} \
${DEMUXERS} \
${MUXERS} \
${PARSERS}

make clean
make -j4 install

echo "Switch (libnx) build finished: ${FFMPEG_SWITCH_PREFIX}"
