// bitrate-main (unmodified library) evaluated at a per-frame byte budget, with an ORACLE rate control:
// for each frame, every (levels 1..6, QP 1..255) whose stream fits the budget is decoded and
// the one with the lowest MSE is kept. This is more favourable than the repo's own
// rate control (a fixed QP looked up from a bpp curve, no per-frame guarantee).
// in: u16 raw. mode: direct (8-bit data in u16), shipped (>>8), agc (per-frame min/max to 8 bit, free side info)
// out: recon u16 + per-frame CSV: frame,levels,qp,bytes,enc_ms,dec_ms,search_ms
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
std::vector<uint8_t> encode_frame(const std::vector<uint8_t>&, int, int, int, int, int);
std::vector<uint8_t> decode_frame(const std::vector<uint8_t>&);
using clk = std::chrono::steady_clock;
static double ms(clk::time_point a, clk::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }
int main(int argc, char** argv) {
    if (argc < 9) { fprintf(stderr, "bm_eval in.u16 W H N budget mode out.u16 out.csv [maxlevels]\n"); return 2; }
    const int W = atoi(argv[2]), H = atoi(argv[3]), N = atoi(argv[4]), budget = atoi(argv[5]);
    const std::string mode = argv[6];
    const int maxL = argc > 9 ? atoi(argv[9]) : 6;
    std::ifstream f(argv[1], std::ios::binary); std::ofstream o(argv[7], std::ios::binary);
    FILE* cs = fopen(argv[8], "w"); fprintf(cs, "frame,levels,qp,bytes,enc_ms,dec_ms,search_ms\n");
    const size_t n = size_t(W) * H;
    std::vector<uint16_t> fr(n), rec(n);
    for (int k = 0; k < N && f.read(reinterpret_cast<char*>(fr.data()), std::streamsize(n * 2)); ++k) {
        std::vector<uint8_t> px(n);
        int lo = 65535, hi = 0;
        for (auto v : fr) { lo = std::min(lo, int(v)); hi = std::max(hi, int(v)); }
        const double sc = hi > lo ? 255.0 / (hi - lo) : 1.0;
        for (size_t i = 0; i < n; ++i)
            px[i] = mode == "shipped" ? uint8_t(fr[i] >> 8) : mode == "agc" ? uint8_t(std::lround((fr[i] - lo) * sc)) : uint8_t(std::min<int>(255, fr[i]));
        auto back = [&](uint8_t d) { return mode == "shipped" ? double(d) * 256.0 : mode == "agc" ? lo + d / sc : double(d); };
        auto t0 = clk::now();
        double best = 1e300; int bL = 1, bQ = 255; std::vector<uint8_t> bbs;
        for (int L = 1; L <= maxL; ++L)                  // exhaustive: every QP 1..255 that fits, keep min MSE
            for (int qp = 1; qp <= 255; ++qp) {
                auto bs = encode_frame(px, W, H, qp, L, 1);
                if (int(bs.size()) > budget) continue;
                auto dec = decode_frame(bs);
                double mse = 0; for (size_t i = 0; i < n; ++i) { double d = std::round(back(dec[i])) - fr[i]; mse += d * d; }
                if (mse < best) { best = mse; bL = L; bQ = qp; bbs = bs; }
            }
        auto t1 = clk::now();
        if (bbs.empty()) { bL = 1; bQ = 255; bbs = encode_frame(px, W, H, 255, 1, 1); }
        std::vector<double> te, td; std::vector<uint8_t> dec;
        for (int r = 0; r < 5; ++r) {
            auto a = clk::now(); auto bs = encode_frame(px, W, H, bQ, bL, 1); auto b = clk::now();
            dec = decode_frame(bs); auto c = clk::now(); te.push_back(ms(a, b)); td.push_back(ms(b, c));
        }
        std::sort(te.begin(), te.end()); std::sort(td.begin(), td.end());
        const double top = mode == "direct" ? 255.0 : 16383.0;
        for (size_t i = 0; i < n; ++i) rec[i] = uint16_t(std::min(top, std::max(0.0, std::round(back(dec[i])))));
        o.write(reinterpret_cast<const char*>(rec.data()), std::streamsize(n * 2));
        fprintf(cs, "%d,%d,%d,%zu,%.3f,%.3f,%.1f\n", k, bL, bQ, bbs.size(), te[2], td[2], ms(t0, t1));
    }
    fclose(cs); return 0;
}
