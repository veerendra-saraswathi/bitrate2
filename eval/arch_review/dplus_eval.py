import os, sys
# Architecture D (CharLS JPEG-LS) vs ewc "D+" (JPEG-LS modelling + adaptive range coder + auto stripes,
# CRC/stripe resilience kept) vs ewc JPEG-LS-Golomb. Bound checked on every frame.
import sys, subprocess, re, json, numpy as np, imagecodecs as ic, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'scripts')); import evalkit as ek
def bench(seqfile, n, bd, k, extra):
    p = subprocess.run([os.path.join(ek.BIN, 'ewc_bench'), '--in', seqfile, '--w', '640', '--h', '512', '--n', str(n), '--bd', str(bd), '--lossless', '--near', str(k), '--out-rec', '/tmp/r.u16', '-q'] + extra, capture_output=True, text=True)
    o = p.stdout + p.stderr
    g = lambda key: float(re.search(key + r' ([0-9.]+)', o).group(1))
    return g('avg_bytes'), g('enc_med'), g('dec_med'), np.fromfile('/tmp/r.u16', '<u2').reshape(-1, 512, 640)
R = []
sets = [('KAIST thermal 8b', ek.kaist_u16('thermal'), 1, 8), ('KAIST visible 8b', ek.kaist_u16('visible'), 1, 8)] + \
       [(c, ek.seq_path(c), 5, 14) for c in ['flirv_4FRnNp', 'flirv_5RSrbW', 'flirv_6tLtjd', 'flirv_SCiKdG', 'flirv_ePoikf', 'flirv_vbrSzr', 'flirstill_val']]
for name, f, n, bd in sets:
    x = np.fromfile(f, '<u2').reshape(-1, 512, 640)[:n]
    for k in (0, 1, 2, 4, 8, 16, 32):
        t = time.perf_counter(); d = np.mean([len(ic.jpegls_encode(fr if bd == 14 else fr.astype(np.uint8), level=k)) for fr in x]); dt = (time.perf_counter() - t) / n * 1e3
        a = bench(f, n, bd, k, []); gl = bench(f, n, bd, k, ['--golomb', '--fixed-stripes'])
        assert np.abs(a[3].astype(int) - x.astype(int)).max() <= k and np.abs(gl[3].astype(int) - x.astype(int)).max() <= k
        R.append(dict(data=name, near=k, charls=d, dplus=a[0], golomb=gl[0], enc=a[1], dec=a[2], enc_g=gl[1], dec_g=gl[2], charls_ms=dt))
        print(f'{name[:16]:16} NEAR={k:2d}  CharLS {d:9.0f}  D+ {a[0]:9.0f} ({100*(a[0]/d-1):+5.1f}%)  ewc-Golomb {gl[0]:9.0f} ({100*(gl[0]/d-1):+5.1f}%)  enc/dec ms D+ {a[1]:.1f}/{a[2]:.1f} Golomb {gl[1]:.1f}/{gl[2]:.1f} CharLS-enc(1 thread) {dt:.1f}', flush=True)
json.dump(R, open('dplus_results.json', 'w'), indent=1)
w = sum(1 for r in R if r['dplus'] < r['charls']); print(f'D+ smaller than CharLS on {w}/{len(R)} points; median {np.median([100*(r["dplus"]/r["charls"]-1) for r in R]):+.1f}%, worst {max(100*(r["dplus"]/r["charls"]-1) for r in R):+.1f}%')
