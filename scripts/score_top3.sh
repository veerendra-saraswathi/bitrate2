#!/usr/bin/env bash
# score_top3.sh — score E-tiled, E-D+, and two-layer on one raw frame
# Usage: ./scripts/score_top3.sh <rawfile> <width> <height> [bitdepth=14]
set -euo pipefail

RAW=${1:?raw file}
W=${2:?width}
H=${3:?height}
BITS=${4:-14}
FPS=30
BUDGET=$(( (2000000 / FPS + 7) / 8 ))

BIN=./build
OUT=./score_out
mkdir -p "$OUT"

# Auto-convert packed 8-bit → uint16 if needed
INPUT="$RAW"
if [[ "$BITS" == "8" ]]; then
  expected_packed=$((W * H))
  actual=$(stat -f%z "$RAW" 2>/dev/null || stat -c%s "$RAW")
  if [[ "$actual" -eq "$expected_packed" ]]; then
    echo "Detected packed 8-bit; converting to uint16..."
    python3 - << PY
import numpy as np
raw = np.fromfile("$RAW", dtype=np.uint8)
u16 = raw.astype(np.uint16)
u16.tofile("$OUT/input_u16.raw")
print("converted", u16.nbytes, "bytes")
PY
    INPUT="$OUT/input_u16.raw"
  fi
fi

CSV="$OUT/score.csv"
echo "arch,bytes,mbps,psnr,max_err,encode_ms,note" > "$CSV"

run_enc() {
  local name=$1
  shift
  local out="$OUT/${name}.ewc"
  local stats="$OUT/${name}_stats.csv"
  local log="$OUT/${name}.log"
  echo "=== $name ==="
  /usr/bin/time -p "$BIN/ewc_enc" -i "$INPUT" -o "$out" -W "$W" -H "$H" --bd "$BITS" \
    "$@" --stats "$stats" >"$log" 2>&1 || true

  local bytes=$(wc -c < "$out" 2>/dev/null || echo 0)
  local mbps=$(awk -v b="$bytes" -v f="$FPS" 'BEGIN{printf "%.2f", b*8*f/1e6}')

  # extract latency from log
  local enc_ms=$(grep -oE 'median [0-9.]+' "$log" | awk '{print $2}' | head -1)
  enc_ms=${enc_ms:-}

  # extract any psnr / max_err if the encoder wrote them (future-proof)
  local psnr=$(grep -oiE 'psnr[=: ]*[0-9.]+' "$log" | grep -oE '[0-9.]+' | head -1 || true)
  local maxerr=$(grep -oiE 'max.?err[=: ]*[0-9.]+' "$log" | grep -oE '[0-9.]+' | head -1 || true)

  echo "$name,$bytes,$mbps,${psnr:-},${maxerr:-},${enc_ms:-}," >> "$CSV"
  echo "  bytes=$bytes  (~${mbps} Mbps)  enc=${enc_ms:-?} ms"
  grep -E "frames|latency|kbps" "$log" || true
}

# 1. E-tiled wavelet (exact 2 Mbps)
run_enc wavelet --tile 128 --tile-levels 4 --frame-bytes "$BUDGET" --threads 2

# 2. E-D+ lossless
run_enc dplus_ll --lossless

# 3. E-D+ NEAR=4
run_enc dplus_n4 --lossless --near 4

# 4. Two-layer — parse its own CSV
echo "=== layered ==="
/usr/bin/time -p "$BIN/ewc_layered" "$INPUT" "$W" "$H" 1 "$BITS" "$BUDGET" 16 \
  "$OUT/layered_rec.u16" "$OUT/layered_stats.csv" "$OUT/layered.ewc" \
  > "$OUT/layered.log" 2>&1 || true

if [[ -f "$OUT/layered_stats.csv" ]]; then
  # header: frame,base_bytes,enh_bytes,total,fits,maxerr,enc_ms,dec_ms,trials
  IFS=',' read -r _ base enh total fits maxerr enc_ms dec_ms trials < <(tail -n1 "$OUT/layered_stats.csv")
  local_mbps=$(awk -v b="$total" -v f="$FPS" 'BEGIN{printf "%.2f", b*8*f/1e6}')
  echo "layered,$total,$local_mbps,,$maxerr,$enc_ms,fits=$fits trials=$trials" >> "$CSV"
  echo "  total=$total  (~${local_mbps} Mbps)  maxerr=$maxerr  enc=${enc_ms} ms"
fi

echo
echo "Results: $CSV"
echo "Logs and stats in $OUT/"
cat "$CSV"
