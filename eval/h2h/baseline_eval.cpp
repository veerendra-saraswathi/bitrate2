// Evaluate the ORIGINAL repository codec (bitrate-main, unmodified sources) on 14-bit data.
// Mode "shipped": exactly what encoder_main.cpp does for >8-bit input (pixel >> 8).
// Mode "agc"    : favourable pre-conversion: per-frame linear min/max scaling to 8 bits
//                 (min/max sent for free), then the repo codec, then inverse scaling.
// For each frame, the smallest QP (1..255) whose bitstream fits the byte budget is used.
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

std::vector<uint8_t> encode_frame(const std::vector<uint8_t>& pixels, int width, int height, int qp, int levels, int coder_mode);
std::vector<uint8_t> decode_frame(const std::vector<uint8_t>& bitstream);

int main(int argc, char** argv) {
    if (argc < 7) { fprintf(stderr, "baseline_eval in.u16 W H N budget mode(shipped|agc) [out.u16]\n"); return 2; }
    const int W = atoi(argv[2]), H = atoi(argv[3]), N = atoi(argv[4]), budget = atoi(argv[5]);
    const std::string mode = argv[6];
    std::ifstream f(argv[1], std::ios::binary);
    std::ofstream o;
    if (argc > 7) o.open(argv[7], std::ios::binary);
    const size_t n = size_t(W) * H;
    std::vector<uint16_t> fr(n), rec(n);
    double psum = 0; int fit = 0;
    for (int k = 0; k < N && f.read(reinterpret_cast<char*>(fr.data()), std::streamsize(n * 2)); ++k) {
        std::vector<uint8_t> px(n);
        int lo = 65535, hi = 0;
        for (auto v : fr) { lo = std::min(lo, int(v)); hi = std::max(hi, int(v)); }
        const double sc = hi > lo ? 255.0 / (hi - lo) : 1.0;
        for (size_t i = 0; i < n; ++i)
            px[i] = mode == "shipped" ? uint8_t(fr[i] >> 8) : uint8_t(std::lround((fr[i] - lo) * sc));
        int qp = 1;
        std::vector<uint8_t> bs;
        for (qp = 1; qp <= 255; ++qp) {
            bs = encode_frame(px, W, H, qp, 1, 1);
            if (int(bs.size()) <= budget) break;
        }
        if (qp > 255) qp = 255;
        auto dec = decode_frame(bs);
        double mse = 0;
        for (size_t i = 0; i < n; ++i) {
            double r = mode == "shipped" ? double(dec[i]) * 256.0 : lo + dec[i] / sc;
            rec[i] = uint16_t(std::min(16383.0, std::max(0.0, std::round(r))));
            double d = double(rec[i]) - fr[i];
            mse += d * d;
        }
        mse /= double(n);
        const double ps = 10 * std::log10(16383.0 * 16383.0 / std::max(mse, 1e-9));
        psum += ps;
        fit += int(bs.size()) <= budget;
        printf("frame %d qp %d bytes %zu fits %d psnr14 %.3f rmse %.2f\n", k, qp, bs.size(), int(int(bs.size()) <= budget), ps, std::sqrt(mse));
        if (o) o.write(reinterpret_cast<const char*>(rec.data()), std::streamsize(n * 2));
    }
    printf("SUMMARY psnr_avg %.3f frames_fitting_budget %d\n", psum / N, fit);
    return 0;
}
