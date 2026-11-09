# ewc — session report (2026-11-04 / 05)

This document covers one working session on the ewc thermal image codec. It says what the
repository looked like at the start, what was changed and why, how each change was tested,
what was measured, what was tried and rejected, and what is still open.

Every number here was measured in this session. Nothing was estimated. Raw result files are
in `eval/results/`, scripts in `eval/scripts/`, `eval/h2h/` and `eval/flir_prep/`.

---

## 1. Starting point

Repository `work/ewc`, 3 commits, plus uncommitted work:

| Item | State at start |
|---|---|
| Codec | C++17: 5/3 and 9/7 wavelets, embedded SPIHT + arithmetic coding, motion-compensated P frames, LOCO-I (JPEG-LS algorithm) lossless / near-lossless, CRC-protected headers and blocks |
| Uncommitted | a concealment cleanup in `codec.cpp`; a new UDP packetiser / reassembler with XOR FEC (`ewc_net.h`, `net.cpp`); a bench rewrite with channel simulation |
| Tests | one test binary, 8 tests, passing; no network tests |
| Docs | none inside `ewc` (project docs lived in the separate `bitrate` repo) |
| Builds | five build folders, several stale (one predated the LOCO profile) |

The original project, `bitrate-main` (same sources as the `baseline` copy), is a separate,
8-bit-only wavelet codec. It is compared head-to-head in section 6.

---

## 2. Commits made in this session

| Commit | What |
|---|---|
| `e4fe5fe` | Pre-existing concealment cleanup, committed unchanged on its own |
| `9121905` | Pre-existing network work, committed unchanged ("as found") so later fixes are reviewable |
| `9e233ac` | Decoder reference validation, encoder intra request, input range checks |
| `4dfdf3a` | Network protocol v2 (balanced payloads, XOR / interleaved XOR / Reed–Solomon FEC), network tests, bench rewrite |
| `127cfe5` | Fix group length overflow above 64 KiB (broke embedded lossless) |
| `c0b1632` | README, format and network docs, evaluation scripts and results |
| `4f71ba4` | FLIR ADAS v2 14-bit evaluation |
| `9338580` | Head-to-head against the original `bitrate-main` |
| (last) | This report, FLIR preparation scripts |

Each commit was made only after both test suites passed. Unrelated cleanup and network
changes were kept in separate commits.

---

## 3. Bugs found and fixed

### 3.1 P frames built on a missing or broken reference were reported as fine
- **Problem.** A P frame is predicted from the previous frame. If that frame was lost, the
  decoder silently predicted from an older one. The result was wrong, but every group decoded
  cleanly, so the frame looked intact. The error then carried on until the next I frame.
  Corrupt motion data was also replaced by zero vectors without any report.
- **Fix.** The decoder now remembers which frame it holds as reference and whether that frame
  was itself reliable. `FrameInfo` gains `ref_gap`, `ref_ok`, `ref_sequence`, `motion_ok`,
  `skipped_bytes` and `reliable()`. Every affected frame is flagged.
- **Recovery.** `Encoder::request_intra()` forces the next frame to be an I frame, so a receiver
  with a feedback channel can stop the error early. The bench models this (`--feedback D`).
- **Test.** `test_reference_tracking`: drop one P frame → the next frame is flagged
  `ref_gap`, the one after is flagged `ref_ok = false`, the next I frame is reliable again;
  corrupt motion data → `motion_ok = false`; `request_intra()` produces an I frame.
- **Effect.** At 2% packet loss with no FEC, 52 of 90 frames were wrong but would have been
  counted as fine by the old accounting.

### 3.2 Embedded lossless was not lossless with few groups
- **Problem.** The group chunk header stored the payload length in 16 bits. A group larger
  than 65,535 bytes (high budgets, or the embedded lossless profile with 2 groups) wrapped
  the length. Frames decoded as damaged, and embedded lossless with `--groups 2` was not
  bit-exact.
- **Fix.** Frames whose budget is ≥ 64 KiB or unlimited set a new flag (`F_LEN32`) and use
  15-byte group headers with a 32-bit length. All other streams are byte-identical to before
  (checked on the 2 Mbps and LOCO profiles).
- **Test.** `test_large_groups`: embedded lossless with 2 groups is exact; a 200 kB lossy
  budget with 2 groups decodes intact.

### 3.3 Out-of-range input was silently clipped in lossless mode
- **Problem.** `ewc_enc --lossless` with samples above `2^bd − 1` printed one warning, clipped,
  and exited with success. The output was not the input. Example: raw FLIR SEQ data (15-bit)
  at `--bd 14` gave 8.6 million wrong pixels.
- **Fix.** The library now rejects out-of-range samples in every profile. `ewc_enc` stops with
  an error in lossless mode and counts all clipped samples in lossy mode. Frame type in
  `--stats` is now read from the stream (scene cuts were mislabelled).
- **Test.** `test_input_range`; the lossless check script includes an out-of-range case.

### 3.4 Benchmark tool errors
- `--kbps` used whatever `--fps` had been parsed so far: `--kbps 2000 --fps 25` gave 1666 kbps.
  Now applied after parsing.
- Timestamps were fixed at 30 fps. Now use `--fps`.
- After a lost frame header, the decoder resynchronised on the next frame, and that intact
  frame was counted as damaged. Classification now uses group, motion and reference status.
- Packet loss could be simulated with block CRCs off, which decodes zero-filled bytes as data.
  Now refused.
- The burst model's real loss rate was below the requested rate. Replaced by a Gilbert–Elliott
  model with exact average loss and mean burst length.

### 3.5 Network layer issues
- Parity datagrams were always full MTU size, even for tiny units (300-byte unit → 1,724 wire
  bytes). Payloads are now split evenly; parity is never larger than the data
  (300-byte unit → 648 wire bytes).
- Truncated datagrams were accepted. The receiver now checks the exact expected length.
- Unrecoverable bytes were not reported. `Unit::missing()` and `missing_ranges()` now do.

---

## 4. Features added

| Feature | Where | Notes |
|---|---|---|
| Reference tracking and `reliable()` | `ewc.h`, `codec.cpp` | see 3.1 |
| `Encoder::request_intra()` | `ewc.h`, `codec.cpp` | feedback-driven recovery |
| Network protocol v2 | `ewc_net.h`, `net.cpp`, `docs/NETWORK.md` | balanced payloads; exact validation; dup / late / bad counters |
| Interleaved XOR FEC (`xori`) | `net.cpp` | repairs bursts up to the number of parity datagrams |
| Reed–Solomon FEC (`rs`) | `net.cpp` | systematic Cauchy RS over GF(256); any np losses repairable; large units split into interleaved blocks |
| End-to-end bench | `tools/ewc_bench.cpp` | per-frame encode → packetise → lossy channel → reassemble → decode; feedback; freeze / show policy; codec, FEC, header and wire rates reported separately |
| Network tests | `tests/test_net.cpp` | 8 test groups, see section 5 |
| Docs | `README.md`, `docs/FORMAT.md`, `docs/NETWORK.md`, `docs/EVALUATION.md` | actual frame header (44 bytes; the old comment said 40), LOCO profile, protocol, FEC limits |
| Evaluation kit | `eval/` | scripts, raw results, head-to-head harness, FLIR preparation |

---

## 5. Testing

| Suite | Contents | Release | ASan + UBSan |
|---|---|---|---|
| `test_ewc` | wavelet round trip; lossless; near-lossless bound; LOCO damage; CBR and closed loop; thread determinism; metadata; 200 corrupted streams; **reference tracking; input range; >64 KiB groups** (new) | pass | pass |
| `test_net` (new) | zero-loss round trip in 230 configurations (4 FEC modes, sizes 0–400,000 bytes, 2 MTUs); XOR single loss repaired / double loss reported; interleaved bursts; Reed–Solomon random losses (163 trials) and overload; duplicates; reordering; late datagrams; 10 kinds of malformed or truncated datagrams; missing first datagram; incomplete release; empty unit; **end-to-end: under 5% loss no frame is marked reliable while differing from the encoder's output** | pass | pass |

Independent lossless check (`eval/scripts/lossless_check.py`): encode with `ewc_enc`,
decode with `ewc_dec`, byte-compare the files and compare pixel by pixel. Zero mismatches on
every sequence, including 15-bit data at `--bd 16`, 1024×768 frames, and all 1,000 FLIR frames.

All five original build folders were rebuilt; all binaries and tests are current.

---

## 6. Results

Host: Intel Xeon 2.1 GHz, 2 vCPUs, GCC 13.3, Release. No GPU, no Jetson.

### 6.1 Against the original pipeline (`bitrate-main`, unmodified)

On its own comparison contract (KAIST 8-bit frames):

| | bitrate-main | ewc | Target |
|---|---|---|---|
| Lossless thermal | 59.46 Mbps | **10.07 Mbps** (9.81 without CRC / stripes) | ≤ 12 |
| Max error ≤ 1, thermal | not possible | **5.73 Mbps @ 50.27 dB** | ≤ 6 |
| Lossless visible | 135.93 Mbps | **32.60 Mbps** | — |

- ewc meets both targets. Without integrity overhead it is 0.5% below the JPEG-LS reference.
- The original's STATUS file claims ~20.3 Mbps lossless; its code reaches 59.46 at best.

On 14-bit FLIR at 2 Mbps the original keeps only the top 8 bits (about 3 grey levels left).
Even with a free contrast stretch, its error is 100–830 DN RMSE; ewc's is 3.7–8.2 DN.

### 6.2 Lossless, 14-bit

| Data | bits/pixel | Mbps @ 30 fps | vs JPEG-LS |
|---|---|---|---|
| Earlier corpus (pv24, seq28, csq7, pan90) | 5.35–6.01 | 51–59 | +1.5 to +1.8% |
| FLIR ADAS v2 (6 videos + stills, 1,000 frames) | 4.39–4.80 | 43–47 | +1.4 to +1.6% |

The gap to JPEG-LS is entirely the resilience cost: 64-row stripes +1.2%, block CRCs +0.8%.
The core coder alone is 0.5% better than JPEG-LS. JPEG XL lossless is smaller but about 15×
too slow for real time on this CPU. **Lossless cannot reach 2 Mbps on this content.**

### 6.3 Lossy at 2 Mbps

| Data | ewc intra vs JPEG 2000 | ewc I/P (GOP 30) vs x265 |
|---|---|---|
| Earlier corpus | 0.2–0.6 dB below | equal or better on all 3 videos (+0.3 to +1.3 dB) |
| FLIR ADAS v2 | 0.5–0.7 dB below | ewc intra is 1.6–2.4 dB above x265 |

- On moving content, P frames help a lot (pan90: +8 dB over intra).
- On FLIR ADAS, P frames help nothing. The data is noise-limited: frame-to-frame change
  (~4 DN) equals the sensor noise (~4 DN). **For such sensors use intra-only (`--gop 1`)**:
  same quality, ~12 ms instead of ~30 ms encode, better loss resilience.
- Predictive near-lossless is 16 dB worse than the wavelet coder at 2 Mbps. It is the right
  tool only when a hard per-pixel error bound is needed, or above ~20–25 Mbps.

### 6.4 Rate control

- Strict CBR (default) meets the per-frame budget on every frame.
- A 2-frame buffer with a larger I-frame budget gains up to +0.35 dB on long moving clips,
  but overshoots the average rate on short clips (+6.4% on a 7-frame clip). Not adopted as
  default.

### 6.5 Packet loss (equal 2 Mbps total wire rate, FEC overhead comes out of codec bits)

| Case | No FEC | XOR k=4 | Reed–Solomon k=4 |
|---|---|---|---|
| pan90, GOP 30, 5% random loss: reliable frames | 34% | 90% | **100%** |
| pan90, GOP 30, 10% random loss | 19% | 75% | **94%** |
| FLIR, intra-only, 10% random loss | 51% | 92% | **98.7%** |
| FLIR, intra-only, 10% loss in bursts of 4 | 78% | 88% | **90%** |

At 1% loss, no FEC plus feedback gives the best average quality, but 8–23% of frames are not
reliable. Bursts are the weak point: a 2 Mbps frame is only ~7 datagrams.

### 6.6 Speed and memory

| Profile | Encode ms/frame (1 / 2 threads) | Decode ms/frame | Peak RAM |
|---|---|---|---|
| Intra 2 Mbps | 7.6 / 7.8 | 6–8 | ~17 MB |
| GOP 30 2 Mbps | 34.7 / 24.2 | 9–12 | ~29 MB enc, ~20 MB dec |
| Lossless | 22.1 / 15.6 | 13–23 | ~14 MB enc, ~19 MB dec |

I/P encoding needs both cores to fit in the 33 ms frame period; a few frames still run over.

---

## 7. Tried and rejected (with evidence)

| Idea | Result | Decision |
|---|---|---|
| Multithreaded wavelet transform (bit-identical output) | ~1 ms faster median on 2 cores, inside run-to-run noise | Reverted. May help on more cores (untested) |
| Stricter intra/inter switch (`--scut 0.9 / 0.75`) | FLIR +0.1 to +0.45 dB, but pan90 −8 dB | Default kept; recommend `--gop 1` for noise-limited sensors instead |
| Reconstruction offset (`--rq5` 10/12/20), wavelet levels 5/7 | within ±0.07 dB | Defaults kept |
| Larger I-frame budget with 2–3 frame buffer as default | small gain, overshoots average on short clips | Kept strict CBR |

Development / validation split was used for tuning (earlier corpus: seq28 + pan90 frames 0–44
for development; pan90 frames 45–89 + csq7 for validation). FLIR ADAS v2 was never used for
tuning defaults; it is a held-out test.

---

## 8. Data used

| Set | Source | Notes |
|---|---|---|
| pv24 / pv315 | DJI M30T raw counts (solar site) | independent stills, 13-bit values |
| seq28, csq7 | FLIR SEQ / CSQ camera samples | 13–14-bit video |
| pan90 | **semi-synthetic** (real frame warped along a pan path + moving target + noise) | label as synthetic in any paper |
| FLIR ADAS v2 | user's copy, `analyticsData` 16-bit TIFFs | 6 of 8 test videos are 14-bit; **2 are 8-bit and excluded**. 150-frame clips + 100 val stills. Prepared with `eval/flir_prep/pack.py` |
| KAIST frames | `bitrate` repo contract | 8-bit, used only for the head-to-head |

No 8-bit conversion happens anywhere in ewc. Samples go in and out as `uint16`.

---

## 9. How to reproduce

```bash
# build and test
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
ctest --test-dir build

# lossless check (paths inside the scripts point at /home/claude/data; edit for your machine)
gcc -O2 -o rssrun eval/scripts/rssrun.c          # small launcher that records peak RAM
RSSRUN=./rssrun python3 eval/scripts/lossless_check.py build out.csv

# one lossy run with metrics (needs numpy, scikit-image, and work/scripts/evalkit.py)
python3 eval/scripts/codec_eval.py build tag out.jsonl seq28 -- --kbps 2000 --gop 30 --threads 2

# FLIR ADAS v2 preparation (on the machine that has the dataset)
python3 eval/flir_prep/pack.py /path/FLIR_ADAS_v2 /path/out

# FEC comparison at equal wire rate
FECS="none:0,xor:4,rs:4" LOSSES="0.05,0.10" BURSTS="1,4" SEEDS=3 \
  python3 eval/scripts/net_sweep.py build out.jsonl flirv_ePoikf 1 -1
```

The evaluation scripts use absolute paths from the session workspace (`/home/claude/data`,
`/home/claude/work/scripts/evalkit.py`). They are kept as run, so results stay traceable;
adjust the paths to rerun elsewhere. The datasets themselves are not in the repository.

---

## 10. Known limitations and next steps

**Not tested:** Jetson / CUDA (no hardware), SIMD-specific work, real sockets (the transport
is tested through the in-process channel model), sequences longer than 150 frames, the full
FLIR ADAS v2 set (1,000 of ~15,600 thermal frames used).

**Limitations:**
- Lossless needs 43–59 Mbps on this content; 2 Mbps applies to lossy only.
- FEC works within one frame; long bursts cannot be repaired without cross-frame
  interleaving, which adds delay.
- The feedback path (receiver → encoder) is simulated in the bench, not a protocol.
- I/P encoder latency p99 exceeds one frame period on this 2-core CPU.
- `Reassembler` frame ordering does not handle 32-bit sequence wrap.

**Next steps, in order of expected value:**
1. Run on Jetson Orin Nano: latency, memory, sustained FPS.
2. Full FLIR ADAS v2 test set and longer sequences.
3. A rate–distortion intra/inter decision (code both, keep the cheaper) to replace the
   pixel-domain cost estimate; costs encode time, so make it optional.
4. Cross-frame FEC interleaving as an option for bursty links, with its delay measured.
5. Closing the 0.5–0.7 dB intra gap to JPEG 2000 would need EBCOT-style block coding with
   rate–distortion optimised truncation; a redesign, not tuning.
