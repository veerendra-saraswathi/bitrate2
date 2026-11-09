#!/usr/bin/env python3
"""Encoder / decoder latency per the SoW definition, PoC profile.
Encoder latency of frame k = time from 'frame k fully in memory' (ewc_enc has just read it from disk)
to 'encoded unit of frame k complete in memory' (Encoder::encode returned). File I/O is excluded.
Decoder latency = time of one Decoder::decode_next call (unit in memory -> frame in memory).
Reported per frame: median, p99, max, in ms and in frame periods at the stated fps.
usage: latency.py BIN OUT.csv threads fps seq[,seq...]"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import evalkit as ek
import sys, subprocess, tempfile, numpy as np, json, os
B, OUT, TH, FPS = sys.argv[1], sys.argv[2], sys.argv[3], float(sys.argv[4]); seqs = sys.argv[5].split(',')
T = tempfile.mkdtemp(); enc = []; dec = []
for s in seqs:
    src = ek.seq_path(s)
    subprocess.run([f'{B}/ewc_enc', '-i', src, '-o', f'{T}/s.ewc', '--poc', '--fps', str(FPS), '--threads', TH, '--stats', f'{T}/e.csv']
                   + os.environ.get('EXTRA', '').split(), check=True, capture_output=True)
    enc += [float(l.split(',')[3]) for l in open(f'{T}/e.csv').read().splitlines()[1:]]
    subprocess.run([f'{B}/ewc_dec', '-i', f'{T}/s.ewc', '-o', f'{T}/r.raw', '--threads', TH, '--report', f'{T}/d.csv'], check=True, capture_output=True)
    dec += [float(l.split(',')[14]) for l in open(f'{T}/d.csv').read().splitlines()[1:]]
per = 1000.0 / FPS
TAG = os.environ.get('TAG', 'poc')
row = lambda name, v: f"{TAG},{name},{TH},{FPS},{len(v)},{np.median(v):.2f},{np.percentile(v,99):.2f},{max(v):.2f},{np.median(v)/per:.3f},{np.percentile(v,99)/per:.3f},{max(v)/per:.3f}"
hdr = "config,stage,threads,fps,frames,median_ms,p99_ms,max_ms,median_frames,p99_frames,max_frames"
lines = [row('encoder', enc), row('decoder', dec)]
new = not os.path.exists(OUT)
with open(OUT, 'a') as f:
    if new: f.write(hdr + '\n')
    f.write('\n'.join(lines) + '\n')
print(hdr); print('\n'.join(lines))
