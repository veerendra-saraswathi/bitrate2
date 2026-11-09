# ewc UDP transport (protocol version 2)

`include/ewc_net.h` cuts each frame unit into datagrams and puts them back. It does not open
sockets; the application sends and receives the byte vectors.

## Datagram header (24 bytes)

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | `E` `U` |
| 2 | 1 | version = 2 |
| 3 | 1 | flags: bit0 last data datagram, bit1 parity |
| 4 | 4 | datagram sequence (per sender, informational) |
| 8 | 4 | frame (unit) sequence |
| 12 | 4 | data: byte offset in the unit. parity: parity index × pay |
| 16 | 4 | unit size |
| 20 | 2 | pay: payload size of every data datagram of this unit |
| 22 | 1 | k: data datagrams per parity datagram (0 = no FEC) |
| 23 | 1 | fec: 0 none, 1 xor, 2 xor-interleaved, 3 rs |

There is no checksum in the datagram header; UDP's checksum covers it. The codec's own
CRCs protect the unit content.

## Payload sizing

A unit of S bytes becomes `nd = ceil(S / (mtu − 24))` data datagrams of equal size
`pay = ceil(S / nd)` (the last may be shorter). Parity datagrams are `pay` bytes. So a small
unit never carries a full-MTU parity datagram. Measured wire saving versus full-MTU parity:
62% for a 300-byte unit, 15% for 1000 bytes, under 1% for units near 8 kB
(`eval/results/parity_overhead.txt`).

## FEC modes

All modes send `np = ceil(nd / k)` parity datagrams, so overhead is the same for the same k.

| Mode | Parity covers | Repairs |
|---|---|---|
| `xor` | data `[j·k, j·k + k)` | 1 loss per group of k consecutive datagrams |
| `xori` | data `{i : i mod np = j}` | any burst of up to np consecutive data datagrams |
| `rs` | all data of the block (Cauchy Reed–Solomon, GF(256)) | **any** np losses among data and parity |

Units with `nd + np > 255` use several interleaved RS blocks (data i goes to block i mod nb),
each with its own parity.

## Reception

- Payloads are placed at their offsets. Repaired datagrams are counted.
- Datagrams with the wrong length (truncated), an impossible offset or parity index, an
  unknown FEC mode, or fields that disagree with the unit already open are rejected and
  counted as bad. Duplicates and datagrams for already released units are counted and dropped.
- Units are released in frame order once complete. If more than `max_pending` units are
  open, the oldest is released with its gaps. Gaps are zero-filled and reported by
  `Unit::missing()` and `Unit::missing_ranges()`.
- **Block CRCs are required** under loss (`EncoderConfig::block_crc`, the default). The
  zero-filled ranges then fail their CRC. `ewc_bench` refuses `--udp-loss` with `--noblockcrc`.

## Limitations

- FEC works inside one frame unit. It adds no latency beyond the frame, but at 2 Mbps a frame
  is only about 6–7 datagrams, so `np` is 2 for k = 4. A burst longer than the parity of one
  frame cannot be repaired. Cross-frame interleaving would help bursts but adds frames of delay.
  It is not implemented.
- With few datagrams per frame, `ceil()` makes the real overhead larger than 1/k
  (k = 4 costs about 28% at 2 Mbps, not 25%).
- RS encoding cost grows with `nd × np × pay`. It is small at 2 Mbps. For lossless frames
  (~230 kB, nd ≈ 170) it was not measured as part of a real-time budget.
- No retransmission and no congestion control. The receiver-to-encoder feedback path is
  modelled in `ewc_bench` (`--feedback D`) but not implemented as a protocol.
- The frame-order map in `Reassembler` does not handle 32-bit sequence wrap.
