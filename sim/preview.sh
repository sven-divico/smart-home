#!/usr/bin/env bash
# Build the host UI, render the pages to build/preview/*.pgm, convert to PNG.
# Usage: sim/preview.sh            (from the repo root)
set -euo pipefail
cd "$(dirname "$0")/.."
~/.platformio/penv/bin/pio run -e host_sim >/dev/null
mkdir -p build/preview
.pio/build/host_sim/program build/preview
python3 sim/pgm2png.py
