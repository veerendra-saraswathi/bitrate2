#!/bin/bash
# Codec evaluation sweep. usage: sweep_baseline.sh BIN TAGPREFIX OUT.jsonl
B=$1; P=$2; O=$3; E=$(dirname "$0")/codec_eval.py
for s in pv24 seq28 csq7 pan90; do
  python3 $E $B ${P}intra2M $O $s -- --kbps 2000 --threads 1
  python3 $E $B ${P}ip30_2M $O $s -- --kbps 2000 --gop 30 --threads 1
  python3 $E $B ${P}lossless $O $s -- --lossless --threads 1
  for nr in 1 2 4 8; do python3 $E $B ${P}near$nr $O $s -- --near $nr --threads 1; done
done
