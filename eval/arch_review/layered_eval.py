import os, sys
import sys, subprocess, csv, json, numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'scripts')); import evalkit as ek
clips = ['flirv_4FRnNp', 'flirv_5RSrbW', 'flirv_6tLtjd', 'flirv_SCiKdG', 'flirv_ePoikf', 'flirv_vbrSzr', 'flirstill_val']
R = {}
for c in clips:
    o = ek.load_seq(c, 10)
    w = np.fromfile(f'rec/ewc_{c}.u16', '<u2').reshape(-1, 512, 640)[:10]   # earlier ewc PoC run, same frames (pre-pshift)
    subprocess.run([os.path.join(ek.BIN, 'ewc_bench'), '--in', ek.seq_path(c), '--w', '640', '--h', '512', '--n', '10', '--poc', '--out-rec', '/tmp/w.u16', '-q'], capture_output=True)
    w = np.fromfile('/tmp/w.u16', '<u2').reshape(-1, 512, 640)
    mw = ek.seq_metrics(o, w)
    res = None
    for k in (6, 8, 10, 12, 14, 16, 20, 24, 32, 48, 64):
        subprocess.run([os.path.join(ek.BIN, 'ewc_layered'), ek.seq_path(c), '640', '512', '10', '14', '8333', str(k), '/tmp/l.u16', '/tmp/l.csv'], env={'EWL_RBD': '12'}, capture_output=True)
        rows = list(csv.DictReader(open('/tmp/l.csv')))
        if all(r['fits'] == '1' for r in rows):
            l = np.fromfile('/tmp/l.u16', '<u2').reshape(-1, 512, 640); ml = ek.seq_metrics(o, l)
            res = dict(k=k, psnr_dr=ml['psnr_dr'], ssim=ml['ssim_agc'], maxerr=ml['maxerr'], bytes=np.mean([float(r['total']) for r in rows]),
                       enc_ms=np.median([float(r['enc_ms']) for r in rows]), dec_ms=np.median([float(r['dec_ms']) for r in rows]))
            break
    R[c] = dict(wavelet=dict(psnr_dr=mw['psnr_dr'], ssim=mw['ssim_agc'], maxerr=mw['maxerr']), layered=res)
    print(c, 'wavelet: %.2f dB maxerr %d' % (mw['psnr_dr'], mw['maxerr']), '| layered:', 'k=%d %.2f dB ssim %.3f maxerr %d %.0f B enc %.1f dec %.1f' % (res['k'], res['psnr_dr'], res['ssim'], res['maxerr'], res['bytes'], res['enc_ms'], res['dec_ms']) if res else 'none fits')
json.dump(R, open('layered_results_dplus.json', 'w'), indent=1, default=float)
