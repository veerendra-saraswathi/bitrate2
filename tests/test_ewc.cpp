// ewc regression tests: exactness, rate control, closed-loop consistency, determinism,
// metadata, and decoder robustness against corrupted / truncated / random input.
#include "ewc.h"
#include "../src/dwt.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { ++failures; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

// synthetic IR-like frame: smooth background + hot blobs + edges + noise, bit depth bd
static std::vector<uint16_t> make_frame(int W, int H, int bd, int t, uint32_t seed, double noise = 3.0) {
    std::mt19937 rng(seed);
    std::normal_distribution<double> nd(0, noise);
    std::vector<uint16_t> f(size_t(W) * H);
    const double maxv = double((1 << bd) - 1), base = maxv * 0.4, amp = maxv * 0.1;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            double v = base + amp * std::sin((x + 2.0 * t) * 0.02) * std::cos(y * 0.015);
            double dx = x - (W * 0.3 + 3 * t), dy = y - H * 0.5;
            v += amp * 2 * std::exp(-(dx * dx + dy * dy) / 200.0);
            if (((x + t) / 37) % 2 == 0 && y > H / 3 && y < H / 2) v += amp * 0.5;
            v += nd(rng) * (maxv / 16383.0 > 1 ? 1 : std::max(maxv / 16383.0, 0.05));
            f[size_t(y) * W + x] = uint16_t(std::min(std::max(v, 0.0), maxv));
        }
    return f;
}

static void test_dwt() {
    std::mt19937 rng(7);
    for (int wv = 0; wv < 2; ++wv)
        for (int L = 1; L <= 6; ++L) {
            const int W = 64 * 5, H = 64 * 3;
            std::vector<int32_t> a(size_t(W) * H), b, tmp;
            for (auto& v : a) v = int32_t(rng() % 65536) - 32768;
            b = a;
            ewc::dwt_forward(b.data(), W, H, L, ewc::Wavelet(wv), tmp);
            ewc::dwt_inverse(b.data(), W, H, L, ewc::Wavelet(wv), tmp);
            CHECK(a == b, "dwt round trip wavelet=%d levels=%d", wv, L);
        }
    printf("dwt round-trip: done\n");
}

static std::vector<std::vector<uint16_t>> decode_all(const std::vector<uint8_t>& bs, std::vector<ewc::FrameInfo>* infos = nullptr) {
    ewc::Decoder dec;
    size_t pos = 0;
    std::vector<std::vector<uint16_t>> out;
    std::vector<uint16_t> f;
    ewc::FrameInfo info;
    int w, h;
    while (dec.decode_next(bs.data(), bs.size(), pos, f, info, w, h)) {
        out.push_back(f);
        if (infos) infos->push_back(info);
    }
    return out;
}

static void test_lossless() {
    const int sizes[][2] = {{640, 512}, {641, 481}, {100, 37}, {64, 64}};
    for (int emb = 0; emb < 2; ++emb)
    for (int bd : {8, 10, 12, 14, 16})
        for (auto& s : sizes) {
            ewc::EncoderConfig cfg;
            cfg.width = s[0]; cfg.height = s[1]; cfg.bit_depth = bd; cfg.lossless = true; cfg.embedded_lossless = emb;
            cfg.threads = 1 + (bd & 1);
            cfg.levels = (s[0] < 128 || s[1] < 128) ? 3 : 6;
            ewc::Encoder enc(cfg);
            std::vector<uint8_t> bs;
            std::vector<std::vector<uint16_t>> in;
            for (int t = 0; t < 2; ++t) { in.push_back(make_frame(s[0], s[1], bd, t, 11 + t, 20.0)); enc.encode(in.back().data(), t, bs); }
            std::vector<ewc::FrameInfo> inf;
            auto out = decode_all(bs, &inf);
            CHECK(out.size() == in.size(), "lossless frame count bd=%d %dx%d", bd, s[0], s[1]);
            for (size_t k = 0; k < std::min(out.size(), in.size()); ++k) {
                CHECK(out[k] == in[k], "lossless not exact bd=%d %dx%d frame %zu", bd, s[0], s[1], k);
                CHECK(inf[k].lossless_complete, "lossless flag bd=%d", bd);
            }
        }
    // extreme values (all-zero, all-max, checkerboard of extremes)
    for (int emb = 0; emb < 2; ++emb)
    for (int bd : {14, 16}) {
        ewc::EncoderConfig cfg;
        cfg.width = 128; cfg.height = 128; cfg.bit_depth = bd; cfg.lossless = true; cfg.levels = 5;
        cfg.embedded_lossless = emb; cfg.stripe_rows = 16;
        ewc::Encoder enc(cfg);
        std::vector<uint8_t> bs;
        std::vector<std::vector<uint16_t>> in(3, std::vector<uint16_t>(128 * 128));
        const uint16_t mx = uint16_t((1u << bd) - 1);
        std::fill(in[1].begin(), in[1].end(), mx);
        for (int i = 0; i < 128 * 128; ++i) in[2][i] = ((i / 128 + i) & 1) ? mx : 0;
        for (int t = 0; t < 3; ++t) enc.encode(in[t].data(), t, bs);
        auto out = decode_all(bs);
        for (int t = 0; t < 3; ++t) CHECK(t < int(out.size()) && out[t] == in[t], "lossless extreme pattern %d bd=%d", t, bd);
    }
    printf("lossless: done\n");
}

// near-lossless: every sample within +-near; encoder reconstruction == decoder output
static void test_near_lossless() {
    for (int bd : {12, 14, 16})
        for (int near : {1, 3, 10}) {
            ewc::EncoderConfig cfg;
            cfg.width = 333; cfg.height = 200; cfg.bit_depth = bd; cfg.lossless = true; cfg.near = near; cfg.stripe_rows = 48;
            ewc::Encoder enc(cfg);
            std::vector<uint8_t> bs;
            std::vector<std::vector<uint16_t>> in, rec;
            for (int t = 0; t < 2; ++t) {
                in.push_back(make_frame(333, 200, bd, t, 21 + t, 30.0));
                enc.encode(in.back().data(), t, bs);
                rec.push_back(enc.last_reconstruction());
            }
            auto out = decode_all(bs);
            CHECK(out.size() == 2, "near-lossless frame count");
            for (size_t k = 0; k < out.size() && k < 2; ++k) {
                int me = 0;
                for (size_t i = 0; i < in[k].size(); ++i) me = std::max(me, std::abs(int(out[k][i]) - int(in[k][i])));
                CHECK(me <= near, "near-lossless bound bd=%d near=%d maxerr=%d", bd, near, me);
                CHECK(out[k] == rec[k], "near-lossless encoder/decoder reconstruction differ bd=%d near=%d", bd, near);
            }
        }
    printf("near-lossless: done\n");
}

// LOCO stream damage: no crash, intact stripes still exact, damage reported
static void test_loco_robustness() {
    ewc::EncoderConfig cfg;
    cfg.width = 256; cfg.height = 256; cfg.lossless = true; cfg.stripe_rows = 32;
    ewc::Encoder enc(cfg);
    std::vector<uint8_t> bs;
    std::vector<std::vector<uint16_t>> in;
    for (int t = 0; t < 4; ++t) { in.push_back(make_frame(256, 256, 14, t, 5 + t, 10.0)); enc.encode(in.back().data(), t, bs); }
    std::mt19937 rng(3);
    int detected = 0, trials = 0;
    for (int rep = 0; rep < 60; ++rep) {
        std::vector<uint8_t> c = bs;
        const size_t at = 200 + rng() % (c.size() - 400);
        if (rep % 3 == 0) c[at] ^= uint8_t(1u << (rng() % 8));
        else if (rep % 3 == 1) c.erase(c.begin() + at, c.begin() + at + 1 + rng() % 500);
        else c.resize(at);
        std::vector<ewc::FrameInfo> inf;
        auto out = decode_all(c, &inf);
        CHECK(out.size() <= in.size(), "loco robust: too many frames");
        for (size_t k = 0; k < out.size(); ++k) {
            CHECK(out[k].size() == in[0].size(), "loco robust: frame size");
            if (inf[k].lossless_complete) CHECK(out[k] == in[inf[k].sequence], "loco robust: frame flagged complete but differs");
            else ++detected;
        }
        ++trials;
    }
    printf("loco robustness: %d damaged streams, %d damaged frames detected, no false 'complete' flags\n", trials, detected);
}

static void test_cbr_and_closed_loop() {
    for (int gop : {1, 8}) {
        ewc::EncoderConfig cfg;
        cfg.width = 640; cfg.height = 512; cfg.bit_depth = 14; cfg.frame_bytes = 8333; cfg.gop = gop;
        cfg.mv_block = 32;
        ewc::Encoder enc(cfg);
        std::vector<uint8_t> bs;
        std::vector<std::vector<uint16_t>> recs;
        std::vector<size_t> sizes;
        for (int t = 0; t < 10; ++t) {
            auto f = make_frame(640, 512, 14, t, 100 + t);
            size_t b = bs.size();
            enc.encode(f.data(), t, bs);
            sizes.push_back(bs.size() - b);
            if (gop > 1) recs.push_back(enc.last_reconstruction());
        }
        for (size_t k = 0; k < sizes.size(); ++k) {
            CHECK(sizes[k] <= 8333, "frame %zu exceeds budget: %zu", k, sizes[k]);
            CHECK(sizes[k] >= 8333 * 95 / 100, "frame %zu underuses budget: %zu", k, sizes[k]);
        }
        auto out = decode_all(bs);
        CHECK(out.size() == 10, "cbr decode count");
        if (gop > 1)
            for (size_t k = 0; k < out.size(); ++k) CHECK(out[k] == recs[k], "closed loop mismatch frame %zu", k);
    }
    printf("cbr + closed loop: done\n");
}

static void test_determinism_and_threads() {
    std::vector<uint8_t> a, b;
    for (int th : {1, 3}) {
        ewc::EncoderConfig cfg;
        cfg.width = 320; cfg.height = 256; cfg.levels = 5; cfg.frame_bytes = 3000; cfg.gop = 4; cfg.threads = th;
        ewc::Encoder enc(cfg);
        auto& bs = th == 1 ? a : b;
        for (int t = 0; t < 6; ++t) { auto f = make_frame(320, 256, 14, t, 5 + t); enc.encode(f.data(), t, bs); }
    }
    CHECK(a == b, "bitstream differs between 1 and 3 threads");
    printf("determinism: done\n");
}

static void test_metadata() {
    ewc::EncoderConfig cfg;
    cfg.width = 128; cfg.height = 128; cfg.levels = 4; cfg.frame_bytes = 2000;
    ewc::Encoder enc(cfg);
    std::vector<uint8_t> bs, md = {'R', 'C', 'I', 0, 1, 2, 255};
    auto f = make_frame(128, 128, 14, 0, 1);
    enc.encode(f.data(), 123456789ull, bs, md);
    std::vector<ewc::FrameInfo> inf;
    decode_all(bs, &inf);
    CHECK(inf.size() == 1 && inf[0].metadata == md && inf[0].timestamp_us == 123456789ull && inf[0].bit_depth == 14,
          "metadata / timestamp round trip");
    printf("metadata: done\n");
}

static void test_robustness() {
    ewc::EncoderConfig cfg;
    cfg.width = 320; cfg.height = 256; cfg.levels = 5; cfg.frame_bytes = 4000; cfg.gop = 5; cfg.mv_block = 16;
    ewc::Encoder enc(cfg);
    std::vector<uint8_t> bs;
    const int nf = 12;
    for (int t = 0; t < nf; ++t) { auto f = make_frame(320, 256, 14, t, 40 + t); enc.encode(f.data(), t, bs); }
    std::mt19937 rng(99);
    int trials = 0;
    for (int mode = 0; mode < 5; ++mode)
        for (int rep = 0; rep < 40; ++rep) {
            std::vector<uint8_t> c = bs;
            switch (mode) {
                case 0: for (int k = 0; k < 1 + int(rng() % 50); ++k) c[rng() % c.size()] ^= uint8_t(1u << (rng() % 8)); break;  // bit flips
                case 1: { size_t p = rng() % c.size(), n = std::min(c.size() - p, size_t(1 + rng() % 3000)); c.erase(c.begin() + p, c.begin() + p + n); break; }
                case 2: { size_t p = rng() % c.size(), n = std::min(c.size() - p, size_t(1 + rng() % 3000)); std::fill(c.begin() + p, c.begin() + p + n, 0); break; }
                case 3: c.resize(rng() % c.size()); break;  // truncation
                case 4: for (auto& v : c) if (rng() % 7 == 0) v = uint8_t(rng()); break;  // heavy garbage
            }
            auto out = decode_all(c);   // must not crash / hang
            for (auto& f : out) CHECK(f.size() == size_t(320 * 256), "robust frame size");
            CHECK(out.size() <= size_t(nf), "robust: more frames than encoded (%zu)", out.size());
            ++trials;
        }
    // pure random input
    for (int rep = 0; rep < 20; ++rep) {
        std::vector<uint8_t> c(1 + rng() % 100000);
        for (auto& v : c) v = uint8_t(rng());
        auto out = decode_all(c);
        CHECK(out.empty(), "random data decoded as frames");
    }
    printf("robustness: %d corrupted streams decoded without failure\n", trials);
}

// P-frame reference tracking: a missing P frame, corrupted motion data and request_intra().
static void test_reference_tracking() {
    ewc::EncoderConfig cfg;
    cfg.width = 320; cfg.height = 256; cfg.levels = 5; cfg.frame_bytes = 4000; cfg.gop = 5; cfg.mv_block = 16;
    cfg.scene_cut_q8 = 0;   // deterministic I/P pattern
    ewc::Encoder enc(cfg);
    std::vector<std::vector<uint8_t>> units;
    for (int t = 0; t < 12; ++t) {
        auto f = make_frame(320, 256, 14, t, 300 + t);
        std::vector<uint8_t> u;
        if (t == 8) enc.request_intra();
        enc.encode(f.data(), uint64_t(t), u);
        units.push_back(u);
    }
    // types: 0 I, 1-4 P, 5 I, 6-7 P, 8 I (requested), 9-11 P
    CHECK(units[8][5] == 0 && units[7][5] == 1 && units[9][5] == 1, "request_intra did not produce an I frame");
    auto decode = [](const std::vector<uint8_t>& bs, std::vector<ewc::FrameInfo>& infos) {
        ewc::Decoder dec;
        size_t pos = 0; std::vector<uint16_t> out; ewc::FrameInfo fi; int w, h;
        infos.clear();
        while (dec.decode_next(bs.data(), bs.size(), pos, out, fi, w, h)) infos.push_back(fi);
    };
    std::vector<ewc::FrameInfo> inf;
    {   // intact stream: every frame reliable
        std::vector<uint8_t> bs;
        for (auto& u : units) bs.insert(bs.end(), u.begin(), u.end());
        decode(bs, inf);
        CHECK(inf.size() == 12, "intact count %zu", inf.size());
        for (auto& i : inf) CHECK(i.reliable(), "intact frame %u not reliable", i.sequence);
    }
    {   // drop P frame 2: frame 3 has a reference gap, frame 4 inherits the error, frame 5 (I) recovers
        std::vector<uint8_t> bs;
        for (size_t k = 0; k < units.size(); ++k) if (k != 2) bs.insert(bs.end(), units[k].begin(), units[k].end());
        decode(bs, inf);
        CHECK(inf.size() == 11, "gap count %zu", inf.size());
        auto by = [&](uint32_t s) -> const ewc::FrameInfo* { for (auto& i : inf) if (i.sequence == s) return &i; return nullptr; };
        CHECK(by(1) && by(1)->reliable(), "frame 1 should be reliable");
        CHECK(by(3) && by(3)->ref_gap && !by(3)->ref_ok && !by(3)->reliable() && by(3)->ref_sequence == 1, "frame 3 gap not flagged");
        CHECK(by(3) && by(3)->header_ok, "frame 3 header itself is intact");
        CHECK(by(4) && !by(4)->ref_gap && !by(4)->ref_ok && !by(4)->reliable(), "frame 4 propagated error not flagged");
        CHECK(by(5) && by(5)->is_intra && by(5)->reliable(), "I frame 5 should recover");
        CHECK(by(6) && by(6)->reliable(), "frame 6 should be reliable after recovery");
    }
    {   // corrupt the motion data of P frame 6: motion_ok false, frame 7 inherits, I frame 8 recovers
        auto u6 = units[6];
        CHECK(u6.size() > 60, "unit 6 too small");
        u6[44 + 4 + 2] ^= 0x5A;   // first motion bytes (after header and mv_len/crc)
        std::vector<uint8_t> bs;
        for (size_t k = 0; k < units.size(); ++k) { const auto& u = k == 6 ? u6 : units[k]; bs.insert(bs.end(), u.begin(), u.end()); }
        decode(bs, inf);
        CHECK(inf.size() == 12, "motion count %zu", inf.size());
        CHECK(!inf[6].motion_ok && !inf[6].reliable(), "corrupt motion not flagged");
        CHECK(!inf[7].ref_ok && !inf[7].reliable(), "frame after corrupt motion not flagged");
        CHECK(inf[8].reliable() && inf[9].reliable(), "requested I frame should recover");
    }
    printf("reference tracking: done\n");
}

// Out-of-range samples are rejected (they would silently break bit-exactness).
static void test_input_range() {
    for (bool ll : {false, true}) {
        ewc::EncoderConfig cfg;
        cfg.width = 64; cfg.height = 64; cfg.levels = 3; cfg.groups = 2; cfg.bit_depth = 14; cfg.lossless = ll;
        ewc::Encoder enc(cfg);
        std::vector<uint16_t> f(64 * 64, 100);
        f[777] = 16384;
        std::vector<uint8_t> bs;
        bool threw = false;
        try { enc.encode(f.data(), 0, bs); } catch (const std::invalid_argument&) { threw = true; }
        CHECK(threw, "out-of-range sample accepted (lossless=%d)", int(ll));
    }
    printf("input range: done\n");
}

// Group payloads above 65535 bytes (high budgets / embedded lossless with few groups) must
// round-trip (u32 group lengths, flag F_LEN32).
static void test_large_groups() {
    auto f = make_frame(640, 512, 14, 0, 77, 40.0);
    for (int mode = 0; mode < 2; ++mode) {
        ewc::EncoderConfig cfg;
        cfg.width = 640; cfg.height = 512; cfg.groups = 2;
        if (mode == 0) { cfg.lossless = true; cfg.embedded_lossless = true; }
        else cfg.frame_bytes = 200000;
        ewc::Encoder enc(cfg);
        std::vector<uint8_t> bs;
        enc.encode(f.data(), 0, bs);
        std::vector<ewc::FrameInfo> inf;
        auto out = decode_all(bs, &inf);
        CHECK(out.size() == 1 && inf.size() == 1 && inf[0].reliable(), "large groups (mode %d): frame not intact", mode);
        if (mode == 0) CHECK(out.size() == 1 && out[0] == f, "embedded lossless with 2 groups not exact");
        else CHECK(bs.size() <= 200000 && bs.size() > 150000, "large budget size %zu", bs.size());
    }
    printf("large groups: done\n");
}

// PoC preset: 640x512 14-bit, every unit <= 8333 bytes (2 Mbps at 30 fps), intra-only, reliable decode.
static void test_poc_profile() {
    ewc::EncoderConfig cfg = ewc::poc_config();
    CHECK(cfg.width == 640 && cfg.height == 512 && cfg.bit_depth == 14 && cfg.gop == 1 && cfg.frame_bytes == 8333, "poc config fields");
    ewc::Encoder enc(cfg);
    std::vector<uint8_t> bs;
    for (int t = 0; t < 4; ++t) {
        auto f = make_frame(640, 512, 14, t, 900 + t);
        const size_t b = bs.size();
        enc.encode(f.data(), uint64_t(t) * 33333, bs);
        CHECK(bs.size() - b <= 8333 && bs.size() - b >= 8000, "poc frame %d size %zu", t, bs.size() - b);
        CHECK(bs[b + 5] == 0, "poc frame %d not intra", t);
    }
    std::vector<ewc::FrameInfo> inf;
    auto out = decode_all(bs, &inf);
    CHECK(out.size() == 4, "poc decode count");
    for (auto& i : inf) CHECK(i.reliable() && i.bit_depth == 14, "poc frame %u not reliable", i.sequence);
    printf("poc profile: done\n");
}

// Tiled profile: CBR, odd sizes, thread determinism, high-rate fidelity, damage handling.
static void test_tiled() {
    for (int T : {32, 64}) for (bool ci : {true, false}) {   // compact (varint) and fixed 7-byte tile index
        ewc::EncoderConfig cfg;
        cfg.width = 200; cfg.height = 130; cfg.tile = T; cfg.tile_levels = 3; cfg.frame_bytes = 3000;   // edges not multiples of T
        cfg.tile_compact_index = ci;
        std::vector<uint8_t> a, b;
        std::vector<std::vector<uint16_t>> src;
        for (int th : {1, 3}) {
            cfg.threads = th;
            ewc::Encoder enc(cfg);
            auto& bs = th == 1 ? a : b;
            for (int t = 0; t < 4; ++t) {
                auto f = make_frame(200, 130, 14, t, 40 + t);
                if (th == 1) src.push_back(f);
                const size_t s0 = bs.size();
                enc.encode(f.data(), uint64_t(t), bs);
                CHECK(bs.size() - s0 <= 3000, "tiled frame over budget (%zu)", bs.size() - s0);
            }
        }
        CHECK(a == b, "tiled bitstream differs between 1 and 3 threads (T=%d)", T);
        std::vector<ewc::FrameInfo> inf;
        auto out = decode_all(a, &inf);
        CHECK(out.size() == 4, "tiled decode count");
        for (size_t k = 0; k < out.size(); ++k) {
            CHECK(inf[k].reliable() && inf[k].groups_total == ((200 + T - 1) / T) * ((130 + T - 1) / T), "tiled frame %zu flags", k);
            double se = 0; for (size_t i = 0; i < out[k].size(); ++i) { double d = double(out[k][i]) - src[k][i]; se += d * d; }
            CHECK(std::sqrt(se / double(out[k].size())) < 40.0, "tiled frame %zu rmse too high", k);
        }
        // high budget -> close to lossless
        cfg.frame_bytes = 200000; cfg.threads = 1;
        ewc::Encoder hi(cfg);
        std::vector<uint8_t> hb;
        hi.encode(src[0].data(), 0, hb);
        auto ho = decode_all(hb);
        int me = 0; for (size_t i = 0; i < ho[0].size(); ++i) me = std::max(me, std::abs(int(ho[0][i]) - int(src[0][i])));
        CHECK(ho.size() == 1 && me <= 4, "tiled high-rate max error %d", me);
        // damage: never crash, never more frames than encoded, damaged frames flagged
        std::mt19937 rng(5);
        int flagged = 0;
        for (int rep = 0; rep < 40; ++rep) {
            auto c = a;
            for (int k = 0; k < 1 + int(rng() % 20); ++k) c[44 + rng() % (c.size() - 44)] ^= uint8_t(1u << (rng() % 8));
            std::vector<ewc::FrameInfo> fi;
            auto o = decode_all(c, &fi);
            CHECK(o.size() <= 4, "tiled damage: too many frames");
            for (auto& f : o) CHECK(f.size() == size_t(200 * 130), "tiled damage: frame size");
            for (auto& x : fi) flagged += !x.reliable();
        }
        CHECK(flagged > 0, "tiled damage never flagged");
    }
    // regression: an intact tile coded coarsely (few bit planes) must not be replaced by the previous
    // frame. Consecutive frames differ by 3000 DN; a wrongly concealed tile would show that error.
    {
        ewc::EncoderConfig cfg;
        cfg.width = 256; cfg.height = 128; cfg.tile = 64; cfg.tile_levels = 4; cfg.frame_bytes = 600;
        ewc::Encoder enc(cfg);
        std::vector<uint8_t> bs;
        std::vector<std::vector<uint16_t>> src;
        for (int t = 0; t < 4; ++t) {
            auto f = make_frame(256, 128, 14, t, 70 + t, 60.0);       // busy, noisy tiles take the budget
            double m = 0; size_t cnt = 0;
            for (size_t i = 0; i < f.size(); ++i) if ((i % 256) >= 64) { m += f[i]; ++cnt; }
            m /= double(cnt);
            std::mt19937 r2(t);
            for (size_t i = 0; i < f.size(); ++i) {
                int v = (i % 256) < 64 ? int(m) + int(r2() % 7) - 3 : int(f[i]);   // first tile column: near the mean, almost flat
                f[i] = uint16_t(std::min(16383, std::max(0, v + (t % 2) * 3000)));
            }
            src.push_back(f);
            enc.encode(f.data(), uint64_t(t), bs);
        }
        std::vector<ewc::FrameInfo> inf;
        auto out = decode_all(bs, &inf);
        CHECK(out.size() == 4, "coarse-tile regression: count");
        for (size_t k = 0; k < out.size(); ++k) {
            int me = 0; for (size_t i = 0; i < out[k].size(); ++i) me = std::max(me, std::abs(int(out[k][i]) - int(src[k][i])));
            CHECK(me < 1500 && inf[k].reliable(), "coarse tile replaced by previous frame (frame %zu, max err %d)", k, me);
        }
    }
    // adaptive concealment: a tile damaged early is copied from the previous frame only when the
    // scene barely changed; for an unrelated previous frame its own partial decode is used.
    for (int related = 0; related < 2; ++related) {
        ewc::EncoderConfig cfg;
        cfg.width = 256; cfg.height = 256; cfg.tile = 128; cfg.tile_levels = 5; cfg.frame_bytes = 6000;
        ewc::Encoder enc(cfg);
        auto A = make_frame(256, 256, 14, 0, 500, 3.0), Bf = A;
        std::mt19937 r3(9);
        for (auto& v : Bf) v = related ? uint16_t(std::min(16383, int(v) + int(r3() % 5) - 2)) : uint16_t(16383 - v);   // unrelated: inverted image
        std::vector<uint8_t> ua, ub;
        enc.encode(A.data(), 0, ua);
        enc.encode(Bf.data(), 1, ub);
        size_t m = 44;                                   // first tile row header of frame B
        while (m + 1 < ub.size() && !(ub[m] == 0xFF && ub[m + 1] == 0xE6)) ++m;
        const size_t pay = m + 6 + 2 * 7 + 2;           // 2 tiles per row
        for (size_t i = pay + 4; i < pay + 260 && i < ub.size(); ++i) ub[i] ^= 0x5A;   // damage tile 0 early
        std::vector<uint8_t> bs = ua; bs.insert(bs.end(), ub.begin(), ub.end());
        std::vector<ewc::FrameInfo> inf;
        auto out = decode_all(bs, &inf);
        CHECK(out.size() == 2 && !inf[1].reliable(), "adaptive concealment: damage not flagged");
        if (out.size() == 2) {
            double se = 0; int n = 0;
            for (int y = 0; y < 128; ++y) for (int x = 0; x < 128; ++x) { double d = double(out[1][y * 256 + x]) - Bf[y * 256 + x]; se += d * d; ++n; }
            const double rmse = std::sqrt(se / n);
            if (related) CHECK(rmse < 60, "related frames: damaged tile should come from the previous frame (rmse %.1f)", rmse);
            else CHECK(rmse < 2000, "unrelated frames: damaged tile copied from the previous frame (rmse %.1f)", rmse);
        }
    }
    // invalid configurations are refused
    bool threw = false;
    try { ewc::EncoderConfig c; c.tile = 48; ewc::Encoder e(c); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw, "tile 48 accepted");
    printf("tiled profile: done\n");
}

int main() {
    test_dwt();
    test_lossless();
    test_near_lossless();
    test_loco_robustness();
    test_cbr_and_closed_loop();
    test_determinism_and_threads();
    test_metadata();
    test_robustness();
    test_reference_tracking();
    test_input_range();
    test_large_groups();
    test_poc_profile();
    test_tiled();
    if (failures) { printf("%d FAILURE(S)\n", failures); return 1; }
    printf("ALL TESTS PASSED\n");
    return 0;
}
