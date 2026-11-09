import json, sys, collections, numpy as np
rows = [json.loads(l) for l in open(sys.argv[1])]
g = collections.defaultdict(list)
for r in rows: g[(r['loss'], r['burst'], r['fec'], r['k'])].append(r)
print(f"{'loss':>5} {'burst':>5} {'fec':>5} {'k':>2} | {'codec':>6} {'fec':>5} {'hdr':>4} {'wire':>6} | {'ok%':>6} {'lost%':>6} {'psnr':>6} {'psnr_ok':>7} {'p_min':>6} {'reqs':>5} {'delay':>5}")
for key in sorted(g):
    v = g[key]; m = lambda x: np.mean([r[x] for r in v])
    fr = m('frames')
    print(f"{key[0]:5.2f} {key[1]:5d} {key[2]:>5} {key[3]:2d} | {m('kbps'):6.0f} {m('fec_kbps'):5.0f} {m('hdr_kbps'):4.0f} {m('wire_kbps'):6.0f} | "
          f"{100*m('ok')/fr:6.1f} {100*m('lost')/fr:6.1f} {m('psnr_avg'):6.2f} {m('psnr_ok_avg'):7.2f} {min(r['psnr_min'] for r in v):6.2f} {m('intra_requests'):5.1f} {m('release_delay_avg'):5.2f}")
