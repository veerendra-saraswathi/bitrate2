#!/usr/bin/env python3
"""Run ewc_bench on a sequence, then compute evalkit fidelity metrics on the reconstruction.
usage: codec_eval.py BIN_DIR TAG OUT_JSONL seq[:nframes] -- <ewc_bench args>
Appends one JSON line per run. Bench parses frames as raw uint16 LE (no conversion)."""
import os, sys
import sys, os, json, tempfile, numpy as np
sys.path.insert(0, os.path.dirname(__file__)); sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'scripts'))
from runlib import run
import evalkit as ek
B, tag, out, seqspec = sys.argv[1:5]; args = sys.argv[sys.argv.index('--') + 1:]
name, nf, sk = (seqspec.split(':') + ['', ''])[:3]; sk = int(sk or 0)
m = ek.corpus()[name]; w, h = m['w'], m['h']
orig = ek.load_seq(name)[sk:]; orig = orig[:int(nf)] if nf else orig; n = len(orig)
T = tempfile.mkdtemp(); rec, csv = f'{T}/rec.raw', f'{T}/f.csv'
cmd = [f'{B}/ewc_bench', '--in', ek.seq_path(name), '--w', str(w), '--h', str(h), '--n', str(n), '--skip', str(sk),
       '--out-rec', rec, '--csv', csv, '-q'] + args
rc, wall, rss, so, se = run(cmd)
if rc: print('FAIL', rc, se[-500:]); sys.exit(1)
s = so.strip().splitlines()[-1].split()[1:]
summ = {s[i]: (float(s[i + 1]) if s[i + 1].replace('.', '', 1).replace('-', '', 1).isdigit() else s[i + 1]) for i in range(0, len(s) - 1, 2)}
r = np.fromfile(rec, '<u2').reshape(-1, h, w)
q = ek.seq_metrics(orig, r); pf = q.pop('per_frame')
fr = [l.split(',') for l in open(csv).read().splitlines()[1:]]
by = np.array([int(x[2]) for x in fr]); fps = float(args[args.index('--fps') + 1]) if '--fps' in args else 30.0
win = min(len(by), int(fps)); win_kbps = max(by[i:i + win].sum() for i in range(len(by) - win + 1)) * 8 * fps / win / 1000
res = dict(tag=tag, seq=name, skip=sk, n=n, w=w, h=h, args=' '.join(args), bench=summ,
           codec_kbps=summ.get('kbps'), wire_kbps=summ.get('wire_kbps'), bpp=summ.get('bpp'),
           ratio=float(orig.nbytes / max(by.sum(), 1)), frame_bytes_max=int(by.max()), frame_bytes_std=float(by.std()),
           peak_frame_kbps=float(by.max() * 8 * fps / 1000), max_1s_window_kbps=float(win_kbps),
           enc_ms_med=summ.get('enc_med'), enc_ms_p99=summ.get('enc_p99'), dec_ms_med=summ.get('dec_med'), dec_ms_p99=summ.get('dec_p99'),
           bench_rss_MB=rss / 1024, exact=int(all(x['maxerr'] == 0 for x in pf)), **q,
           ssim_min=min(x['ssim_agc'] for x in pf), psnr_dr_min=min(x['psnr_dr'] for x in pf))
open(out, 'a').write(json.dumps(res) + '\n')
print(f"{tag:14s} {name:6s} kbps {res['codec_kbps']:8.1f} wire {res['wire_kbps']:8.1f} ratio {res['ratio']:6.2f} bpp {res['bpp']:.3f} "
      f"psnr14 {q['psnr14']:6.2f} psnr_dr {q['psnr_dr']:6.2f} ssim {q['ssim_agc']:.4f} rmse {q['rmse']:7.2f} maxerr {q['maxerr']:5d} "
      f"exact {res['exact']} enc {res['enc_ms_med']:.1f}/{res['enc_ms_p99']:.1f} dec {res['dec_ms_med']:.1f} peak1s {win_kbps:7.1f}")
