#!/bin/sh
# Jetson / host runner for Architecture E.
# Records commit, input hash, geometry, settings, host and per-frame CSV.
#
# Usage:
#   ./run_e_jetson.sh <14bit.u16> [frames] [build-dir]
#
# Example:
#   ./run_e_jetson.sh test_14bit_10f.u16 10 ./build
set -e
IN=${1:?need .u16 path}
FRAMES=${2:-10}
BIN=${3:-./build}
W=640
H=512
BD=14
BUDGET=8333

COMMIT=$(git rev-parse --short HEAD 2>/dev/null || echo "unknown")
HASH=$(shasum -a 256 "$IN" 2>/dev/null | awk '{print $1}' || md5 -q "$IN" 2>/dev/null || echo "nohash")
HOST=$(uname -s)-$(uname -m)
DATE=$(date -u +%Y-%m-%dT%H:%M:%SZ)
OUTDIR="results_${COMMIT}_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$OUTDIR"

{
  echo "commit=$COMMIT"
  echo "input=$IN"
  echo "sha256=$HASH"
  echo "host=$HOST"
  echo "date=$DATE"
  echo "geometry=${W}x${H}"
  echo "bit_depth=$BD"
  echo "budget=$BUDGET"
  echo "frames=$FRAMES"
  echo "peak=$(( (1 << BD) - 1 ))"
} | tee "$OUTDIR/meta.txt"

echo
echo "=== Wavelet PoC (strict $BUDGET) ===" | tee -a "$OUTDIR/log.txt"
$BIN/ewc_validate --in "$IN" --w $W --h $H --bd $BD --budget $BUDGET \
  --frames $FRAMES --strict --csv "$OUTDIR/poc.csv" | tee -a "$OUTDIR/log.txt"

echo | tee -a "$OUTDIR/log.txt"
echo "=== D+ lossless ===" | tee -a "$OUTDIR/log.txt"
$BIN/ewc_validate --in "$IN" --w $W --h $H --bd $BD --lossless \
  --frames $FRAMES --strict --csv "$OUTDIR/dplus_ll.csv" | tee -a "$OUTDIR/log.txt"

echo | tee -a "$OUTDIR/log.txt"
echo "=== D+ near=4 ===" | tee -a "$OUTDIR/log.txt"
$BIN/ewc_validate --in "$IN" --w $W --h $H --bd $BD --near 4 \
  --frames $FRAMES --strict --csv "$OUTDIR/dplus_n4.csv" | tee -a "$OUTDIR/log.txt"

echo | tee -a "$OUTDIR/log.txt"
echo "Results in $OUTDIR/" | tee -a "$OUTDIR/log.txt"
ls -la "$OUTDIR"
