// Packetizer / Reassembler unit tests (all FEC modes) and an end-to-end check that bytes
// lost in transport are never decoded as valid codec data.
#include "ewc.h"
#include "ewc_net.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <random>
#include <set>
#include <vector>

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { ++failures; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

using Dg = std::vector<std::vector<uint8_t>>;
using ewc::Fec;

static std::vector<uint8_t> payload(size_t n, uint32_t seed) {
    std::mt19937 r(seed);
    std::vector<uint8_t> v(n);
    for (auto& b : v) b = uint8_t(r());
    return v;
}

// Pushes `dg` except the indices in `drop`; returns the released units (flushed).
static std::vector<ewc::Reassembler::Unit> deliver(const Dg& dg, const std::set<size_t>& drop, ewc::Reassembler* ra_out = nullptr) {
    ewc::Reassembler ra;
    for (size_t i = 0; i < dg.size(); ++i) if (!drop.count(i)) ra.push(dg[i].data(), dg[i].size());
    std::vector<ewc::Reassembler::Unit> u;
    ra.pop(u, true);
    if (ra_out) *ra_out = ra;
    return u;
}

static bool is_parity(const std::vector<uint8_t>& d) { return d[3] & 2; }

static void test_roundtrip_all_modes() {
    const size_t sizes[] = {0, 1, 15, 63, 64, 1375, 1376, 1377, 4000, 8333, 50000, 400000};
    int n = 0;
    for (Fec f : {Fec::None, Fec::Xor, Fec::XorInterleaved, Fec::RS})
        for (int k : {1, 4, 8})
            for (size_t mtu : {1400, 200})
                for (size_t sz : sizes) {
                    if (f == Fec::None && k != 1) continue;
                    if (mtu == 200 && sz > 50000) continue;
                    ewc::Packetizer pk(mtu, f == Fec::None ? 0 : k, f);
                    auto data = payload(sz, uint32_t(sz + 7 * k));
                    Dg dg;
                    pk.packetize(data.data(), sz, 42, dg);
                    size_t wire = 0;
                    for (auto& d : dg) { wire += d.size(); CHECK(d.size() <= mtu, "datagram exceeds mtu (%zu > %zu)", d.size(), mtu); }
                    CHECK(wire == pk.wire_bytes(sz), "wire_bytes mismatch %s k=%d sz=%zu: %zu vs %zu", ewc::fec_name(f), k, sz, wire, pk.wire_bytes(sz));
                    CHECK(dg.size() == pk.datagrams(sz), "datagram count mismatch");
                    auto u = deliver(dg, {});
                    CHECK(u.size() == 1 && u[0].data == data && u[0].complete() && u[0].missing() == 0 && u[0].frame_seq == 42,
                          "zero-loss roundtrip %s k=%d mtu=%zu sz=%zu", ewc::fec_name(f), k, mtu, sz);
                    ++n;
                }
    printf("net roundtrip: %d configurations exact\n", n);
}

static void test_xor_single_and_double_loss() {
    ewc::Packetizer pk(200, 4, Fec::Xor);   // pay <= 176
    auto data = payload(3000, 1);
    Dg dg; pk.packetize(data.data(), data.size(), 7, dg);
    size_t nd = 0; for (auto& d : dg) nd += !is_parity(d);
    // one loss in every XOR group of 4 -> fully repaired
    std::set<size_t> drop;
    for (size_t i = 1; i < nd; i += 4) drop.insert(i);
    ewc::Reassembler ra;
    auto u = deliver(dg, drop, &ra);
    CHECK(u.size() == 1 && u[0].data == data && u[0].complete(), "xor: one loss per group not repaired");
    CHECK(ra.datagrams_recovered == drop.size(), "xor recovered count %llu vs %zu", (unsigned long long)ra.datagrams_recovered, drop.size());
    // two losses in group 0 -> unrecoverable, reported exactly, rest intact
    u = deliver(dg, {0, 1});
    CHECK(u.size() == 1 && !u[0].complete() && u[0].missing() == 2, "xor: double loss should be unrecoverable");
    auto mr = u[0].missing_ranges();
    const size_t pay = u[0].pay;
    CHECK(mr.size() == 1 && mr[0].first == 0 && mr[0].second == 2 * pay, "xor: missing range wrong");
    bool zero = std::all_of(u[0].data.begin(), u[0].data.begin() + long(2 * pay), [](uint8_t b) { return b == 0; });
    bool rest = std::equal(u[0].data.begin() + long(2 * pay), u[0].data.end(), data.begin() + long(2 * pay));
    CHECK(zero && rest, "xor: unrecovered bytes must be zero, others intact");
    printf("net xor: single repaired, double reported\n");
}

static void test_interleaved_burst() {
    ewc::Packetizer pk(200, 4, Fec::XorInterleaved);
    auto data = payload(5000, 2);
    Dg dg; pk.packetize(data.data(), data.size(), 1, dg);
    size_t nd = 0, np = 0; for (auto& d : dg) (is_parity(d) ? np : nd)++;
    // a burst of np consecutive data datagrams is repairable
    for (size_t s = 0; s + np <= nd; s += 3) {
        std::set<size_t> drop;
        for (size_t i = s; i < s + np; ++i) drop.insert(i);
        auto u = deliver(dg, drop);
        CHECK(u.size() == 1 && u[0].data == data, "xori: burst of %zu at %zu not repaired", np, s);
    }
    // the same burst defeats consecutive XOR groups
    ewc::Packetizer pc(200, 4, Fec::Xor);
    Dg dc; pc.packetize(data.data(), data.size(), 1, dc);
    auto u = deliver(dc, {0, 1});
    CHECK(u.size() == 1 && !u[0].complete(), "xor: burst of 2 in one group should not be repairable");
    printf("net xori: bursts up to %zu repaired (nd=%zu)\n", np, nd);
}

static void test_rs_any_losses() {
    std::mt19937 rng(5);
    int trials = 0;
    for (size_t sz : {500, 3000, 8333, 30000, 400000}) {
        ewc::Packetizer pk(sz > 100000 ? 1400 : 300, 4, Fec::RS);
        auto data = payload(sz, uint32_t(sz));
        Dg dg; pk.packetize(data.data(), sz, 9, dg);
        size_t nd = 0, np = 0; for (auto& d : dg) (is_parity(d) ? np : nd)++;
        const bool multi = nd + np > 255;
        for (int rep = 0; rep < (multi ? 3 : 40); ++rep) {
            // drop np datagrams (data or parity) at random: always repairable for one block
            std::vector<size_t> idx(dg.size());
            for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
            std::shuffle(idx.begin(), idx.end(), rng);
            const size_t ndrop = multi ? np / 4 : np;   // multi-block: per-block limit; stay well inside
            std::set<size_t> drop(idx.begin(), idx.begin() + long(ndrop));
            auto u = deliver(dg, drop);
            CHECK(u.size() == 1 && u[0].data == data, "rs: %zu random losses not repaired (sz=%zu nd=%zu np=%zu)", ndrop, sz, nd, np);
            ++trials;
        }
        if (!multi) {   // np + 1 data losses -> exactly np + 1 datagrams reported missing
            std::set<size_t> drop;
            for (size_t i = 0; i <= np && i < nd; ++i) drop.insert(i);
            auto u = deliver(dg, drop);
            CHECK(u.size() == 1 && u[0].missing() == drop.size(), "rs: overload must leave all %zu missing (got %zu)", drop.size(), u.size() ? u[0].missing() : 0);
        }
    }
    printf("net rs: %d random-loss trials repaired\n", trials);
}

static void test_dup_reorder_late() {
    ewc::Packetizer pk(300, 4, Fec::Xor);
    std::vector<std::vector<uint8_t>> frames;
    Dg dg;
    for (uint32_t s = 0; s < 4; ++s) { frames.push_back(payload(1000 + 300 * s, s)); pk.packetize(frames[s].data(), frames[s].size(), s, dg); }
    std::mt19937 rng(3);
    Dg sh = dg;
    for (auto& d : dg) sh.push_back(d);            // every datagram twice
    std::shuffle(sh.begin(), sh.end(), rng);       // arbitrary order across all units
    ewc::Reassembler ra;
    ra.max_pending = 8;
    for (auto& d : sh) ra.push(d.data(), d.size());
    std::vector<ewc::Reassembler::Unit> u;
    ra.pop(u, true);
    CHECK(u.size() == 4, "reorder: unit count %zu", u.size());
    for (size_t i = 0; i < u.size(); ++i) CHECK(u[i].frame_seq == i && u[i].data == frames[i], "reorder: unit %zu wrong", i);
    CHECK(ra.datagrams_dup + ra.datagrams_late > 0 && ra.datagrams_bad == 0, "dup/late accounting");
    // late datagram for an already released unit is ignored
    ewc::Reassembler rb;
    for (auto& d : dg) rb.push(d.data(), d.size());
    std::vector<ewc::Reassembler::Unit> v; rb.pop(v);
    const uint64_t late0 = rb.datagrams_late;
    rb.push(dg[0].data(), dg[0].size());
    rb.pop(v, true);
    CHECK(rb.datagrams_late == late0 + 1 && v.size() == 4, "late datagram not rejected");
    printf("net dup/reorder/late: done\n");
}

static void test_malformed_and_truncated() {
    ewc::Packetizer pk(300, 4, Fec::RS);
    auto data = payload(2000, 4);
    Dg dg; pk.packetize(data.data(), data.size(), 3, dg);
    auto bad = [&](std::vector<uint8_t> d, const char* what) {
        ewc::Reassembler ra;
        ra.push(d.data(), d.size());
        std::vector<ewc::Reassembler::Unit> u; ra.pop(u, true);
        CHECK(ra.datagrams_bad == 1 && u.empty(), "malformed accepted: %s", what);
    };
    { auto d = dg[0]; d[0] = 'X'; bad(d, "magic"); }
    { auto d = dg[0]; d[2] = 1; bad(d, "old version"); }
    { auto d = dg[0]; d[20] = d[21] = 0; bad(d, "pay 0"); }
    { auto d = dg[1]; d[12] ^= 1; bad(d, "offset not a multiple of pay"); }
    { auto d = dg[0]; d[23] = 9; bad(d, "fec mode"); }
    { auto d = dg[0]; d[22] = 0; bad(d, "fec without k"); }
    { auto d = dg[0]; d.pop_back(); bad(d, "truncated data datagram"); }
    { auto d = dg.back(); d.pop_back(); bad(d, "truncated parity datagram"); }
    { auto d = dg.back(); d[12] = 0xFF; d[13] = 0xFF; bad(d, "parity index out of range"); }
    { std::vector<uint8_t> d(10, 0); bad(d, "short"); }
    // inconsistent second datagram (different unit size) is rejected, first one kept
    ewc::Reassembler ra;
    ra.push(dg[0].data(), dg[0].size());
    auto d = dg[1]; d[16] ^= 0x10;
    ra.push(d.data(), d.size());
    CHECK(ra.datagrams_bad == 1, "inconsistent unit size accepted");
    printf("net malformed/truncated: done\n");
}

static void test_missing_header_and_incomplete() {
    ewc::Packetizer pk(300, 0, Fec::None);
    auto a = payload(2000, 8), b = payload(900, 9), c = payload(10, 10);
    Dg dg;
    pk.packetize(a.data(), a.size(), 0, dg);
    const size_t na = dg.size();
    pk.packetize(b.data(), b.size(), 1, dg);
    pk.packetize(c.data(), c.size(), 2, dg);
    ewc::Reassembler ra;   // max_pending = 2
    for (size_t i = 1; i < dg.size(); ++i) ra.push(dg[i].data(), dg[i].size());   // datagram 0 of unit 0 lost
    std::vector<ewc::Reassembler::Unit> u; ra.pop(u);
    CHECK(u.size() == 3, "incomplete head should be released once later units are complete (got %zu)", u.size());
    if (u.size() == 3) {
        CHECK(u[0].missing() == 1 && u[0].missing_ranges().size() == 1 && u[0].missing_ranges()[0].first == 0, "unit 0: missing first datagram not reported");
        CHECK(u[1].data == b && u[2].data == c, "units 1/2 corrupted");
    }
    (void)na;
    // whole unit lost: simply absent; empty unit survives
    Dg e; pk.packetize(nullptr, 0, 5, e);
    auto v = deliver(e, {});
    CHECK(v.size() == 1 && v[0].data.empty() && v[0].complete(), "empty unit");
    printf("net missing header / incomplete: done\n");
}

// End-to-end: transport losses never produce a frame marked reliable from zero-filled bytes.
static void test_end_to_end() {
    ewc::EncoderConfig cfg;
    cfg.width = 320; cfg.height = 256; cfg.levels = 5; cfg.frame_bytes = 5000; cfg.gop = 6; cfg.mv_block = 16;
    for (bool lossless : {false, true}) {
        cfg.lossless = lossless;
        ewc::Encoder enc(cfg);
        std::mt19937 rng(11);
        std::vector<std::vector<uint8_t>> units;
        std::vector<std::vector<uint16_t>> recs;
        for (int t = 0; t < 18; ++t) {
            std::vector<uint16_t> f(size_t(320) * 256);
            for (int y = 0; y < 256; ++y) for (int x = 0; x < 320; ++x) f[size_t(y) * 320 + x] = uint16_t(4000 + 20 * ((x + 2 * t) / 9 % 7) + y + (rng() & 7));
            std::vector<uint8_t> u; enc.encode(f.data(), uint64_t(t), u); units.push_back(u);
            recs.push_back(enc.last_reconstruction());
            if (lossless) recs.back() = f;
        }
        int unreliable = 0, checked = 0, wrong_reliable = 0;
        for (int rep = 0; rep < 30; ++rep) {
            ewc::Packetizer pk(400, 0, Fec::None);
            ewc::Reassembler ra; ra.max_pending = 1;
            std::vector<ewc::Reassembler::Unit> got;
            for (size_t k = 0; k < units.size(); ++k) {
                Dg dg; pk.packetize(units[k].data(), units[k].size(), uint32_t(k), dg);
                for (auto& d : dg) if (rng() % 20) ra.push(d.data(), d.size());   // 5% loss
                ra.pop(got);
            }
            ra.pop(got, true);
            std::vector<uint8_t> rx;
            for (auto& u : got) rx.insert(rx.end(), u.data.begin(), u.data.end());
            ewc::Decoder dec; size_t pos = 0; std::vector<uint16_t> out; ewc::FrameInfo fi; int w, h;
            while (dec.decode_next(rx.data(), rx.size(), pos, out, fi, w, h)) {
                ++checked;
                if (!fi.reliable()) { ++unreliable; continue; }
                // a frame reported reliable must equal the encoder's reconstruction exactly
                if (fi.sequence >= recs.size() || out != recs[fi.sequence]) ++wrong_reliable;
            }
        }
        CHECK(wrong_reliable == 0, "end-to-end (%s): %d frames marked reliable but wrong", lossless ? "lossless" : "lossy", wrong_reliable);
        CHECK(unreliable > 0, "end-to-end: loss produced no unreliable frames (test ineffective)");
        printf("net end-to-end (%s): %d decoded, %d flagged unreliable, 0 wrong-but-reliable\n", lossless ? "lossless" : "lossy", checked, unreliable);
    }
}

int main() {
    test_roundtrip_all_modes();
    test_xor_single_and_double_loss();
    test_interleaved_burst();
    test_rs_any_losses();
    test_dup_reorder_late();
    test_malformed_and_truncated();
    test_missing_header_and_incomplete();
    test_end_to_end();
    if (failures) { printf("%d FAILURE(S)\n", failures); return 1; }
    printf("ALL NET TESTS PASSED\n");
    return 0;
}
