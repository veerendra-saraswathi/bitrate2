#!/usr/bin/env python3
"""External references on FLIR ADAS v2 clips (reference only, not shipped):
JPEG-LS lossless (all frames), JPEG 2000 at 8333 B/frame (first 30 frames),
HEVC x265 low-delay CBR 2 Mbps (all frames; x265 path is 12-bit, input >> 2)."""
import os, sys
import sys, json, time, numpy as np, imagecodecs as ic
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'scripts')); import evalkit as ek
OUT = sys.argv[1]; seqs = sys.argv[2].split(',')
for name in seqs:
    s = ek.load_seq(name); res = dict(seq=name)
    tot = 0; ok = True
    for f in s:
        b = ic.jpegls_encode(f); tot += len(b); ok &= bool(np.array_equal(ic.jpegls_decode(b), f))
    res['jpegls_bpp'] = tot * 8 / s.size; res['jpegls_exact'] = int(ok)
    sub = s[:30]; rec = []; sz = []
    for f in sub:
        b, r = ek.ref_j2k(f, 8333); rec.append(r); sz.append(len(b))
    m = ek.seq_metrics(sub, np.stack(rec)); m.pop('per_frame'); m['kbps'] = float(np.mean(sz)) * 8 * 30 / 1000
    res['j2k_30'] = m
    if 'still' not in name:
        total, sizes, rec = ek.ref_ffmpeg_video(s, 30, 2000, 'x265')
        m = ek.seq_metrics(s, rec); m.pop('per_frame'); m['kbps'] = total * 8 * 30 / len(s) / 1000; m['bytes_max'] = int(max(sizes))
        res['x265'] = m
    open(OUT, 'a').write(json.dumps(res) + '\n')
    print(name, f"jpegls {res['jpegls_bpp']:.3f} exact {res['jpegls_exact']} | j2k30 psnr_dr {res['j2k_30']['psnr_dr']:.2f} ssim {res['j2k_30']['ssim_agc']:.4f} kbps {res['j2k_30']['kbps']:.0f}",
          f"| x265 psnr_dr {res['x265']['psnr_dr']:.2f} ssim {res['x265']['ssim_agc']:.4f} kbps {res['x265']['kbps']:.0f}" if 'x265' in res else '', flush=True)
