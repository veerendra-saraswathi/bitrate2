#!/usr/bin/env python3
"""FEC comparison at equal total wire bitrate. usage: net_sweep.py BIN OUT.jsonl SEQ GOP FEEDBACK"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import evalkit as ek
import sys, json, itertools, subprocess, os
from concurrent.futures import ThreadPoolExecutor
B, OUT, SEQ, GOP, FB = sys.argv[1:6]
c = ek.corpus()[SEQ]; w, h, n = c['w'], c['h'], c['n']
E = os.environ   # optional overrides: FECS="none:0,rs:4" LOSSES="0.05" BURSTS="1,4" SEEDS=3
fecs = [(a, int(b)) for a, b in (x.split(':') for x in E['FECS'].split(','))] if E.get('FECS') else \
       [('none', 0), ('xor', 4), ('xori', 4), ('rs', 4), ('rs', 2), ('rs', 8)]
losses = [float(x) for x in E.get('LOSSES', '0.01,0.05,0.10').split(',')]
bursts = [int(x) for x in E.get('BURSTS', '1,4').split(',')]; seeds = range(1, int(E.get('SEEDS', '5')) + 1)
def one(a):
    (fm, k), p, b, s = a
    cmd = [f'{B}/ewc_bench', '--in', ek.seq_path(SEQ), '--w', str(w), '--h', str(h), '--n', str(n), '-q',
           '--link-kbps', '2000', '--gop', GOP, '--feedback', FB, '--udp-loss', str(p), '--burst', str(b), '--seed', str(s), '--threads', '1']
    if k: cmd += ['--fec', str(k), '--fec-mode', fm]
    o = subprocess.run(cmd, capture_output=True, text=True).stdout.split()
    d = {o[i]: o[i + 1] for i in range(1, len(o) - 1, 2)}
    return dict(seq=SEQ, gop=int(GOP), feedback=int(FB), fec=fm, k=k, loss=p, burst=b, seed=s, **{x: float(d[x]) for x in
                ['ok', 'damaged', 'ref_invalid', 'lost', 'kbps', 'fec_kbps', 'hdr_kbps', 'wire_kbps', 'peak_wire_kbps', 'psnr_avg',
                 'psnr_min', 'psnr_ok_avg', 'loss_actual', 'recovered', 'intra_requests', 'release_delay_avg', 'frames']})
jobs = list(itertools.product(fecs, losses, bursts, seeds))
with ThreadPoolExecutor(2) as ex, open(OUT, 'a') as f:
    for r in ex.map(one, jobs): f.write(json.dumps(r) + '\n'); f.flush()
