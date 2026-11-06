// ewc — Embedded Wavelet Codec for high-bit-depth IR / visible imagery.
// Public C++17 API. Pure standard library, no external dependencies.
//
//  * 8..16-bit monochrome input (uint16 container, row-major)
//  * byte-exact rate control: per-frame budget with a leaky-bucket (VBV) buffer; with the
//    default vbv_frames_q8 = 256 no frame exceeds frame_bytes (strict CBR)
//  * input samples must lie in [0, 2^bit_depth - 1]; out-of-range input is rejected
//  * lossless / near-lossless profiles (LOCO-I predictive coding, the JPEG-LS algorithm):
//    bit-exact, or every sample within +-near of the original
//  * optional embedded lossless wavelet profile (reversible 5/3; any prefix decodes lossy)
//  * optional closed-loop temporal prediction (P frames, OBMC motion compensation)
//  * error resilience: CRC-protected frame/group headers, per-256-byte block CRCs,
//    resynchronisation markers, embedded group streams that degrade gracefully,
//    decoder-side concealment
#pragma once
#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace ewc {

struct EncoderConfig {
    int width = 640;
    int height = 512;
    int bit_depth = 14;          // 8..16
    bool lossless = false;       // true: bit-exact (LOCO-I, near = 0), budget ignored
    int near = 0;                // > 0 with lossless: near-lossless, |error| <= near
    bool embedded_lossless = false;  // with lossless: reversible 5/3 + bit planes instead of LOCO-I
                                 // (4-5x slower; a truncated stream still decodes as a lossy image)
    bool loco_ac = true;         // LOCO-I: adaptive range coder instead of JPEG-LS Golomb codes
    bool loco_auto_stripes = true; // LOCO-I: double stripe height while stripes average < 3 KB
    int stripe_rows = 64;        // LOCO-I: rows per independently coded stripe
    int wavelet = 1;             // 1 = CDF 9/7 (integer lifting), 0 = LeGall 5/3
    int levels = 6;              // decomposition levels (image padded to multiple of 2^levels)
    int groups = 8;              // independent coefficient-tree groups (stripes); <= LL rows
    uint32_t frame_bytes = 8333; // per-frame budget incl. all headers (2 Mbps @ 30 fps)
    int precision_shift = -1;    // lossy wavelet: scale samples by 2^n before the integer transform
                                 // (-1 = auto: 18 - bit_depth, max 8); removes the lifting rounding floor
    int frac_bits = 2;           // sub-integer precision of weighted coefficients (lossy)
    int cp_interval = 32;        // symbols between rate-allocation checkpoints
    bool block_crc = true;       // CRC-16 every 256 payload bytes (finer loss localisation)
    int redundancy = 24;         // I frames: bytes of each group's embedded stream repeated in a
                                 // trailer chunk (coarse fallback when a group is lost); 0 = off
    int threads = 1;             // worker threads for group coding
    // tiled profile (lossy, intra-only): 0 = off (whole-frame wavelet). 16/32/64/128/256 =
    // independent tile x tile blocks, each with its own wavelet, coefficient trees and coder;
    // the frame budget is shared across tiles by rate-distortion allocation.
    int tile = 0;
    int tile_levels = 4;         // wavelet levels inside a tile (tile must be a multiple of 2^levels)
    int tile_redundancy = 0;     // tiled: bytes of each tile stream repeated in a trailer (coarse
                                 // fallback for lost / early-damaged tiles); 0 = off
    bool tile_compact_index = true; // tiled: varint tile index (nmax | nsym | len, ~3-5 B/tile)
                                 // instead of fixed 7 B/tile; decoder reads both
    // temporal prediction
    int gop = 1;                 // 1 = intra only; N = one I frame every N frames (P in between)
    int i_frame_scale_q8 = 256;  // I-frame budget multiplier (Q8), limited by the VBV; following P
                                 // frames repay the excess
    int vbv_frames_q8 = 256;     // encoder buffer in frame periods (Q8). 256 = strict per-frame CBR
                                 // (no frame exceeds frame_bytes); 512 bounds queueing to 2 frames
    int scene_cut_q8 = 768;      // P frame coded as I when inter/intra residual-energy ratio > value/256 (0 = off)
    int mv_block = 32;           // motion block size (OBMC window = 2x block)
    int mv_range = 24;           // integer search range around the global motion (pixels)
    int global_range = 48;       // global-motion search range (pixels)
    int mv_lambda = 32;          // motion rate penalty (SAD units per quarter-pel of MV difference)
};

// Proof-of-concept profile (RCI Scope of Work, PoC phase): 640x512 14-bit infrared, fixed 2 Mbps,
// frame-level (intra-only) coding, strict per-frame CBR, block CRCs, independent 128x128 tiles
// (5 wavelet levels each). Chosen on measured quality, robustness to errors in the stored
// bitstream, and encoder/decoder speed (docs/POC_REPORT.md). fps sets the per-frame byte budget.
inline EncoderConfig poc_config(double fps = 30.0, double kbps = 2000.0) {
    EncoderConfig c;
    c.width = 640; c.height = 512; c.bit_depth = 14;
    c.gop = 1;                       // intra-only: no error propagation, lowest latency
    c.frame_bytes = uint32_t(kbps * 1000.0 / 8.0 / fps);
    c.vbv_frames_q8 = 256;           // strict CBR: no frame above frame_bytes
    c.levels = 6; c.groups = 8; c.wavelet = 1;
    c.block_crc = true;
    c.redundancy = 64;               // used by the whole-frame path only (tile = 0)
    c.tile = 128; c.tile_levels = 5;
    c.threads = 2;
    return c;
}

struct FrameInfo {
    uint32_t sequence = 0;
    uint64_t timestamp_us = 0;
    int bit_depth = 0;
    bool is_intra = true;
    uint32_t bytes = 0;
    int groups_total = 0;
    int groups_ok = 0;            // fully intact
    int groups_partial = 0;       // truncated by a detected error, prefix used
    int groups_lost = 0;
    bool header_ok = true;        // header found where the previous frame ended (no bytes skipped)
    uint32_t skipped_bytes = 0;   // bytes skipped before this frame's header (resynchronisation)
    bool concealed = false;       // whole frame concealed (P frame without any reference)
    bool lossless_complete = false;
    // Temporal-prediction integrity (P frames; always true for I / LOCO frames):
    bool motion_ok = true;        // motion field received intact (false: zero vectors were used)
    bool ref_gap = false;         // the reference (sequence - 1) was never decoded: predicted
                                  // from an older frame
    bool ref_ok = true;           // predicted from sequence - 1, which was itself reliable
    uint32_t ref_sequence = 0;    // sequence of the frame actually used as reference
    // Frame decoded from complete, CRC-verified data and (for P frames) a reliable reference.
    // When false the output is a best-effort / concealed image.
    bool reliable() const {
        return groups_lost == 0 && groups_partial == 0 && !concealed && motion_ok && ref_ok;
    }
    std::vector<uint8_t> metadata;
};

class Encoder {
public:
    explicit Encoder(const EncoderConfig& cfg);
    ~Encoder();
    // Encode one frame (width*height uint16 samples). Appends the self-contained frame unit
    // to `out`. `metadata` is an opaque application blob (<= 65535 bytes) stored per frame.
    void encode(const uint16_t* frame, uint64_t timestamp_us, std::vector<uint8_t>& out,
                const std::vector<uint8_t>& metadata = {});
    // Encoder-side reconstruction of the last frame (identical to the decoder's when the
    // stream is received intact).
    const std::vector<uint16_t>& last_reconstruction() const;
    // Code the next frame as an I frame (e.g. on receiver feedback after a loss). The GOP
    // restarts from that frame. No effect in the LOCO (all-intra) profile.
    void request_intra();
    const EncoderConfig& config() const;
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

class Decoder {
public:
    Decoder();
    ~Decoder();
    // Decode the next frame from a byte stream. `pos` is advanced past the frame. Corrupted
    // regions are skipped by scanning for the next valid frame header. Returns false at end
    // of stream. `out` receives width*height samples. Damaged frames and P frames whose
    // reference is missing or unreliable are still decoded (best effort) and reported
    // through FrameInfo (see FrameInfo::reliable()); errors propagate through P frames until
    // the next I frame, and every affected frame is flagged.
    bool decode_next(const uint8_t* data, size_t size, size_t& pos, std::vector<uint16_t>& out,
                     FrameInfo& info, int& width, int& height);
    void set_threads(int n);
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

}  // namespace ewc
