#!/bin/bash
# Builds Reel Studio and puts "Reel Studio.app" next to this script.
# First run downloads JUCE and Tracktion Engine (about 150 MB) and takes roughly 10 minutes.
set -e
cd "$(dirname "$0")"

if ! xcode-select -p >/dev/null 2>&1; then
  echo "Xcode command line tools are missing. Installing them now; run this script again when that finishes."
  xcode-select --install
  exit 1
fi
if ! command -v cmake >/dev/null 2>&1; then
  echo "CMake is missing. Install it with:  brew install cmake   (or from https://cmake.org/download)"
  exit 1
fi

JOBS=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j "$JOBS"

APP=$(find build -name "Reel Studio.app" -type d -path "*Release*" | head -1)
if [ -z "$APP" ]; then echo "Build finished but the app was not found."; exit 1; fi
rm -rf "Reel Studio.app"
cp -R "$APP" "Reel Studio.app"
echo ""
echo "Done. Open it with:  open \"Reel Studio.app\"   (or drag it to Applications)"
