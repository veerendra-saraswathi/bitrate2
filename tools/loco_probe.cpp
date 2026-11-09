// loco_probe — standalone check of the LOCO-I coder: exactness / error bound, size, speed.
#include "../src/loco.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>
int main(int argc, char** argv) {
    if (argc < 6) { fprintf(stderr, "loco_probe in.u16 W H near stripe_rows [nframes]\n"); return 2; }
    const int W = atoi(argv[2]), H = atoi(argv[3]), near = atoi(argv[4]), sr = atoi(argv[5]);
    const int nf = argc > 6 ? atoi(argv[6]) : 1 << 30;
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<uint16_t> img(size_t(W) * H), rec(img.size()), dec(img.size());
    ewc::LocoParams p; p.maxval = 16383; p.near = near;
    double tot = 0, te = 0, td = 0; int k = 0, maxerr = 0; bool rows_ok = true;
    while (k < nf && f.read(reinterpret_cast<char*>(img.data()), std::streamsize(img.size() * 2))) {
        std::vector<std::vector<uint8_t>> pay;
        auto t0 = std::chrono::steady_clock::now();
        for (int y = 0; y < H; y += sr) {
            pay.emplace_back();
            ewc::loco_encode_stripe(img.data() + size_t(y) * W, W, std::min(sr, H - y), p, pay.back(), rec.data() + size_t(y) * W);
        }
        auto t1 = std::chrono::steady_clock::now();
        int s = 0;
        for (int y = 0; y < H; y += sr, ++s) {
            int ok = ewc::loco_decode_stripe(pay[s].data(), pay[s].size(), pay[s].size(), W, std::min(sr, H - y), p, dec.data() + size_t(y) * W);
            rows_ok &= ok == std::min(sr, H - y);
            tot += double(pay[s].size());
        }
        auto t2 = std::chrono::steady_clock::now();
        te += std::chrono::duration<double, std::milli>(t1 - t0).count();
        td += std::chrono::duration<double, std::milli>(t2 - t1).count();
        for (size_t i = 0; i < img.size(); ++i) {
            if (dec[i] != rec[i]) { printf("MISMATCH enc/dec recon at frame %d px %zu\n", k, i); return 1; }
            maxerr = std::max(maxerr, std::abs(int(dec[i]) - int(img[i])));
        }
        ++k;
    }
    printf("frames %d near %d stripe %d: kbps@30 %.0f bpp %.3f maxerr %d rows_ok %d enc %.2f ms dec %.2f ms\n", k, near, sr,
           tot * 8 * 30 / k / 1000, tot * 8 / k / (double(W) * H), maxerr, int(rows_ok), te / k, td / k);
    return maxerr > near;
}
