// ewc — LOCO-I predictive coder (the algorithm of JPEG-LS, ITU-T T.87) for the lossless and
// near-lossless profiles. Self-contained re-implementation; not bitstream-compatible with
// JPEG-LS files (own edge rules and framing), same modelling: MED prediction, 365 gradient
// contexts with bias cancellation, adaptive limited-length Golomb codes and run mode.
//
// A frame is split into horizontal stripes coded independently (own contexts, a virtual
// constant row above the first row) so that stripes can be coded in parallel and a damaged
// stripe does not affect the others.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ewc {

struct LocoParams {
    int maxval = 16383;   // 2^bit_depth - 1
    int near = 0;         // 0 = lossless; otherwise |reconstruction error| <= near
    bool ac = false;      // entropy back-end: false = JPEG-LS Golomb codes, true = adaptive range coder
};

// Codes rows [0, nrows) of `src` (stride W). Appends the payload to `out`. `recon` receives the
// decoder's reconstruction (identical to src when near == 0). Returns payload size.
size_t loco_encode_stripe(const uint16_t* src, int W, int nrows, const LocoParams& p, std::vector<uint8_t>& out,
                          uint16_t* recon);

// Decodes a stripe. Only the first `valid` bytes of `data` are trusted (the rest may be
// corrupt). Rows that were fully decoded from trusted bytes are counted in the return value;
// the remaining rows of `out` are filled anyway (garbage-safe, values clamped to maxval).
int loco_decode_stripe(const uint8_t* data, size_t size, size_t valid, int W, int nrows, const LocoParams& p,
                       uint16_t* out);

}  // namespace ewc
