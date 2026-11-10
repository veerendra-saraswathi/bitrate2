#!/usr/bin/env bash
# Run the score_top3 harness on a Jetson (native build assumed)
# Usage: ./scripts/run_on_jetson.sh <rawfile> <W> <H> [bitdepth]
set -euo pipefail

RAW=${1:?}
W=${2:?}
H=${3:?}
BITS=${4:-14}

echo "=== Jetson device ==="
cat /etc/nv_tegra_release 2>/dev/null || true
uname -a
nvidia-smi -L 2>/dev/null || true
echo

# ensure build
if [[ ! -x build/ewc_enc ]]; then
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build -j
fi

./scripts/score_top3.sh "$RAW" "$W" "$H" "$BITS"

echo
echo "Results in score_out/score.csv"
cat score_out/score.csv
