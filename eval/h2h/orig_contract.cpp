// Head-to-head harness: ORIGINAL bitrate-main codec (unmodified library) on the contract frames.
// For each levels (1..6) and QP (1..8): size, Mbps@30, PSNR, max_err, exact, median encode ms
// (1 warm-up + 11 timed runs, as in COMPARISON_CONTRACT.md).
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <vector>
std::vector<uint8_t> encode_frame(const std::vector<uint8_t>& pixels, int width, int height, int qp, int levels, int coder_mode);
std::vector<uint8_t> decode_frame(const std::vector<uint8_t>& bitstream);
int main(int argc, char** argv) {
    const int W = 640, H = 512; std::vector<uint8_t> px(W * H);
    std::ifstream(argv[1], std::ios::binary).read(reinterpret_cast<char*>(px.data()), W * H);
    printf("levels,qp,bytes,mbps30,psnr,maxerr,exact,enc_ms_med\n");
    for (int L = 1; L <= 6; ++L) for (int qp = 1; qp <= 8; ++qp) {
        std::vector<double> t; std::vector<uint8_t> bs;
        for (int r = 0; r < 12; ++r) {
            auto a = std::chrono::steady_clock::now(); bs = encode_frame(px, W, H, qp, L, 1); auto b = std::chrono::steady_clock::now();
            if (r) t.push_back(std::chrono::duration<double, std::milli>(b - a).count());
        }
        std::sort(t.begin(), t.end());
        auto rec = decode_frame(bs);
        double se = 0; int me = 0;
        for (int i = 0; i < W * H; ++i) { int d = int(rec[i]) - int(px[i]); se += d * d; me = std::max(me, std::abs(d)); }
        double mse = se / (W * H);
        printf("%d,%d,%zu,%.3f,%.3f,%d,%d,%.3f\n", L, qp, bs.size(), bs.size() * 8.0 * 30 / 1e6, mse > 0 ? 10 * log10(255.0 * 255 / mse) : 199.0, me, int(me == 0), t[t.size() / 2]);
    }
}
