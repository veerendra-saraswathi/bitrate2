#!/usr/bin/env python3
"""Lossless references on the same 14-bit data (external libraries, reference only):
JPEG-LS (CharLS), JPEG 2000 reversible, JPEG XL lossless. Verifies exact decode."""
import os, sys
import sys, time, numpy as np, imagecodecs as ic
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'scripts')); import evalkit as ek
print("seq,codec,bpp,Mbps@30,ratio,enc_ms,exact")
for name in ['pv24', 'seq28', 'csq7', 'pan90']:
    s = ek.load_seq(name, 30)
    for codec, enc, dec in [('jpegls', lambda f: ic.jpegls_encode(f), ic.jpegls_decode),
                            ('j2k_rev', lambda f: ic.jpeg2k_encode(f, level=0, reversible=True, bitspersample=14), ic.jpeg2k_decode),
                            ('jxl_ll', lambda f: ic.jpegxl_encode(f, lossless=True, effort=7, bitspersample=14), ic.jpegxl_decode)]:
        tot = 0; t = 0; ok = True
        for f in s:
            t0 = time.perf_counter(); b = enc(f); t += time.perf_counter() - t0
            tot += len(b); ok &= bool(np.array_equal(dec(b), f))
        n = len(s); px = s[0].size
        print(f"{name},{codec},{tot*8/(px*n):.4f},{tot*8*30/n/1e6:.2f},{s.nbytes/tot:.3f},{t*1e3/n:.1f},{int(ok)}", flush=True)
