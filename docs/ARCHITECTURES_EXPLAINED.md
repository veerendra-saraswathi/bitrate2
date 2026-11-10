# Architectures in this project — how they work, how they differ, how they score

Date: 6 Nov 2026. Branch `exp-2026-11-05`. All numbers below were measured in this project.
Scripts and raw results are in `eval/arch_review/` and `eval/results/exp/`.

**Video-results update: 10 November 2026.** Sections 1–8 retain the historical
6 November architecture review and its PoC assumptions. The first reported
Beauty/LTIR video runs from 9 November are recorded in [section 9](#9-first-video-results--9-november-2026).
For full-SoW evaluation, 2 Mbps and 640×512 are test points, not universal
acceptance limits; the scope also includes approximately 1920×1200, visible/IR,
8/10/12/14/16-bit and monochrome/color. Historical "retired" labels below do not
exclude buildable architectures from the revised video comparison.

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

## 9. First video results — 9 November 2026

### 9.1 Date, provenance and evidence status

| Field | Record |
|---|---|
| Experiment date | 9 November 2026 |
| Documentation date | 10 November 2026 |
| Beauty wavelet reporting time | Approximately 23:23–23:24 IST on 9 November (17:53–17:54 UTC) |
| Evidence | Terminal results reported in the working discussion; summarized in `Bitrate_Revised_Video_MVP_Bullets_2026-11-10.md` |
| Evidence level | Short offline video sequences; reported observations pending raw-log and artifact audit |
| Beauty working checkout | `~/bitrate2` on test host/container `0fb8b27cea6b` |
| Source revision actually tested | Not recovered; do not substitute the documentation commit or the current `main` revision |
| Hardware identity | Exact board, CPU, power mode and clocks not verified for these runs; a container hostname does not establish a Jetson model |
| Build configuration | Compiler, build flags, executable hashes and thread counts not recovered |
| Timing reference | 30 fps: one frame period = 33.33 ms; two-frame encoder deadline = 66.67 ms |

These are the first video results recorded in this dated log. They do not imply
that earlier FLIR clip evaluations in section 5 were absent. No new benchmark
was run when writing this section. Missing measurements are left unreported;
the tables are not substitutes for the original per-frame CSVs.

### 9.2 Inputs and test conditions

| Sequence | Geometry | Frames | Input representation | Metadata status |
|---|---|---:|---|---|
| Beauty, visible | 1920×1080 | 50 | 8-bit luma stored in uint16; `data/real/beauty_y_50.u16` relative to the test checkout | Wavelet commands omitted the 128-byte application metadata; replay required |
| LTIR, thermal/IR | 640×480 | 5 | Stored-16-bit samples; native sensor precision and exact source selection need verification | Wavelet runs reported with 128-byte application metadata |

Beauty tests only the luma plane, not full color reconstruction. A uint16 storage
container does not turn 8-bit luma into native high-depth data. Beauty is
1920×1080, not a demonstrated 1920×1200 case. Record source archive, frame indices,
source frame rate, conversion command, endianness and input SHA-256 before replay.

The 2/4/8 Mbps values below are fixed-rate test settings. They do not demonstrate
mid-stream bandwidth adaptation. Exact tile, level, GOP and thread settings must
be recovered from the commands; no temporal-prediction claim is made here.

### 9.3 Beauty — E-wavelet fixed-rate results

All three runs reported 50/50 frames decoded as reliable. "Reliable" is the
decoder's integrity/reference status; it does not mean lossless reconstruction
or establish reliability under network faults.

| Target (Mbps) | Reported actual (Mbps) | Maximum encoded frame (B) | Average PSNR (dB) | Minimum frame PSNR (dB) | Worst absolute error (DN) |
|---|---:|---:|---:|---:|---:|
| 2 | 1.978 | 8,321 | 35.629 | 34.555 | 97 |
| 4 | 3.9961 | 16,652 | 37.983 | 37.091 | 48 |
| 8 | 7.9959 | Not recovered | 39.152 | Not recovered | Not recovered |

PSNR values are reported from the original run. Beauty's 8-bit sample peak is
255; confirm the actual decoder/reference configuration in the raw logs before
using these values in a cross-depth comparison. Do not compare them directly
with the 14-bit/scene-normalized PSNR columns in section 5.

| Target (Mbps) | Encoder median (ms) | Encoder maximum (ms) | Decoder median (ms) | Decoder maximum (ms) | Observed encoder maximum vs 66.67 ms |
|---|---:|---:|---:|---:|---|
| 2 | 41.99 | 48.30 | 51.411 | 70.772 | Within reference deadline |
| 4 | 48.06 | 57.61 | 56.120 | 78.574 | Within reference deadline |
| 8 | 64.34 | 79.06 | Not recovered | Not recovered | Exceeded reference deadline |

Increasing rate improved reported average quality from 35.63 to 39.15 dB while
increasing encoding time. The 2 and 4 Mbps maxima fit the two-frame reference
deadline on these 50 frames; this is not a full-SoW latency acceptance result.
All encoder medians exceed the 33.33 ms input interval at 30 fps. These timings
do not establish sustained 30 fps operation or bounded queues. Decoder timing is
reported separately; the SoW two-frame encoder limit is not a decoder limit.
P95/P99, deadline-miss counts, end-to-end transport latency and resource use were
not recovered.

### 9.4 LTIR — E-wavelet short-sequence results

| Target (Mbps) | Average PSNR (dB), as reported | Encoder maximum (ms) | Reliable decoded frames | Application metadata |
|---|---:|---:|---|---|
| 2 | 65.62 | 10.76 | 5/5 | 128 B/frame |
| 4 | 68.50 | 14.92 | 5/5 | 128 B/frame |
| 8 | 71.81 | 23.43 | 5/5 | 128 B/frame |

Reported rates were approximately the targets; exact actual bitrates, encoded
frame sizes, maximum errors, encoder medians and decoder timings need recovery.
Verify the PSNR peak and effective signal depth before comparing these values
with Beauty. All reported encoder maxima were below 66.67 ms, but five frames
do not establish sustained throughput or broad scene coverage.

**Excluded invalid result:** an earlier LTIR attempt produced zero-byte or empty
outputs. It is not a compression success and contributes no numeric pass values.
Keep it separate from the valid five-frame observations above during archive audit.

### 9.5 Beauty — D+ exact and near-lossless results

| Mode | Reported bitrate (Mbps) | Reconstruction | Encoder maximum (ms) | Maximum vs 66.67 ms |
|---|---:|---|---:|---|
| D+ exact / NEAR=0 | 221.7 | Reported bit-perfect; max error 0 | 161.7 | Exceeded |
| D+ NEAR=4 | 58.6 | Reported maximum error 4 DN | 66.8 | Slightly exceeded |

These are fidelity-controlled modes whose size is an output. They are not
2/4/8 Mbps budget passes. Exact reconstruction has zero MSE and infinite PSNR;
do not replace it with a finite display sentinel. Metadata accounting, detailed
timings, frame-level logs and artifact locations for these D+ runs remain to be
recovered. Preserve the tested source revision when reproducing them.

### 9.6 Where the video artifacts were written

The following are last recorded paths on test host/container `0fb8b27cea6b`;
their present existence has not been checked. They are outside the Git working
tree and are not repository-relative download links.

| Run | Artifact | Last recorded path |
|---|---|---|
| Beauty input | 50 luma frames | `~/bitrate2/data/real/beauty_y_50.u16` |
| Beauty 2 Mbps | Encoded stream | `/tmp/beauty.ewc` |
| Beauty 2 Mbps | Encoder per-frame metrics | `/tmp/beauty_enc.csv` |
| Beauty 2 Mbps | Decoder per-frame metrics | `/tmp/beauty_dec.csv` |
| Beauty 2 Mbps | Reconstructed samples | `/tmp/beauty_rec.u16` |
| Beauty 4 Mbps | Encoded stream | `/tmp/b4.ewc` |
| Beauty 4 Mbps | Reported metrics CSV | `/tmp/b4.csv` |
| Beauty 4 Mbps | Reconstruction path reported in discussion; verify file | `/tmp/b4.raw` |
| Beauty 8 Mbps | Encoded stream | `/tmp/b8.ewc` |
| Beauty 8 Mbps | Reported metrics CSV | `/tmp/b8.csv` |
| Beauty 8 Mbps | Reconstruction path reported in discussion; verify file | `/tmp/b8.raw` |

- `~/bitrate2/jetson_results/beauty50_2m_*/` was a proposed save destination;
  copying the video files there was not confirmed.
- `~/jetson_validation_final.tar.gz` (reported 13 MB) was created before the
  Beauty runs. Its existence does not establish that it contains these videos.
- Creation of the proposed combined `jetson_all_results_complete.tar.gz`,
  download of video artifacts to the Mac, and a push of the raw video results
  were not confirmed in the recovered record.
- This documentation update records the reported numbers. It does not upload
  the original CSVs, source videos or encoded/reconstructed streams.
- ShakeNDry was named as a planned test sequence; no completed ShakeNDry result
  or result path was recovered. Do not count it as tested.

### 9.7 What these results establish and what remains

The observations establish short offline E-wavelet video operating points on
visible luma and thermal data, and reported D+ exact/bounded-error modes on
Beauty. They do not complete the image/video SoW.

Before promoting this record to reproducible acceptance evidence:

1. Recover raw CSVs, terminal commands, source/encoded/reconstructed files and
   hashes; link them from this section using stable repository/archive paths.
   Do not fabricate per-frame CSVs from aggregate numbers.
2. Pin the tested commit, executable/build identity, exact hardware, clocks,
   power mode, threads, tile/GOP settings and timing boundaries.
3. Replay Beauty with serialized 128-byte application metadata, count complete
   frame/container bytes, and verify metadata round trip and sample metrics.
4. Measure longer sequences, encoder latency distribution and deadline misses,
   actual throughput, queue growth and measured memory/CPU/GPU use.
5. Extend to representative approximately 1920×1200 video, supported color and
   native 8/10/12/14/16-bit inputs; record unsupported formats explicitly.
6. Test file plus actual Ethernet/UDP delivery, independent PC decoding and a
   mid-stream rate change where implemented. Separate unsupported mechanisms
   from successful fixed-rate runs.
7. Apply the same source and measurement contract to every buildable architecture
   in `bitrate` and `bitrate2`; report build failures and missing paths explicitly.

For each subsequent entry, retain: experiment date/time and timezone, record
date, run ID, architecture/mode, source commit, platform/build, input manifest,
full command/configuration, metadata/byte accounting, per-frame artifact links,
aggregate quality/rate/timing, failures and the precise acceptance scope.

<!-- JETSON_VIDEO_RESULTS_2026_11_11 -->
## 10. Jetson video validation — 11 November 2026 (IST)

### 10.1 Date, platform and evidence scope

Recorded 11 November 2026, India Standard Time (UTC+05:30). Run IDs use UTC on 10 November. These are user-supplied completed-run summaries, not independently reproduced measurements by the documentation author. Original per-frame CSVs, command logs, streams and reconstructions remain on the Jetson at the paths below. The JSON copies in `docs/results/jetson_2026-11-11/` are the supplied aggregate reports, not regenerated per-frame data.

- Platform: NVIDIA Jetson Orin Nano Engineering Reference Developer Kit Super; Linux 5.15.148-tegra, aarch64; L4T R36 revision 4.7 reported in the first environment record.
- Recorded source revision for the first full-set and Beauty runs: `ab31fda985392dc6baa92b7a8f9759e727b54cea`. Initial working tree contained a modified `scripts/score_top3.sh` and untracked data/results. Do not describe the tree as clean.
- Initial executable SHA256: encoder `29c182de96e1974c020a26fd5480eb379988551b864b3d003ed22d53a5b7a9f2`; decoder `17a33162e3b8bdb21b8e2ddc714e073d2d03ed546b36ecf5ad1db9f2e34ce256`. These identify the initial run only; recover the FLIR run environment to establish its identities after the container changed.
- First container: `8bc983542a1a`; FLIR container: `cb1b0c61b75c`. Board model was checked in both. Power mode, clocks and thermal state are not established; `nvpmodel` and `jetson_clocks` were unavailable in the initial container.
- Build: CMake Release, GNU C++ 11.4.0 in the recorded initial build. Test configuration: two codec threads, 128×128 tiles, four tile wavelet levels, GOP 1, 30 fps evaluation reference.
- Wavelet commands serialized 128 ASCII metadata bytes via `--meta`. Complete frame bytes were checked against the selected byte budget. Metadata semantic round trip and network overhead were not independently verified.
- **2 Mbps is the primary questionnaire operating point. 4 and 8 Mbps are authorized supplementary rate/quality experiments, not additional questionnaire requirements.**
- Encoder deadline used in this evaluation: 2/30 seconds = 66.67 ms. Measured encode time does not establish live pipeline latency or sustained 30 fps.
- Source capture cadence is not established by the 30 fps evaluation reference. All runs were offline file replay.

### 10.2 FLIR thermal video: six clips, 900 source frames

Geometry: 640×512, 150 consecutive selected frames per clip, 14-bit codec profile with little-endian uint16 storage. All six transferred files matched the Mac-generated SHA256 list. File size and sample range ≤16383 were checked before staging; source provenance and sensor precision remain distinct from this range check. The prior source manifest records the original clip identifiers and selected frame intervals. No scaling, AGC, normalization or widening from 8-bit was introduced by this test setup.

All 18 cases met their per-frame byte budgets and had zero measured encoder deadline misses (900 frame encodes per rate). PSNR below is the arithmetic mean of decoder-reported per-frame dB values, peak 16383; it is not sequence PSNR computed from pooled MSE.

| Clip | Target Mbps | Actual Mbps @30fps | Maximum B/frame | Mean PSNR dB | Max sample error | Encode median / p95 / max ms | Deadline misses |
|---|---:|---:|---:|---:|---:|---|---:|
| 4FRnNp | 2 | 1.998379 | 8329 | 71.72 | 33 | 14.877 / 15.348 / 17.151 | 0/150 |
| 4FRnNp | 4 | 3.997949 | 16660 | 72.46 | 26 | 15.080 / 18.181 / 23.206 | 0/150 |
| 4FRnNp | 8 | 7.997718 | 33327 | 73.87 | 17 | 29.592 / 30.550 / 34.540 | 0/150 |
| 5RSrbW | 2 | 1.998424 | 8329 | 73.27 | 22 | 11.881 / 12.437 / 16.634 | 0/150 |
| 5RSrbW | 4 | 3.997882 | 16660 | 73.90 | 19 | 11.754 / 25.423 / 26.353 | 0/150 |
| 5RSrbW | 8 | 7.997840 | 33327 | 75.21 | 17 | 25.882 / 26.883 / 33.188 | 0/150 |
| 6tLtjd | 2 | 1.998171 | 8327 | 72.80 | 40 | 12.905 / 13.464 / 16.782 | 0/150 |
| 6tLtjd | 4 | 3.998072 | 16662 | 73.82 | 21 | 13.093 / 17.808 / 25.037 | 0/150 |
| 6tLtjd | 8 | 7.997766 | 33327 | 75.21 | 18 | 26.685 / 28.025 / 34.811 | 0/150 |
| SCiKdG | 2 | 1.998370 | 8329 | 70.82 | 48 | 8.830 / 16.181 / 18.563 | 0/150 |
| SCiKdG | 4 | 3.997986 | 16660 | 71.94 | 33 | 16.685 / 17.588 / 22.643 | 0/150 |
| SCiKdG | 8 | 7.997680 | 33327 | 73.37 | 21 | 31.097 / 32.419 / 43.519 | 0/150 |
| ePoikf | 2 | 1.998298 | 8329 | 72.67 | 35 | 13.069 / 13.744 / 18.389 | 0/150 |
| ePoikf | 4 | 3.997963 | 16662 | 73.69 | 28 | 13.370 / 14.207 / 19.945 | 0/150 |
| ePoikf | 8 | 7.997805 | 33327 | 75.06 | 17 | 27.096 / 28.262 / 35.570 | 0/150 |
| vbrSzr | 2 | 1.998306 | 8329 | 71.87 | 33 | 14.681 / 18.650 / 29.361 | 0/150 |
| vbrSzr | 4 | 3.997966 | 16660 | 72.59 | 26 | 14.835 / 15.467 / 18.890 | 0/150 |
| vbrSzr | 8 | 7.997741 | 33327 | 73.98 | 17 | 29.341 / 30.221 / 36.968 | 0/150 |

At 2 Mbps the largest frame was 8,329 bytes against an 8,333-byte budget; worst measured encoder time was 29.361 ms, below one 33.33 ms reference frame interval. Worst absolute sample error was 48 counts. These are observations on these clips, not universal error bounds.

**Quality interpretation:** full-scale 14-bit PSNR is 70.82–73.27 dB at 2 Mbps. The source manifest reports narrow clip ranges (6689–9994 across the selected set, with individual clip spans from 255 to 2876 counts). A large common offset does not add image detail. Full-scale PSNR can obscure errors relative to local thermal contrast; do not compare it directly to earlier scene-normalized ~40–41 dB results. Preserve absolute errors, inspect fixed-window original/reconstruction crops, and independently verify metrics before making quality acceptance claims.

### 10.3 Beauty: high-resolution visible luma at 2 Mbps

Input: `data/real/beauty_y_50.u16`, 50 frames, 1920×1080, 8-bit luma stored in little-endian uint16. Input size and range ≤255 were checked, and encoder bit depth was 8. This does not demonstrate RGB/YUV colour-plane support.

Input SHA256: `4cbe16d59503b3dedb281c1413cf515b8cec5bb9b9657c89185754a200d6da61`.

| Frames | Actual Mbps @30fps | Maximum B/frame | Mean PSNR (peak 255) | Max sample error | Encode median / p95 / max ms | Deadline misses |
|---:|---:|---:|---:|---:|---|---:|
| 50 | 1.9929552 | 8321 | 35.61114 dB | 97 | 42.3015 / 43.165 / 62.485 | 0/50 |

Frame count, reconstructed size, decoder sequence order, reliability/no concealment and byte accounting checks passed. Both budget and measured two-frame encoder deadline passed; subjective/task quality acceptance is not established. The encoder median exceeds 33.33 ms, so meeting the two-frame deadline must not be described as proof of sustained 30 fps. Beauty 4/8 Mbps on this Jetson remain to be measured; earlier runs in section 9 do not replace this configuration.

### 10.4 Bracken, Marathon and CRVD: all available frames

This full-set run extends the initial ten-frame thermal smoke tests. Bracken and Marathon are supplied as 16-bit PNG-derived data, tested with a 16-bit codec profile; effective sensor precision remains unverified. CRVD is 12-bit noisy Bayer scene 6, ISO1600, noisy0, with only seven capture positions, not a long continuous-video acceptance sample. Bayer sample-domain fidelity is distinct from reconstructed colour-image fidelity.

| Input | Mode | Frames | Actual Mbps @30fps | Maximum B/frame | Mean PSNR dB | Max sample error | Encode median / max ms | Deadline misses |
|---|---|---:|---:|---:|---:|---:|---|---:|
| Bracken | wavelet_2m | 151 | 1.997385 | 8325 | 50.29 | 10854 | 22.265 / 29.306 | 0/151 |
| Bracken | wavelet_4m | 151 | 3.996675 | 16658 | 53.88 | 4287 | 26.805 / 55.370 | 0/151 |
| Bracken | wavelet_8m | 151 | 7.996382 | 33323 | 57.89 | 1170 | 40.600 / 53.405 | 0/151 |
| Bracken | dplus_exact | 151 | 261.338303 | 1091919 | infinite | 0 | 122.879 / 132.102 | 151/151 |
| Bracken | dplus_near4 | 151 | 161.061883 | 674142 | 88.09 | 4 | 99.097 / 110.606 | 151/151 |
| Marathon | wavelet_2m | 1000 | 1.998362 | 8329 | 54.96 | 2621 | 15.102 / 27.813 | 0/1000 |
| Marathon | wavelet_4m | 1000 | 3.997753 | 16660 | 58.39 | 1010 | 18.136 / 33.511 | 0/1000 |
| Marathon | wavelet_8m | 1000 | 7.997706 | 33327 | 61.26 | 659 | 26.627 / 56.249 | 0/1000 |
| Marathon | dplus_exact | 1000 | 130.627660 | 545162 | infinite | 0 | 61.595 / 79.798 | 16/1000 |
| Marathon | dplus_near4 | 1000 | 80.479944 | 336161 | 88.09 | 4 | 49.590 / 65.868 | 0/1000 |
| CRVD scene6 | wavelet_2m | 7 | 1.996286 | 8319 | 19.40 | 3367 | 45.154 / 54.897 | 0/7 |
| CRVD scene6 | wavelet_4m | 7 | 3.996137 | 16652 | 20.67 | 3367 | 59.837 / 68.491 | 1/7 |
| CRVD scene6 | wavelet_8m | 7 | 7.995531 | 33317 | 22.30 | 2881 | 59.770 / 68.009 | 1/7 |
| CRVD scene6 | dplus_exact | 7 | 648.195017 | 2723698 | infinite | 0 | 264.619 / 275.192 | 7/7 |
| CRVD scene6 | dplus_near4 | 7 | 452.667326 | 1911958 | 64.06 | 4 | 211.649 / 219.608 | 7/7 |

At the primary 2 Mbps point, all 1,158 frames met their budgets and measured encoder deadlines. Bracken/Marathon PSNR uses peak 65535; CRVD peak is 4095. CRVD 2 Mbps quality is unresolved (19.40 dB, max error 3367), although rate and encoder deadline passed. At supplementary 4/8 Mbps CRVD had one deadline miss in seven frames at each rate. Those misses do not invalidate the separate 2 Mbps latency observation.

**D+ correctness versus operating-point acceptance:** exact mode reconstructed every tested sample exactly (max error 0; lossless PSNR infinite); NEAR=4 respected max error 4 on all three inputs. Neither mode met 2 Mbps on these inputs, and they were run without a rate budget. Bracken missed every encoder deadline in both modes; Marathon exact missed 16/1000, Marathon NEAR=4 missed 0/1000; CRVD missed 7/7 in both. These results demonstrate reconstruction guarantees on the supplied samples, not a simultaneous lossless/near-lossless 2 Mbps solution. Their Mbps values are projections at 30 fps, not measured delivered throughput.

### 10.5 Result locations and preservation

Paths are on the rented Jetson filesystem, not GitHub downloads. Preserve them before the rental/container is removed. Do not assume a container restart retains them.

| Run | UTC start | IST start | Directory relative to Jetson home |
|---|---|---|---|
| Full Bracken/Marathon/CRVD | 2026-11-10 18:43:39Z | 2026-11-11 00:13:39 | `jetson_video_inputs_20261110/results_20261110T184339Z/` |
| Beauty 2 Mbps | 2026-11-10 19:00:02Z | 2026-11-11 00:30:02 | `bitrate2/results/beauty_2m_20261110T190002Z/` |
| FLIR 2/4/8 Mbps | 2026-11-10 19:36:42Z | 2026-11-11 01:06:42 | `bitrate2/data/flir/jetson_test_20261110T193640Z/results_20261110T193642Z/` |

The standard runner saves `summary.json`, `environment.json`, `input_manifest.json`, `commands.json`, encoder/decoder CSVs and logs, reference/reconstruction `.u16` files and `.ewc` streams. The standalone Beauty block saved `summary.json`, `commands.json`, `enc.csv`, `dec.csv`, `encode.log`, `decode.log`, `beauty.ewc`, and `beauty_rec.u16`; it did not create a separate environment JSON. The provided full-set and FLIR aggregate JSONs are checked into `docs/results/jetson_2026-11-11/`. Original per-frame artifacts have not been uploaded by this documentation update.

To create a compact metrics/log archive on Jetson (raw samples and streams excluded):

```bash
tar --exclude='*.u16' --exclude='*.ewc' \
  -czf "$HOME/jetson_video_metrics_20261111.tar.gz" -C "$HOME" \
  jetson_video_inputs_20261110/results_20261110T184339Z \
  bitrate2/results/beauty_2m_20261110T190002Z \
  bitrate2/data/flir/jetson_test_20261110T193640Z/results_20261110T193642Z
```

This archive is not a backup of source videos, encoded streams or reconstructions. Save those separately, subject to source redistribution rights; do not commit large datasets into ordinary Git history.

### 10.6 Acceptance position and next work

At 2 Mbps, the completed full runs cover 900 FLIR + 151 Bracken + 1000 Marathon + 7 CRVD + 50 Beauty = **2,108 source frames**, with zero measured two-frame encoder deadline misses. FLIR/Bracken/Marathon also completed supplementary 4/8 Mbps tests; CRVD did so with the deadline misses above. Earlier smoke tests overlap these samples and must not be counted as new source frames.

1. Preserve and independently review original CSVs, environment records and visual reconstructions; verify metadata content after decode and clarify its acceptance schema.
2. Address noisy high-resolution Bayer quality at 2 Mbps and inspect original/reconstruction crops with identical display mapping. Do not infer a single cause without controlled tests.
3. Run Beauty at supplementary 4/8 Mbps under the same metadata/configuration contract.
4. Extend native-depth and scene coverage: 8/10/12/14/16-bit monochrome/colour and representative approximately 1920×1200 visible/IR. Mark unsupported paths and source-precision uncertainties explicitly.
5. Measure live throughput, queues, sustained latency, resource use and known power/clock conditions; test actual Ethernet/UDP delivery and independent Linux/Windows PC decoding. A loopback is preliminary evidence only.
6. Verify bandwidth-change behavior where implemented and repeat the common contract across buildable architectures. Two-layer mode is optional unless a requirement explicitly calls for it.
7. Maintain requirement-level pass/partial/not-tested evidence against the SoW and questionnaire. These milestones do not justify a precise overall MVP percentage without a defined, agreed scoring method.
