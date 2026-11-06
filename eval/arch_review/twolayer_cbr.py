import os, sys
import sys, numpy as np, imagecodecs as ic
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'scripts')); import evalkit as ek
from twolayer import wav
B = 8333
def two(x, bd, k):
    best = None
    for base in (1500, 2500, 3500, 4500, 5500, 6500):
        b = wav(x, bd, base); r = x.astype(np.int32) - b; off = -r.min()
        s = ic.jpegls_encode((r + off).astype(np.uint16), level=k)
        tot = base + len(s) + 4
        if best is None or tot < best[0]:
            rec = ic.jpegls_decode(s).astype(np.int32) - off + b
            best = (tot, base, rec)
    return best
def ps(x, rec, peak): d = x.astype(float) - rec; return 10 * np.log10(peak ** 2 / (d * d).mean()), int(np.abs(d).max())
import twolayer
for name, seq in [('8bit thermal', None), ('14bit', 'flirv_ePoikf'), ('14bit', 'flirv_6tLtjd'), ('14bit', 'flirv_5RSrbW'), ('14bit', 'flirstill_val')]:
    if seq is None: x = np.fromfile(ek.kaist_raw('thermal'), 'u1').reshape(512, 640); bd = 8; ks = [1, 2, 3, 4]
    else: x = ek.load_seq(seq, 1)[0]; bd = 14; ks = [4, 6, 8, 10, 12, 16, 20, 24, 32]
    lo, hi = np.percentile(x, [0.1, 99.9]); peak = 255 if bd == 8 else (hi - lo)
    w = wav(x, bd, B); wp = ps(x, w, peak)
    fit = None
    for k in ks:
        t = two(x, bd, k)
        if t[0] <= B: fit = (k, t); break
    if fit:
        k, (tot, base, rec) = fit; p = ps(x, rec, peak)
        print(f'{name} {seq or "kaist"}: wavelet-only @2Mbps psnr {wp[0]:.2f} maxerr {wp[1]} | two-layer @2Mbps guaranteed maxerr<={k}: {tot} B (base {base}), psnr {p[0]:.2f} maxerr {p[1]}')
    else: print(name, seq, 'no fit')
