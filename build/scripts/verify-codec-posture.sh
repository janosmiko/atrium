#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
# Pins the four facts the licensing question rests on, so a Chromium
# rebase cannot quietly change the answer while the decision is still open.
#
# The question is whether Atrium can drop the bundled H.264/AAC decoders and let
# Apple's already-licensed frameworks do the work. Video: yes. Audio: no, and
# the reason is one CHECK in Chromium's macOS AAC decoder. If a rebase moves
# that CHECK, the answer changes and this guard is how anyone finds out.
#
# Each assertion prints what it read, so the output is the evidence.
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="${ATRIUM_CHROMIUM_SRC:-${HOME}/chromium-atrium/src}"
INVERT=0
[ "${1:-}" = "--invert" ] && INVERT=1

fail() { echo "codec-posture: FAILED: $*" >&2; exit 1; }

# 1. What Atrium's own build asks for. This is the state that carries the
# licensing exposure: Chrome branding compiles the patented decoders in.
ARGS="${REPO}/build/args.gn"
grep -q '^proprietary_codecs = true$' "${ARGS}" ||
  fail "build/args.gn no longer sets proprietary_codecs = true"
if [ "${INVERT}" = 1 ]; then
  grep -q '^ffmpeg_branding = "Chromium"$' "${ARGS}" ||
    fail "(--invert) build/args.gn is not on Chromium branding"
else
  grep -q '^ffmpeg_branding = "Chrome"$' "${ARGS}" ||
    fail "build/args.gn no longer sets ffmpeg_branding = \"Chrome\"; if that is" \
         "deliberate, this guard needs updating"
fi
echo "codec-posture: args.gn: proprietary_codecs=true, ffmpeg_branding=Chrome"

if [ ! -d "${SRC}" ]; then
  echo "codec-posture: no Chromium checkout at ${SRC}; skipped assertions 2-4"
  echo "codec-posture: OK (partial)"
  exit 0
fi

CFG="${SRC}/third_party/ffmpeg/chromium/config"
ARCH="$(uname -m)"; [ "${ARCH}" = "x86_64" ] && ARCH=x64

# 2. Chrome branding really does bundle the decoders the licence attaches to.
CHROME_CFG="${CFG}/Chrome/mac/${ARCH}/config_components.h"
grep -q '^#define CONFIG_H264_DECODER 1$' "${CHROME_CFG}" ||
  fail "Chrome branding no longer compiles ffmpeg's H.264 decoder"
grep -q '^#define CONFIG_AAC_DECODER 1$' "${CHROME_CFG}" ||
  fail "Chrome branding no longer compiles ffmpeg's AAC decoder"
echo "codec-posture: Chrome branding bundles ffmpeg H264+AAC decoders"

# 3. Chromium branding drops both, and keeps the mp4 demuxer. That is what
# makes platform decoding possible at all: the container is still parsed.
CHROMIUM_CFG="${CFG}/Chromium/mac/${ARCH}/config_components.h"
grep -q '^#define CONFIG_H264_DECODER 0$' "${CHROMIUM_CFG}" ||
  fail "Chromium branding now bundles an H.264 decoder"
grep -q '^#define CONFIG_AAC_DECODER 0$' "${CHROMIUM_CFG}" ||
  fail "Chromium branding now bundles an AAC decoder"
grep -q '^#define CONFIG_MOV_DEMUXER 1$' "${CHROMIUM_CFG}" ||
  fail "Chromium branding no longer builds the mp4/mov demuxer"
echo "codec-posture: Chromium branding drops both decoders, keeps mov demuxer"

# 4. THE ONE THAT DECIDES IT. macOS routes AAC to AudioToolbox only for the
# xHE-AAC profile; the decoder CHECKs the profile and hands AudioToolbox a USAC
# format id. AAC-LC - what the whole web ships alongside H.264 - has no path
# but ffmpeg. Drop Chrome branding today and audio breaks, video does not.
DEC="${SRC}/media/filters/mac/audio_toolbox_audio_decoder.cc"
grep -q 'CHECK_EQ(config.profile(), AudioCodecProfile::kXHE_AAC)' "${DEC}" ||
  fail "the macOS AAC decoder no longer restricts itself to xHE-AAC" \
       "(${DEC}); AAC-LC may now have a platform path - re-open the licensing question"
echo "codec-posture: macOS AudioToolbox AAC path is still xHE-AAC only"

echo "codec-posture: OK"
