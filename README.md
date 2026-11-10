# Bitrate2 — image and video compression experiments

C++17 encoder/decoder experiments for visible and infrared imagery. The current ewc candidate combines embedded wavelet coding with separate exact and near-lossless modes. This repository records algorithms, reproducible experiments, measured operating limits and work remaining against the full Scope of Work.

**Status: 11 November 2026 (IST).** Working Jetson offline-video candidate; full MVP acceptance remains open. **2 Mbps is the primary questionnaire point; 4/8 Mbps are supplementary tests.**

## Read the project

| Read | What you will find |
|---|---|
| [Experiments and results](docs/EXPERIMENTS.md) | Completed image/video experiments, settings, results, failures and source artifacts |
| [Algorithms and architecture](docs/ARCHITECTURES_EXPLAINED.md) | A–F experiment history and explanations; [current E implementation](docs/ARCHITECTURE.md) |
| [Full MVP acceptance](docs/mvp/ACCEPTANCE.md) | SoW traceability, acceptance gates and unresolved decisions |
| [Plan to close the gaps](docs/mvp/PLAN.md) | Priorities, dates, dependencies and exit evidence |
| [Documentation index](docs/README.md) | Specifications, APIs, data, evaluation and historical reports |
| [Repository and branch guide](docs/REPOSITORY_LAYOUT.md) | Folder roles, data policy and the Mac `ombitrate` reconciliation |

## What is done and what remains

This is the project status table. Bounded observations are not full-scope acceptance. Use the linked evidence for configuration and timing limits.

| Area | Achieved / measured | Remaining for full acceptance |
|---|---|---|
| Images | VIS/IR frame experiments across A–F; exact/NEAR references and stored-corruption tests | Current common-corpus replay, actual acceptance samples and full format coverage |
| FLIR thermal video | Six 640×512 14-bit clips, 900 frames at 2/4/8 Mbps; no measured two-frame encode misses | Representative high-resolution IR, quality review and agreed final operating envelope |
| High-resolution visible luma | Beauty 1920×1080, 50 frames at 2 Mbps; 35.61 dB, worst encode 62.49 ms | Colour, longer sequences, target-resolution coverage and current 4/8 Mbps replay |
| Additional thermal and Bayer | Bracken 151, Marathon 1000 and CRVD 7 frames; rate/deadline passed at 2 Mbps | Effective thermal precision audit; CRVD quality is unresolved (19.40 dB sample-plane PSNR) |
| Primary 2 Mbps envelope | **2,108 source frames; all ≤8,333 bytes/frame and 0 measured encoder misses >66.67 ms** | Agreed frame rate and complete latency boundary; sustained live queue/throughput validation |
| D+ exact / NEAR=4 | Exact max error 0; NEAR max error 4 on 1,158 frames | Tested rates far above 2 Mbps; many latency failures; agree exact-mode acceptance conditions |
| Required formats | Mono/luma/Bayer sample paths and several bit-depth profiles exercised | All 8/10/12/14/16-bit mono/colour paths and original-format reconstruction |
| Metadata and files | Files encode/decode; 128 metadata bytes serialized in latest runs | Final schema and field-by-field decode round trip |
| Transport and bandwidth | Packetizer/reassembler, simulation and fixed-rate sweeps exist | Actual Ethernet/UDP endpoints, agreed framework scope and bandwidth-change/buffer evidence |
| Platform and delivery | C++ Release build/run on Jetson Orin Nano Super; source and build scripts available | Independent designated PC decoding, dependency/offline/resource audits and complete release handover |

All current video timings are offline measurements at a **30 fps evaluation reference**. Metadata allowance is included in recorded budgets; UDP/IP/FEC overhead is separate. PSNR peaks differ by dataset and cannot be pooled. D+ exactness is not a claim of lossless 2 Mbps on arbitrary inputs. No overall completion percentage replaces the acceptance gates.

## Current algorithm profiles

| Profile | Algorithm / selection | Status |
|---|---|---|
| E tiled wavelet, intra | CDF 9/7 + embedded SPIHT/range coding; `--tile 128 --tile-levels 4 --gop 1` | Primary measured video candidate |
| E temporal wavelet | Motion-compensated P frames; `--gop` greater than 1 | Existing experimental path; current headline runs use intra |
| D+ exact / near-lossless | LOCO-I-style prediction/context modelling + custom adaptive range coder; `--lossless [--near N]` | Reconstruction demonstrated; rate/latency limits recorded |
| Two-layer | Wavelet base + predictive residual; `ewc_layered` | Experimental; simultaneous rate/error/latency guarantee not established |
| Embedded lossless | Reversible 5/3 + embedded coding | Experimental; separate from the main lossy 9/7 path |

These are custom implementations, not claims of JPEG-LS/JPEG2000/JPEG XS standards conformance. External CharLS in the older `bitrate` project is a separate reference.

## Build and run

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

Input to the CLI is headerless little-endian uint16 sample planes, row-major. Set the actual bit depth, width and height explicitly. Inspect values first: lossy CLI range handling can clip out-of-range samples; do not silently relabel data.

For the historical 640×512 / 14-bit PoC profile:

```bash
./run_poc.sh input_640x512_14bit.u16 30 build poc_out
```

For the recent verified dataset runner, see [the data kit](scripts/JETSON_DATA_KIT.md). It records per-frame metrics and environment information. Its `PASS_RECORDED_CHECKS` does not itself imply latency or quality acceptance; inspect those fields separately.

| Interface / tool | Reference |
|---|---|
| Encoder / decoder API | [include/ewc.h](include/ewc.h) |
| Packetizer / reassembler API | [include/ewc_net.h](include/ewc_net.h) |
| Bitstream and metadata | [docs/FORMAT.md](docs/FORMAT.md) |
| Network protocol / FEC | [docs/NETWORK.md](docs/NETWORK.md) |
| CLI and experiments | [tools/](tools/), [scripts/](scripts/), [eval/](eval/) |

Optional GStreamer experiments use external libraries and are not evidence of the self-contained configuration. Current source/dependency and offline compliance still require the designated acceptance audit.

## Repository policy

Keep source, manifests, curated metrics and reports in Git. Keep downloaded datasets, reconstructed videos, build outputs and transient runs outside ordinary Git tracking. Existing large historical objects remain documented; this cleanup does not rewrite Git history.

**Branches:** `video-test` is the proposed active development line; `main` remains the published baseline until reviewed promotion. This cleanup is prepared on `cleanup/reconcile-2026-11-11`. No other branch is deleted, merged, hidden or force-pushed. See the [branch audit](docs/BRANCHES.md).
