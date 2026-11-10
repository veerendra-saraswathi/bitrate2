# Image and video MVP: achievements and current limits

**Evidence cutoff: 11 November 2026, IST. Status: working codec and Jetson offline video candidate; full MVP acceptance is incomplete.**

This page covers `bitrate` architecture experiments and `bitrate2` / ewc. It separates earlier reported image measurements from the completed November Jetson video runs. No overall percentage is asserted: an implementation estimate is not a count of satisfied mandatory acceptance gates.

| Question | Answer |
|---|---|
| Has the image PoC produced useful results? | Yes: real VIS/IR frame coding, exact/near-lossless references, high-depth cases, rate/error/latency comparisons and stored-bitstream corruption tests are reported. |
| Has video now run on Jetson? | Yes: offline encode/decode of FLIR, Bracken, Marathon, Beauty and CRVD. |
| What passed at the primary 2 Mbps point? | All 2,108 source frames met recorded complete-frame byte budgets and measured encoder deadlines at a 30 fps evaluation reference. |
| Is reconstruction quality accepted? | No agreed acceptance threshold is recorded. FLIR and Beauty are useful operating points; CRVD noisy Bayer quality is unresolved. |
| What does 4/8 Mbps mean? | Authorized supplementary experiments; 2 Mbps remains the primary questionnaire point. These rates are not invented contractual requirements. |
| Is sustained live video accepted? | No. Offline replay, even faster than real time on some inputs, is not live queue/transport acceptance. |
| Is full SoW acceptance complete? | No: format coverage, target-resolution coverage, quality agreement, live integration, metadata, bandwidth behavior, PC decoding, dependency audit and handover evidence remain. |

## 1. Images: achieved and evidenced

The following older measurements are historical reported observations, not a fresh common-corpus comparison. Preserve their exact source/configuration identities before ranking architectures.

| Architecture / feature | Achieved | Evidence / example | Remaining limitation |
|---|---|---|---|
| A: reversible 5/3 wavelet | Exact frame reconstruction and native-container high-depth tests reported; host/Jetson timings exist | 640×512 thermal QP1: 59.46 Mbps; max error 0 | High rate; historical bitrate discrepancy needs reconciliation; common video replay pending |
| B: block DCT | Lossy thermal/VIS frame results and tile CRC experiments | Thermal QP32, 32×32 CRC: about 7.02 Mbps, 43.49 dB, about 61 ms host encode | Earlier Jetson encode about 120 ms; QP1 was not bit-exact; not established at 2 Mbps |
| C: predictive residual | Exact mode and lossy predictor experiments | Thermal QP32 left predictor: about 24 Mbps / 31.5 dB | Poor rate-quality trade-off; deferred development, retain comparison evidence |
| D: CharLS JPEG-LS reference | Lossless and bounded-error image reference, host/Jetson measurements | Thermal NEAR0: 9.86 Mbps, exact; NEAR4: 2.08 Mbps / 41.0 dB, later Jetson median about 1.86 ms | 2.08 is not ≤2 Mbps. Static linking does not resolve the full no-external-library wording |
| E: tiled embedded wavelet | Fixed-budget real IR/VIS frame coding; metadata serialization; stored-corruption behavior | Prior FLIR PoC report: scene-normalized video PSNR roughly 34.9–42.1 dB | Historical settings differ from current tiled sweep; no direct PSNR comparison across peak definitions |
| E D+: custom predictive | Exact and NEAR modes on real frames; separate from external CharLS reference | Current Jetson sequences reproduce all samples exactly or within NEAR=4 | Correctness does not imply rate or latency acceptance |
| E two-layer | Base/residual experiments reported | See existing architecture/evaluation reports | No fresh equivalent Jetson video acceptance; not mandatory merely because it exists |
| F: 5/3 + Rice | Corrected lossless round trip and experimental lossy diagnostics | V5 lossless thermal: 215,548 bytes/frame; max error 0 reported | Did not meet 2 Mbps; deferred optimization |
| Native-depth still candidates | Raw 12-bit Canon/Blackmagic DNG metadata inspected | Canon S100 true raw SubIFD: 12-bit, WhiteLevel 4095, no linearization table | Metadata inspection is not a completed codec test or a video sequence; Blackmagic linearization must be respected |
| Robustness | Stored-file corruption, CRC detection and concealment reported | [Earlier PoC report](../POC_REPORT.md): 1% deleted packets left about 94.1% frames reliable | Different configuration and environment; not actual Ethernet acceptance in the latest Jetson runs |

**Implementation distinction:** the current repository describes the principal E lossy path as CDF 9/7 + embedded SPIHT/range coding. Reversible 5/3 belongs to other paths, including embedded-lossless experiments. Do not label every E profile as the same transform or assume a cited paper proves conformance.

## 2. Video: completed Jetson observations

Device: NVIDIA Jetson Orin Nano Engineering Reference Developer Kit Super. The first environment reports L4T R36.4.7 / Linux 5.15.148-tegra. Two threads; 30 fps evaluation reference; 128 ASCII metadata bytes; tiled intra path, tile 128, four tile levels, GOP 1. The FLIR session used a different container ID; retain each run's own environment and binary hashes.

| Input | Format actually tested | Frames | Actual Mbps at 30 fps | Mean per-frame PSNR | Encode median / max ms | Deadline misses >66.67 ms | Budget |
|---|---|---:|---:|---:|---|---:|---|
| FLIR, 6 clips | 640×512, 14-bit profile, uint16 LE | 900 | 1.99817–1.99842 | 70.82–73.27 dB, peak 16383 | Clip medians 8.83–14.88; worst 29.36 | 0/900 | Pass, maximum 8,329 B |
| Bracken | 1024×1024, stored-16-bit mono | 151 | 1.99738 | 50.29 dB, peak 65535 | 22.27 / 29.31 | 0/151 | Pass, maximum 8,325 B |
| Marathon | 1024×512, stored-16-bit mono | 1,000 | 1.99836 | 54.96 dB, peak 65535 | 15.10 / 27.81 | 0/1,000 | Pass, maximum 8,329 B |
| Beauty | 1920×1080, 8-bit luma in uint16 | 50 | 1.99296 | 35.61 dB, peak 255 | 42.30 / 62.49 | 0/50 | Pass, maximum 8,321 B |
| CRVD scene6 ISO1600 noisy0 | 1920×1080, 12-bit Bayer sample plane | 7 | 1.99629 | 19.40 dB, peak 4095 | 45.15 / 54.90 | 0/7 | Pass, maximum 8,319 B |
| **Total, primary rate** | **Distinct source frames within these runs** | **2,108** | **≤2 Mbps** | **Different peak definitions; do not pool dB** | **Measured encoder timing only** | **0/2,108** | **All ≤8,333 B** |

PSNR is a mean of per-frame values, not pooled-MSE sequence PSNR. Effective sensor precision for Bracken/Marathon remains unverified. FLIR clips have narrow signal ranges; full-14-bit PSNR must be accompanied by absolute error, matched-window views and scene-contrast assessment. CRVD sample-plane PSNR does not establish colour quality. Source cadence is not proved by the 30 fps evaluation setting. Input acquisition, disk/network I/O and queues are not established by the codec timer.

| Supplementary video evidence | Result | Meaning |
|---|---|---|
| FLIR 4/8 Mbps | All 18 rate/clip cases including 2 Mbps passed budgets; no measured encoder deadline misses | Useful rate-quality-latency envelope on these six clips |
| Bracken/Marathon 4/8 Mbps | Budgets and measured two-frame deadlines passed | Not proof of live throughput |
| CRVD 4/8 Mbps | Budgets passed; 1/7 encoder deadline misses at each rate | Exploratory limitations; separate from its 2 Mbps pass |
| Beauty 4/8 Mbps | Earlier reported runs exist; latest metadata-inclusive Jetson configuration tested only at 2 Mbps | Current supplementary sweeps remain pending |
| D+ exact | Max error 0 on Bracken 151, Marathon 1000, CRVD 7 | Correctness passed; respective rates 261.34 / 130.63 / 648.20 Mbps; deadline misses 151 / 16 / 7 |
| D+ NEAR=4 | Max error 4 on all three | Rates 161.06 / 80.48 / 452.67 Mbps; deadline misses 151 / 0 / 7 |

D+ measurements use their own unrestricted-rate modes. Do not describe them as lossless 2 Mbps. Perfect reconstruction on arbitrary high-entropy inputs cannot be guaranteed under an arbitrary fixed small byte budget; the accepted sample set and mode/rate relationship must be settled explicitly.

## 3. What is not yet achieved

| Gap | Practical implication | Closure reference |
|---|---|---|
| Agreed acceptance samples, frame rates, quality and bitrate envelope | No final acceptance decision can be made solely from current metrics | A01/A04/A05/A07 in [acceptance matrix](ACCEPTANCE.md) |
| Approximately 1920×1200 VIS/IR and full bit-depth/colour support | Existing 640×512 IR and 1080p luma/Bayer do not establish the full format range | A02/A03 |
| Quality on noisy Bayer | 19.40 dB at 2 Mbps needs investigation and visual review | A05 |
| Metadata content round trip | Serialized allowance is not semantic verification | A08 |
| Real UDP/Ethernet, bandwidth selection/change behavior, live queues | Integration and streaming acceptance remain open | A09/A10/A13 |
| Independent PC decoding and offline/dependency audit | Jetson-local decode is not a designated PC acceptance run | A11/A12 |
| Resources, package and specifications | No quantified power/resource footprint or complete handover claim | A14/A15 |

See [acceptance gates](ACCEPTANCE.md), [execution plan](PLAN.md), [data coverage](DATA_COVERAGE.md), [evidence index](EVIDENCE.md), and [detailed dated Jetson report](../JETSON_VIDEO_RESULTS_2026-11-11.md).
