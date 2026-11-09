#!/usr/bin/env python3
"""Pack FLIR ADAS v2 16-bit TIFFs into raw little-endian uint16 clips for ewc (was run on the
user's PC). No scaling or bit shifting: values are copied as stored.
- Each 16-bit test video: first run of up to 150 consecutive frame numbers.
- 8-bit videos (mode 'L') are skipped.
- 100 random images_thermal_val stills (seed 7).
Writes <out>/flirv_<id>_640x512_<n>.u16, flirstill_val_640x512_100.u16 and manifest.json.
Usage: python3 pack.py /path/to/FLIR_ADAS_v2 /path/to/out"""
import os, re, sys, glob, json, random, numpy as np
from PIL import Image
R, O = sys.argv[1], sys.argv[2]; os.makedirs(O, exist_ok=True)
vids = {}
for p in sorted(glob.glob(f'{R}/video_thermal_test/analyticsData/*.tiff')):
    m = re.match(r'(video-\w+)-frame-(\d+)-', os.path.basename(p)); vids.setdefault(m.group(1), []).append((int(m.group(2)), p))
man = {}
for v, fr in vids.items():
    fr.sort()
    if Image.open(fr[0][1]).mode != 'I;16': man[v] = 'skipped: 8-bit'; continue
    run = [fr[0]]
    for a in fr[1:]:
        if a[0] != run[-1][0] + 1 or len(run) >= 150: break
        run.append(a)
    arr = np.stack([np.asarray(Image.open(p)) for _, p in run]).astype('<u2')
    fn = f'flirv_{v[6:12]}_640x512_{len(run)}.u16'; arr.tofile(f'{O}/{fn}')
    man[fn] = dict(video=v, frames=[run[0][0], run[-1][0]], n=len(run), min=int(arr.min()), max=int(arr.max()))
random.seed(7)
ps = random.sample(sorted(glob.glob(f'{R}/images_thermal_val/analyticsData/*.tiff')), 100)
arr = np.stack([np.asarray(Image.open(p)) for p in ps]).astype('<u2'); arr.tofile(f'{O}/flirstill_val_640x512_100.u16')
man['flirstill_val_640x512_100.u16'] = dict(files=[os.path.basename(p) for p in ps], n=100, min=int(arr.min()), max=int(arr.max()))
json.dump(man, open(f'{O}/manifest.json', 'w'), indent=1)
for k, v in man.items(): print(k, v if isinstance(v, str) else {a: b for a, b in v.items() if a != 'files'})
