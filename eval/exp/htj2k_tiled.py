#!/usr/bin/env python3
"""Experiment 1c: feasibility of an ewc-framed HTJ2K hybrid. Each 128x128 tile is coded as an
independent HTJ2K codestream (OpenJPH, 9/7, 5 levels, one quantisation step for the whole frame).
The main header (identical for every tile when q is shared) is not sent: it can be rebuilt by the
decoder from the ewc frame header. Per tile we count the codestream bytes from the first SOT
marker (tile-part header + data) plus 7 bytes of ewc tile index; per frame the ewc 44-byte header,
4 tile-row headers (8 B) and block CRCs (2 B per 256 B). q is bisected to the largest total <= 8333 B.
Measures whether HTJ2K's quality gain survives tiling + framing overhead."""
import os, sys
import sys, os, subprocess, tempfile, json, numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'scripts')); import evalkit as ek
A = os.environ.get('OPENJPH_APPS', 'OpenJPH/build/src/apps'); T = tempfile.mkdtemp(); OUT = sys.argv[1]; NF = int(sys.argv[2])
def code_tile(t, q, k):
    open(f'{T}/t{k}.pgm', 'wb').write(b'P5\n128 128\n16383\n' + t.astype('>u2').tobytes())
    subprocess.run([f'{A}/ojph_compress/ojph_compress', '-i', f'{T}/t{k}.pgm', '-o', f'{T}/t{k}.j2c', '-qstep', f'{q:.8g}',
                    '-reversible', 'false', '-num_decomps', '5'], capture_output=True)
    cs = open(f'{T}/t{k}.j2c', 'rb').read()
    return cs, len(cs) - cs.index(b'\xff\x90')        # bytes from first SOT (tile-part) to EOC
def frame_bytes(sum_tiles):
    payload = sum_tiles + 20 * 7
    return 44 + 4 * 8 + payload + 2 * ((payload + 255) // 256)
hdr_sizes = []
for s in ['flirv_4FRnNp', 'flirv_5RSrbW', 'flirv_ePoikf', 'flirstill_val']:
    o = ek.load_seq(s, NF); rec = []; used = []
    for fr in o:
        tiles = [fr[y:y + 128, x:x + 128] for y in range(0, 512, 128) for x in range(0, 640, 128)]
        lo, hi = np.log(1e-5), np.log(0.5); best = None
        for _ in range(14):
            m = (lo + hi) / 2; tot = sum(code_tile(t, np.exp(m), k)[1] for k, t in enumerate(tiles))
            if frame_bytes(tot) <= 8333: hi = m; best = np.exp(m)
            else: lo = m
        out = np.zeros_like(fr); tot = 0
        for k, t in enumerate(tiles):
            cs, body = code_tile(t, best, k); tot += body; hdr_sizes.append(cs.index(b'\xff\x90'))
            subprocess.run([f'{A}/ojph_expand/ojph_expand', '-i', f'{T}/t{k}.j2c', '-o', f'{T}/d.pgm'], capture_output=True)
            d = open(f'{T}/d.pgm', 'rb').read(); i = d.index(b'16383\n') + 6
            y, x = (k // 5) * 128, (k % 5) * 128; out[y:y + 128, x:x + 128] = np.frombuffer(d[i:], '>u2').reshape(128, 128)
        rec.append(out); used.append(frame_bytes(tot))
    m = ek.seq_metrics(o, np.stack(rec)); m.pop('per_frame')
    r = dict(seq=s, n=len(o), frame_bytes_mean=float(np.mean(used)), **m); open(OUT, 'a').write(json.dumps(r) + '\n')
    print(f"{s:14s} frame bytes {np.mean(used):7.1f}  psnr_dr {m['psnr_dr']:6.2f} ssim {m['ssim_agc']:.4f} rmse {m['rmse']:.2f} maxerr {m['maxerr']}", flush=True)
print('main header bytes per tile codestream (not sent):', sorted(set(hdr_sizes)))
