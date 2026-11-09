#!/usr/bin/env python3
"""Experiment 1b: realistic HTJ2K rate control (no PCRD in OpenJPH). Per frame:
q = previous q * (previous size / target)^(1/a), a = measured slope of log(size) vs log(q);
target = 97% of 8333 bytes; if the result exceeds 8333 bytes, re-encode with a larger q until it
fits. Reports encodes per frame, rate, and quality. usage: htj2k_rc.py OJPH_APPS OUT.jsonl NFRAMES"""
import os, sys
import sys, os, json, subprocess, tempfile, numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'scripts')); import evalkit as ek
A, OUT, NF = sys.argv[1], sys.argv[2], int(sys.argv[3])
T = tempfile.mkdtemp(); BUD = 8333; TGT = 0.97 * BUD
def enc(q):
    r = subprocess.run([f'{A}/ojph_compress/ojph_compress', '-i', f'{T}/f.pgm', '-o', f'{T}/f.j2c', '-qstep', f'{q:.8g}',
                        '-reversible', 'false', '-num_decomps', '6'], capture_output=True, text=True)
    return os.path.getsize(f'{T}/f.j2c'), float(r.stdout.split('=')[-1]) * 1e3
for s in ['flirv_4FRnNp', 'flirv_5RSrbW', 'flirv_6tLtjd', 'flirv_SCiKdG', 'flirv_ePoikf', 'flirv_vbrSzr', 'flirstill_val']:
    o = ek.load_seq(s, NF); rec = []; sizes = []; passes = []; tt = []
    q = 0.002; a = -1.0   # initial guess; slope updated from the first two encodes
    for k, fr in enumerate(o):
        open(f'{T}/f.pgm', 'wb').write(b'P5\n640 512\n16383\n' + fr.astype('>u2').tobytes())
        n = 0; t_tot = 0
        sz, t = enc(q); n += 1; t_tot += t
        if k == 0 or abs(np.log(sz / TGT)) > 0.15:   # far off (first frame / scene change): one correction step
            q2 = q * (sz / TGT) ** (-1.0 / a) if k else q * (sz / TGT) ** 1.0
            sz2, t = enc(q2); n += 1; t_tot += t
            if sz2 != sz and q2 != q: a = float(np.clip(np.log(sz2 / sz) / np.log(q2 / q), -3, -0.3))
            q, sz = q2, sz2
        while sz > BUD:                                # hard budget: never exceed
            q *= (sz / TGT) ** (-1.0 / a) * 1.01
            sz, t = enc(q); n += 1; t_tot += t
        subprocess.run([f'{A}/ojph_expand/ojph_expand', '-i', f'{T}/f.j2c', '-o', f'{T}/g.pgm'], capture_output=True)
        d = open(f'{T}/g.pgm', 'rb').read(); i = d.index(b'16383\n') + 6
        rec.append(np.frombuffer(d[i:], '>u2').reshape(512, 640)); sizes.append(sz); passes.append(n); tt.append(t_tot)
        q = q * (sz / TGT) ** (-1.0 / a)              # prediction for the next frame
    m = ek.seq_metrics(o, np.stack(rec)); m.pop('per_frame')
    res = dict(seq=s, n=len(o), kbps=float(np.mean(sizes)) * 8 * 30 / 1000, max_bytes=int(max(sizes)), passes_mean=float(np.mean(passes)),
               passes_max=int(max(passes)), enc_ms_total_med=float(np.median(tt)), enc_ms_total_max=float(max(tt)), **m)
    open(OUT, 'a').write(json.dumps(res) + '\n')
    print(f"{s:14s} kbps {res['kbps']:7.1f} max {res['max_bytes']} B  encodes/frame mean {res['passes_mean']:.2f} max {res['passes_max']}  enc total med {res['enc_ms_total_med']:.2f} max {res['enc_ms_total_max']:.2f} ms  psnr_dr {m['psnr_dr']:.2f} ssim {m['ssim_agc']:.4f}", flush=True)
