# Experiment catalogue

**11 November 2026, IST.** An experiment is indexed by source, profile, configuration and evidence, not merely by architecture name. Algorithm explanations are in [ARCHITECTURES_EXPLAINED.md](ARCHITECTURES_EXPLAINED.md); the [current project table](../README.md#what-is-done-and-what-remains) owns the headline status.

| Experiment | Architecture / idea | Input / mode | Established result | Evidence | Next question |
|---|---|---|---|---|---|
| FLIR Jetson sweep | E tiled intra wavelet + embedded coder | 6 × 150 frames, 640×512, 14-bit; 2/4/8 Mbps | Budgets and measured encoder deadlines passed; 2 Mbps worst encode 29.361 ms, max sample error 48 | [Dated report](JETSON_VIDEO_RESULTS_2026-11-11.md#102-flir-thermal-video-six-clips-900-source-frames), [aggregate](results/jetson_2026-11-11/flir_summary_reported.json) | Visual/scene-contrast review, native high-resolution IR and live integration |
| Beauty current Jetson run | E tiled intra | 50 frames, 1920×1080, 8-bit luma, 2 Mbps, 128 metadata bytes | 1.9929552 Mbps, 35.61114 dB peak 255, max encode 62.485 ms, 0 deadline misses | [Dated report](JETSON_VIDEO_RESULTS_2026-11-11.md#103-beauty-high-resolution-visible-luma-at-2-mbps) | Current 4/8 rates, colour, longer cadence test |
| Bracken/Marathon full runs | E tiled intra | 151 × 1024² / 1000 × 1024×512, stored-16-bit | At 2 Mbps, all budgets/deadlines passed; supplementary 4/8 passed too | [Full aggregate](results/jetson_2026-11-11/full_sequence_summary_reported.json) | Source precision audit; sustained queues |
| CRVD scene6 | E tiled intra on Bayer samples | Seven 1920×1080 12-bit positions; 2/4/8 Mbps | 2 Mbps rate/deadline passed but 19.40 dB; 4/8 each missed 1/7 deadlines | [Dated report](JETSON_VIDEO_RESULTS_2026-11-11.md#104-bracken-marathon-and-crvd-all-available-frames) | Diagnose fidelity with controlled sample/visual comparisons |
| D+ exact / NEAR=4 Jetson | Custom predictor + range coder | Same Bracken/Marathon/CRVD sources | Exact/error bounds passed; rate and many latency limits do not | [Full aggregate](results/jetson_2026-11-11/full_sequence_summary_reported.json) | Agreed exact-mode sample/rate/latency contract |
| Historical PoC robustness | Embedded wavelet, CRC, grouping/concealment | Stored-bitstream bit errors, deletions, zero bursts | Reported decode synchronization and reliable/damaged/lost behavior under stated cases | [PoC report](POC_REPORT.md), [eval/results](../eval/results/) | Reproduce current selected profile; actual UDP receiver tests |
| Earlier image A/B/C/D/F | Reversible wavelet, DCT, predictors, JPEG-LS reference, block Rice | Still-frame and synthetic/real-depth experiments | Useful exactness, rate-quality and timing comparisons; heterogeneous conditions | [Architecture history](ARCHITECTURES_EXPLAINED.md), [status](mvp/STATUS.md#1-images-achieved-and-evidenced) | Bounded common-video replay for buildable candidates |
| Two-layer | Lossy base + bounded-error residual | Earlier host experiments and search refinement | Experimental; simultaneous rate/error/time closure absent | [E design](ARCHITECTURE.md), [arch review](../eval/arch_review/) | Only refine if a controlled run shows an advantage |
| Other coding / network variants | Temporal prediction, HTJ2K references, FEC variants | Historical evaluation settings | Existing code/data, not automatic accepted support | [eval/exp](../eval/exp/), [network](NETWORK.md), [evaluation](EVALUATION.md) | Keep external-reference and compliant configurations distinct |

## Evidence rules

| Record | Rule |
|---|---|
| Quality | Declare peak, source precision, transformation and averaging. Compare matched originals/reconstructions; do not pool different PSNR definitions |
| Rate | Count complete frame bytes including serialized metadata; separate codec rate from UDP/IP/FEC overhead |
| Latency | Record agreed frame availability/output boundary, fps, threads, power/clocks, distributions/max/misses |
| Correctness | Verify frame count/order, exactness/NEAR bounds, reliability and concealment independently of rate/latency |
| Provenance | Pin source/build/binary hashes and dirty state; different container IDs do not prove identical operating conditions |
| Pass state | `PASS_RECORDED_CHECKS` is not a full acceptance decision; retain latency/quality failures explicitly |
