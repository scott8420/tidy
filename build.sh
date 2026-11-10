#!/usr/bin/env bash
# Tidy build. Deps -- Fedora: gtkmm4.0-devel spdlog-devel cmake gcc-c++ (ninja-build, ccache optional);
# Ubuntu: libgtkmm-4.0-dev libspdlog-dev cmake g++. And latch, installed (its ./install.sh).
set -e
cd "$(dirname "${BASH_SOURCE[0]}")"
if [[ ! -d build ]] && command -v ninja >/dev/null; then
    cmake -B build -G Ninja "$@"
else
    cmake -B build "$@"
fi
cmake --build build -j"$(nproc)"
echo ""
echo "Built: ./build/tidy  and  ./build/tidy_selftest"
