// Experiment: two-layer "bounded error at fixed rate" coding, built only on the public ewc API.
//   layer 1 (base):        ewc lossy wavelet unit (tiled PoC profile) at B1 bytes
//   layer 2 (enhancement): ewc LOCO near-lossless unit (NEAR = k) of the residual
//                          r = x - base + 2^bd, coded at bit depth bd+1
// Decoded frame = base + r - 2^bd, so |x - decoded| <= k whenever both layers arrive.
// If the enhancement unit is lost, the base layer alone is still a valid image.
// Per frame the encoder tries a few base budgets around the previous frame's best split and
// keeps the smallest total; the frame meets `budget` or is reported as over budget.
//
// usage: ewc_layered in.u16 W H N bd budget_bytes k out_rec.u16 out.csv [stream.ewl]
#include "ewc.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <vector>

using clk = std::chrono::steady_clock;
static double ms(clk::time_point a, clk::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }

struct Trial { uint32_t base_bytes = 0; std::vector<uint8_t> base, enh; std::vector<uint16_t> rec; };

int main(int argc, char** argv) {
    if (argc < 10) { fprintf(stderr, "usage: ewc_layered in.u16 W H N bd budget k out_rec.u16 out.csv [stream]\n"); return 2; }
    const int W = atoi(argv[2]), H = atoi(argv[3]), N = atoi(argv[4]), bd = atoi(argv[5]);
    const uint32_t budget = uint32_t(atoi(argv[6]));
    const int k = atoi(argv[7]);
    const int rbd_min = getenv("EWL_RBD") ? atoi(getenv("EWL_RBD")) : 15;
    std::ifstream in(argv[1], std::ios::binary);
    std::ofstream rec_out(argv[8], std::ios::binary);
    FILE* csv = fopen(argv[9], "w");
    std::ofstream stream_out;
    if (argc > 10) stream_out.open(argv[10], std::ios::binary);
    fprintf(csv, "frame,base_bytes,enh_bytes,total,fits,maxerr,enc_ms,dec_ms,trials\n");
    const size_t n = size_t(W) * H;

    // residual coder: LOCO near-lossless; the residual is shifted by its minimum and coded at
    // the smallest bit depth that holds its range (offset + depth go in the unit's metadata)
    std::vector<std::unique_ptr<ewc::Encoder>> rencs(17);
    auto renc_for = [&](int rbd) -> ewc::Encoder& {
        if (!rencs[rbd]) {
            ewc::EncoderConfig rc;
            rc.width = W; rc.height = H; rc.bit_depth = rbd; rc.lossless = true; rc.near = k; rc.threads = 2;
            rencs[rbd] = std::make_unique<ewc::Encoder>(rc);
        }
        return *rencs[rbd];
    };

    double frac = 0.55;                    // base share of the budget, adapted frame to frame
    std::vector<uint16_t> x(n), res(n), out(n);
    for (int f = 0; f < N && in.read(reinterpret_cast<char*>(x.data()), std::streamsize(n * 2)); ++f) {
        const auto t0 = clk::now();
        // candidates: no base layer (pure near-lossless) + three splits around the last best
        std::vector<double> fracs = {0.0, frac - 0.12, frac, frac + 0.12};
        if (frac <= 0.0) fracs = {0.0, 0.3, 0.5};
        Trial best; bool have = false; int trials = 0;
        for (double fr : fracs) {
            Trial t;
            if (fr <= 0.0) {                       // pure near-lossless: no base unit
                t.base_bytes = 0; t.rec.assign(n, 0);
                renc_for(bd).encode(x.data(), uint64_t(f) * 33333, t.enh, {uint8_t('R'), uint8_t(k), 0, 0, 0, 0});
                ++trials;
                if (!have || t.enh.size() < best.base.size() + best.enh.size()) { best = std::move(t); have = true; }
                continue;
            }
            fr = std::min(0.95, std::max(0.1, fr));
            t.base_bytes = uint32_t(budget * fr);
            ewc::EncoderConfig bc = ewc::poc_config(30.0);
            bc.bit_depth = bd; bc.width = W; bc.height = H; bc.frame_bytes = t.base_bytes;
            if (getenv("EWL_TILE")) bc.tile = atoi(getenv("EWL_TILE"));
            ewc::Encoder benc(bc);
            benc.encode(x.data(), uint64_t(f) * 33333, t.base);
            ewc::Decoder bdec; size_t pos = 0; ewc::FrameInfo fi; int w, h;
            bdec.decode_next(t.base.data(), t.base.size(), pos, t.rec, fi, w, h);
            int lo = 1 << 30, hi = -(1 << 30);
            for (size_t i = 0; i < n; ++i) { const int d = int(x[i]) - int(t.rec[i]); lo = std::min(lo, d); hi = std::max(hi, d); }
            // centre the residual in a wide declared range: LOCO thresholds and prediction clamping
            // behave badly when values sit at the edge of a narrow range (measured)
            int rbd = 2; while ((1 << (rbd - 1)) <= std::max(-lo, hi) + 1 && rbd < 16) ++rbd;
            rbd = std::max(rbd, rbd_min);
            const int ctr = 1 << (rbd - 1);
            for (size_t i = 0; i < n; ++i) res[i] = uint16_t(int(x[i]) - int(t.rec[i]) + ctr);
            const uint32_t ul = uint32_t(-ctr);
            renc_for(rbd).encode(res.data(), uint64_t(f) * 33333, t.enh,
                                 {uint8_t('R'), uint8_t(k), uint8_t(ul), uint8_t(ul >> 8), uint8_t(ul >> 16), uint8_t(ul >> 24)});
            ++trials;
            if (!have || t.base.size() + t.enh.size() < best.base.size() + best.enh.size()) { best = std::move(t); have = true; }
        }
        frac = double(best.base_bytes) / budget;
        const auto t1 = clk::now();
        // decoder side: both layers
        const auto t2 = clk::now();
        ewc::Decoder d1, d2; size_t p1 = 0, p2 = 0; ewc::FrameInfo i1, i2; int w, h;
        std::vector<uint16_t> b, r;
        if (best.base.empty()) b.assign(n, 0);
        else d1.decode_next(best.base.data(), best.base.size(), p1, b, i1, w, h);
        d2.decode_next(best.enh.data(), best.enh.size(), p2, r, i2, w, h);
        const auto& md = i2.metadata;
        const int lo = md.size() >= 6 ? int(uint32_t(md[2]) | uint32_t(md[3]) << 8 | uint32_t(md[4]) << 16 | uint32_t(md[5]) << 24) : 0;
        int maxerr = 0;
        for (size_t i = 0; i < n; ++i) {
            const int v = std::min((1 << bd) - 1, std::max(0, int(b[i]) + int(r[i]) + lo));
            out[i] = uint16_t(v); maxerr = std::max(maxerr, std::abs(v - int(x[i])));
        }
        const auto t3 = clk::now();
        rec_out.write(reinterpret_cast<const char*>(out.data()), std::streamsize(n * 2));
        if (stream_out) {
            stream_out.write(reinterpret_cast<const char*>(best.base.data()), std::streamsize(best.base.size()));
            stream_out.write(reinterpret_cast<const char*>(best.enh.data()), std::streamsize(best.enh.size()));
        }
        const size_t tot = best.base.size() + best.enh.size();
        fprintf(csv, "%d,%zu,%zu,%zu,%d,%d,%.2f,%.2f,%d\n", f, best.base.size(), best.enh.size(), tot, tot <= budget ? 1 : 0,
                maxerr, ms(t0, t1), ms(t2, t3), trials);
    }
    fclose(csv);
    return 0;
}
