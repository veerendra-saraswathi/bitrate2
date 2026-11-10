
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
