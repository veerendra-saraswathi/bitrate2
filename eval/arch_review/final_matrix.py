import os, sys
# Final 2 Mbps scoreboard: every architecture, same 70 frames (7 FLIR ADAS v2 clips x first 10 frames),
# budget 8333 B/frame (2 Mbps @ 30 fps), metrics from one function.
import sys, subprocess, re, json, time, numpy as np, imagecodecs as ic
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'scripts')); import evalkit as ek
from skimage.metrics import structural_similarity as ssim
B = 8333; BIN = ek.BIN
clips = ['flirv_4FRnNp', 'flirv_5RSrbW', 'flirv_6tLtjd', 'flirv_SCiKdG', 'flirv_ePoikf', 'flirv_vbrSzr', 'flirstill_val']
def frame_bench(f, args):
    f.astype('<u2').tofile('/tmp/fm.u16')
    p = subprocess.run([f'{BIN}/ewc_bench', '--in', '/tmp/fm.u16', '--w', '640', '--h', '512', '--n', '1', '--out-rec', '/tmp/fr.u16', '-q'] + args, capture_output=True, text=True)
    o = p.stdout + p.stderr; g = lambda k: float(re.search(k + r' ([0-9.]+)', o).group(1))
    return g('avg_bytes'), g('enc_med'), g('dec_med'), np.fromfile('/tmp/fr.u16', '<u2').reshape(512, 640)
def near_fit(f, args):            # smallest NEAR whose frame fits the budget (binary search)
    lo, hi = 0, 255
    while lo < hi:
        m = (lo + hi) // 2
        if frame_bench(f, ['--bd', '14', '--lossless', '--near', str(m)] + args)[0] <= B: hi = m
        else: lo = m + 1
    return frame_bench(f, ['--bd', '14', '--lossless', '--near', str(lo)] + args)
def charls_fit(f):
    lo, hi = 0, 255
    while lo < hi:
        m = (lo + hi) // 2
        if len(ic.jpegls_encode(f, level=m)) <= B: hi = m
        else: lo = m + 1
    t = time.perf_counter(); s = ic.jpegls_encode(f, level=lo); te = (time.perf_counter() - t) * 1e3
    t = time.perf_counter(); r = ic.jpegls_decode(s); td = (time.perf_counter() - t) * 1e3
    return len(s), te, td, r
def seq_bench(c, args, n=10):
    p = subprocess.run([f'{BIN}/ewc_bench', '--in', ek.seq_path(c), '--w', '640', '--h', '512', '--n', str(n), '--out-rec', '/tmp/sr.u16', '-q'] + args, capture_output=True, text=True)
    o = p.stdout + p.stderr; g = lambda k: float(re.search(k + r' ([0-9.]+)', o).group(1))
    return g('avg_bytes'), g('enc_med'), g('dec_med'), np.fromfile('/tmp/sr.u16', '<u2').reshape(-1, 512, 640)[:n]
R = {}
def add(arch, c, o, r, nbytes, te, td):
    m = ek.seq_metrics(o, r); m.pop('per_frame')
    m['mae'] = float(np.abs(o.astype(float) - r).mean()); m['bytes'] = float(np.mean(nbytes)); m['bytes_max'] = float(np.max(nbytes))
    m['enc_ms'] = float(np.median(te)); m['dec_ms'] = float(np.median(td))
    R.setdefault(arch, {})[c] = m
import csv
lay_k = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'layered_results_dplus.json')))
for c in clips:
    o = ek.load_seq(c, 10)
    for tag in ('shipped', 'agc'):               # bitrate-main recon from eval/h2h_v2/run14.sh (optional)
        if not os.path.exists(f'rec/bm_{tag}_{c}.u16'): continue
        r = np.fromfile(f'rec/bm_{tag}_{c}.u16', '<u2').reshape(-1, 512, 640)[:10]
        rows = list(csv.DictReader(open(f'rec/bm_{tag}_{c}.csv')))
        add(f'A_bitrate_main_{tag}', c, o, r, [float(x['bytes']) for x in rows], [float(x['enc_ms']) for x in rows], [float(x['dec_ms']) for x in rows])
    res = [charls_fit(f) for f in o]
    add('D_CharLS', c, o, np.array([x[3] for x in res]), [x[0] for x in res], [x[1] for x in res], [x[2] for x in res])
    for arch, extra in (('ewc_near_Golomb', ['--golomb', '--fixed-stripes']), ('Dplus', [])):
        res = [near_fit(f, extra) for f in o]
        add(arch, c, o, np.array([x[3] for x in res]), [x[0] for x in res], [x[1] for x in res], [x[2] for x in res])
    for arch, extra in (('ewc_wavelet_whole', ['--poc', '--tile', '0']), ('ewc_wavelet_tiled_PoC', ['--poc'])):
        b, e, d, r = seq_bench(c, extra); add(arch, c, o, r, [b], [e], [d])
    if lay_k[c]['layered']:
        k = lay_k[c]['layered']['k']
        subprocess.run([f'{BIN}/ewc_layered', ek.seq_path(c), '640', '512', '10', '14', str(B), str(k), '/tmp/l.u16', '/tmp/l.csv'], env={'EWL_RBD': '12'}, capture_output=True)
        rows = list(csv.DictReader(open('/tmp/l.csv')))
        add('two_layer', c, o, np.fromfile('/tmp/l.u16', '<u2').reshape(-1, 512, 640)[:10], [float(x['total']) for x in rows], [float(x['enc_ms']) for x in rows], [float(x['dec_ms']) for x in rows])
    print('done', c, flush=True)
json.dump(R, open('final_matrix.json', 'w'), indent=1)
keys = ['psnr14', 'psnr_dr', 'ssim_agc', 'rmse', 'mae']
for a, v in R.items():
    n = len(v); print(f"{a:24s} clips {n} " + ' '.join(f"{k} {np.mean([x[k] for x in v.values()]):.3f}" for k in keys) +
          f" maxerr {max(x['maxerr'] for x in v.values())} bytes {np.mean([x['bytes'] for x in v.values()]):.0f} max {max(x['bytes_max'] for x in v.values()):.0f} enc {np.median([x['enc_ms'] for x in v.values()]):.1f} dec {np.median([x['dec_ms'] for x in v.values()]):.1f}")
