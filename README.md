# ewc — embedded wavelet codec for high-bit-depth thermal and visible imagery

Pure C++17. No external libraries. Monochrome 1–16-bit samples in `uint16` containers.

ewc is application-neutral: the same encoder, decoder and transport serve storage,
live streaming over UDP, and archival (lossless) use. The main lossy operating point
studied so far is 2 Mbps for 640×512 14-bit video at 30 fps.

## RCI proof-of-concept (start here)

The Scope of Work PoC asks for 640×512 14-bit infrared at a fixed 2 Mbps, frame-level coding,
encoder latency ≤ 2 frames, raw 14-bit decoder output, and robustness to losses in the stored
bitstream. The `--poc` preset does exactly that; `docs/POC_REPORT.md` has the measured results.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./run_poc.sh input_640x512_14bit.u16 30 build poc_out   # encode, decode, inject losses, latency
```

Headline (FLIR ADAS v2, 1,000 frames): 1,999.7 kbps with no frame over budget; 34.9–42.1 dB PSNR
over scene range, RMSE 3.8–7.6 DN; encoder worst case 23 ms = 0.69 frame at 30 fps; 94% of frames
fully intact with 1% of packets lost from the stored stream; the decoder never loses synchronisation.

## Test data

KAIST 8-bit frames are in `data/kaist/`. FLIR ADAS v2 14-bit clips: see `data/README.md`
(set `EWC_DATA` to their folder and `EWC_BIN` to the build folder before running `eval/` scripts).

## Profiles

Why these three paths, with the measurements: `docs/ARCHITECTURE.md`. Plain-language guide to every architecture and the full score tables: `docs/ARCHITECTURES_EXPLAINED.md`.


| Profile | How to select | What you get |
|---|---|---|
| Lossy wavelet, intra | default (`gop = 1`) | CDF 9/7 wavelet + embedded SPIHT/arithmetic coding, exact per-frame byte budget |
| Lossy wavelet, I/P | `gop > 1` | adds motion-compensated P frames (OBMC), closed loop |
| Lossless (D+) | `lossless = true` | JPEG-LS modelling (LOCO-I) + adaptive range coder, bit-exact; 1.0-1.9% smaller than CharLS on 14-bit FLIR |
| Near-lossless (D+) | `lossless = true, near = N` | every sample within ±N; 1.3-17% smaller than CharLS (median -9%) |
| Bounded error at fixed rate | `tools/ewc_layered` (experimental) | wavelet base + D+ residual; guaranteed max error at 2 Mbps on video |
| Embedded lossless | `lossless = true, embedded_lossless = true` | reversible 5/3 wavelet; any prefix decodes lossy; 4–5× slower |

All profiles protect headers with CRC-32 / CRC-16 and payloads with a CRC-16 per 256 bytes,
so damaged regions are found and concealed instead of decoded as data.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Sanitizer build:

```bash
cmake -S . -B build_asan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined"
cmake --build build_asan -j
```

## Tests

```bash
build/test_ewc    # codec: transforms, lossless, near-lossless bound, LOCO damage, CBR,
                  # closed loop, threads, metadata, corruption, reference tracking,
                  # input range, >64 KiB groups
build/test_net    # transport: all FEC modes, losses, duplicates, reordering, malformed and
                  # truncated datagrams, end-to-end "never reliable-but-wrong"
ctest --test-dir build
```

## Tools

| Tool | Purpose |
|---|---|
| `ewc_enc` | raw `uint16` LE file → `.ewc` stream (`--lossless`, `--gop`, `--kbps`, `--bd`, ...) |
| `ewc_dec` | `.ewc` stream → raw file; optional per-frame report against a reference |
| `ewc_bench` | encode → packetise → simulated UDP channel → reassemble → decode; prints a `SUMMARY` line |
| `ewc_corrupt` | introduce losses into a stored bitstream: bit errors, deleted or zero-filled packets, bursts |
| `loco_probe` | LOCO-I diagnostics |

Input files are raw little-endian `uint16`, row-major, 2 bytes per pixel, no header.
Samples above `2^bd − 1` are rejected by the library. `ewc_enc` refuses them in lossless
mode and clips them (with a count) in lossy mode.

### ewc_bench examples

```bash
# 2 Mbps codec rate, I/P with GOP 30, 2 threads
ewc_bench --in seq.u16 --w 640 --h 512 --kbps 2000 --gop 30 --threads 2 -q

# 2 Mbps total on the wire (headers + FEC included), 5% loss, mean burst 2,
# Reed-Solomon FEC with one parity per 4 data datagrams, intra refresh on feedback
ewc_bench --in seq.u16 --w 640 --h 512 --link-kbps 2000 --gop 30 \
  --udp-loss 0.05 --burst 2 --fec 4 --fec-mode rs --feedback 2 -q

# lossless, verify bit-exactness (SUMMARY: exact 1)
ewc_bench --in seq.u16 --w 640 --h 512 --lossless --threads 2 -q
```

The `SUMMARY` line reports codec, FEC, header and wire kbps separately; frame classes
`ok / damaged / ref_invalid / lost`; PSNR (14-bit peak), RMSE, MAE, max error; latency
percentiles; datagram loss and FEC recovery. `--csv` writes per-frame rows.

## Library API

See `include/ewc.h` (codec) and `include/ewc_net.h` (transport).

- `Encoder::encode()` appends one self-contained frame unit.
- `Encoder::request_intra()` forces the next frame to be an I frame (use on receiver feedback).
- `Decoder::decode_next()` decodes the next unit. Damaged data is concealed.
  `FrameInfo::reliable()` is true only for frames decoded from complete, CRC-verified data
  whose reference chain (for P frames) is also intact.
- `Packetizer` / `Reassembler` cut units into datagrams with optional FEC and put them back.

## Documentation

- `docs/POC_REPORT.md` — the RCI proof-of-concept, answered section by section.
- `docs/FORMAT.md` — frame unit (bitstream) format, including the LOCO profile.
- `docs/NETWORK.md` — UDP datagram protocol, FEC modes and their limits.
- `docs/EVALUATION.md` — measured results, method and known limitations.
- `docs/SESSION_REPORT.md` — what changed in the 2026-11-04/05 session, why, how it was tested.
- `eval/` — scripts and raw results (`scripts/`, `results/`), head-to-head harness against
  the original bitrate-main (`h2h/`), FLIR ADAS v2 preparation (`flir_prep/`).
