// ewc — integer lifting wavelet transforms (deterministic, exactly invertible).
//   W53 : JPEG 2000 reversible LeGall 5/3 (used for the lossless profile)
//   W97 : CDF 9/7 implemented as integer-to-integer lifting in Q14 fixed point
//         (bit-exact on every platform, required for closed-loop temporal prediction)
// Layout: Mallat (LL top-left). Width/height of the transformed region must be multiples
// of 2^levels (the encoder pads by edge replication).
#pragma once
#include <cstdint>
#include <vector>

namespace ewc {

enum class Wavelet : uint8_t { W53 = 0, W97 = 1 };

void dwt_forward(int32_t* img, int W, int H, int levels, Wavelet w, std::vector<int32_t>& tmp);
void dwt_inverse(int32_t* img, int W, int H, int levels, Wavelet w, std::vector<int32_t>& tmp);

// L2 norm of the synthesis basis function of a 1-D subband: level 1..L, hi=0 lowpass chain
// (only meaningful for the coarsest level), hi=1 highpass at that level.
double synth_norm_1d(Wavelet w, int level, int hi);

}  // namespace ewc
