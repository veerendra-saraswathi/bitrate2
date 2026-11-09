#!/usr/bin/env python3
"""SoW 'robustness to channel errors' / 'introducing losses in the stored bitstream'.
Encode a sequence with ewc_enc (stored .ewc file), inject errors into the stored file, decode with
ewc_dec (writes raw 14-bit, freezes lost frames), measure every frame against the original.
Error models (p = parameter):
  ber     flip each bit with probability p
  pktdel  cut the stream into 1400-byte packets, delete each with probability p (stream shrinks)
  pktzero same packets, zero-fill each with probability p (stream keeps its length, as after
          UDP reassembly without FEC)
  burst   zero-fill 512-byte bursts; p = expected bursts per 100 kB
usage: stored_loss.py BIN OUT.jsonl CONFIG_TAG "enc args" seq[,seq...]   (env SEEDS, CONDS)"""
import os, sys
import sys, os, json, tempfile, subprocess, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'scripts'))
import evalkit as ek

def corrupt(data, mode, p, seed, pkt=1400):
    rng = np.random.default_rng(seed); a = np.frombuffer(data, np.uint8).copy()
    if mode == 'ber':
        k = rng.binomial(a.size * 8, p); pos = rng.choice(a.size * 8, size=k, replace=False)
        np.bitwise_xor.at(a, pos // 8, (1 << (pos % 8)).astype(np.uint8)); return a.tobytes()
    if mode in ('pktdel', 'pktzero'):
        n = (a.size + pkt - 1) // pkt; hit = rng.random(n) < p
        if mode == 'pktzero':
            for i in np.nonzero(hit)[0]: a[i * pkt:(i + 1) * pkt] = 0
            return a.tobytes()
        return b''.join(a[i * pkt:(i + 1) * pkt].tobytes() for i in range(n) if not hit[i])
    if mode == 'burst':
        for _ in range(rng.poisson(p * a.size / 1e5)):
            s = int(rng.integers(0, max(1, a.size - 512))); a[s:s + 512] = 0
        return a.tobytes()
    raise ValueError(mode)

def main():
    B, OUT, TAG, ENC = sys.argv[1:5]; seqs = sys.argv[5].split(',')
    SEEDS = int(os.environ.get('SEEDS', '3'))
    CONDS = [(c.split(':')[0], float(c.split(':')[1])) for c in os.environ.get('CONDS',
             'none:0,ber:1e-6,ber:1e-5,ber:1e-4,pktdel:0.01,pktdel:0.05,pktzero:0.01,pktzero:0.05,pktzero:0.10,burst:1,burst:5').split(',')]
    T = tempfile.mkdtemp()
    for name in seqs:
        m = ek.corpus()[name]; w, h = m['w'], m['h']; orig = ek.load_seq(name); n = len(orig)
        lo, hi = np.percentile(orig.reshape(n, -1), [0.1, 99.9], axis=1)
        src = ek.seq_path(name); bs = f'{T}/s.ewc'
        subprocess.run([f'{B}/ewc_enc', '-i', src, '-o', bs, '-W', str(w), '-H', str(h)] + ENC.split(), check=True, capture_output=True)
        clean = open(bs, 'rb').read()
        for mode, p in CONDS:
            agg = []
            for seed in (range(SEEDS) if p > 0 else [0]):
                data = corrupt(clean, mode, p, seed) if p > 0 else clean
                open(f'{T}/c.ewc', 'wb').write(data)
                r = subprocess.run([f'{B}/ewc_dec', '-i', f'{T}/c.ewc', '-o', f'{T}/r.raw', '--frames', str(n)], capture_output=True, text=True)
                s = r.stdout.split('SUMMARY')[-1].split(); S = {s[i]: float(s[i + 1]) for i in range(0, len(s) - 1, 2)}
                rec = np.fromfile(f'{T}/r.raw', '<u2'); nf = rec.size // (w * h)
                rec = rec[:nf * w * h].reshape(nf, h, w)
                if nf < n: rec = np.concatenate([rec, np.full((n - nf, h, w), 8192, np.uint16)])
                d = orig.astype(np.float64) - rec[:n]; rmse = np.sqrt((d ** 2).reshape(n, -1).mean(1))
                ps14 = 20 * np.log10(16383.0 / np.maximum(rmse, 1e-9)); psdr = 20 * np.log10((hi - lo) / np.maximum(rmse, 1e-9))
                agg.append(dict(reliable=S.get('reliable', 0) / n, damaged=S.get('damaged', 0) / n, ref_invalid=S.get('ref_invalid', 0) / n,
                                lost=S.get('lost', 0) / n, psnr14=float(np.minimum(ps14, 199).mean()), psnr_dr=float(np.minimum(psdr, 199).mean()),
                                psnr_dr_p5=float(np.percentile(np.minimum(psdr, 199), 5)), bad20=float((psdr < psdr.max() - 20).mean()) if p > 0 else 0.0,
                                bytes_changed=len(clean) - len(data) if len(data) != len(clean) else int(np.count_nonzero(np.frombuffer(clean, np.uint8) != np.frombuffer(data, np.uint8)))))
            res = dict(seq=name, config=TAG, enc=ENC, mode=mode, p=p, seeds=len(agg), **{k: float(np.mean([a[k] for a in agg])) for k in agg[0]})
            open(OUT, 'a').write(json.dumps(res) + '\n')
            print(f"{TAG:10s} {name:13s} {mode:7s} {p:<7g} reliable {100*res['reliable']:5.1f}% damaged {100*res['damaged']:5.1f}% refbad {100*res['ref_invalid']:5.1f}% "
                  f"lost {100*res['lost']:5.1f}% psnr_dr {res['psnr_dr']:6.2f} p5 {res['psnr_dr_p5']:6.2f}", flush=True)

if __name__ == '__main__':
    main()
