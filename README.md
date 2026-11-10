# Bitrate2 — Image and Video Compression

C++ experiments for low-bitrate visible and infrared compression on Jetson. **2 Mbps is the primary test point; 4/8 Mbps are supplementary.** Status: 11 November 2026 (IST).

## Results and remaining work

Image results and video results are listed separately in the same table. “Passed” applies to the stated checks and inputs, not full MVP acceptance.

| Type | Experiment | Input / coverage | Result achieved | What remains |
|---|---|---|---|---|
| **Images** | E wavelet | Real visible/IR still frames | Fixed 2 Mbps frame budgets demonstrated; earlier quality/error and reconstruction comparisons recorded | Full format coverage and agreed quality acceptance |
| **Images** | Exact / near-lossless | A, D and custom D+ frame experiments | Exact reconstruction and bounded-error modes demonstrated on tested samples | Acceptance sample validation; exactness does not imply 2 Mbps |
| **Images** | Other architectures | B DCT, C prediction, F wavelet/Rice; hybrid experiments | Algorithms compared; rate, error and latency limitations documented | Fair common-corpus replay of buildable paths |
| **Video** | FLIR thermal | 6 clips, **900 frames**, 640×512, 14-bit | At 2 Mbps: **70.82–73.27 dB**, worst encode **29.36 ms**, no budget/deadline misses; 4/8 Mbps also tested | Visual/scene-contrast review; native high-resolution IR |
| **Video** | Bracken | **151 frames**, 1024×1024, stored 16-bit | At 2 Mbps: **50.29 dB**, worst encode **29.31 ms**, no budget/deadline misses; 4/8 also tested | Verify effective sensor precision and live streaming |
| **Video** | Marathon | **1,000 frames**, 1024×512, stored 16-bit | At 2 Mbps: **54.96 dB**, worst encode **27.81 ms**, no budget/deadline misses; 4/8 also tested | Verify effective sensor precision and live streaming |
| **Video** | Beauty visible luma | **50 frames**, 1920×1080, 8-bit luma | At 2 Mbps: **35.61 dB**, worst encode **62.49 ms**, no budget/deadline misses | Colour, longer runs and current Jetson 4/8 Mbps replay |
| **Video** | CRVD noisy Bayer | **7 positions**, 1920×1080, 12-bit | At 2 Mbps: **19.40 dB**, worst encode **54.90 ms**; budget/deadline passed | **Quality unresolved**; 4/8 Mbps each had one deadline miss |
| **Video** | D+ exact / NEAR=4 | Bracken + Marathon + CRVD, **1,158 frames** | Exact max error **0**; NEAR=4 max error **4** | Rates **80–648 Mbps** across these modes; multiple latency failures |

**Primary-rate total:** 2,108 source frames at 2 Mbps, all ≤8,333 bytes/frame including the configured 128-byte metadata, with zero measured encoder deadline misses at a 30 fps reference (66.67 ms limit).

These are offline runs. They do not establish sustained live 30 fps or Ethernet acceptance. PSNR uses different declared peaks: 16383 for FLIR, 65535 for Bracken/Marathon, 255 for Beauty and 4095 for CRVD. FLIR has narrow thermal contrast; full-scale PSNR should be read alongside absolute errors and visual review. Image observations above are historical reported results; video timings are the dated Jetson measurements.

## Read more

[Experiments](docs/EXPERIMENTS.md) · [Algorithms and architecture](docs/ARCHITECTURES_EXPLAINED.md) · [Detailed Jetson results](docs/JETSON_VIDEO_RESULTS_2026-11-11.md) · [Full MVP acceptance](docs/mvp/ACCEPTANCE.md) · [Plan](docs/mvp/PLAN.md) · [All documentation](docs/README.md)

**Full MVP remains open:** mono/colour and native-depth coverage; representative approximately 1920×1200 VIS/IR; agreed quality criteria; metadata round trip; UDP/Ethernet and bandwidth behavior; live queues; independent PC decoding; dependency/resource audits and release handover.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

See the [data guide](data/README.md), [runner instructions](scripts/JETSON_DATA_KIT.md), [bitstream format](docs/FORMAT.md) and [network interface](docs/NETWORK.md). The current input interface uses uint16 little-endian sample planes; colour support is not established by luma/Bayer tests.

`main` is the landing page and reviewed baseline. Experiment branches remain available for history; the [branch guide](docs/BRANCHES.md) records their roles. The codec/API name is **ewc**.
