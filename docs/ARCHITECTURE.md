# ewc architecture — final shortlist (2026-11-06)

Three coding paths, one container, one decoder. Each path is the measured winner for one
part of the Scope of Work. Everything is self-contained C++17 (no CharLS, no OpenJPH, no
GStreamer in the codec).

| SoW need | Path | Selected by |
|---|---|---|
| ~2 Mbps fixed rate, best image quality | Tiled 9/7 wavelet + SPIHT + range coder, with internal precision shift | `--poc` (default) |
| Fixed rate **and** a guaranteed max error | Two-layer: wavelet base + D+ residual, split chosen per frame (pure D+ if smaller) | `tools/ewc_layered` (experimental) |
| Lossless / near-lossless ("perfect reconstruction") | **D+**: JPEG-LS modelling + adaptive range coder + automatic stripe height | `--lossless [--near N]` |

## 1. D+ — the improved Architecture D

Architecture D in bitrate-main was JPEG-LS through the CharLS library (external, not
allowed by the SoW). ewc already had its own LOCO-I coder with the same modelling (MED
predictor, 365 gradient contexts, bias cancellation, run mode). D+ keeps that modelling and
takes two things from the ewc wavelet path:

1. **Adaptive binary range coder instead of Golomb codes** (`loco.cpp`, `AC = true`).
   Prediction errors are binarised Golomb-style (unary prefix + k low bits); prefix bits
   and the top low bit are adaptive per Golomb parameter k; run bits adaptive per run index.
   Probabilities warm up fast (rate 1/2, 1/4, ... settling at 1/64) because every stripe
   starts from scratch. Golomb codes cannot spend under 1 bit per sample and adapt coarsely;
   that is where JPEG-LS loses on noisy data and at higher NEAR.
2. **Automatic stripe height.** Independent stripes keep damage local but each re-learns
   its statistics. Below ~3 KB per stripe that costs more than it saves, so the encoder
   doubles the stripe height until stripes average >= 3 KB (stored in the header; decoders
   need no change).

Header byte 38 = 1 marks the range-coder back-end; 0 (older streams, or `--golomb`) is the
JPEG-LS Golomb back-end. Both decode.

Measured against CharLS (bitrate-main's Architecture D, numbers reproduced byte-exact),
9 data sets (KAIST 8-bit thermal and visible; 7 FLIR ADAS v2 14-bit clips, 5 frames each),
NEAR = 0, 1, 2, 4, 8, 16, 32, error bound verified on every frame
(`eval/arch_review/dplus_eval.py`, `eval/results/exp/dplus_results.json`):

- D+ smaller on **62 of 63** points; median **-9.1%**; range -27.3% .. +0.1%
  (the +0.1% is 8-bit visible lossless).
- 14-bit lossless -1.0 .. -1.9%; NEAR 1-2: -1.3 .. -3.6%; NEAR 4: -5.0 .. -9.3%;
  NEAR 8-32: -8.2 .. -17.2%. 8-bit thermal: -11.5 .. -27.3%.
- Old ewc Golomb back-end was +1.4 .. +65% *larger* than CharLS on the same points.
- Robustness (stored-bitstream losses, ePoikf, NEAR 0 and 4): reliable / lost / PSNR equal
  or better than the Golomb back-end (`eval/results/exp/dplus_loss.jsonl`).
- Corrupt-input safety: the Golomb parameter search is capped (k <= 24); before this an
  optimised build could hang on garbage. Release-build fuzz: 360 corrupted streams, 0 crashes,
  0 timeouts. Valid streams unchanged.
- Cost: speed. 14-bit lossless 640x512 encodes in ~22-26 ms on 2 threads (Golomb ~13-15 ms,
  CharLS ~7 ms on 1 thread). Still under one frame period at 30 fps.

## 2. Wavelet path — precision fix

The integer 9/7 lifting rounded every step at sample precision: a ~1 DN noise floor. Lossy
quality stalled at 43-47 dB on low-contrast data and max error never fell below 7.
Samples are now scaled by 2^(18 - bit_depth) (max 2^8) before the transform; band weights
absorb the scale (header byte 38 for wavelet streams; 0 = old streams).

| Data, 2 Mbps | Before | After |
|---|---|---|
| KAIST thermal 8-bit | 43.3 dB, max err 9 | 51.9 dB, max err 4 |
| FLIR 14-bit, `--poc`, 7 clips | 40.21 dB PSNR (scene range) | 40.62 dB (+0.41 on every clip) |

At D's own operating points (same bytes) the wavelet path beats JPEG-LS PSNR on thermal by
5.7-11 dB, but its max error is not bounded.

## 3. Two-layer bounded-error mode

Base = tiled wavelet at B1 bytes; enhancement = D+ (NEAR = k) on the residual, centred in a
12-bit range. Per frame the encoder tries no base (pure D+) and three splits around the last
best one, and keeps the smallest. |error| <= k is guaranteed when both layers arrive; the
base alone is still a valid image.

At 2 Mbps on 6 FLIR video clips (D+ residual coder): guaranteed max error 10-20 DN
(wavelet alone: 19-39 DN, unbounded) for -0.8 .. -2.1 dB PSNR, 5.9-7.5 KB per frame;
encode 39-55 ms (< 2 frames, but close: the split search runs up to 4 trial encodes),
decode 4-6 ms. Stills do not fit. Results: `eval/arch_review/layered_results_dplus.json`.
Same-max-error size vs CharLS: thermal -22/-42/-51% (NEAR 1/2/4), FLIR NEAR 16 -45%.

## Rejected, with evidence

| Idea | Result |
|---|---|
| Replace coder with HTJ2K (OpenJPH) | +0.07..+0.40 dB inside robust tiled framing, -0.90 on stills; bare stream 35% silently wrong under bit errors; external library |
| GStreamer transport | no compression; ULPFEC needs 20-50 ms jitter buffer to match ewc RS FEC |
| Temporal prediction (P frames) at 2 Mbps | +0.3 dB, 4x encode time, error propagation; FLIR frame-to-frame difference ~ neighbour difference |
| 5/3 instead of 9/7 for lossy | equal on 14-bit FLIR once the precision floor was found; the floor was the real cause |
