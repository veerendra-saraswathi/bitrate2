import os, sys
# Feasibility: total bytes for max-error <= k using (a) direct near-lossless (JPEG-LS NEAR=k)
# vs (b) ewc wavelet base layer at B bytes + JPEG-LS NEAR=k on the residual (orig - base).
import sys, subprocess, numpy as np, imagecodecs as ic
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'scripts')); import evalkit as ek
E = os.path.join(ek.BIN, 'ewc_bench')
def wav(x, bd, nbytes):
    x.astype('<u2').tofile('/tmp/in.u16')
    k = max(1, int(nbytes * 8 * 30 / 1000))
    subprocess.run([E, '--in', '/tmp/in.u16', '--w', '640', '--h', '512', '--n', '1', '--bd', str(bd), '--kbps', str(k), '--out-rec', '/tmp/r.u16', '-q'], capture_output=True)
    return np.fromfile('/tmp/r.u16', '<u2').reshape(512, 640).astype(np.int32)
def resid_bytes(x, base, k):
    r = x.astype(np.int32) - base
    off = -r.min(); u = (r + off).astype(np.uint16)
    s = ic.jpegls_encode(u, level=k)
    back = ic.jpegls_decode(s).astype(np.int32) - off + base
    assert np.abs(back - x).max() <= k
    return len(s)
cases = [('8bit thermal', np.fromfile(ek.kaist_raw('thermal'), 'u1').reshape(512, 640), 8),
         ('8bit visible', np.fromfile(ek.kaist_raw('visible'), 'u1').reshape(512, 640), 8),
         ('14bit ePoikf', ek.load_seq('flirv_ePoikf', 1)[0], 14)]
for name, x, bd in cases:
    for k in ([1, 2, 4] if bd == 8 else [1, 4, 16]):
        direct = len(ic.jpegls_encode(x, level=k))
        best = (direct, 0)
        for frac in (0.1, 0.2, 0.3, 0.45, 0.6):
            B = int(direct * frac)
            tot = B + resid_bytes(x, wav(x, bd, B), k)
            if tot < best[0]: best = (tot, B)
        print(f'{name} maxerr<={k}: direct near-lossless {direct} B | two-layer best {best[0]} B (base {best[1]}) -> {100*(best[0]/direct-1):+.1f}%')
