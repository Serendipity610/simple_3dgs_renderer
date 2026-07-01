#!/usr/bin/env bash
set -euo pipefail

cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Debug
