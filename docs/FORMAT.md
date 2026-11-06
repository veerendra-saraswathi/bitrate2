# ewc frame unit format

A stream is a concatenation of self-contained **frame units**. All integers are little
endian. Every unit starts with a CRC-protected header; the decoder resynchronises after
damage by scanning for the next valid header.

## Frame header (44 bytes)

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 4 | magic `EWC1` | |
| 4 | 1 | version | 1 |
| 5 | 1 | type | 0 = I, 1 = P |
| 6 | 1 | flags | see below |
| 7 | 1 | wavelet | 0 = LeGall 5/3, 1 = CDF 9/7 (LOCO: 0) |
| 8 | 2 | width | 1..16384 |
| 10 | 2 | height | 1..16384 |
| 12 | 1 | bit depth | 1..16 |
| 13 | 1 | levels | wavelet levels 1..8 (LOCO: 1) |
| 14 | 1 | groups | wavelet groups; LOCO: number of stripes |
| 15 | 1 | frac_bits | 0..4 |
| 16 | 4 | sequence | frame counter, +1 per unit |
| 20 | 8 | timestamp_us | |
| 28 | 4 | total_len | bytes of the whole unit, header included |
| 32 | 2 | meta_len | application metadata bytes |
| 34 | 1 | mv_block | motion block size 8/16/32/64 |
| 35 | 1 | reserved | LOCO: `near` |
| 36 | 2 | dc | intra level shift; LOCO: stripe rows |
| 38 | 2 | reserved | |
| 40 | 4 | crc32 | CRC-32 (IEEE) of bytes 0..39 |

Flags: `1` lossless, `2` motion present (P), `4` block CRCs, `8` LOCO profile,
`16` group lengths are u32 (set when the frame budget is ≥ 64 KiB or unlimited),
`32` tiled profile (byte 13 = wavelet levels per tile, byte 34 = log2 of the tile size,
bytes 36–37 = the frame's level shift `dc`).

## Body

In order:

1. **Metadata** (if `meta_len > 0`): `meta_len` bytes, then CRC-16.
2. **Motion** (P frames): `mv_len u16`, `crc16 u16`, then `mv_len` bytes of motion vectors.
   If the CRC fails, the decoder uses zero vectors and reports `FrameInfo::motion_ok = false`.
3. **Wavelet group chunks** (non-LOCO), one per group:
   `FF E7 | gid u8 | nmax i8 | seq8 u8 | nsym u32 | len u16 | crc16` (13 bytes), or
   `len u32` and 15 bytes with flag 16. The CRC-16 covers the preceding header bytes.
   Then `len` payload bytes, split into 256-byte blocks. With flag 4 each block is followed
   by its CRC-16. The decoder trusts only the prefix of blocks before the first CRC failure;
   the embedded group stream still decodes from that prefix.
4. **Redundancy trailer** (optional, lossy I frames): `FF E8 | seq8 | G | crc16`, then per
   group `nmax i8 | len u8 | first len bytes of the group stream | crc16`. Used when a group is
   lost or broken earlier than `len`.
5. **LOCO stripe chunks** (flag 8, instead of 3 and 4), one per stripe:
   `FF E9 | sid u8 | seq8 u8 | y0 u16 | rows u16 | len u32 | crc16` (14 bytes), then the
   payload in 256-byte blocks with CRC-16s as above.

6. **Tile rows** (flag 32, instead of 3–5), one per row of tiles:
   Header byte 35 selects the tile index format.
   - `0` (fixed): `FF E6 | row u16 | seq8 | ntx u8` then `ntx` entries of
     `nmax i8 | nsym u24 | len u24` (7 bytes each), then a CRC-16 over all of the above.
   - `1` (compact, encoder default since exp-2026-11-05): `FF E6 | row u16 | seq8 | ntx u8 | elen u16`
     then `ntx` entries of `nmax i8 | nsym varint | len varint` (LEB128, `elen` bytes in total,
     typically 3–5 per tile), then a CRC-16 over all of the above. Decoders older than this
     change do not read format 1; use `tile_compact_index = false` (`--tile-fixed-index`) for them. Then the row's tile streams back to
   back (`len` bytes each, left to right), in 256-byte blocks each followed by a CRC-16.
   Each tile is coded on its own: tile × tile block (edge-replicated at the frame border) minus
   `dc`, its own wavelet (`levels` deep), coefficient trees, and arithmetic coder. The frame
   budget is shared across all tiles by one rate-distortion allocation. The decoder trusts each
   tile's bytes up to the first failed block CRC inside that tile, so one bad block damages one
   tile only.

`seq8` is the low byte of the frame sequence; it stops chunks of a neighbouring unit from
being taken for this unit's chunks after resynchronisation.

## LOCO profile (lossless / near-lossless)

- Algorithm: LOCO-I, the algorithm of JPEG-LS (ITU-T T.87): MED prediction, 365 gradient
  contexts with bias cancellation, limited-length Golomb codes, run mode. Not
  bitstream-compatible with JPEG-LS files (own framing and edge rules).
- The frame is cut into horizontal stripes (`stripe_rows`, default 64). Each stripe has its
  own contexts and a virtual constant row above it. Stripes are coded in parallel, and a
  damaged stripe does not affect the others.
- `near = 0` is bit-exact. `near = N` bounds every sample error by N.
- Measured cost of the resilience features on seq28: 64-row stripes +1.2% bits,
  block CRCs +0.8% bits, versus one stripe without CRCs (see `docs/EVALUATION.md`).

## Decoder behaviour on damage

- Lost tiles, or tiles damaged before 4 bit planes were decoded: copied from the previous output
  frame (or left at `dc` if there is none). A tile that is intact but coded with few bit planes
  by the rate allocator is decoded normally.
- Lost or partial wavelet groups: intact prefix decoded; rows of weak groups replaced by the
  previous output frame (or interpolated in the LL band when there is none).
- Lost LOCO rows: copied from the previous output frame, else the row above.
- P frame with a missing or unreliable reference: decoded against what the decoder holds
  and flagged (`ref_gap`, `ref_ok = false`). The error persists until the next I frame;
  every affected frame is flagged. `Encoder::request_intra()` shortens this when a feedback
  channel exists.
