#!/usr/bin/env bash
# Single-pass Jetson sweep: E-wavelet, D+, layered on key extremes
set -euo pipefail
mkdir -p jetson_results

run() {
  local name=$1 raw=$2 w=$3 h=$4 bd=$5
  echo "===== $name (${w}x${h} ${bd}-bit) ====="
  ./scripts/score_top3.sh "$raw" "$w" "$h" "$bd"
  cp -f score_out/score.csv "jetson_results/${name}.csv"
  echo
}

# real 16-bit IR
run ltir16_car00 "$HOME/ombitrate/data/real/ltir16/ltir16_car_00.u16" 640 480 16

# synthetic bit-depth sweep
run ir_8   data/synthetic/ir/ir_640x512_8.u16   640 512 8
run ir_10  data/synthetic/ir/ir_640x512_10.u16  640 512 10
run ir_12  data/synthetic/ir/ir_640x512_12.u16  640 512 12
run ir_14  data/synthetic/ir/ir_640x512_14.u16  640 512 14
run ir_16  data/synthetic/ir/ir_640x512_16.u16  640 512 16

# high-res extreme
run grad_1920 data/synthetic/highbit/grad_1920x1200_14.u16 1920 1200 14

echo "Done. Results in jetson_results/"
ls -l jetson_results/
