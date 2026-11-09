#!/usr/bin/env bash
# Generate a minimal SoW-scope synthetic set
set -euo pipefail
mkdir -p data/synthetic/{thermal,visible,highbit,seq}

python3 - << 'PY'
import numpy as np, os

def write(path, arr):
    arr.tofile(path)
    print(f"wrote {path}  shape={arr.shape} dtype={arr.dtype}")

# 640x512 8-bit thermal-like (low contrast + hot blobs)
h, w = 512, 640
base = np.random.normal(80, 12, (h, w)).astype(np.float32)
yy, xx = np.mgrid[0:h, 0:w]
for cy, cx, r, val in [(180, 220, 40, 180), (300, 400, 25, 160), (100, 500, 15, 200)]:
    mask = (yy-cy)**2 + (xx-cx)**2 < r**2
    base[mask] = val
write("data/synthetic/thermal/therm_640x512_8.u16", np.clip(base, 0, 255).astype(np.uint16))

# 640x512 8-bit visible-like (edges + texture)
vis = np.random.normal(128, 30, (h, w)).astype(np.float32)
vis[100:120, :] = 200
vis[:, 300:310] = 40
write("data/synthetic/visible/vis_640x512_8.u16", np.clip(vis, 0, 255).astype(np.uint16))

# 1920x1200 14-bit gradient + noise (high-depth)
H, W = 1200, 1920
yy, xx = np.mgrid[0:H, 0:W]
grad = (yy / H * 8000 + xx / W * 4000).astype(np.float32)
noise = np.random.normal(0, 30, (H, W))
write("data/synthetic/highbit/grad_1920x1200_14.u16", np.clip(grad + noise, 0, 16383).astype(np.uint16))

# short 14-bit sequence (5 frames, 640x512)
for i in range(5):
    f = np.random.normal(4000 + i*200, 40, (512, 640)).astype(np.float32)
    write(f"data/synthetic/seq/seq14_{i:02d}_640x512.u16", np.clip(f, 0, 16383).astype(np.uint16))

print("synthetic set ready")
PY