#!/usr/bin/env python3
"""Visual evidence for the SoW 'visual measures': one figure per clip.
Row 1 (full frame) and row 2 (2x crop), columns:
  original | ewc PoC 2 Mbps | |error| map | ewc PoC 2 Mbps, stored stream with 5% of 1400-byte
  packets zero-filled (worst frame of the clip) | original bitrate-main at 2 Mbps (8-bit, given a
  free per-frame contrast stretch).
All images use the same display stretch (p0.5..p99.5 of the original frame).
usage: visual.py BIN BASELINE_EVAL OUTDIR seq[,seq...]"""
import os, sys
import sys, os, subprocess, tempfile, numpy as np
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'scripts'))
import evalkit as ek
from stored_loss import corrupt  # noqa: E402  (module guards its CLI below)
B, BE, OUT = sys.argv[1:4]; T = tempfile.mkdtemp()
def dec(bsfile, n):
    subprocess.run([f'{B}/ewc_dec', '-i', bsfile, '-o', f'{T}/r.raw', '--frames', str(n)], check=True, capture_output=True)
    return np.fromfile(f'{T}/r.raw', '<u2').reshape(-1, 512, 640)
for name in sys.argv[4].split(','):
    o = ek.load_seq(name); n = len(o); src = ek.seq_path(name)
    subprocess.run([f'{B}/ewc_enc', '-i', src, '-o', f'{T}/s.ewc', '--poc'], check=True, capture_output=True)
    clean = dec(f'{T}/s.ewc', n)
    open(f'{T}/c.ewc', 'wb').write(corrupt(open(f'{T}/s.ewc', 'rb').read(), 'pktzero', 0.05, 1))
    dmg = dec(f'{T}/c.ewc', n)
    rm = lambda a, b: np.sqrt(((a.astype(float) - b) ** 2).mean())
    k = int(np.argmax([rm(o[i], dmg[i]) for i in range(n)]))            # worst damaged frame
    subprocess.run([BE, src, '640', '512', str(k + 1), '8333', 'agc', f'{T}/b.raw'], check=True, capture_output=True)
    base = np.fromfile(f'{T}/b.raw', '<u2').reshape(-1, 512, 640)[k]
    of = o[k]; lo, hi = np.percentile(of, [0.5, 99.5]); rng = np.percentile(of, 99.9) - np.percentile(of, 0.1)
    def lab(img):
        if img is of: return 'original (14-bit)'
        e = rm(of, img); m = ek.frame_metrics(of, img)
        return f"RMSE {e:.1f} DN, PSNR(range) {20*np.log10(rng/max(e,1e-9)):.1f} dB, SSIM {m['ssim_agc']:.3f}"
    cols = [(of, 'Original'), (clean[k], 'ewc PoC, 2 Mbps'), (None, '|error| ewc PoC'),
            (dmg[k], 'ewc PoC, 5% packets lost (worst frame)'), (base, 'bitrate-main, 2 Mbps (fair 8-bit)')]
    y0, x0 = 128, 160
    fig, ax = plt.subplots(2, 5, figsize=(25, 9.4))
    for j, (img, title) in enumerate(cols):
        for r in range(2):
            a = ax[r, j]; a.set_xticks([]); a.set_yticks([])
            if img is None:
                err = np.abs(of.astype(float) - clean[k]); err = err if r == 0 else err[y0:y0 + 256, x0:x0 + 320]
                im = a.imshow(err, cmap='magma', vmin=0, vmax=30)
                if r == 0: fig.colorbar(im, ax=a, fraction=0.035, pad=0.02, label='DN (0-30)')
            else:
                v = img if r == 0 else img[y0:y0 + 256, x0:x0 + 320]
                a.imshow(np.clip((v.astype(float) - lo) / (hi - lo), 0, 1), cmap='gray', vmin=0, vmax=1, interpolation='nearest')
            if r == 0: a.set_title(title + '\n' + (lab(img) if img is not None else f'max {int(np.abs(of.astype(int)-clean[k]).max())} DN'), fontsize=11)
    ax[1, 0].set_ylabel('2x crop', fontsize=11)
    fig.suptitle(f'{name}, frame {k} of {n}  |  640x512, 14-bit  |  display stretch p0.5-p99.5 of original = {lo:.0f}-{hi:.0f} DN', fontsize=13)
    fig.tight_layout(); fig.savefig(f'{OUT}/{name}.png', dpi=80); plt.close(fig)
    print(name, 'frame', k, 'saved')
