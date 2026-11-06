#!/bin/bash
# GOP x rate-control sweep at 2 Mbps codec rate. usage: rc_sweep.sh BIN OUT.jsonl seqspec...
B=$1; O=$2; shift 2; E=$(dirname "$0")/codec_eval.py
for s in "$@"; do
 for gop in 1 8 15 30; do
  for rc in "1:1" "2:2" "3:3"; do IFS=: read v i <<<"$rc"
   [ $gop = 1 ] && [ $rc != "1:1" ] && continue
   python3 $E $B g${gop}_v${v}i${i} $O $s -- --kbps 2000 --gop $gop --vbv $v --iscale $i --threads 2 | cut -c1-220
  done; done; done
