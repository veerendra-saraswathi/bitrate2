#!/usr/bin/env python3
"""FLIR ADAS v2 audit (was run on the user's PC, inside the folder that holds FLIR_ADAS_v2).
Reads every 16-bit TIFF in video_thermal_test/analyticsData and 300 random TIFFs from the
train/val stills; reports shape, dtype, range, mean, within-frame std, bits needed, LSB use
and frame-number gaps. Output: flir_audit.json (summary in eval/results/flir_audit.txt).
Usage: python3 audit.py /path/to/FLIR_ADAS_v2"""
import os, re, sys, glob, json, random, numpy as np
from PIL import Image
R = sys.argv[1]

def stats(paths, label):
    shapes, modes = {}, {}; mins, maxs, means, stds = [], [], [], []; lsb = 0; hi = 0
    for p in paths:
        im = Image.open(p); modes[im.mode] = modes.get(im.mode, 0) + 1
        a = np.asarray(im); key = str(a.shape) + str(a.dtype); shapes[key] = shapes.get(key, 0) + 1
        a = a.astype(np.int64); mins.append(a.min()); maxs.append(a.max()); means.append(a.mean()); stds.append(a.std())
        lsb |= int((a & 1).any()); hi = max(hi, int(a.max()))
    return dict(label=label, n=len(paths), shapes=shapes, modes=modes, min=int(min(mins)), max=int(max(maxs)),
                mean=float(np.mean(means)), std_within_frame=float(np.mean(stds)),
                bits_needed=int(np.ceil(np.log2(hi + 1))), lsb_used=lsb)

vids = {}
for p in sorted(glob.glob(f'{R}/video_thermal_test/analyticsData/*.tiff')):
    m = re.match(r'(video-\w+)-frame-(\d+)-', os.path.basename(p)); vids.setdefault(m.group(1), []).append((int(m.group(2)), p))
res = []
for v, fr in vids.items():
    fr.sort(); nums = [n for n, _ in fr]; gaps = sum(1 for a, b in zip(nums, nums[1:]) if b != a + 1)
    s = stats([p for _, p in fr], v); s.update(first=nums[0], last=nums[-1], gaps=gaps); res.append(s); print(json.dumps(s), flush=True)
random.seed(1)
for split in ['images_thermal_train', 'images_thermal_val']:
    ps = sorted(glob.glob(f'{R}/{split}/analyticsData/*.tiff'))
    s = stats(random.sample(ps, min(300, len(ps))), split + ' (300 random)'); s['total_files'] = len(ps); res.append(s); print(json.dumps(s), flush=True)
json.dump(res, open('flir_audit.json', 'w'), indent=1)
