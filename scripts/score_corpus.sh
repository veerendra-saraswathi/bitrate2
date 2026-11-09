#!/usr/bin/env bash
# Score every .u16 under data/synthetic with the top-3 harness
set -euo pipefail
mkdir -p results_corpus
find data/synthetic -name '*.u16' | sort | while read -r f; do
  base=$(basename "$f" .u16)
  echo "=== $base ==="
  # default 640x512; override if filename contains 1920
  W=640; H=512
  [[ "$base" == *1920* ]] && W=1920 && H=1200
  # bit depth from filename if present
  BD=8
  for b in 16 14 12 10 8; do
    [[ "$base" == *_${b} || "$base" == *${b}.u16 ]] && BD=$b && break
  done
  ./scripts/score_top3.sh "$f" "$W" "$H" "$BD" > "results_corpus/${base}.log" 2>&1 || true
  cp -f score_out/score.csv "results_corpus/${base}.csv" 2>/dev/null || true
done
echo "Done. CSVs and logs in results_corpus/"
