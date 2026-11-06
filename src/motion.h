// ewc — block motion estimation and overlapped block motion compensation (OBMC).
// All arithmetic is integer so encoder and decoder predictions are bit-identical.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ewc {

struct MotionField {
    int bs = 16, nbx = 0, nby = 0;
    std::vector<int16_t> mvx, mvy;     // quarter-pel units, raster block order
    void init(int W, int H, int bs_) {
        bs = bs_; nbx = (W + bs - 1) / bs; nby = (H + bs - 1) / bs;
        mvx.assign(size_t(nbx) * nby, 0); mvy.assign(size_t(nbx) * nby, 0);
    }
};

struct MotionParams {
    int bs = 16;
    int range = 24;          // per-block search radius (pixels) around the predictor
    int grange = 48;         // global search radius (pixels)
    int lambda = 4;          // SAD units per quarter-pel of MV-difference (rate proxy)
};

// Reference plane with an edge-replicated margin (M pixels on every side) so that motion
// compensation can read outside the frame without coordinate clamping.
struct RefPlane {
    int W = 0, H = 0, M = 0, S = 0;    // frame size, margin, row stride
    std::vector<int32_t> buf;
    const int32_t* p = nullptr;        // pixel (0,0)
    void build(const int32_t* src, int W, int H, int margin);
};

// cur: W*H int32 (padded frame). Fills mf. Deterministic for any thread count.
void estimate_motion(const int32_t* cur, const RefPlane& ref, const MotionParams& mp, MotionField& mf,
                     int threads = 1);
// OBMC prediction into pred (W*H). Raised-cosine windows of size 2*bs, weights sum to 1 exactly.
void compensate_obmc(const RefPlane& ref, const MotionField& mf, int32_t* pred);

void encode_motion(const MotionField& mf, std::vector<uint8_t>& out);
bool decode_motion(const uint8_t* data, size_t size, MotionField& mf);

}  // namespace ewc
