# First Jetson video data kit — 10 November 2026

This kit prepares the archives already identified in Downloads. It does not download datasets, change originals or access a remote Jetson.

## On the Mac

Install preparation dependencies in the existing audit environment:

```bash
/tmp/bitrate-data-audit-venv/bin/python -m pip install numpy tifffile pillow
/tmp/bitrate-data-audit-venv/bin/python "$HOME/Downloads/jetson_data_kit/prepare_jetson_data.py" --downloads "$HOME/Downloads" --output "$HOME/jetson_video_inputs_20261110"
```

The folder contains full Bracken (151 frames), full Marathon (1,000 frames), and CRVD scene6 at the lowest available ISO using only its noisy0 track. CRVD has very few independent temporal positions; it must not be described as a long natural-motion sequence. Exact frame count and selected source members are recorded. No ISO/noise variants are concatenated. All frames must decode as 2D unsigned integers. The script validates every frame's dimensions and sample range and refuses clipping, scaling, overwrite, missing frames and frame-number gaps. It makes a transfer TAR and SHA-256 sidecar in the Mac home directory. Expect roughly 1.4 GB of raw inputs plus a similarly sized TAR, and temporary space for the nested CRVD ZIP. Preparation can take several minutes.

Thermal codec depth 16 labels uint16 input/storage. Effective sensor precision is not established. CRVD codec depth 12 is admitted only if values fit 0..4095; this validates range, not radiometric effective precision. Source fps is unknown. Test cadence 30 fps is a reference, not a capture-fps claim. Bayer input is not native monochrome. Pixel values are preserved with little-endian unpacking only. These inputs do not cover full color, 10-bit video or native high-resolution IR near 1920x1200. WB TIFFs and DNG stills are intentionally outside this first video bundle pending their separate format/provenance audit.

## Transfer

Use the SSH destination and port supplied by the rental provider; do not guess them. With the appropriate destination, copy `jetson_video_inputs_20261110.tar` and its `.sha256` sidecar to the Jetson. For an ordinary SSH setup:

```bash
scp "$HOME/jetson_video_inputs_20261110.tar" "$HOME/jetson_video_inputs_20261110.tar.sha256" USER@HOST:~/
```

Replace USER@HOST; use `scp -P PORT` if the provider requires a nondefault port.

## On the Jetson

```bash
cd "$HOME"
sha256sum -c jetson_video_inputs_20261110.tar.sha256
tar -xf jetson_video_inputs_20261110.tar
```

Build the actual source checkout using its instructions. This runner was checked against the ewc_enc/ewc_dec interfaces in bitrate2 baseline f07161bb0469153852d496a4aa4acacf8fd38179. It records the actual tested commit, dirty-tree status and executable hashes. It does not copy or replace the codec. If tools have changed, verify their flags first.

```bash
cmake -S /PATH/TO/REPO -B /PATH/TO/REPO/build -DCMAKE_BUILD_TYPE=Release
cmake --build /PATH/TO/REPO/build --target ewc_enc ewc_dec -j2
python3 "$HOME/jetson_video_inputs_20261110/run_jetson_video.py" --repo /PATH/TO/REPO
```

Replace /PATH/TO/REPO with the real checkout. Default smoke checks encode at most ten frames per sequence at 2/4/8 Mbps using intra-only 128x128 tiles, four tile levels, two workers, 30 fps reference and 128 actual ASCII metadata bytes. Other coder defaults come from the pinned implementation. A seven-frame input stays seven frames; no padding or looping.

After the smoke checks pass:

```bash
python3 "$HOME/jetson_video_inputs_20261110/run_jetson_video.py" --repo /PATH/TO/REPO --frames 0 --include-exact
```

This adds full-length wavelet checks plus D+ exact and NEAR=4. Rate limits are not applied to D+ modes. The runner checks staged hashes, output sizes/counts, frame order, decoder reliability, encoded byte accounting, fixed-rate budgets and NEAR bounds; exact mode also checks reconstructed bytes independently. It writes commands, encoder/decoder CSVs/logs, encoded streams, reconstructed samples, input manifest, environment identity and summary under timestamped results folders. Preserve these folders outside transient /tmp. The manifest/commands identify settings; the metadata bytes are placeholders, not the final application schema. Metadata round-trip is not independently verified by this CLI runner.

Tegrastats is captured when available. Short runs may yield few samples; do not claim measured peak resources or isolated encoder/decoder utilization solely from this log. Existing board clocks/power settings are observed, not modified. The runner performs offline file replay; it does not establish live throughput, queue stability, Ethernet/UDP or full SoW acceptance. PSNR is reported by the decoder against codec peak; do not compare cross-depth PSNR without matching definitions. Zero-error PSNR sentinels are normalized to infinity in summaries. Source noise and processing provenance remain material.

## Validation performed here

- Python syntax checks.
- Preparation checks on synthetic integer frames: numeric ordering, exact output bytes/endianness, sizes, hashes and overflow rejection.
- Built the real EWC encoder/decoder on the available Linux host and passed five three-frame synthetic runner cases: 2/4/8 Mbps tiled, D+ exact and D+ NEAR=4.
- The user's actual archives and Jetson have not been accessed from this workspace. Their runs remain to be performed.
