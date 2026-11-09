import os, sys
import sys, json, numpy as np, csv
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'scripts')); import evalkit as ek
from skimage.metrics import structural_similarity as ssim
R = {}
def m8(o, r):
    o = o.astype(float); r = r.astype(float); d = o - r; mse = (d*d).mean()
    return dict(psnr=199 if mse == 0 else 10*np.log10(255**2/mse), ssim=ssim(o, r, data_range=255, gaussian_weights=True, sigma=1.5, use_sample_covariance=False),
                mae=np.abs(d).mean(), maxerr=int(np.abs(d).max()))
for img in ['thermal', 'visible']:
    o = np.fromfile(ek.kaist_u16(img), '<u2').reshape(-1, 512, 640)[0]
    bm = np.fromfile(f'/tmp/b{img[0]}.u16', '<u2').reshape(512, 640) if img == 'visible' else np.fromfile('/tmp/kt.u16', '<u2').reshape(512, 640)
    R[f'8bit_{img}_bm'] = m8(o, bm)
    for t in ['t128', 'whole']:
        e = np.fromfile(f'/tmp/e8_{img}_{t}.u16', '<u2').reshape(-1, 512, 640)[0]
        R[f'8bit_{img}_ewc_{t}'] = m8(o, e)
clips = ['flirv_4FRnNp', 'flirv_5RSrbW', 'flirv_6tLtjd', 'flirv_SCiKdG', 'flirv_ePoikf', 'flirv_vbrSzr', 'flirstill_val']
for c in clips:
    o = ek.load_seq(c, 10)
    for tag, f in [('bm_shipped', f'rec/bm_shipped_{c}.u16'), ('bm_agc', f'rec/bm_agc_{c}.u16'), ('ewc', f'rec/ewc_{c}.u16')]:
        r = np.fromfile(f, '<u2').reshape(-1, 512, 640)[:10]
        m = ek.seq_metrics(o, r); m.pop('per_frame'); m['mae'] = float(np.mean(np.abs(o.astype(float) - r)))
        rows = list(csv.DictReader(open(f.replace('.u16', '.csv'))))
        m['bytes_avg'] = np.mean([float(x['bytes']) for x in rows]); m['bytes_max'] = max(float(x['bytes']) for x in rows)
        R[f'14_{c}_{tag}'] = m
json.dump(R, open('h2h_all.json', 'w'), indent=1, default=float)
for k, v in R.items():
    if k.startswith('8bit'): print(k, {a: round(b, 3) for a, b in v.items()})
print()
for tag in ['bm_shipped', 'bm_agc', 'ewc']:
    vs = [R[f'14_{c}_{tag}'] for c in clips]
    print(tag, {k: round(float(np.mean([v[k] for v in vs])), 3) for k in ['psnr14', 'psnr_dr', 'ssim_agc', 'rmse', 'mae', 'bytes_avg']}, 'maxerr', max(v['maxerr'] for v in vs), 'bytes_max', max(v['bytes_max'] for v in vs))
for c in clips: print(c, *[round(R[f'14_{c}_{t}']['psnr_dr'], 2) for t in ['bm_shipped', 'bm_agc', 'ewc']], *[round(R[f'14_{c}_{t}']['ssim_agc'], 3) for t in ['bm_shipped', 'bm_agc', 'ewc']])
