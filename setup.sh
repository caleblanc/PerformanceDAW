#!/usr/bin/env bash
# One-time setup: clone tracktion_engine (which vendors JUCE) and create build dir.
set -euo pipefail

echo "==> Initialising git repo..."
git init

echo "==> Adding tracktion_engine submodule..."
git submodule add https://github.com/Tracktion/tracktion_engine.git tracktion_engine

echo "==> Pulling tracktion_engine's own submodules (JUCE, etc.)..."
git -C tracktion_engine submodule update --init --recursive

echo "==> Configuring CMake (Release)..."
cmake -B build -DCMAKE_BUILD_TYPE=Release

echo ""
echo "Done. Build with:  cmake --build build --config Release -j$(sysctl -n hw.logicalcpu)"
