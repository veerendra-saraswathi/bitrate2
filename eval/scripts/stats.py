#!/usr/bin/env python3
"""Per-sequence statistics of the raw uint16 LE corpus (no conversion)."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import evalkit as ek
import json, numpy as np
D = ek.DATA; c=json.load(open(f'{D}/corpus.json'))
print("name,w,h,n,min,max,mean,std,bits_needed,distinct_frame0,lsb_used,mb_raw")
for k,v in c.items():
    a=np.fromfile(f"{D}/{k}_{v['w']}x{v['h']}.u16",dtype='<u2').reshape(-1,v['h'],v['w'])
    assert a.shape[0]==v['n']
    print(f"{k},{v['w']},{v['h']},{v['n']},{a.min()},{a.max()},{a.mean():.1f},{a.std():.1f},"
          f"{int(np.ceil(np.log2(int(a.max())+1)))},{len(np.unique(a[0]))},{int((a&1).any())},{a.nbytes/1e6:.1f}")
for f,w,h in [('flirseq_640x480_28',640,480),('flircsq_1024x768_7',1024,768)]:
    a=np.fromfile(f'{D}/src/{f}.u16',dtype='<u2').reshape(-1,h,w)
    print(f"src/{f},{w},{h},{a.shape[0]},{a.min()},{a.max()},{a.mean():.1f},{a.std():.1f},{int(np.ceil(np.log2(int(a.max())+1)))},{len(np.unique(a[0]))},{int((a&1).any())},{a.nbytes/1e6:.1f}")
