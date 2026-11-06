#!/usr/bin/env bash
# run_poc.sh — RCI Scope of Work proof-of-concept, end to end, with the ewc tools only.
#
#   1. encode a raw 14-bit 640x512 sequence at 2 Mbps (PoC profile) and store the bitstream
#   2. decode it to a raw 14-bit file (2 bytes/pixel, row-major) and compare with the input
#   3. introduce losses into the stored bitstream, decode again, report quality and validation
#   4. report encoder and decoder latency
#
# usage: ./run_poc.sh input.u16 [fps=30] [build_dir=build] [out_dir=poc_out]
# input: raw little-endian uint16, 640x512 per frame, 14-bit values (0..16383), no header.
set -euo pipefail
IN=${1:?usage: run_poc.sh input.u16 [fps] [build_dir] [out_dir]}
FPS=${2:-30}
BIN=${3:-build}
OUT=${4:-poc_out}
mkdir -p "$OUT"
FRAMES=$(( $(stat -c%s "$IN") / (640 * 512 * 2) ))
echo "== input: $IN  ($FRAMES frames, 640x512, 14-bit), fps $FPS, budget $((2000000 / 8 / FPS)) bytes/frame"

echo; echo "== 1. encode (PoC profile: 2 Mbps strict CBR, intra-only, CRC + redundancy)"
"$BIN/ewc_enc" -i "$IN" -o "$OUT/stream.ewc" --poc --fps "$FPS" --meta "RCI-PoC ewc fps=$FPS" --stats "$OUT/encode_stats.csv"

echo; echo "== 2. decode clean stream -> raw 14-bit"
"$BIN/ewc_dec" -i "$OUT/stream.ewc" -o "$OUT/decoded_clean.u16" --ref "$IN" --frames "$FRAMES" --report "$OUT/decode_clean.csv"

echo; echo "== 3. losses in the stored bitstream"
for cond in "ber 1e-6" "ber 1e-5" "pktzero 0.01" "pktzero 0.05" "pktdel 0.05" "burst 5"; do
  set -- $cond
  tag="${1}_${2}"
  echo "-- $1 p=$2"
  "$BIN/ewc_corrupt" "$OUT/stream.ewc" "$OUT/stream_$tag.ewc" "$1" "$2" 1
  "$BIN/ewc_dec" -i "$OUT/stream_$tag.ewc" -o "$OUT/decoded_$tag.u16" --ref "$IN" --frames "$FRAMES" --report "$OUT/decode_$tag.csv" \
    | grep -E "vs reference|SUMMARY"
done

echo; echo "== 4. latency (encoder: frame in memory -> encoded unit complete; decoder: one decode call)"
stat_line() {   # $1 = label, then values on stdin (one per line)
  sort -g | awk -v lab="$1" -v fps="$FPS" '{v[NR]=$1} END{if(!NR){print lab": no data"; exit}
    med=v[int((NR+1)/2)]; printf "%s ms: median %.2f  max %.2f  (= %.3f frame periods at max)\n", lab, med, v[NR], v[NR]*fps/1000}'
}
tail -n +2 "$OUT/encode_stats.csv" | cut -d, -f4 | stat_line encoder
tail -n +2 "$OUT/decode_clean.csv" | cut -d, -f15 | stat_line decoder
echo "SoW limit: encoder latency <= 2 frame periods. Measure with nothing else running."
echo; echo "outputs in $OUT/: stream.ewc, decoded_*.u16 (raw 14-bit), encode_stats.csv, decode_*.csv (per-frame validation)"
