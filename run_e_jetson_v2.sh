#!/bin/sh
# Jetson / host runner for Architecture E (v2).
# Includes metadata budget check and optional crops.
#
# Usage:
#   ./run_e_jetson_v2.sh <14bit.u16> [frames] [build-dir]
set -e
IN=${1:?need .u16 path}
FRAMES=${2:-4}
BIN=${3:-./build}
W=640
H=512
BD=14
BUDGET=8333
META=128

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
  echo "metadata_bytes=$META"
  echo "frames=$FRAMES"
  echo "peak=$(( (1 << BD) - 1 ))"
} | tee "$OUTDIR/meta.txt"

echo
echo "=== Wavelet PoC + ${META}B metadata (strict $BUDGET) ===" | tee -a "$OUTDIR/log.txt"
$BIN/ewc_validate --in "$IN" --w $W --h $H --bd $BD --budget $BUDGET \
  --frames $FRAMES --meta $META --crops --strict \
  --csv "$OUTDIR/poc_meta.csv" | tee -a "$OUTDIR/log.txt"

# move crops into the results dir if they were written in cwd
for f in crop_orig_*.pgm crop_rec_*.pgm; do
  [ -f "$f" ] && mv "$f" "$OUTDIR/"
done

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
