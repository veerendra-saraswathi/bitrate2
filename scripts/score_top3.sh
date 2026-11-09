#!/usr/bin/env bash
# score_top3.sh — score E-tiled, E-D+, and two-layer with full reconstruction metrics
# Usage: ./scripts/score_top3.sh <rawfile> <width> <height> [bitdepth=14]
set -euo pipefail

RAW=${1:?raw file}
W=${2:?width}
H=${3:?height}
BITS=${4:-14}
FPS=30
BUDGET=$(( (2000000 / FPS + 7) / 8 ))
DEADLINE_MS=$(awk -v f="$FPS" 'BEGIN{printf "%.2f", 2000.0/f}')   # 2 frames

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
echo "arch,bytes,mbps,budget_pass,psnr,rmse,max_err,bit_exact,enc_med_ms,enc_p95_ms,enc_max_ms,deadline_miss,note" > "$CSV"

metric_from_dec() {
  local name=$1
  local ewc="$OUT/${name}.ewc"
  local rec="$OUT/${name}_rec.raw"
  local report="$OUT/${name}_report.csv"
  "$BIN/ewc_dec" -i "$ewc" -o "$rec" --ref "$INPUT" --report "$report" >"$OUT/${name}_dec.log" 2>&1 || true
  if [[ -f "$report" ]]; then
    # frame,seq,type,bytes,...,psnr,maxerr
    IFS=',' read -r _ _ _ bytes _ _ _ _ _ _ _ _ _ _ _ psnr maxerr < <(tail -n1 "$report")
    echo "$psnr,$maxerr,$bytes"
  else
    echo ",,0"
  fi
}

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
  local budget_pass=$(awk -v b="$bytes" -v bud="$BUDGET" 'BEGIN{print (b<=bud)?1:0}')
  
  local enc_med=$(grep -oE 'latency ms: median [0-9.]+' "$log" | awk '{print $4}' | head -1)
  local enc_p95=$(grep -oE 'p99 [0-9.]+' "$log" | awk '{print $2}' | head -1)
  local enc_max=$(grep -oE 'latency ms:.*max [0-9.]+' "$log" | grep -oE 'max [0-9.]+' | awk '{print $2}' | head -1)
  local miss=$(awk -v m="${enc_max:-0}" -v d="$DEADLINE_MS" 'BEGIN{print (m>d)?1:0}')

  local metrics
  metrics=$(metric_from_dec "$name")
  local psnr maxerr dec_bytes
  IFS=',' read -r psnr maxerr dec_bytes <<< "$metrics"

  local bit_exact=0
  [[ "${maxerr:-999}" == "0" ]] && bit_exact=1

  # RMSE from PSNR if we have it (8-bit or 14-bit peak)
  local peak=$(( (1<<BITS) - 1 ))
  local rmse=""
  if [[ -n "${psnr:-}" ]]; then
    rmse=$(awk -v p="$psnr" -v pk="$peak" 'BEGIN{printf "%.3f", pk/10^(p/20)}')
  fi

  echo "$name,$bytes,$mbps,$budget_pass,${psnr:-},${rmse:-},${maxerr:-},$bit_exact,${enc_med:-},${enc_p95:-},${enc_max:-},$miss," >> "$CSV"
  echo "  bytes=$bytes (~${mbps} Mbps) budget=$budget_pass psnr=${psnr:-?} maxerr=${maxerr:-?} enc_max=${enc_max:-?}ms miss=$miss"
}

# 1. E-tiled wavelet
run_enc wavelet --tile 128 --tile-levels 4 --frame-bytes "$BUDGET" --threads 2

# 2. E-D+ lossless
run_enc dplus_ll --lossless

# 3. E-D+ NEAR=4
run_enc dplus_n4 --lossless --near 4

# 4. Two-layer (k=16 explicit)
echo "=== layered (k=16) ==="
/usr/bin/time -p "$BIN/ewc_layered" "$INPUT" "$W" "$H" 1 "$BITS" "$BUDGET" 16 \
  "$OUT/layered_rec.u16" "$OUT/layered_stats.csv" "$OUT/layered.ewc" \
  > "$OUT/layered.log" 2>&1 || true

if [[ -f "$OUT/layered_stats.csv" ]]; then
  IFS=',' read -r _ base enh total fits maxerr enc_ms dec_ms trials < <(tail -n1 "$OUT/layered_stats.csv")
  local_mbps=$(awk -v b="$total" -v f="$FPS" 'BEGIN{printf "%.2f", b*8*f/1e6}')
  budget_pass=$(awk -v b="$total" -v bud="$BUDGET" 'BEGIN{print (b<=bud)?1:0}')
  miss=$(awk -v m="$enc_ms" -v d="$DEADLINE_MS" 'BEGIN{print (m>d)?1:0}')
  # decode for PSNR confirmation
  metrics=$(metric_from_dec layered)
  IFS=',' read -r psnr _ _ <<< "$metrics"
  peak=$(( (1<<BITS) - 1 ))
  rmse=""
  [[ -n "${psnr:-}" ]] && rmse=$(awk -v p="$psnr" -v pk="$peak" 'BEGIN{printf "%.3f", pk/10^(p/20)}')
  echo "layered,$total,$local_mbps,$budget_pass,${psnr:-},${rmse:-},$maxerr,0,${enc_ms:-},${enc_ms:-},${enc_ms:-},$miss,k=16 fits=$fits trials=$trials" >> "$CSV"
  echo "  total=$total (~${local_mbps} Mbps) maxerr=$maxerr (configured k=16) enc=${enc_ms}ms miss=$miss"
fi

echo
echo "Results: $CSV"
cat "$CSV"
