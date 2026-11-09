#!/usr/bin/env python3
"""Peak RSS and CPU utilisation of ewc_enc / ewc_dec per profile (pan90, 640x512, 30 frames)."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import evalkit as ek
import subprocess, os, sys, tempfile
B = sys.argv[1]; R = os.environ.get('RSSRUN', 'rssrun'); D = ek.DATA; T = tempfile.mkdtemp()
src = f'{T}/in.u16'
open(src, 'wb').write(open(f'{D}/pan90_640x512.u16', 'rb').read(640 * 512 * 2 * 30))
def go(cmd):
    p = subprocess.run([R] + cmd, capture_output=True, text=True)
    l = [x for x in p.stderr.splitlines() if x.startswith('RSSRUN')][-1].split()
    return float(l[1]), int(l[2]) / 1024, float(l[3])
print("profile,threads,enc_wall_ms_per_frame,enc_rss_MB,enc_cpu_util,dec_wall_ms_per_frame,dec_rss_MB,dec_cpu_util,stream_MB")
for name, a in [('intra_2M', []), ('gop30_2M', ['--gop', '30']), ('lossless', ['--lossless']), ('near4', ['--lossless'])]:
    for th in ['1', '2']:
        args = ['-W', '640', '-H', '512', '--threads', th] + a
        if name == 'near4': args = ['-W', '640', '-H', '512', '--threads', th, '--lossless']  # ewc_enc has no --near
        w, r, c = go([f'{B}/ewc_enc', '-i', src, '-o', f'{T}/s.ewc'] + args)
        w2, r2, c2 = go([f'{B}/ewc_dec', '-i', f'{T}/s.ewc', '-o', f'{T}/o.raw', '--threads', th])
        if name == 'near4': continue
        print(f"{name},{th},{w*1e3/30:.1f},{r:.1f},{c/w:.2f},{w2*1e3/30:.1f},{r2:.1f},{c2/w2:.2f},{os.path.getsize(T+'/s.ewc')/1e6:.2f}")
