#!/usr/bin/env python3
"""Experiment 1: HTJ2K (OpenJPH, ISO/IEC 15444-15) vs ewc at 2 Mbps on FLIR ADAS v2, same frames.
OpenJPH has no target-size rate control, so per frame the quantisation step is bisected (log scale,
20 steps) to the largest codestream <= 8333 bytes. Encode/decode times are OpenJPH's own
'Elapsed time' for the final setting (single thread). usage: htj2k_eval.py OJPH_APPS OUT.jsonl NFRAMES"""
import os, sys
import sys, os, json, subprocess, tempfile, numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'scripts')); import evalkit as ek
A, OUT, NF = sys.argv[1], sys.argv[2], int(sys.argv[3])
T = tempfile.mkdtemp(); BUD = 8333
def enc(fr, q, extra):
    r = subprocess.run([f'{A}/ojph_compress/ojph_compress', '-i', f'{T}/f.pgm', '-o', f'{T}/f.j2c', '-qstep', f'{q:.8g}',
                        '-reversible', 'false'] + extra, capture_output=True, text=True)
    return os.path.getsize(f'{T}/f.j2c'), float(r.stdout.split('=')[-1]) if 'Elapsed' in r.stdout else float('nan')
for cfg, extra in [('htj2k_L5_cb64', ['-num_decomps', '5']), ('htj2k_L6_cb64', ['-num_decomps', '6'])]:
    for s in ['flirv_4FRnNp', 'flirv_5RSrbW', 'flirv_6tLtjd', 'flirv_SCiKdG', 'flirv_ePoikf', 'flirv_vbrSzr', 'flirstill_val']:
        o = ek.load_seq(s, NF); rec = []; sizes = []; te = []; td = []
        for fr in o:
            open(f'{T}/f.pgm', 'wb').write(b'P5\n640 512\n16383\n' + fr.astype('>u2').tobytes())
            lo, hi = np.log(1e-5), np.log(0.5); best = None
            for _ in range(20):
                m = (lo + hi) / 2; sz, _t = enc(fr, np.exp(m), extra)
                if sz <= BUD: hi = m; best = np.exp(m)
                else: lo = m
            sz, t = enc(fr, best, extra)
            r = subprocess.run([f'{A}/ojph_expand/ojph_expand', '-i', f'{T}/f.j2c', '-o', f'{T}/g.pgm'], capture_output=True, text=True)
            d = open(f'{T}/g.pgm', 'rb').read(); i = d.index(b'16383\n') + 6
            rec.append(np.frombuffer(d[i:], '>u2').reshape(512, 640)); sizes.append(sz); te.append(t * 1e3)
            td.append(float(r.stdout.split('=')[-1]) * 1e3)
        m = ek.seq_metrics(o, np.stack(rec)); m.pop('per_frame')
        res = dict(config=cfg, seq=s, n=len(o), bytes_mean=float(np.mean(sizes)), kbps=float(np.mean(sizes)) * 8 * 30 / 1000,
                   enc_ms_med=float(np.median(te)), dec_ms_med=float(np.median(td)), **m)
        open(OUT, 'a').write(json.dumps(res) + '\n')
        print(f"{cfg:14s} {s:14s} kbps {res['kbps']:7.1f} psnr_dr {m['psnr_dr']:6.2f} ssim {m['ssim_agc']:.4f} rmse {m['rmse']:5.2f} maxerr {m['maxerr']:4d} enc {res['enc_ms_med']:.2f} dec {res['dec_ms_med']:.2f}", flush=True)
