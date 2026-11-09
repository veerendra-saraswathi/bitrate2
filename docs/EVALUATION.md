# ewc evaluation (2026-11-05)

All numbers below were measured in this session unless marked *stored*. Raw files are in
`eval/results/`, scripts in `eval/scripts/`. Nothing here was estimated.

## Setup

- Host: Intel Xeon @ 2.10 GHz, **2 vCPUs**, 7 GB RAM, Linux 6.18, GCC 13.3, CMake 3.28,
  Release (`-O3`-equivalent CMake default), no `-march=native`. No GPU, no Jetson.
- Threads: 1 unless stated; "2 threads" uses both cores.
- Metrics (per frame, then averaged), from `work/scripts/evalkit.py`:
  - **PSNR (14-bit peak)**: 10·log10(16383² / MSE). Inflated for low-contrast thermal scenes.
  - **PSNR (scene range)**: 20·log10(range / RMSE), range = p99.9 − p0.1 of the original.
    This is the more honest fidelity number for thermal data.
  - **SSIM (display)**: SSIM after the same linear contrast stretch (p0.5..p99.5 of the
    original) is applied to both images.
  - RMSE, MAE, max absolute error in raw counts (DN).
  - Lossless: exact pixel equality, checked with a byte compare of the files and a
    pixel-by-pixel compare (`eval/scripts/lossless_check.py`).

## Data

Sections 1–6 use the high-bit-depth thermal data that was in the workspace, stored as raw
little-endian `uint16` (`eval/results/data_stats.csv`). FLIR ADAS v2 was added later and is
evaluated separately, as a fully held-out test set, in section 7.

| Sequence | Source | Size × frames | Values (min–max) | Needs | Kind |
|---|---|---|---|---|---|
| pv24 | DJI M30T raw counts, solar site | 640×512 × 24 | 2596–5333 | 13 bit | independent stills |
| seq28 | FLIR SEQ sample, offset −16384 | 640×480 × 28 | 2921–7229 | 13 bit | 30 fps video |
| csq7 | FLIR CSQ sample, centre crop | 640×512 × 7 | 13595–16319 | 14 bit | video |
| pan90 | **semi-synthetic**: pv frame warped along a pan path + moving hot target + noise | 640×512 × 90 | 3696–5350 | 13 bit | video |
| src/flirseq | FLIR SEQ raw (no offset) | 640×480 × 28 | 19305–23613 | **15 bit** | video |
| src/flircsq | FLIR CSQ raw | 1024×768 × 7 | 13595–16319 | 14 bit | video |

The low bit is used in all sequences. No 8-bit conversion happens anywhere in the tools or
library: samples go in and out as `uint16` unchanged.

Development / validation split for tuning: development = seq28 + pan90 frames 0–44;
validation = pan90 frames 45–89 + csq7.

## 1. Lossless (bit-exact)

`eval/results/baseline_lossless.csv`, `ref_lossless.csv`. ewc LOCO profile, 1 thread.

| Sequence | bit depth | bits/pixel | ratio | Mbps @ 30 fps | exact | enc / dec ms/frame |
|---|---|---|---|---|---|---|
| pv24 | 14 | 5.749 | 2.78 | 56.5 | yes | 22 / 25 |
| seq28 | 14 | 5.565 | 2.88 | 51.3 | yes | 19 / 22 |
| csq7 | 14 | 5.351 | 2.99 | 52.6 | yes | 19 / 20 |
| pan90 | 14 | 6.013 | 2.66 | 59.1 | yes | 23 / 25 |
| flirseq raw | **16** | 5.702 | 2.81 | 52.6 | yes | 21 / 21 |
| flircsq 1024×768 | 14 | 5.512 | 2.90 | 130.1 | yes | 58 / 52 |

All six round trips are byte-identical. With 2 threads, lossless encodes in about 12–16 ms
per 640×512 frame.

**Lossless at 2 Mbps is not possible on this data**: about 51–59 Mbps is needed at 30 fps.

Comparison with external lossless coders (first 30 frames, reference only, not shipped):

| Sequence | ewc LOCO | JPEG-LS (CharLS) | JPEG 2000 reversible | JPEG XL lossless |
|---|---|---|---|---|
| pv24 | 5.749 | 5.646 | 5.664 | 5.365 (166 ms/frame) |
| seq28 | 5.565 | 5.482 | 5.502 | 5.378 (165 ms/frame) |
| csq7 | 5.351 | 5.261 | 5.644 | 3.648 (207 ms/frame) |
| pan90 | 6.013 (90 fr.) | 5.888 | 5.891 | 5.581 (179 ms/frame) |

Cost of resilience (seq28): one stripe and no CRCs gives 5.455 bpp, **0.5% below CharLS**.
64-row stripes add 1.2%, block CRCs add 0.8%. So the ewc gap to JPEG-LS comes entirely from
its error-resilience features. JPEG XL is smaller (much smaller on csq7) but about 15× slower
than real time on this CPU.

## 2. Near-lossless (LOCO, max error bounded)

`baseline_codec.jsonl`, `nearfar.jsonl`. Codec Mbps @ 30 fps / PSNR (scene range) dB.

| near | pv24 | seq28 | csq7 | pan90 |
|---|---|---|---|---|
| 1 | 41.1 / 62.9 | 37.1 / 74.0 | 38.2 / 69.5 | 43.8 / 61.6 |
| 2 | 34.2 / 58.1 | 30.3 / 69.2 | 31.5 / 64.7 | 36.7 / 56.9 |
| 4 | 26.5 / 52.9 | 22.8 / 64.0 | 24.2 / 59.5 | 29.0 / 51.6 |
| 8 | 19.6 / 47.4 | 15.9 / 58.6 | 17.4 / 54.1 | 21.6 / 46.1 |
| 16 | | 9.5 / 53.4 | 12.2 / 48.9 | |
| 32 | | 5.4 / 48.1 | 8.3 / 43.2 | |
| 128 | | 2.1 / 36.0 | 2.9 / 31.5 | |

Max error always equals `near` (bound holds). Against the wavelet coder at the same rate
(`wavhigh.jsonl`), the wavelet coder has lower RMSE below about 20–25 Mbps (seq28 at 8 Mbps:
wavelet 56.5 dB vs LOCO near=16 at 9.5 Mbps 53.4 dB). LOCO is the right choice only when a
hard per-pixel error bound is required, or at high rates.

## 3. Lossy at 2 Mbps (main operating point)

Codec rate 2000 kbps = 8333 bytes/frame at 30 fps. ewc numbers measured; JPEG 2000,
JPEG XL, x265 and AV1 are *stored* results from the earlier session (same metrics code,
same frames). The JPEG 2000 csq7 result was re-run here and reproduced exactly.

| Sequence | Method | kbps | PSNR 14-bit | PSNR range | SSIM | max err |
|---|---|---|---|---|---|---|
| pv24 | ewc intra | 2000 | 57.64 | 34.45 | 0.879 | 344 |
| pv24 | JPEG 2000 | 1982 | 58.13 | 34.95 | 0.890 | 339 |
| seq28 | ewc intra | 2000 | 64.22 | 52.11 | 0.995 | 183 |
| seq28 | ewc I/P GOP 30 | 2000 | **65.75** | **53.64** | 0.996 | 111 |
| seq28 | JPEG 2000 | 1986 | 64.43 | 52.33 | 0.995 | 159 |
| seq28 | HEVC x265 | 2021 | 64.41 | 52.30 | 0.995 | 217 |
| seq28 | LOCO near=128 | 2128 | 48.15 | 36.04 | 0.800 | 128 |
| csq7 | ewc intra | 2000 | 61.73 | 45.20 | 0.976 | 112 |
| csq7 | ewc I/P GOP 30 | 2000 | **61.93** | **45.39** | 0.976 | 109 |
| csq7 | JPEG 2000 | 1990 | 61.92 | 45.38 | 0.977 | 143 |
| csq7 | HEVC x265 | 2083 | 60.91 | 44.38 | 0.974 | 215 |
| csq7 | LOCO near=128 | 2879 | 48.08 | 31.54 | 0.691 | 128 |
| pan90 | ewc intra | 2000 | 55.88 | 31.47 | 0.872 | 230 |
| pan90 | ewc I/P GOP 30 | 2000 | **64.19** | **39.78** | 0.970 | 193 |
| pan90 | JPEG 2000 (30 fr.) | 1961 | 56.51 | 31.93 | 0.882 | 191 |
| pan90 | HEVC x265 | 2007 | 63.94 | 39.53 | 0.970 | 292 |

JPEG XL and AV1 runs did not reach the target rate (68–1370 kbps) and are left out of the
comparison; they are in `work/results_refs_*.json`.

Findings:
- Intra: ewc is 0.2–0.6 dB below JPEG 2000. Not worth an architecture change.
- I/P: ewc matches or beats x265 at the same rate on all three video sequences, with the
  lowest max error. On pv24 (independent stills) the scene-cut detector codes every frame
  as I, so I/P equals intra.
- Predictive near-lossless is 16 dB worse at 2 Mbps. Prediction suits lossless; the wavelet
  suits low rates.

## 4. Rate control and GOP (2 Mbps codec rate, 2 threads)

`rc_dev.jsonl`, `rc_val.jsonl`. "strict" = `vbv 1, iscale 1` (no frame above 8333 bytes).
`v2i2` = 2-frame buffer, I frames get 2× budget.

| Set | Config | avg kbps | peak frame kbps | PSNR range | SSIM |
|---|---|---|---|---|---|
| dev seq28 | intra | 2000 | 2000 | 52.11 | 0.995 |
| dev seq28 | GOP 30 strict | 2000 | 2000 | 53.64 | 0.996 |
| dev seq28 | GOP 30 v2i2 | 2032 | — | 53.71 | 0.996 |
| dev pan90 0–44 | intra | 2000 | 2000 | 31.54 | 0.875 |
| dev pan90 0–44 | GOP 30 strict | 2000 | 2000 | 39.58 | 0.968 |
| dev pan90 0–44 | GOP 30 v3i3 | 2014 | — | 40.17 | 0.975 |
| val pan90 45–89 | intra | 2000 | 2000 | 31.40 | 0.870 |
| val pan90 45–89 | GOP 30 strict | 2000 | 2000 | 39.46 | 0.967 |
| val pan90 45–89 | GOP 30 v2i2 | 2007 | — | 39.81 | 0.972 |
| val csq7 | GOP 30 strict | 2000 | 2000 | 45.39 | 0.976 |
| val csq7 | GOP 30 v2i2 | **2128** | — | 45.57 | 0.976 |

- Strict CBR meets the rate on every frame (peak = average = budget).
- A larger I-frame budget helps moving content (+0.35 dB on validation pan90) but overshoots
  the average on short clips (csq7: +6.4% rate for +0.18 dB), because the buffer has no time
  to drain. At equal rate it is not a clear win. **Default stays strict CBR.** Use a 2-frame
  buffer only where the link tolerates short bursts and averaging over seconds.
- GOP 15 vs 30: within 0.1–0.5 dB. Longer GOPs trade quality for slower loss recovery.

## 5. Network and FEC (equal total wire rate)

`net_pan90.jsonl`, `net_pan90_table.txt`. pan90, GOP 30, receiver feedback after 2 frames,
**2000 kbps total on the wire for every configuration** (codec budget shrinks to make room
for headers and parity), MTU 1400, Gilbert–Elliott loss, 5 seeds × 90 frames each.

"ok" = frame decoded reliably (complete data, valid reference). PSNR includes frozen frames.

| Loss | Burst | FEC | codec kbps | ok % | lost % | PSNR 14-bit |
|---|---|---|---|---|---|---|
| 1% | 1 | none | 1965 | 77.1 | 0.9 | 62.76 |
| 1% | 1 | rs k=8 | 1679 | 100 | 0 | **63.45** |
| 1% | 4 | none | 1965 | 92.4 | 0.4 | **63.65** |
| 1% | 4 | rs k=4 | 1399 | 97.8 | 0.2 | 62.24 |
| 5% | 1 | none | 1965 | 34.0 | 4.7 | 58.38 |
| 5% | 1 | xor k=4 | 1399 | 90.0 | 0.9 | 61.77 |
| 5% | 1 | xori k=4 | 1399 | 87.1 | 1.1 | 61.55 |
| 5% | 1 | rs k=4 | 1399 | **100** | 0 | **62.54** |
| 5% | 4 | none | 1965 | 73.6 | 4.7 | **61.49** |
| 5% | 4 | rs k=2 | 1310 | **85.6** | 2.2 | 60.80 |
| 10% | 1 | none | 1965 | 19.3 | 10.0 | 55.18 |
| 10% | 1 | xor k=4 | 1399 | 75.1 | 2.2 | 60.20 |
| 10% | 1 | rs k=4 | 1399 | **94.0** | 0.7 | **61.95** |
| 10% | 4 | none | 1965 | 57.3 | 8.9 | 59.45 |
| 10% | 4 | rs k=2 | 1310 | **75.1** | 4.2 | **59.87** |

(Full table with all 36 configurations: `eval/results/net_pan90_table.txt`.)

- Random loss ≥ 5%: Reed–Solomon k=4 is clearly best at equal wire rate. XOR repairs only
  one loss per group; RS repairs any np losses.
- Bursty loss (mean 4 datagrams): every in-frame FEC degrades, because a 2 Mbps frame is
  only ~6–7 datagrams with 2 parity. Interleaving does not help much at this size. RS k=2
  gives the most reliable frames; no FEC gives the highest mean PSNR (more codec bits).
- Low loss (1%): no FEC + feedback gives the best mean PSNR, but 8–23% of frames are not
  reliable. Whether that is acceptable depends on the application.
- Differences of a few percent "ok" between XOR variants are within seed noise (5 seeds).

Without the new reference tracking, the old bench would have reported most of the
`ref_invalid` frames (e.g. 52 of 90 at 2% loss, no FEC) as fine.

## 6. Performance and resources

`resources.csv` (pan90, 30 frames, standalone `ewc_enc` / `ewc_dec`, ms per frame):

| Profile | Threads | Encode ms | Enc RSS MB | Decode ms | Dec RSS MB |
|---|---|---|---|---|---|
| intra 2 Mbps | 1 | 7.6 | 17 | 8.1 | 16 |
| intra 2 Mbps | 2 | 7.8 | 17 | 6.1 | 16 |
| GOP 30 2 Mbps | 1 | 34.7 | 29 | 9.3 | 20 |
| GOP 30 2 Mbps | 2 | 24.2 | 29 | 11.7 | 20 |
| lossless | 1 | 22.1 | 14 | 23.4 | 19 |
| lossless | 2 | 15.6 | 14 | 13.4 | 19 |

Decoder RSS includes the whole input file, which `ewc_dec` reads at once.

- I/P encoding needs 2 threads to stay inside the 33 ms frame period on this CPU
  (p99 about 30–40 ms with 2 threads, so occasional frames are late).
- P-frame encode time (1 thread, seq28): motion estimation 11.3 ms, wavelet + trees 4.9,
  coding 5.9, local decode 6.6, motion compensation 2.9.
- **Tested and rejected:** multithreaded wavelet transform (rows/columns split; output
  bit-identical). On 2 vCPUs it saved about 1 ms median (26.6 vs 27.7 ms), inside run-to-run
  noise (`dwt_mt_timing.txt`). Reverted. It may help on more cores; untested.
- **Not tested:** CUDA, Jetson (no hardware), SIMD-specific work, `-march=native` builds.

## 7. FLIR ADAS v2, 14-bit (held-out test)

Source: the user's copy of FLIR ADAS v2. Only the 16-bit TIFFs in `analyticsData` were
used; the `data` folders hold 8-bit JPEG previews. Audit of every test-video frame and 300
random train/val stills: `eval/results/flir_audit.txt`. Clip list: `flir_adas_v2_manifest.json`.

- All frames are 640×512. Stills and 6 of the 8 test videos are `uint16` holding 13–14-bit
  values (overall range 3112–11947). **Two test videos (`ZAtDSNuZZjkZFvMAo`,
  `t3f7QC8hZr6zYXpEZ`) are 8-bit even in `analyticsData`** and are excluded from 14-bit results.
- Contrast is very low: mean within-frame std 53–122 DN for the videos, ~250 DN for stills.
  Temporal change between frames (~4 DN) is about the size of the sensor noise (~4 DN).
- Clips: 150 consecutive frames (no numbering gaps) from each 14-bit video, plus 100 random
  `images_thermal_val` stills. TIFF → raw `uint16` without any scaling.
- No parameter was tuned on this data. All settings come from sections 1–6.

### Lossless (bit-exact on all 1000 frames, 2 threads)

| Clip | frames | ewc bpp | ratio | Mbps @ 30 | exact | JPEG-LS bpp | enc / dec ms |
|---|---|---|---|---|---|---|---|
| flirv_4FRnNp | 150 | 4.667 | 3.43 | 45.9 | yes | 4.603 | 14 / 21 |
| flirv_5RSrbW | 150 | 4.387 | 3.65 | 43.1 | yes | 4.324 | 13 / 20 |
| flirv_6tLtjd | 150 | 4.396 | 3.64 | 43.2 | yes | 4.334 | 13 / 20 |
| flirv_SCiKdG | 150 | 4.774 | 3.35 | 46.9 | yes | 4.708 | 13 / 20 |
| flirv_ePoikf | 150 | 4.442 | 3.60 | 43.7 | yes | 4.373 | 12 / 22 |
| flirv_vbrSzr | 150 | 4.647 | 3.44 | 45.7 | yes | 4.584 | 13 / 21 |
| flirstill_val | 100 | 4.795 | 3.34 | 47.1 | yes | 4.723 | 13 / 23 |

ewc LOCO is 1.4–1.6% above JPEG-LS (CharLS), the same resilience cost as before.
Lossless needs **43–47 Mbps** at 30 fps for this content.

### Near-lossless (codec Mbps / PSNR scene range; max error always = near)

| Clip | near=1 | near=2 | near=4 |
|---|---|---|---|
| flirv_4FRnNp | 29.8 Mbps / 54.8 dB | 23.0 Mbps / 50.0 dB | 16.2 Mbps / 44.8 dB |
| flirv_5RSrbW | 27.9 Mbps / 48.1 dB | 20.5 Mbps / 43.4 dB | 14.5 Mbps / 38.2 dB |
| flirv_6tLtjd | 28.2 Mbps / 55.1 dB | 20.9 Mbps / 50.3 dB | 14.5 Mbps / 45.2 dB |
| flirv_SCiKdG | 30.8 Mbps / 56.5 dB | 24.1 Mbps / 51.7 dB | 17.1 Mbps / 46.5 dB |
| flirv_ePoikf | 28.4 Mbps / 55.8 dB | 21.2 Mbps / 51.0 dB | 15.0 Mbps / 45.8 dB |
| flirv_vbrSzr | 29.6 Mbps / 55.7 dB | 22.7 Mbps / 51.0 dB | 16.1 Mbps / 45.8 dB |
| flirstill_val | 31.7 Mbps / 60.6 dB | 24.7 Mbps / 55.8 dB | 17.9 Mbps / 50.6 dB |

### Lossy at 2 Mbps (PSNR scene range dB / SSIM display)

JPEG 2000 was run on the first 30 frames only (it is slow), so the matched ewc column uses
the same 30 frames. x265 uses its 12-bit path (input >> 2), which adds about 1.15 DN RMSE.

| Clip | ewc intra (all) | ewc GOP 30 (all) | ewc intra (30 fr.) | JPEG 2000 (30 fr.) | x265 (all) |
|---|---|---|---|---|---|
| flirv_4FRnNp | 40.17 / 0.911 | 40.03 / 0.910 | 40.19 / 0.910 | 40.69 / 0.916 | 38.52 / 0.892 |
| flirv_5RSrbW | 34.87 / 0.773 | 34.42 / 0.758 | 34.05 / 0.741 | 34.76 / 0.764 | 33.20 / 0.725 |
| flirv_6tLtjd | 41.54 / 0.918 | 41.47 / 0.917 | 41.55 / 0.919 | 42.18 / 0.925 | 39.51 / 0.901 |
| flirv_SCiKdG | 41.15 / 0.810 | 41.34 / 0.814 | 42.36 / 0.862 | 42.83 / 0.869 | 39.34 / 0.778 |
| flirv_ePoikf | 42.10 / 0.926 | 42.04 / 0.924 | 40.38 / 0.916 | 40.97 / 0.922 | 39.71 / 0.904 |
| flirv_vbrSzr | 41.29 / 0.892 | 41.09 / 0.890 | 40.94 / 0.891 | 41.45 / 0.898 | 39.59 / 0.870 |
| flirstill_val | 42.09 / 0.928 | 42.08 / 0.928 | 42.79 / 0.939 | 43.27 / 0.943 | — |


- ewc intra is **0.5–0.7 dB below JPEG 2000** on matched frames, and **1.6–2.4 dB above x265**.
- **P frames give no gain on FLIR ADAS** (GOP 30 vs intra: −0.45 to +0.19 dB) while costing
  about 2.5× the encode time. The clips are noise-limited: at 2 Mbps the coder already works
  at the noise level, and noise cannot be predicted from the previous frame.
- Tested and rejected:
  - Stricter intra/inter switching (`--scut 0.9` / `0.75`). FLIR dev clips gained up to
    0.45 dB, but pan90 lost 8 dB, because every frame went intra
    (`eval/results/scut_dev.jsonl`). The cost estimate depends on reference quality, so a
    fixed threshold cannot separate the two cases.
  - Reconstruction offset (`--rq5` 10/12/20) and wavelet levels (5/7): within ±0.07 dB on the
    dev clips (`flir_tune_dev.jsonl`). Defaults kept.
- **Recommendation for noise-limited sensors: intra-only (`--gop 1`).** Same quality, about
  12 ms encode instead of 30 ms, and better loss resilience (below).

### Packet loss (clip ePoikf, 2 Mbps total on the wire, 3 seeds, `eval/results/net_flir.jsonl`)

| Loss | Burst | Mode | FEC | ok % | lost % | PSNR 14-bit |
|---|---|---|---|---|---|---|
| 5% | 1 | intra | rs k=4 | 99.8 | 0.2 | 71.76 |
| 5% | 1 | GOP 30 + feedback | rs k=4 | 98.9 | 0.2 | 71.99 |
| 10% | 1 | intra | none | 50.9 | 8.0 | 68.17 |
| 10% | 1 | intra | rs k=4 | **98.7** | 0.7 | 71.65 |
| 10% | 1 | GOP 30 + feedback | rs k=4 | 94.0 | 0.7 | 71.71 |
| 10% | 4 | intra | rs k=4 | **90.2** | 6.0 | 70.68 |
| 10% | 4 | GOP 30 + feedback | rs k=4 | 72.0 | 6.0 | 70.03 |

Intra-only with Reed–Solomon k=4 keeps the most frames reliable, especially under bursty
loss, because no frame depends on an earlier one.

## 8. Against the original pipeline (bitrate-main, unmodified)

`bitrate-main` on the user's machine has the same sources as the `baseline` copy here
(md5 match). Its library was called directly, unmodified (`eval/h2h/`).

### Its own comparison contract (KAIST 640×512 8-bit, single frame, encode = median of 11 runs)

| Profile | Original bitrate-main (best of levels 1–6 × QP 1–8) | ewc (CRC + stripes) | ewc (raw: no CRC, 1 stripe) | Target | JPEG-LS reference (D) |
|---|---|---|---|---|---|
| Thermal lossless | 59.46 Mbps, 2.4 ms | **10.07 Mbps**, 9.1 ms | **9.81 Mbps** | ≤ 12 | 9.86 |
| Thermal max err ≤ 1 | not possible (QP 2 already max err 3) | **5.73 Mbps @ 50.27 dB** | 5.51 @ 50.25 | ≤ 6 | 5.47 @ 50.32 |
| Visible lossless | 135.93 Mbps, 3.9 ms | 32.60 Mbps | 32.17 | — | 32.16 |
| Visible max err ≤ 1 | not possible | 19.60 Mbps @ 49.91 dB | 19.27 | — | 19.27 @ 49.9 |

- The STATUS.md figure of "~20.3 Mbps lossless" for architecture A could not be reproduced
  with this code; its best is 59.46 Mbps (coder mode 0 is worse: 213 Mbps).
- ewc meets both contract targets. Without integrity overhead it beats the D lossless
  reference by 0.5%. The original is faster per frame but 5.9× larger.

### 14-bit FLIR at 2 Mbps (first 30 frames per clip; PSNR scene range dB / SSIM / RMSE DN)

| Clip | ewc intra | original as shipped (pixel >> 8) | original with free per-frame contrast stretch |
|---|---|---|---|
| flirv_4FRnNp | 40.2 / 0.910 / 4.4 | -23.9 / 0.005 / 7021.1 | 2.6 / 0.013 / 331.9 |
| flirv_5RSrbW | 34.0 / 0.741 / 3.7 | -31.6 / 0.006 / 7199.5 | 5.5 / 0.012 / 99.5 |
| flirv_6tLtjd | 41.5 / 0.919 / 3.9 | -24.1 / 0.012 / 7391.5 | 7.0 / 0.027 / 207.4 |
| flirv_SCiKdG | 42.4 / 0.862 / 4.8 | -20.8 / 0.021 / 6863.8 | 13.8 / 0.042 / 129.1 |
| flirv_ePoikf | 40.4 / 0.916 / 3.9 | -25.1 / 0.007 / 7359.4 | 4.1 / 0.015 / 254.5 |
| flirv_vbrSzr | 40.9 / 0.891 / 4.3 | -23.3 / 0.006 / 7019.8 | 4.4 / 0.014 / 289.6 |
| flirstill_val | 42.8 / 0.939 / 8.2 | -16.8 / 0.011 / 7268.4 | 4.1 / 0.024 / 826.5 |

- The original takes 8-bit input only. For wider input it keeps the top 8 bits, which leaves
  about 3 grey levels on FLIR data. It also has no lossless or near-lossless path for 14-bit data.
- To fit 8333 bytes per frame it needs QP 30–60; even with a free contrast stretch the error is
  100–830 DN RMSE, versus 3.7–8.2 DN for ewc.

## Known limitations

- FLIR ADAS v2: 150-frame clips from 6 of the 8 test videos (2 are 8-bit only) and 100 stills, not the full set. pan90 is semi-synthetic.
- Short sequences (7–90 frames). Rate-control conclusions on long streams are untested.
- Network results use one sequence (pan90) and 5 seeds; small differences are noise.
- Encoder latency p99 exceeds one frame period for I/P on this 2-core CPU.
- Lossless needs ~50–60 Mbps for this content; the 2 Mbps target applies to lossy only.
