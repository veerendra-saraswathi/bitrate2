#!/usr/bin/env python3
"""Shared evaluation helpers: corpus loading, quality metrics, reference codecs.

Metrics (per frame, reconstructed vs original, both uint16 14-bit):
  psnr14   : 10*log10(16383^2/MSE)          (standard 14-bit peak; inflated for low-contrast IR)
  psnr_dr  : 20*log10(range/RMSE), range = p99.9 - p0.1 of the original frame (contrast-relative)
  rmse     : RMSE in DN
  ssim_agc : SSIM on the operator view: linear AGC (p0.5..p99.5 of original) applied to both
  maxerr   : max absolute error in DN
"""
import json, os, subprocess, tempfile
import numpy as np
from skimage.metrics import structural_similarity

import glob, re

# ---- paths (override with environment variables; see data/README.md)
REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
DATA = os.environ.get('EWC_DATA', os.path.join(REPO, 'data', 'flir'))      # packed FLIR .u16 clips
BIN = os.environ.get('EWC_BIN', os.path.join(REPO, 'build'))               # ewc_bench, ewc_enc, ...
KAIST = os.path.join(REPO, 'data', 'kaist')                                # 8-bit KAIST frames (in repo)
PEAK14 = 16383.0


def seq_path(name):
    """Packed clip file for `name`: accepts NAME_WxH.u16 and pack.py's NAME_WxH_N.u16."""
    for pat in (f'{name}_*x*.u16',):
        hits = sorted(p for p in glob.glob(os.path.join(DATA, pat)) if re.fullmatch(re.escape(name) + r'_\d+x\d+(_\d+)?\.u16', os.path.basename(p)))
        if hits:
            return hits[0]
    raise FileNotFoundError(f'{name}: no {name}_WxH[_N].u16 in {DATA} (set EWC_DATA, see data/README.md)')


def corpus():
    """corpus.json in DATA if present, else built from the .u16 file names (FLIR packs)."""
    p = os.path.join(DATA, 'corpus.json')
    if os.path.exists(p):
        return json.load(open(p))
    c = {}
    for f in glob.glob(os.path.join(DATA, '*.u16')):
        m = re.fullmatch(r'(.+)_(\d+)x(\d+)(?:_\d+)?\.u16', os.path.basename(f))
        if m:
            w, h = int(m.group(2)), int(m.group(3))
            c[m.group(1)] = dict(w=w, h=h, n=os.path.getsize(f) // (2 * w * h))
    return c


def load_seq(name, n=None):
    m = corpus()[name]
    w, h = m['w'], m['h']
    a = np.fromfile(seq_path(name), dtype='<u2').reshape(-1, h, w)
    return a if n is None else a[:n]


def kaist_raw(which):
    """8-bit KAIST 640x512 frame ('thermal' or 'visible') shipped in data/kaist."""
    return os.path.join(KAIST, f'kaist_{which}.raw')


def kaist_u16(which, n=12):
    """The KAIST frame repeated n times as uint16 (input format of ewc_bench); cached in a temp dir."""
    out = os.path.join(tempfile.gettempdir(), f'ewc_kaist_{which}_{n}.u16')
    if not os.path.exists(out):
        x = np.fromfile(kaist_raw(which), 'u1').astype('<u2')
        np.tile(x, n).tofile(out)
    return out


def agc_params(x):
    lo, hi = np.percentile(x, [0.5, 99.5])
    return float(lo), float(max(hi, lo + 1))


def frame_metrics(o, r):
    o = o.astype(np.float64)
    r = r.astype(np.float64)
    d = o - r
    mse = float(np.mean(d * d))
    rmse = np.sqrt(mse)
    lo1, hi1 = np.percentile(o, [0.1, 99.9])
    rng = max(hi1 - lo1, 1.0)
    lo, hi = agc_params(o)
    ao = np.clip((o - lo) / (hi - lo), 0, 1) * 255
    ar = np.clip((r - lo) / (hi - lo), 0, 1) * 255
    ssim = structural_similarity(ao, ar, data_range=255, gaussian_weights=True, sigma=1.5,
                                 use_sample_covariance=False)
    inf = 199.0
    return dict(psnr14=inf if mse == 0 else 10 * np.log10(PEAK14 ** 2 / mse),
                psnr_dr=inf if mse == 0 else 20 * np.log10(rng / rmse),
                rmse=rmse, ssim_agc=float(ssim), maxerr=int(np.max(np.abs(d))))


def seq_metrics(orig, rec):
    rows = [frame_metrics(o, r) for o, r in zip(orig, rec)]
    out = {k: float(np.mean([x[k] for x in rows])) for k in rows[0]}
    out['maxerr'] = int(max(x['maxerr'] for x in rows))
    out['psnr14_min'] = float(min(x['psnr14'] for x in rows))
    out['per_frame'] = rows
    return out


# ---------------------------------------------------------------- reference intra codecs

def _bisect_size(enc, budget, lo, hi, iters=22, decreasing=True):
    """Find parameter giving the largest encoded size <= budget. enc(p)->bytes."""
    best = None
    for _ in range(iters):
        mid = (lo + hi) / 2
        b = enc(mid)
        if len(b) <= budget:
            best = (mid, b)
            if decreasing: hi = mid
            else: lo = mid
        else:
            if decreasing: lo = mid
            else: hi = mid
    return best


def ref_j2k(frame, budget):
    import imagecodecs as ic
    # level = compression ratio for OpenJPEG; larger ratio -> smaller file
    enc = lambda r: ic.jpeg2k_encode(frame, level=r, codecformat='j2k', bitspersample=14,
                                     resolutions=6, reversible=False)
    raw = frame.size * 2
    res = _bisect_size(enc, budget, raw / budget * 0.5, raw / budget * 4, decreasing=False)
    p, b = res
    return b, ic.jpeg2k_decode(b)


def ref_jxl(frame, budget):
    import imagecodecs as ic
    enc = lambda d: ic.jpegxl_encode(frame, distance=d, effort=7, bitspersample=14)
    res = _bisect_size(enc, budget, 0.05, 25.0, iters=18, decreasing=True)
    p, b = res
    return b, ic.jpegxl_decode(b)


# ---------------------------------------------------------------- reference video codecs (ffmpeg)

def ref_ffmpeg_video(seq, fps, kbps, codec='x265', vbv_frames=1.0, keyint=None):
    """Encode 14-bit sequence as gray12 (>>2) with a low-delay CBR configuration; returns
    (bytes_per_frame list approx, reconstructed 14-bit). Reference ceiling only (external libs)."""
    n, h, w = seq.shape
    tmp = tempfile.mkdtemp()
    src = f'{tmp}/in.raw'
    (seq >> 2).astype('<u2').tofile(src)
    keyint = keyint or 10 * fps
    buf = int(kbps * vbv_frames / fps)
    if codec == 'x265':
        out = f'{tmp}/o.hevc'
        params = (f'bframes=0:rc-lookahead=0:keyint={keyint}:min-keyint={keyint}:scenecut=0:'
                  f'vbv-maxrate={kbps}:vbv-bufsize={buf}:strict-cbr=1:frame-threads=1:log-level=error')
        cmd = ['ffmpeg', '-y', '-v', 'error', '-f', 'rawvideo', '-pix_fmt', 'gray12le', '-s', f'{w}x{h}',
               '-r', str(fps), '-i', src, '-c:v', 'libx265', '-preset', 'medium', '-tune', 'zerolatency',
               '-b:v', f'{kbps}k', '-x265-params', params, out]
    elif codec == 'av1':
        out = f'{tmp}/o.ivf'
        cmd = ['ffmpeg', '-y', '-v', 'error', '-f', 'rawvideo', '-pix_fmt', 'gray12le', '-s', f'{w}x{h}',
               '-r', str(fps), '-i', src, '-c:v', 'libaom-av1', '-usage', 'realtime', '-cpu-used', '6',
               '-lag-in-frames', '0', '-b:v', f'{kbps}k', '-minrate', f'{kbps}k', '-maxrate', f'{kbps}k',
               '-bufsize', f'{buf}k', '-g', str(keyint), '-row-mt', '1', out]
    else:
        raise ValueError(codec)
    subprocess.run(cmd, check=True)
    total = os.path.getsize(out)
    dec = f'{tmp}/dec.raw'
    subprocess.run(['ffmpeg', '-y', '-v', 'error', '-i', out, '-f', 'rawvideo', '-pix_fmt', 'gray12le', dec],
                   check=True)
    rec = np.fromfile(dec, dtype='<u2').reshape(-1, h, w)[:n].astype(np.int32)
    rec = np.clip((rec << 2) + 2, 0, 16383).astype(np.uint16)
    # per-frame sizes via ffprobe packets
    pk = subprocess.run(['ffprobe', '-v', 'error', '-show_entries', 'packet=size', '-of', 'csv=p=0', out],
                        capture_output=True, text=True).stdout.split()
    sizes = [int(x) for x in pk]
    return total, sizes, rec
