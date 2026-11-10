#!/bin/sh
# Day-1 validation for Architecture E.
# Usage: ./validate_day1.sh [path-to-build-dir]
set -e
BIN=${1:-.}
echo "=== Budget + recon (wavelet PoC) ==="
$BIN/ewc_validate --poc --frames 4 --strict --csv poc_wavelet.csv

echo
echo "=== D+ lossless ==="
$BIN/ewc_validate --w 640 --h 512 --bd 14 --lossless --frames 2 --strict --csv dplus_lossless.csv

echo
echo "=== D+ near=4 ==="
$BIN/ewc_validate --w 640 --h 512 --bd 14 --near 4 --frames 2 --strict --csv dplus_near4.csv

echo
echo "=== High-bit-depth extrema (14-bit) ==="
$BIN/ewc_validate --w 256 --h 256 --bd 14 --synthetic extrema --frames 2 --budget 2000 --strict

echo
echo "=== High-bit-depth extrema (16-bit) ==="
$BIN/ewc_validate --w 256 --h 256 --bd 16 --synthetic extrema --frames 2 --budget 2500 --strict

echo
echo "=== Large metadata (still under budget) ==="
$BIN/ewc_validate --poc --frames 2 --meta 128 --strict

echo
echo "Day-1 checks complete."
