# Architectures in this project — how they work, how they differ, how they score

Date: 6 Nov 2026. Branch `exp-2026-11-05`. All numbers below were measured in this project.
Scripts and raw results are in `eval/arch_review/` and `eval/results/exp/`.

## 1. Two words first: "average loss" and "max error"

Lossy compression changes pixel values a little. There are two ways to measure how much.

- **Average loss** (PSNR, SSIM, MAE, RMSE). How far pixels are off *on average*.
  Higher PSNR and SSIM are better. Lower MAE and RMSE are better.
- **Max error**. How far the *single worst pixel* is off, in sensor units (DN).

A **guaranteed max error** of k means: no pixel in the frame is ever more than k off.
It does **not** mean less loss overall. A coder with a guarantee usually has a slightly
*lower* average quality at the same size, because it spends bits capping the worst
pixels instead of improving the average.

Use a max-error guarantee when one bad pixel matters (radiometric measurement, a
hot-spot threshold, a hard spec). Use the best average quality when the image is looked
at or fed to a detector.

## 2. What is in this folder

| # | Architecture | In this folder as code? | Status |
|---|---|---|---|
| A | bitrate-main Architecture A (5/3 wavelet, QP) | No. Old repo. Evaluated with `eval/h2h_v2/bm_eval.cpp` against that repo's library | Retired |
| D | bitrate-main Architecture D (JPEG-LS via CharLS) | No. External library (not allowed by the SoW). Evaluated with `eval/h2h_v2/d_eval.py` | Retired, replaced by D+ |
| 1 | ewc wavelet, whole frame | Yes: `src/dwt.cpp`, `src/spiht.cpp`, `src/codec.cpp` (`--tile 0`) | Kept |
| 2 | ewc wavelet, tiled (PoC default) | Yes: same files (`--poc`) | **Kept — main 2 Mbps path** |
| 3 | ewc near-lossless, Golomb (JPEG-LS method) | Yes: `src/loco.cpp` (`--golomb`) | Kept only so old files decode |
| 4 | **D+** (JPEG-LS method + ewc arithmetic coder) | Yes: `src/loco.cpp` (default for `--lossless`) | **Kept — lossless / near-lossless path** |
| 5 | Two-layer (wavelet + D+) | Yes: `tools/ewc_layered.cpp` | Kept as an optional mode |
| 6 | ewc wavelet with P-frames (temporal) | Yes: `--gop N` | Rejected for the PoC (section 6) |
| 7 | HTJ2K (OpenJPH) | No. External library. Scripts in `eval/exp/` | Rejected (section 6) |
| 8 | GStreamer transport | Optional tool `tools/gst/` | Transport only, not a codec |

## 3. How each one works

### A — bitrate-main Architecture A
1. Convert the image to 8 bits (keeps only the top 8 of 14 bits).
2. Reversible 5/3 wavelet.
3. Divide coefficients by a quantiser step (QP), entropy-code them.
4. No rate control: the size depends on QP, not on a byte budget.

Weak points: drops 6 of 14 bits before coding; no way to hit 8,333 bytes per frame;
quality collapses below ~10 KB per frame; no error detection.

### D — bitrate-main Architecture D (JPEG-LS, CharLS)
1. Predict each pixel from its left, top and top-left neighbours.
2. Pick one of 365 "contexts" from the local gradients; learn and remove each
   context's bias.
3. Optionally round the prediction error to steps of 2·NEAR+1 (near-lossless:
   every pixel within ±NEAR).
4. Code the error with Golomb codes; code flat areas as run lengths.

Strengths: simple, fast, exact max-error control. Weak points: Golomb codes cannot use
less than 1 bit per pixel; one flipped bit loses the whole frame; external library.

### 1 and 2 — ewc wavelet (whole frame / tiled)
1. Subtract the frame mean. Scale samples up by 2^(18 − bit depth) so the integer
   transform loses no precision (the "precision shift", added 6 Nov).
2. CDF 9/7 wavelet: splits the image into coarse and fine detail at 5–6 scales.
3. Weight each band by how much it matters for the final error.
4. Code bit-planes from most to least important (SPIHT trees + adaptive arithmetic coder).
   Any prefix of the stream is a valid, coarser image.
5. Rate control: cut every group at the point that gives the least error for exactly
   8,333 bytes. Never over budget.
6. Protect headers with CRC-32 and the payload with a CRC-16 every 256 bytes;
   a redundant copy of the most important bytes is added at the end.

Tiled (PoC) = the same, done in independent 128×128 tiles. A damaged tile does not affect
the others and tiles code in parallel. Costs about 0.05 dB against whole frame.

### 3 — ewc near-lossless, Golomb
The same method as D, written in-house, inside ewc's container (CRC-protected stripes of
64 rows that decode independently). About 1.5–2% larger than CharLS because of the
CRCs and the stripe restarts.

### 4 — D+ (the improved D)
D's method with two changes taken from the ewc wavelet path:
1. **Adaptive arithmetic coder instead of Golomb codes.** Each prediction error is split
   into bits; the important bits get probabilities that adapt to the image. This can go
   below 1 bit per pixel, which Golomb codes cannot.
2. **Automatic stripe height.** Every stripe re-learns its statistics. When stripes would
   be under ~3 KB, the encoder makes them taller.
Keeps ewc's CRC protection and damage isolation. Same exact max-error control as D.

### 5 — Two-layer
1. Code the frame with the wavelet path at part of the budget (layer 1).
2. Decode it, take the difference to the original, and code that difference with D+ at
   max error k (layer 2).
3. Per frame, try a few budget splits and also "no layer 1" (pure D+); keep the smallest.
Result: max error ≤ k is guaranteed. If layer 2 is lost, layer 1 still decodes.

## 4. How they differ

| | A | D (CharLS) | ewc wavelet | D+ | Two-layer |
|---|---|---|---|---|---|
| Core idea | wavelet + QP | pixel prediction | wavelet + bit-plane coding | pixel prediction | wavelet then prediction |
| Entropy coder | basic | Golomb | adaptive arithmetic | adaptive arithmetic | both |
| 14-bit input | no (8-bit only) | yes | yes | yes | yes |
| Hits a fixed byte budget | no | no (search NEAR) | **yes, exactly** | no (search NEAR) | yes (search split) |
| Max error control | no | **exact ±NEAR** | no (average only) | **exact ±NEAR** | **exact ±k** |
| Lossless | yes (8-bit) | yes | no (lossy only) | **yes, best** | — |
| Error detection / damage isolation | no | no (frame lost) | yes (CRC, tiles) | yes (CRC, stripes) | yes |
| External library | no | yes (not allowed) | no | no | no |
| Best at | — | — | **2 Mbps quality** | **lossless, near-lossless** | **2 Mbps with a bound** |

## 5. Evaluation results

### 5.1 The SoW target: 2 Mbps, 14-bit, 640×512

Same 70 frames for all: 7 FLIR ADAS v2 clips (6 video + 1 still set), first 10 frames each.
Budget 8,333 bytes per frame (2 Mbps at 30 fps). PSNR "scene" uses the scene's own range
(0.1–99.9 percentile); PSNR "14-bit" uses 16383 as peak. Max error = worst pixel in any frame.
For D, ewc-Golomb and D+, NEAR was searched per frame to fit the budget (best case for them).
For A, QP and levels were searched exhaustively per frame (5 s per frame; best case for A).
Source: `eval/arch_review/final_matrix.json`.

| Architecture | PSNR scene (dB) | PSNR 14-bit (dB) | SSIM | MAE (DN) | RMSE (DN) | Max error (DN) | Bytes avg / max | Encode / decode (ms) |
|---|---|---|---|---|---|---|---|---|
| A, as shipped (top 8 bits) | −22.88 | 7.37 | 0.008 | 6922 | 7015 | 11070 | 8284 / 8304 | 2.2 / 1.8 (+5 s search) |
| A, contrast-stretched (best case) | 6.41 | 36.66 | 0.023 | 272 | 300 | 2506 | 7432 / 8307 | 1.9 / 1.4 (+5 s search) |
| D (CharLS) | 33.38 | 63.63 | 0.724 | 11.57 | 13.83 | 140 | 8030 / 8319 | 0.8 / 0.7 (1 thread) |
| ewc near-lossless, Golomb | 32.11 | 62.36 | 0.670 | 13.59 | 16.21 | 150 | 8087 / 8331 | 1.4 / 1.2 |
| D+ | 34.35 | 64.60 | 0.752 | 9.79 | 11.83 | 107 | 8049 / 8316 | 5.2 / 1.7 |
| ewc wavelet, whole frame | **41.24** | **71.49** | **0.902** | **3.55** | **4.61** | 155 | 8332 / 8332 | 8.9 / 4.7 |
| **ewc wavelet, tiled (PoC)** | 41.19 | 71.44 | **0.902** | 3.57 | 4.64 | 130 | 8327 / 8328 | 6.3 / 3.2 |

Video clips only (6 clips), to compare with the two-layer mode, which does not fit stills:

| Architecture | PSNR scene (dB) | SSIM | MAE (DN) | Max error, worst clip (DN) | Guaranteed? |
|---|---|---|---|---|---|
| D (CharLS) | 33.88 | 0.726 | 8.10 | 42 | yes (per frame NEAR) |
| D+ | 34.73 | 0.754 | 7.26 | 37 | yes (per frame NEAR) |
| ewc wavelet, tiled (PoC) | **40.98** | **0.894** | **3.16** | 39 | no |
| Two-layer | 39.53 | 0.876 | 3.76 | **20** | **yes** |

Encode times: 2 threads unless noted, idle machine. At 30 fps one frame is 33.3 ms and the
SoW allows 2 frames (66.7 ms). Two-layer encode is 51 ms median (within 2 frames, not 1).

**Reading it:** at 2 Mbps the wavelet path is far ahead on average quality
(+6.8 dB over D+, +7.8 dB over CharLS). The two-layer mode halves the worst-case error
(39 → 20 DN on video) for −1.5 dB average quality. Architecture A cannot work at this rate.

### 5.2 Lossless and near-lossless (where max error is set, size is the result)

Bytes per frame, mean of 7 FLIR clips × 5 frames (14-bit), plus the two KAIST 8-bit
frames. Error bound verified on every frame. Source: `eval/results/exp/dplus_results.json`.

| Max error (NEAR) | D (CharLS) | ewc Golomb | **D+** | D+ vs CharLS |
|---|---|---|---|---|
| 0 (lossless), 14-bit FLIR | 185,273 | 187,941 | **182,756** | −1.4% |
| 1, 14-bit FLIR | 120,804 | 122,989 | **118,104** | −2.2% |
| 2, 14-bit FLIR | 91,564 | 93,520 | **89,312** | −2.5% |
| 4, 14-bit FLIR | 65,084 | 66,355 | **60,476** | −7.1% |
| 8, 14-bit FLIR | 31,391 | 32,560 | **28,058** | −10.6% |
| 16, 14-bit FLIR | 12,784 | 13,963 | **11,228** | −12.2% |
| 32, 14-bit FLIR | 6,090 | 7,358 | **5,286** | −13.2% |
| 0, 8-bit KAIST thermal | 41,067 | 41,954 | **36,326** | −11.5% |
| 0, 8-bit KAIST visible | **134,009** | 135,843 | 134,157 | +0.1% |
| 4, 8-bit KAIST visible | 52,418 | 53,212 | **45,538** | −13.1% |

D+ is smaller than CharLS on 62 of 63 measured points (median −9.1%, best −27.3%,
worst +0.1%). Architecture A lossless: 8-bit only, 247,741 B (thermal) and 566,365 B
(visible) — 6.8× and 4.2× larger than D+. A cannot code 14-bit lossless.

Speed (14-bit lossless, per frame): CharLS 6.8 ms (1 thread), ewc Golomb 13.4 ms,
D+ 23.5 ms (2 threads). D+ is the slowest; all are under one frame period.

### 5.3 8-bit KAIST at D's published points (same bytes)

| Data, D point | D: PSNR, max err | ewc wavelet, same bytes | Two-layer, same max err: bytes |
|---|---|---|---|
| Thermal, NEAR 1 (22,794 B) | 50.32 dB, 1 | 56.04 dB, 2 | 17,879 B (−22%) |
| Thermal, NEAR 4 (8,656 B) | 40.99 dB, 4 | 51.99 dB, 4 | 4,248 B (−51%) |
| Visible, NEAR 4 (52,418 B) | 40.17 dB, 4 | 46.21 dB, 8 | 45,046 B (−14%) |
| Thermal, 2 Mbps | 39.76 dB, 5 | 51.88 dB, 4 | — |
| Visible, 2 Mbps | 25.20 dB, 31 | 35.59 dB, 26 | — |

### 5.4 Errors in the stored bitstream

| Coder | One flipped bit in a frame |
|---|---|
| A | 35% of frames rejected, 62% decoded silently wrong |
| D (CharLS) | 99.5% of frames lost entirely |
| ewc (all paths) | frame kept and marked damaged; never silently wrong; 0 crashes |

D+ versus ewc Golomb under stored losses (bit errors, deleted / zeroed packets, bursts):
equal or better reliability and PSNR (`eval/results/exp/dplus_loss.jsonl`).
Decoder fuzzing (release build, 360 corrupted streams): 0 crashes, 0 hangs.

## 6. Rejected, with the reason

| Idea | Result | Why rejected |
|---|---|---|
| HTJ2K (OpenJPH) as the coder | +0.07 to +0.40 dB on video inside ewc framing, −0.90 dB on stills; 35% of damaged frames decoded silently wrong | small gain, unsafe under errors, external library |
| P-frames (temporal prediction) at 2 Mbps | +0.3 dB, encode 4× slower, errors spread across frames | FLIR frames differ from each other as much as neighbouring pixels do |
| GStreamer | no compression; its FEC needs 20–50 ms extra buffering to match ewc's own FEC | transport only |
| 5/3 instead of 9/7 for lossy | equal on 14-bit once the precision bug was fixed | the bug was the real cause |

## 7. Final choice

| What you need | Use | Command |
|---|---|---|
| 2 Mbps, best image (SoW PoC) | ewc wavelet, tiled | `ewc_enc --poc` |
| 2 Mbps, worst pixel capped | Two-layer | `ewc_layered in.u16 640 512 N 14 8333 k ...` |
| Lossless (perfect reconstruction) | D+ | `ewc_enc --lossless` |
| Near-lossless (max error N) | D+ | `ewc_enc --lossless --near N` |

All of these are one codec (ewc), one file format and one decoder.

## 8. Reproduce

Test data: see `data/README.md`. The 8-bit KAIST frames are in `data/kaist/`. The 14-bit FLIR clips
(≈650 MB) are on Sam's PC in `Bitrate-20261026T054640Z-1-001\FLIR_ADAS_v2_raw16\` (MD5s listed).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j && (cd build && ctest)
pip install numpy scikit-image imagecodecs          # imagecodecs = CharLS, for the D comparisons only
export EWC_DATA=/path/to/FLIR_ADAS_v2_raw16          # or copy the clips into data/flir/
export EWC_BIN=build
python3 eval/arch_review/final_matrix.py     # section 5.1 (A rows need eval/h2h_v2/run14.sh output)
python3 eval/arch_review/dplus_eval.py       # section 5.2
python3 eval/arch_review/rd_table.py         # section 5.3
```
