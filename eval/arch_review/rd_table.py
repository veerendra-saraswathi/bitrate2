import os, sys
import sys, subprocess, csv, re, numpy as np, imagecodecs as ic
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'scripts')); import evalkit as ek
def met(x, r, peak): d = x.astype(float) - r; mse = (d*d).mean(); return (99.0 if mse == 0 else 10*np.log10(peak**2/mse)), int(np.abs(d).max())
def bench(x, bd, args):
    x.astype('<u2').tofile('/tmp/in.u16')
    p = subprocess.run([os.path.join(ek.BIN, 'ewc_bench'), '--in', '/tmp/in.u16', '--w', '640', '--h', '512', '--n', '1', '--bd', str(bd), '--out-rec', '/tmp/r.u16', '-q'] + args, capture_output=True, text=True)
    return float(re.search(r'avg_bytes ([0-9.]+)', p.stdout + p.stderr).group(1)), np.fromfile('/tmp/r.u16', '<u2').reshape(512, 640)
def layered(x, bd, k, budget):
    x.astype('<u2').tofile('/tmp/in.u16')
    subprocess.run([os.path.join(ek.BIN, 'ewc_layered'), '/tmp/in.u16', '640', '512', '1', str(bd), str(budget), str(k), '/tmp/l.u16', '/tmp/l.csv'], env={'EWL_RBD': '12'})
    row = list(csv.DictReader(open('/tmp/l.csv')))[0]
    return int(row['total']), np.fromfile('/tmp/l.u16', '<u2').reshape(512, 640)
cases = [('KAIST thermal 8-bit', np.fromfile(ek.kaist_raw('thermal'), 'u1').reshape(512, 640), 8, [1, 2, 4]),
         ('KAIST visible 8-bit', np.fromfile(ek.kaist_raw('visible'), 'u1').reshape(512, 640), 8, [1, 2, 4]),
         ('FLIR ePoikf 14-bit', ek.load_seq('flirv_ePoikf', 1)[0], 14, [1, 4, 16])]
for name, x, bd, nears in cases:
    peak = 255 if bd == 8 else float(np.subtract(*np.percentile(x, [99.9, 0.1])))
    print(f'\n{name}  (PSNR peak {"255" if bd == 8 else "scene range"})')
    print('point | D JPEG-LS | ewc near-lossless | ewc wavelet, same bytes | ewc two-layer, same max err')
    for k in nears:
        s = ic.jpegls_encode(x, level=k); dB = len(s); dp = met(x, ic.jpegls_decode(s), peak)
        nb, nr = bench(x, bd, ['--lossless', '--near', str(k)]); npm = met(x, nr, peak)
        wb, wr = bench(x, bd, ['--kbps', str(int(dB * 8 * 30 / 1000))]); wp = met(x, wr, peak)
        lb, lr = layered(x, bd, k, dB); lp = met(x, lr, peak)
        print(f'NEAR={k} | {dB} B {dp[0]:.2f} dB e{dp[1]} | {nb:.0f} B {npm[0]:.2f} dB e{npm[1]} | {wb:.0f} B {wp[0]:.2f} dB e{wp[1]} | {lb} B {lp[0]:.2f} dB e{lp[1]}')
    wb, wr = bench(x, bd, ['--kbps', '2000']); wp = met(x, wr, peak)
    dn = next(n for n in range(0, 256) if len(ic.jpegls_encode(x, level=n)) <= 8333); s = ic.jpegls_encode(x, level=dn); dp = met(x, ic.jpegls_decode(s), peak)
    print(f'2 Mbps | {len(s)} B {dp[0]:.2f} dB e{dp[1]} (NEAR={dn}) | - | {wb:.0f} B {wp[0]:.2f} dB e{wp[1]} | -')
