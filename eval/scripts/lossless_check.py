#!/usr/bin/env python3
"""Independent lossless check: ewc_enc --lossless -> ewc_dec -> byte compare + pixel compare.
usage: lossless_check.py BIN_DIR OUT_CSV [extra enc args...]"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import evalkit as ek
import sys, os, tempfile, numpy as np, filecmp
sys.path.insert(0, os.path.dirname(__file__)); from runlib import run
B, OUT, extra = sys.argv[1], sys.argv[2], sys.argv[3:]
D = ek.DATA
cases = [('pv24', f'{D}/pv24_640x512.u16', 640, 512, 14), ('seq28', f'{D}/seq28_640x480.u16', 640, 480, 14),
         ('csq7', f'{D}/csq7_640x512.u16', 640, 512, 14), ('pan90', f'{D}/pan90_640x512.u16', 640, 512, 14),
         ('flirseq_raw_bd16', f'{D}/src/flirseq_640x480_28.u16', 640, 480, 16),
         ('flircsq_1024x768', f'{D}/src/flircsq_1024x768_7.u16', 1024, 768, 14),
         ('flirseq_raw_at_bd14(out-of-range test)', f'{D}/src/flirseq_640x480_28.u16', 640, 480, 14)]
if os.environ.get('SEQS'):   # e.g. SEQS=flirv_4FRnNp,flirstill_val : corpus entries at 14 bit
    import json; c = json.load(open(f'{D}/corpus.json'))
    cases = [(k, ek.seq_path(k), c[k]['w'], c[k]['h'], 14) for k in os.environ['SEQS'].split(',')]
T = tempfile.mkdtemp(); rows = []
hdr = 'seq,w,h,bd,n,raw_MB,enc_MB,ratio,bpp,Mbps@30,enc_ms/f,dec_ms/f,enc_rss_MB,dec_rss_MB,byte_identical,pix_mismatch,max_abs_err,enc_rc,note'
for name, f, w, h, bd in cases:
    s, r = f'{T}/s.ewc', f'{T}/r.raw'
    for x in (s, r):
        if os.path.exists(x): os.remove(x)
    rc, te, me, _, err = run([f'{B}/ewc_enc', '-i', f, '-o', s, '-W', str(w), '-H', str(h), '--bd', str(bd), '--lossless'] + extra)
    note = err.strip().replace(',', ';').replace('\n', ' ')[:80]
    a = np.fromfile(f, '<u2'); n = a.size // (w * h)
    if rc != 0 or not os.path.exists(s):
        rows.append(f'{name},{w},{h},{bd},{n},{a.nbytes/1e6:.2f},,,,,,,,,,,,{rc},{note}'); continue
    rc2, td, md, _, _ = run([f'{B}/ewc_dec', '-i', s, '-o', r])
    b = np.fromfile(r, '<u2'); enc = os.path.getsize(s)
    same = int(filecmp.cmp(f, r, shallow=False))
    mm = int((a != b).sum()) if a.size == b.size else -1
    me_ = int(np.abs(a.astype(np.int64) - b).max()) if a.size == b.size else -1
    rows.append(f'{name},{w},{h},{bd},{n},{a.nbytes/1e6:.2f},{enc/1e6:.3f},{a.nbytes/enc:.3f},{enc*8/a.size:.4f},'
                f'{enc*8*30/n/1e6:.3f},{te*1e3/n:.2f},{td*1e3/n:.2f},{me/1024:.0f},{md/1024:.0f},{same},{mm},{me_},{rc},{note}')
open(OUT, 'w').write(hdr + '\n' + '\n'.join(rows) + '\n')
print(hdr); print('\n'.join(rows))
