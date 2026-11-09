import os, sys
import sys, json, subprocess, numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'scripts')); import evalkit as ek
B = os.environ.get('BM_EVAL', 'baseline_eval')   # built from eval/h2h/baseline_eval.cpp
C30 = {json.loads(l)['seq']: json.loads(l) for l in open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'results', 'flir_codec30.jsonl'))}
out = open('h2h_flir.jsonl', 'w')
print("clip | ewc intra psnr_dr/ssim/rmse | orig shipped | orig fair(AGC)")
for s in ['flirv_4FRnNp', 'flirv_5RSrbW', 'flirv_6tLtjd', 'flirv_SCiKdG', 'flirv_ePoikf', 'flirv_vbrSzr', 'flirstill_val']:
    orig = ek.load_seq(s, 30); row = dict(seq=s, ewc={k: C30[s][k] for k in ['psnr_dr', 'ssim_agc', 'rmse', 'codec_kbps']})
    for mode in ['shipped', 'agc']:
        r = subprocess.run([B, ek.seq_path(s), '640', '512', '30', '8333', mode, f'/tmp/{mode}.u16'], capture_output=True, text=True)
        rec = np.fromfile(f'/tmp/{mode}.u16', '<u2').reshape(-1, 512, 640)
        m = ek.seq_metrics(orig, rec); m.pop('per_frame'); m['log'] = r.stdout.strip().splitlines()[-1] if r.stdout.strip() else r.stderr[-200:]
        row[mode] = m
    out.write(json.dumps(row) + '\n')
    f = lambda m: f"{m['psnr_dr']:.2f} / {m['ssim_agc']:.3f} / {m['rmse']:.1f}"
    print(s, '|', f(row['ewc']), '|', f(row['shipped']), '|', f(row['agc']), '|', row['agc']['log'][:90])
