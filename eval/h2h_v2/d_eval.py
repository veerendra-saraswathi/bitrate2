import os, sys
# Architecture D = JPEG-LS (CharLS via imagecodecs), the same library bitrate-main used for its frozen D results.
# Fixed rate: per frame, the smallest NEAR whose stream fits the byte budget (oracle search, favourable to D).
import sys, json, time, random, numpy as np, imagecodecs as ic
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'scripts')); import evalkit as ek
from skimage.metrics import structural_similarity as ssim
B = 8333
def enc(x, near): return ic.jpegls_encode(x, level=near)
def fit(x, maxnear):
    lo, hi = 0, maxnear
    if len(enc(x, lo)) <= B: return 0
    if len(enc(x, hi)) > B: return hi
    while lo + 1 < hi:
        m = (lo + hi) // 2
        if len(enc(x, m)) <= B: hi = m
        else: lo = m
    return hi
def tm(f, n=5):
    ts = []
    for _ in range(n): t = time.perf_counter(); r = f(); ts.append((time.perf_counter() - t) * 1e3)
    return r, sorted(ts)[n // 2]
out = {}
# 8-bit
for img in ['thermal', 'visible']:
    x = np.fromfile(ek.kaist_raw(img), 'u1').reshape(512, 640)
    for near in [0, 1]:
        s = enc(x, near); out[f'8_{img}_near{near}'] = dict(bytes=len(s), mbps=len(s)*8*30/1e6)
    n = fit(x, 255); s, te = tm(lambda: enc(x, n)); r, td = tm(lambda: ic.jpegls_decode(s))
    o = x.astype(float); d = o - r
    out[f'8_{img}_2mbps'] = dict(near=n, bytes=len(s), psnr=10*np.log10(255**2/(d*d).mean()), ssim=ssim(o, r.astype(float), data_range=255, gaussian_weights=True, sigma=1.5, use_sample_covariance=False),
                                 mae=np.abs(d).mean(), maxerr=int(np.abs(d).max()), enc_ms=te, dec_ms=td)
    print(img, out[f'8_{img}_near0'], out[f'8_{img}_near1'], {k: round(v, 3) for k, v in out[f'8_{img}_2mbps'].items()})
# 14-bit
clips = ['flirv_4FRnNp', 'flirv_5RSrbW', 'flirv_6tLtjd', 'flirv_SCiKdG', 'flirv_ePoikf', 'flirv_vbrSzr', 'flirstill_val']
agg = []
for c in clips:
    o = ek.load_seq(c, 10); rec = []; info = []
    for f in o:
        n = fit(f, 255); s, te = tm(lambda: enc(f, n), 3); r, td = tm(lambda: ic.jpegls_decode(s), 3)
        rec.append(r); info.append((n, len(s), te, td))
    rec = np.array(rec); m = ek.seq_metrics(o, rec); m.pop('per_frame'); m['mae'] = float(np.abs(o.astype(float) - rec).mean())
    m['near_avg'] = np.mean([i[0] for i in info]); m['bytes_avg'] = np.mean([i[1] for i in info]); m['bytes_max'] = max(i[1] for i in info)
    m['enc_ms'] = np.median([i[2] for i in info]); m['dec_ms'] = np.median([i[3] for i in info])
    ll = enc(o[0], 0); m['lossless_bytes_f0'] = len(ll); m['lossless_exact'] = bool((ic.jpegls_decode(ll) == o[0]).all())
    out[f'14_{c}'] = m; agg.append(m)
    print(c, round(m['psnr_dr'], 2), round(m['ssim_agc'], 3), 'near', round(m['near_avg'], 1), 'maxerr', m['maxerr'], 'lossless', len(ll))
print('14-bit mean', {k: round(float(np.mean([a[k] for a in agg])), 3) for k in ['psnr14', 'psnr_dr', 'ssim_agc', 'rmse', 'mae', 'bytes_avg', 'enc_ms', 'dec_ms', 'lossless_bytes_f0']}, 'maxerr', max(a['maxerr'] for a in agg), 'bytes_max', max(a['bytes_max'] for a in agg))
# robustness: bit flips on the 8-bit thermal 2 Mbps stream, compare with clean decode
x = np.fromfile(ek.kaist_raw('thermal'), 'u1').reshape(512, 640)
s = enc(x, out['8_thermal_2mbps']['near']); clean = ic.jpegls_decode(s)
for flips in (1, 4, 16):
    rng = random.Random(1); c = dict(rejected=0, silent=0, same=0)
    for t in range(200):
        a = bytearray(s)
        for _ in range(flips): a[rng.randrange(len(a))] ^= 1 << rng.randrange(8)
        try:
            r = ic.jpegls_decode(bytes(a)); c['same' if (r.shape == clean.shape and (r == clean).all()) else 'silent'] += 1
        except Exception: c['rejected'] += 1
    out[f'robust_{flips}'] = c; print(flips, 'flips', c)
json.dump(out, open('d_results.json', 'w'), indent=1, default=float)
