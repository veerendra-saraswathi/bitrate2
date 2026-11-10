#!/bin/sh
# Real-frame runner for Architecture E.
# Usage: ./run_real_e.sh <path-to-14bit.u16> [frames]
# Records commit, input hash, settings and CSV.
set -e
IN=${1:?need .u16 path}
FRAMES=${2:-10}
BIN=${3:-./build}
W=640
H=512
BD=14
BUDGET=8333

COMMIT=$(git rev-parse --short HEAD 2>/dev/null || echo "unknown")
HASH=$(shasum -a 256 "$IN" 2>/dev/null | awk '{print $1}' || md5sum "$IN" | awk '{print $1}')
HOST=$(uname -s)-$(uname -m)
DATE=$(date -u +%Y-%m-%dT%H:%M:%SZ)

echo "commit=$COMMIT"
echo "input=$IN"
echo "sha256=$HASH"
echo "host=$HOST"
echo "date=$DATE"
echo "geometry=${W}x${H} bd=$BD budget=$BUDGET frames=$FRAMES"
echo

echo "=== Wavelet PoC (strict 8333) ==="
$BIN/ewc_validate --in "$IN" --w $W --h $H --bd $BD --budget $BUDGET --frames $FRAMES --strict --csv poc_${COMMIT}.csv

echo
echo "=== D+ lossless ==="
$BIN/ewc_validate --in "$IN" --w $W --h $H --bd $BD --lossless --frames $FRAMES --strict --csv dplus_ll_${COMMIT}.csv

echo
echo "=== D+ near=4 ==="
$BIN/ewc_validate --in "$IN" --w $W --h $H --bd $BD --near 4 --frames $FRAMES --strict --csv dplus_n4_${COMMIT}.csv

echo
echo "Done. CSVs written."
