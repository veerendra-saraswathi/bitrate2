# Revised MVP Expectations — Video Focus
## Strictly Aligned to Full RCI SoW (Problem 4)

**Date:** 2026-11-10  
**Scope:** Low-bitrate, high-resolution image/video compression with low latency  
**Platform:** Jetson-class edge encoder (pure C++17, self-contained)  
**Primary focus this week:** Video sequences across all architectures

---

### 1. SoW Requirements (Non-negotiable)

- Pure C/C++17 implementation — no external codec libraries
- Low latency: ≤ 2 frames end-to-end on Jetson (≤ 66.7 ms @ 30 fps)
- Configurable bitrate (PoC reference point ~2 Mbps for thermal; must support other rates)
- Perfect reconstruction (lossless) option must exist
- Near-lossless / bounded-error option
- Support for mono raw VIS / IR imagery
- Bit depths: 8 / 10 / 12 / 14 / 16
- Resolutions up to ~1920×1200 (indicative)
- Single stream, metadata, CRC / integrity
- Real-time capable on Jetson Orin Nano class hardware
- Reconstruction quality measurable (PSNR, max absolute error)

---

### 2. Architectures to Evaluate (Video)

| Priority | Architecture              | Role                              | Status          |
|----------|---------------------------|-----------------------------------|-----------------|
| 1        | E-wavelet (tiled 9/7 + SPIHT) | Main low-bitrate path            | Active          |
| 2        | D+ (JPEG-LS / LOCO-I)     | Exact & near-lossless path        | Active          |
| 3        | Two-layer hybrid          | Experimental comparator           | Frozen          |
| 4        | A (5/3 reversible wavelet)| Optional exact reference          | Optional        |
| —        | Others (B DCT, C, etc.)   | Only if already buildable         | Deprioritized   |

---

### 3. Video Test Matrix (This Week)

- **Thermal / IR**
  - LTIR 16-bit sequences (native high-depth)
  - Additional consecutive frames where available (target 100–300)
- **Visible**
  - Beauty 1920×1080 8-bit luma (50+ frames)
  - Bosphorus 1920×1080 8-bit luma
  - SunBath 10-bit (documented crop if used)
- **Rates**
  - Primary: 2 Mbps
  - Diagnostic: 4 Mbps, 8 Mbps
  - Unconstrained lossless (D+)
- **Metrics per sequence**
  - Exact rate (bytes/frame including metadata)
  - Encode latency (median / p95 / max) and deadline misses
  - Decode reliability (all frames)
  - PSNR and max absolute error
  - Bit-exact verification for lossless
  - Throughput (fps)

---

### 4. MVP Acceptance Criteria (Video)

- E-wavelet meets ≤ 2 Mbps and ≤ 2-frame latency on real IR and 1920×1080 visible sequences
- D+ provides bit-exact reconstruction and a verified near-lossless mode
- Results reproducible from committed code + input hashes
- Jetson Orin Nano numbers recorded with board identity
- Hybrid retained only as comparator (no further optimization required for MVP)

---

### 5. Explicit Non-goals for This Week

- Colour (YUV/RGB) end-to-end — luma-only is sufficient unless SoW explicitly requires colour
- Dynamic mid-stream bitrate adaptation (unless already implemented)
- Windows decoder verification
- New architecture development

---

### 6. Deliverables

- Per-architecture video score CSVs / logs
- Jetson validation archive with environment info
- Updated STATUS.md reflecting video results
- This document

---

**Decision:** E-wavelet is the leading low-bitrate candidate; D+ is the exact/near-lossless complement. All video testing this week will be measured against the criteria above.
