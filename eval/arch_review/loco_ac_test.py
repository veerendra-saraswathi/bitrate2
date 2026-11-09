import os, sys
import sys, subprocess, re, numpy as np, imagecodecs as ic
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'scripts')); import evalkit as ek
def bench(x, bd, near, extra=[]):
    x.astype('<u2').tofile('/tmp/in.u16')
    p = subprocess.run([os.path.join(ek.BIN, 'ewc_bench'), '--in', '/tmp/in.u16', '--w', '640', '--h', '512', '--n', '1', '--bd', str(bd), '--lossless', '--near', str(near), '--out-rec', '/tmp/r.u16', '-q'] + extra, capture_output=True, text=True)
    o = p.stdout + p.stderr
    b = float(re.search(r'avg_bytes ([0-9.]+)', o).group(1)); e = float(re.search(r'enc_med ([0-9.]+)', o).group(1)); d = float(re.search(r'dec_med ([0-9.]+)', o).group(1))
    r = np.fromfile('/tmp/r.u16', '<u2').reshape(512, 640).astype(int)
    assert np.abs(r - x.astype(int)).max() <= near, 'bound violated'
    return b, e, d
cases = [('KAIST thermal 8b', np.fromfile(ek.kaist_raw('thermal'), 'u1').reshape(512, 640), 8, [0, 1, 2, 4]),
         ('KAIST visible 8b', np.fromfile(ek.kaist_raw('visible'), 'u1').reshape(512, 640), 8, [0, 1, 2, 4]),
         ('FLIR ePoikf 14b', ek.load_seq('flirv_ePoikf', 1)[0], 14, [0, 1, 4, 16]),
         ('FLIR 5RSrbW 14b', ek.load_seq('flirv_5RSrbW', 1)[0], 14, [0, 1, 4, 16]),
         ('FLIR still 14b', ek.load_seq('flirstill_val', 1)[0], 14, [0, 1, 4, 16])]
for name, x, bd, nears in cases:
    for k in nears:
        d = len(ic.jpegls_encode(x, level=k))
        g = bench(x, bd, k, ['--golomb']); a = bench(x, bd, k)
        a1 = bench(x, bd, k, ['--noblockcrc', '--stripe', '512'])
        print(f'{name} NEAR={k}: CharLS {d} | ewc Golomb {g[0]:.0f} ({100*(g[0]/d-1):+.1f}%) | ewc AC {a[0]:.0f} ({100*(a[0]/d-1):+.1f}%) enc {a[1]:.1f} dec {a[2]:.1f} (golomb {g[1]:.1f}/{g[2]:.1f}) | AC 1 stripe no CRC {a1[0]:.0f} ({100*(a1[0]/d-1):+.1f}%)')
