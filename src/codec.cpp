// ewc — frame-level encoder / decoder: container, rate allocation, prediction, concealment.
#include "ewc.h"
#include "crc.h"
#include "dwt.h"
#include "loco.h"
#include "motion.h"
#include "spiht.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace ewc {

// ============================================================================ container
//  Full description: docs/FORMAT.md. Summary (little endian):
//  Frame header (44 bytes): 40 bytes of fields + crc32 of those 40 bytes
//   0 magic "EWC1" | 4 version=1 | 5 type (0=I,1=P) | 6 flags | 7 wavelet (0=5/3, 1=9/7)
//   8 width u16 | 10 height u16 | 12 bit_depth | 13 levels | 14 groups (LOCO: stripes) | 15 frac_bits
//  16 sequence u32 | 20 timestamp_us u64 | 28 total_len u32 (whole unit) | 32 meta_len u16
//  34 mv_block u8 | 35 reserved (LOCO: near) | 36 dc u16 (intra level shift; LOCO: stripe rows)
//  38 precision shift u8 (lossy wavelet) | 39 reserved | 40 crc32(bytes 0..39)
//  flags: 1 lossless, 2 motion (P), 4 block CRC, 8 LOCO profile, 16 u32 group lengths
//  then: [metadata][crc16]                 (if meta_len > 0)
//        [mv_len u16][crc16][motion data]  (P frames)
//        wavelet group chunks (13-byte header; 15 bytes with flag 16):
//          FF E7 | gid u8 | nmax i8 | seq8 u8 | nsym u32 | len u16 (u32) | crc16(previous bytes)
//          payload, split in 256-byte blocks each followed by a crc16 (flag 4)
//        redundancy trailer (optional, I frames): FF E8 | seq8 | G | crc16(previous 4 bytes)
//          then per group: nmax i8 | len u8 | first `len` bytes of the group stream | crc16
//        LOCO stripe chunks (flag 8, instead of group chunks):
//          FF E9 | sid u8 | seq8 u8 | y0 u16 | rows u16 | len u32 | crc16(previous 12 bytes)
//          payload in 256-byte blocks + crc16 as above
static constexpr uint8_t kMagic[4] = {'E', 'W', 'C', '1'};
static constexpr int kHdr = 44;
static constexpr int kGrpHdr = 13;     // group chunk header with u16 length
static constexpr int kGrpHdr32 = 15;   // with u32 length (flag F_LEN32)
static constexpr int kBlk = 256;
static constexpr size_t kLocoMinStripeBytes = 3000;
static constexpr int kRedHdr = 6;
static constexpr int kRefMargin = 112;   // motion-compensation margin of the reference plane (pixels)
enum : uint8_t { F_LOSSLESS = 1, F_MOTION = 2, F_BLOCKCRC = 4, F_LOCO = 8, F_LEN32 = 16, F_TILED = 32 };
static constexpr int kTileRowHdr = 6;   // FF E6 | tile row u16 | seq8 | tiles in row u8
static constexpr int kTileEnt = 7;      // per tile: nmax i8 | nsym u24 | len u24
static constexpr int kTileRowHdrC = 8;  // compact index: FF E6 | row u16 | seq8 | ntx | entry bytes u16
static inline size_t vlen(uint32_t v) { size_t n = 1; while (v >= 0x80) { v >>= 7; ++n; } return n; }
static inline void putv(std::vector<uint8_t>& o, uint32_t v) { while (v >= 0x80) { o.push_back(uint8_t(v | 0x80)); v >>= 7; } o.push_back(uint8_t(v)); }
static inline bool getv(const uint8_t*& p, const uint8_t* e, uint32_t& v) {
    v = 0;
    for (int sh = 0; sh < 32 && p < e; sh += 7) { const uint8_t b = *p++; v |= uint32_t(b & 0x7F) << sh; if (!(b & 0x80)) return true; }
    return false;
}
static constexpr int kStripeHdr = 14;

namespace {
inline void put16(std::vector<uint8_t>& v, uint32_t x) { v.push_back(uint8_t(x)); v.push_back(uint8_t(x >> 8)); }
inline void put32(std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back(uint8_t(x >> (8 * i))); }
inline uint32_t get16(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8); }
inline uint32_t get32(const uint8_t* p) { return get16(p) | (get16(p + 2) << 16); }

size_t payload_wire_size(size_t n, bool blockcrc) { return n + (blockcrc ? 2 * ((n + kBlk - 1) / kBlk) : 0); }

// Persistent worker pool: threads are created once and reused, the calling thread takes part.
// Work items are independent, so results do not depend on scheduling. If the pool is already
// running a job (nested or concurrent use), parallel_for falls back to spawning threads.
class Pool {
public:
    static Pool& get() { static Pool p; return p; }
    bool try_run(int n, int threads, const std::function<void(int)>& fn) {
        std::unique_lock<std::mutex> busy(run_mu_, std::try_to_lock);
        if (!busy.owns_lock() || in_worker()) return false;
        const int helpers = std::min(threads, n) - 1;   // the caller is one of the threads
        {
            std::lock_guard<std::mutex> lk(mu_);
            while (int(workers_.size()) < helpers) {
                const int id = int(workers_.size());
                const uint64_t g = gen_;          // the worker joins the job published below
                workers_.emplace_back([this, id, g] { loop(id, g); });
            }
            fn_ = &fn; n_ = n; next_.store(0); active_ = helpers; job_helpers_ = helpers; ++gen_;
        }
        cv_.notify_all();
        for (int i; (i = next_.fetch_add(1)) < n;) fn(i);
        std::unique_lock<std::mutex> lk(mu_);
        done_cv_.wait(lk, [this] { return active_ == 0; });
        fn_ = nullptr;
        return true;
    }
    ~Pool() {
        { std::lock_guard<std::mutex> lk(mu_); stop_ = true; ++gen_; }
        cv_.notify_all();
        for (auto& t : workers_) t.join();
    }
private:
    static bool& in_worker() { static thread_local bool w = false; return w; }
    void loop(int id, uint64_t seen) {
        in_worker() = true;
        for (;;) {
            const std::function<void(int)>* fn;
            int n;
            {
                std::unique_lock<std::mutex> lk(mu_);
                cv_.wait(lk, [&] { return stop_ || gen_ != seen; });
                if (stop_) return;
                seen = gen_;
                if (id >= job_helpers_) continue;   // not needed for this job
                fn = fn_; n = n_;
            }
            for (int i; (i = next_.fetch_add(1)) < n;) (*fn)(i);
            std::lock_guard<std::mutex> lk(mu_);
            if (--active_ == 0) done_cv_.notify_all();
        }
    }
    std::mutex run_mu_, mu_;
    std::condition_variable cv_, done_cv_;
    std::vector<std::thread> workers_;
    const std::function<void(int)>* fn_ = nullptr;
    int n_ = 0, active_ = 0, job_helpers_ = 0;
    std::atomic<int> next_{0};
    uint64_t gen_ = 0;
    bool stop_ = false;
    friend void parallel_for(int, int, const std::function<void(int)>&);
};

void parallel_for(int n, int threads, const std::function<void(int)>& fn) {
    if (threads <= 1 || n <= 1) { for (int i = 0; i < n; ++i) fn(i); return; }
    if (Pool::get().try_run(n, threads, fn)) return;
    std::vector<std::thread> th;
    std::atomic<int> next{0};
    for (int t = 0; t < std::min(threads, n); ++t)
        th.emplace_back([&] { for (int i; (i = next.fetch_add(1)) < n;) fn(i); });
    for (auto& t : th) t.join();
}

bool header_valid(const uint8_t* p, size_t avail) {
    if (avail < size_t(kHdr)) return false;
    if (std::memcmp(p, kMagic, 4) != 0) return false;
    if (crc32(p, 40) != get32(p + 40)) return false;
    const int W = int(get16(p + 8)), H = int(get16(p + 10)), bd = p[12], L = p[13], G = p[14];
    return p[4] == 1 && p[5] <= 1 && p[7] <= 1 && W >= 1 && H >= 1 && W <= 16384 && H <= 16384 && bd >= 1 &&
           bd <= 16 && L >= 1 && L <= 8 && G >= 1 && p[15] <= 4 && get32(p + 28) >= uint32_t(kHdr);
}

// --------------------------------------------------------------- geometry shared by enc/dec
struct Geom {
    int W = 0, H = 0, Wp = 0, Hp = 0, L = 0, G = 0, bd = 14, frac = 2;
    int ps = 0;                       // lossy: samples scaled by 2^ps before the integer transform (header byte 38)
    Wavelet wv = Wavelet::W97;
    bool lossless = false;
    Layout lay;
    std::vector<GroupDef> groups;
    std::vector<int64_t> wfix, winv;  // per band weighting (Q16)
    bool same(int w, int h, int l, int g, int b, int f, Wavelet v, bool ll, int p = 0) const {
        return w == W && h == H && l == L && g == G && b == bd && f == frac && v == wv && ll == lossless && p == ps;
    }
    void init(int w, int h, int l, int g, int b, int f, Wavelet v, bool ll, int p = 0) {
        W = w; H = h; L = l; bd = b; frac = f; wv = v; lossless = ll; ps = ll ? 0 : p;
        const int m = 1 << L;
        Wp = (W + m - 1) / m * m; Hp = (H + m - 1) / m * m;
        lay.init(Wp, Hp, L);
        G = std::max(1, std::min(g, lay.hL));
        groups.resize(G);
        for (int i = 0; i < G; ++i) groups[i].init(lay, lay.hL * i / G, lay.hL * (i + 1) / G);
        wfix.resize(lay.bands.size()); winv.resize(lay.bands.size());
        for (size_t bi = 0; bi < lay.bands.size(); ++bi) {
            const auto& B = lay.bands[bi];
            double gsc;
            if (lossless) gsc = 1.0;
            else {
                int lev = B.lvl == 0 ? L : L - B.lvl + 1;
                double nl = synth_norm_1d(wv, lev, 0), nh = synth_norm_1d(wv, lev, 1);
                double w2 = B.lvl == 0 ? nl * nl : (B.orient == 2 ? nh * nh : nl * nh);
                gsc = w2 * double(1 << frac) / double(1 << ps);   // coded integers keep the same scale
            }
            wfix[bi] = std::llround(gsc * 65536.0);
            winv[bi] = std::llround(65536.0 / gsc);
        }
    }
    void pad_in(const uint16_t* src, int32_t* dst, int32_t offset) const {
        for (int y = 0; y < Hp; ++y) {
            const uint16_t* s = src + size_t(std::min(y, H - 1)) * W;
            int32_t* d = dst + size_t(y) * Wp;
            for (int x = 0; x < W; ++x) d[x] = int32_t(s[x]) - offset;
            for (int x = W; x < Wp; ++x) d[x] = int32_t(s[W - 1]) - offset;
        }
    }
    // Reconstruct output frame from decoded coefficient states + prediction (nullptr: intra)
    void reconstruct(const CoefState& cs, const int32_t* pred, int32_t dc, std::vector<int32_t>& img,
                     std::vector<int32_t>& tmp, std::vector<uint16_t>& out,
                     const std::vector<uint8_t>* ll_bad = nullptr) const {
        const size_t N = size_t(Wp) * Hp;
        img.resize(N);
        constexpr int64_t kClamp = int64_t(1) << 26;
        for (size_t i = 0; i < N; ++i) {
            int32_t v = recon_value(cs.a[i], cs.p[i], cs.neg[i]);
            if (v) {
                int64_t m = v < 0 ? -int64_t(v) : int64_t(v);
                int64_t c = std::min((m * winv[lay.band[i]] + 32768) >> 16, kClamp);
                img[i] = int32_t(v < 0 ? -c : c);
            } else img[i] = 0;
        }
        if (ll_bad) {   // concealment: interpolate unreliable LL rows from the nearest good rows
            const auto& bad = *ll_bad;
            const int hL = lay.hL, wL = lay.wL;
            for (int y = 0; y < hL;) {
                if (!bad[y]) { ++y; continue; }
                int y1 = y;
                while (y1 < hL && bad[y1]) ++y1;
                const int a = y - 1, b = y1;
                for (int yy = y; yy < y1; ++yy)
                    for (int x = 0; x < wL; ++x) {
                        int64_t v = 0;
                        if (a >= 0 && b < hL) v = (int64_t(img[size_t(a) * Wp + x]) * (b - yy) + int64_t(img[size_t(b) * Wp + x]) * (yy - a)) / (b - a);
                        else if (a >= 0) v = img[size_t(a) * Wp + x];
                        else if (b < hL) v = img[size_t(b) * Wp + x];
                        img[size_t(yy) * Wp + x] = int32_t(v);
                    }
                y = y1;
            }
        }
        dwt_inverse(img.data(), Wp, Hp, L, wv, tmp);
        if (ps) { const int32_t hr = int32_t(1) << (ps - 1); for (size_t i = 0; i < N; ++i) img[i] = (img[i] + hr) >> ps; }
        out.resize(size_t(W) * H);
        const int32_t maxv = (1 << bd) - 1;
        const int32_t off = pred ? 0 : dc;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const size_t i = size_t(y) * Wp + x;
                int64_t v = int64_t(img[i]) + off + (pred ? pred[i] : 0);
                out[size_t(y) * W + x] = uint16_t(std::min<int64_t>(std::max<int64_t>(v, 0), maxv));
            }
    }
};

// Zerotree maxima: dmax = max magnitude among all descendants, lmax = among grandchildren and below.
void build_trees_on(const Layout& lay, const uint32_t* mag, uint32_t* dmax, uint32_t* lmax) {
    const int W = lay.W;
    uint32_t ch[4];
    for (int d = lay.L; d >= 0; --d) {     // children (level d+1) are final before parents
        for (size_t bi = 0; bi < lay.bands.size(); ++bi) {
            const auto& B = lay.bands[bi];
            if (B.lvl != d) continue;
            for (int y = B.oy; y < B.oy + B.bh; ++y)
                for (int x = B.ox; x < B.ox + B.bw; ++x) {
                    const size_t i = size_t(y) * W + x;
                    int nc = lay.children(y, x, ch);
                    uint32_t dm = 0, lm = 0;
                    for (int c = 0; c < nc; ++c) {
                        dm = std::max(dm, std::max(mag[ch[c]], dmax[ch[c]]));
                        lm = std::max(lm, dmax[ch[c]]);
                    }
                    dmax[i] = dm; lmax[i] = lm;
                }
        }
    }
}

}  // namespace

// ============================================================================ encoder
struct Encoder::Impl {
    EncoderConfig cfg;
    Geom g;
    uint32_t seq = 0;
    std::vector<int32_t> cur, pred, coef, tmp, img;
    std::vector<uint32_t> mag, dmax, lmax;
    std::vector<uint8_t> neg;
    CoefState ecs, dcs;
    std::vector<GroupEncoder> genc;
    std::vector<int32_t> ref;           // padded reference (decoded), valid if have_ref
    RefPlane refp;                      // reference with motion-compensation margin
    bool have_ref = false;
    uint32_t since_i = 0;            // frames since the last I frame
    bool loco = false;               // lossless / near-lossless LOCO-I profile
    int pshift = 0;                  // precision shift for lossy wavelet coding
    int64_t vbv_fill = 0;            // bytes queued beyond the channel's per-frame drain
    std::vector<uint16_t> recon;
    MotionField mf;
    bool force_intra = false;        // set by request_intra(): next frame is coded as I
    int grp_hdr = kGrpHdr;           // group chunk header size of the frame being coded

    explicit Impl(const EncoderConfig& c) : cfg(c) {
        if (cfg.bit_depth < 1 || cfg.bit_depth > 16) throw std::invalid_argument("bit_depth must be 1..16");
        if (cfg.width < 1 || cfg.height < 1 || cfg.width > 16384 || cfg.height > 16384) throw std::invalid_argument("size");
        if (cfg.levels < 1 || cfg.levels > 8) throw std::invalid_argument("levels must be 1..8");
        if (cfg.mv_block != 8 && cfg.mv_block != 16 && cfg.mv_block != 32 && cfg.mv_block != 64)
            throw std::invalid_argument("mv_block must be 8/16/32/64");
        if (cfg.lossless) { cfg.gop = 1; cfg.frac_bits = 0; }
        if (!cfg.lossless || cfg.embedded_lossless) cfg.near = 0;
        if (cfg.near < 0 || cfg.near > 255) throw std::invalid_argument("near must be 0..255");
        cfg.stripe_rows = std::max(8, std::min(cfg.stripe_rows, 4096));
        loco = cfg.lossless && !cfg.embedded_lossless;
        cfg.frac_bits = std::max(0, std::min(cfg.frac_bits, 4));
        cfg.groups = std::max(1, std::min(cfg.groups, 255));
        Wavelet wv = cfg.lossless ? Wavelet::W53 : (cfg.wavelet ? Wavelet::W97 : Wavelet::W53);
        pshift = cfg.lossless ? 0 : (cfg.precision_shift >= 0 ? std::min(cfg.precision_shift, 8) : std::max(0, std::min(8, 18 - cfg.bit_depth)));
        g.init(cfg.width, cfg.height, cfg.levels, cfg.groups, cfg.bit_depth, cfg.frac_bits, wv, cfg.lossless, pshift);
        const size_t N = size_t(g.Wp) * g.Hp;
        cur.resize(N); pred.resize(N); coef.resize(N); mag.resize(N); neg.resize(N);
        dmax.assign(N, 0); lmax.assign(N, 0);
        genc = std::vector<GroupEncoder>(size_t(g.G));
        if (cfg.tile) {
            const int T = cfg.tile;
            if (T != 16 && T != 32 && T != 64 && T != 128 && T != 256) throw std::invalid_argument("tile must be 16/32/64/128/256");
            if (cfg.lossless) throw std::invalid_argument("tiled profile is lossy only");
            if (cfg.tile_levels < 1 || cfg.tile_levels > 6 || (T >> cfg.tile_levels) < 2)
                throw std::invalid_argument("tile_levels must be 1..6 with tile >= 2 * 2^levels");
            cfg.gop = 1;
            init_tiles();
        }
    }

    // ---- tiled profile state: one Geom for a tile, per-tile coefficient buffers and coders
    Geom tg;
    int ntx = 0, nty = 0;
    std::vector<uint32_t> tmag, tdmax, tlmax;
    std::vector<uint8_t> tneg;
    std::vector<CoefState> tcs;
    std::vector<GroupEncoder> tenc;
    void init_tiles() {
        const int T = cfg.tile;
        tg.init(T, T, cfg.tile_levels, 1, cfg.bit_depth, cfg.frac_bits, cfg.wavelet ? Wavelet::W97 : Wavelet::W53, false, pshift);
        ntx = (cfg.width + T - 1) / T; nty = (cfg.height + T - 1) / T;
        if (ntx > 255 || nty > 65535) throw std::invalid_argument("too many tiles per row (raise tile)");
        const size_t nt = size_t(ntx) * nty, A = size_t(T) * T;
        tmag.resize(nt * A); tneg.resize(nt * A); tdmax.assign(nt * A, 0); tlmax.assign(nt * A, 0);
        tcs.resize(nt);
        tenc = std::vector<GroupEncoder>(nt);
    }

    void encode_tiled(const uint16_t* frame, uint64_t ts, std::vector<uint8_t>& out, const std::vector<uint8_t>& meta) {
        const int W = cfg.width, H = cfg.height, T = cfg.tile, nt = ntx * nty;
        const size_t A = size_t(T) * T;
        uint64_t sum = 0;
        for (size_t i = 0; i < size_t(W) * H; ++i) sum += frame[i];
        const int32_t dc = int32_t((sum + uint64_t(W) * H / 2) / (uint64_t(W) * H));
        // ---- per tile: edge-replicated block, forward wavelet, weighting, zerotree maxima, coder
        parallel_for(nt, cfg.threads, [&](int t) {
            const int tx = t % ntx, ty = t / ntx;
            std::vector<int32_t> tb(A), tmp;
            for (int y = 0; y < T; ++y) {
                const uint16_t* srow = frame + size_t(std::min(ty * T + y, H - 1)) * W;
                for (int x = 0; x < T; ++x) tb[size_t(y) * T + x] = (int32_t(srow[std::min(tx * T + x, W - 1)]) - dc) * (int32_t(1) << tg.ps);
            }
            dwt_forward(tb.data(), T, T, tg.L, tg.wv, tmp);
            uint32_t* mg = tmag.data() + size_t(t) * A;
            uint8_t* ng = tneg.data() + size_t(t) * A;
            for (size_t i = 0; i < A; ++i) {
                const int32_t c = tb[i];
                const uint64_t m = uint64_t(c < 0 ? -int64_t(c) : int64_t(c));
                mg[i] = uint32_t((m * uint64_t(tg.wfix[tg.lay.band[i]]) + 32768) >> 16);
                ng[i] = c < 0;
            }
            build_trees_on(tg.lay, mg, tdmax.data() + size_t(t) * A, tlmax.data() + size_t(t) * A);
            tcs[t].resize(A);
            tenc[t].begin(&tg.lay, &tg.groups[0], mg, ng, tdmax.data() + size_t(t) * A, tlmax.data() + size_t(t) * A, &tcs[t],
                          cfg.cp_interval);
        });
        // ---- budget (leaky bucket as in the wavelet path; intra-only)
        const int64_t R = cfg.frame_bytes;
        const int64_t vbv = std::max<int64_t>(R, R * cfg.vbv_frames_q8 / 256);
        int64_t budget64 = std::min<int64_t>(R * std::max(256, cfg.i_frame_scale_q8) / 256, vbv - vbv_fill);
        const bool unlimited = cfg.frame_bytes == 0;
        const uint32_t budget = unlimited ? 0xFFFFFFFFu : uint32_t(std::max<int64_t>(budget64, 64));
        const bool bcrc = cfg.block_crc;
        // fixed bytes: header, metadata, one row header + crc per tile row, block-CRC reserve
        const size_t crc_res = bcrc ? 2 * ((unlimited ? 0 : size_t(budget)) / kBlk + size_t(nty)) : 0;
        // tile redundancy trailer: FF E5 | seq8 | ntiles u16 | crc16, then per tile nmax | len u8 | bytes | crc16
        const int red = std::max(0, std::min(cfg.tile_redundancy, 255));
        const size_t red_bytes = red ? 7 + size_t(nt) * (size_t(red) + 4) : 0;
        const bool cidx = cfg.tile_compact_index;
        const size_t fixed = kHdr + (meta.empty() ? 0 : meta.size() + 2) + size_t(nty) * ((cidx ? kTileRowHdrC : kTileRowHdr) + 2) + crc_res + red_bytes;
        const size_t ent_est = cidx ? 4 : kTileEnt;   // stop test only; allocate() counts exactly
        // ---- embedded coding, bit-plane synchronous across tiles (stop once over budget)
        int top = -1;
        for (auto& e : tenc) top = std::max(top, e.nmax());
        for (int n = top; n >= 0; --n) {
            parallel_for(nt, cfg.threads, [&](int t) { if (tenc[t].next_plane() == n) tenc[t].code_plane(n); });
            if (!unlimited) {
                size_t tot = fixed;
                for (auto& e : tenc) tot += ent_est + e.bytes_now();
                if (tot >= budget) break;
            }
        }
        std::vector<int> pick(nt, 0);
        if (unlimited) for (int t = 0; t < nt; ++t) pick[t] = int(tenc[t].cps().size()) - 1;
        else if (cidx) allocate(budget, fixed, false, pick, tenc, 0, true);
        else allocate(budget, fixed, false, pick, tenc, kTileEnt);
        // ---- emit
        const size_t start = out.size();
        std::vector<uint8_t> hdr(kHdr, 0);
        std::memcpy(hdr.data(), kMagic, 4);
        hdr[4] = 1; hdr[5] = 0;
        hdr[6] = uint8_t(F_TILED | (bcrc ? F_BLOCKCRC : 0));
        hdr[7] = uint8_t(tg.wv);
        hdr[8] = uint8_t(W); hdr[9] = uint8_t(W >> 8); hdr[10] = uint8_t(H); hdr[11] = uint8_t(H >> 8);
        hdr[12] = uint8_t(cfg.bit_depth); hdr[13] = uint8_t(tg.L); hdr[14] = 1; hdr[15] = uint8_t(tg.frac);
        for (int i = 0; i < 4; ++i) hdr[16 + i] = uint8_t(seq >> (8 * i));
        for (int i = 0; i < 8; ++i) hdr[20 + i] = uint8_t(ts >> (8 * i));
        hdr[32] = uint8_t(meta.size()); hdr[33] = uint8_t(meta.size() >> 8);
        int lg = 0; while ((1 << lg) < T) ++lg;
        hdr[34] = uint8_t(lg);                       // tile size = 2^hdr[34]
        hdr[35] = cidx ? 1 : 0;                      // tile index format: 0 fixed 7 B, 1 varint
        hdr[36] = uint8_t(dc); hdr[37] = uint8_t(dc >> 8);
        hdr[38] = uint8_t(tg.ps);                    // precision shift
        out.insert(out.end(), hdr.begin(), hdr.end());
        if (!meta.empty()) {
            out.insert(out.end(), meta.begin(), meta.end());
            put16(out, crc16(meta.data(), meta.size()));
        }
        std::vector<std::vector<uint8_t>> pays(nt);
        parallel_for(nt, cfg.threads, [&](int t) { pays[t] = tenc[t].terminate_at(tenc[t].cps()[pick[t]]); });
        for (int r = 0; r < nty; ++r) {
            //  FF E6 | row u16 | seq8 | ntx | ntx x (nmax i8 | nsym u24 | len u24) | crc16 ; then the
            //  row's tile payloads back to back, in 256-byte blocks each followed by a crc16
            //  compact (hdr[35]=1): FF E6 | row u16 | seq8 | ntx | elen u16 | ntx x (nmax i8 | varint nsym | varint len) | crc16
            std::vector<uint8_t> rh = {0xFF, 0xE6, uint8_t(r), uint8_t(r >> 8), uint8_t(seq), uint8_t(ntx)};
            if (cidx) { rh.push_back(0); rh.push_back(0); }
            std::vector<uint8_t> rowpay;
            for (int k = 0; k < ntx; ++k) {
                const int t = r * ntx + k;
                const uint32_t ns = tenc[t].cps()[pick[t]].nsym, ln = uint32_t(pays[t].size());
                if (ns > 0xFFFFFFu || ln > 0xFFFFFFu) throw std::runtime_error("tile stream too large");
                rh.push_back(uint8_t(int8_t(tenc[t].nmax())));
                if (cidx) { putv(rh, ns); putv(rh, ln); }
                else {
                    rh.push_back(uint8_t(ns)); rh.push_back(uint8_t(ns >> 8)); rh.push_back(uint8_t(ns >> 16));
                    rh.push_back(uint8_t(ln)); rh.push_back(uint8_t(ln >> 8)); rh.push_back(uint8_t(ln >> 16));
                }
                rowpay.insert(rowpay.end(), pays[t].begin(), pays[t].end());
            }
            if (cidx) { const size_t el = rh.size() - kTileRowHdrC; rh[6] = uint8_t(el); rh[7] = uint8_t(el >> 8); }
            put16(rh, crc16(rh.data(), rh.size()));
            out.insert(out.end(), rh.begin(), rh.end());
            for (size_t o = 0; o < rowpay.size(); o += kBlk) {
                const size_t len = std::min<size_t>(kBlk, rowpay.size() - o);
                out.insert(out.end(), rowpay.begin() + long(o), rowpay.begin() + long(o + len));
                if (bcrc) put16(out, crc16(rowpay.data() + o, len));
            }
        }
        if (red) {   // coarse fallback: the first `red` bytes of every tile stream, repeated at the end
            std::vector<uint8_t> th = {0xFF, 0xE5, uint8_t(seq), uint8_t(nt), uint8_t(nt >> 8)};
            put16(th, crc16(th.data(), th.size()));
            out.insert(out.end(), th.begin(), th.end());
            for (int t = 0; t < nt; ++t) {
                const size_t len = std::min<size_t>(size_t(red), pays[t].size());
                std::vector<uint8_t> e = {uint8_t(int8_t(tenc[t].nmax())), uint8_t(len)};
                e.insert(e.end(), pays[t].begin(), pays[t].begin() + long(len));
                put16(e, crc16(e.data(), e.size()));
                out.insert(out.end(), e.begin(), e.end());
            }
        }
        const uint32_t total = uint32_t(out.size() - start);
        vbv_fill = std::max<int64_t>(0, vbv_fill + int64_t(total) - int64_t(cfg.frame_bytes));
        for (int i = 0; i < 4; ++i) out[start + 28 + i] = uint8_t(total >> (8 * i));
        const uint32_t c = crc32(out.data() + start, 40);
        for (int i = 0; i < 4; ++i) out[start + 40 + i] = uint8_t(c >> (8 * i));
        ++seq;
    }

    void build_trees() { build_trees_on(g.lay, mag.data(), dmax.data(), lmax.data()); }

    // ---- lossless / near-lossless profile: independent LOCO-I stripes
    void encode_loco(const uint16_t* frame, uint64_t ts, std::vector<uint8_t>& out, const std::vector<uint8_t>& meta) {
        const int W = cfg.width, H = cfg.height;
        int sr = cfg.stripe_rows;
        int S = (H + sr - 1) / sr;
        if (S > 255) throw std::invalid_argument("too many stripes (raise stripe_rows)");
        LocoParams lp;
        lp.maxval = (1 << cfg.bit_depth) - 1;
        lp.near = cfg.near;
        lp.ac = cfg.loco_ac;
        for (size_t i = 0; i < size_t(W) * H; ++i)
            if (frame[i] > lp.maxval) throw std::invalid_argument("sample exceeds bit_depth");
        recon.resize(size_t(W) * H);
        std::vector<std::vector<uint8_t>> pays;
        for (;;) {
            pays.assign(S, {});
            parallel_for(S, cfg.threads, [&](int k) {
                const int y0 = k * sr, n = std::min(sr, H - y0);
                loco_encode_stripe(frame + size_t(y0) * W, W, n, lp, pays[k], recon.data() + size_t(y0) * W);
            });
            // auto stripe height: each stripe re-learns its statistics, which does not pay off
            // below ~3 KB per stripe (measured); double the stripe height until it does
            if (!cfg.loco_auto_stripes || S <= 1) break;
            size_t tot = 0; for (auto& p : pays) tot += p.size();
            if (tot / size_t(S) >= kLocoMinStripeBytes) break;
            sr *= 2; S = (H + sr - 1) / sr;
        }
        const bool bcrc = cfg.block_crc;
        const size_t start = out.size();
        std::vector<uint8_t> hdr(kHdr, 0);
        std::memcpy(hdr.data(), kMagic, 4);
        hdr[4] = 1; hdr[5] = 0;
        hdr[6] = uint8_t((lp.near == 0 ? F_LOSSLESS : 0) | F_LOCO | (bcrc ? F_BLOCKCRC : 0));
        hdr[8] = uint8_t(W); hdr[9] = uint8_t(W >> 8); hdr[10] = uint8_t(H); hdr[11] = uint8_t(H >> 8);
        hdr[12] = uint8_t(cfg.bit_depth); hdr[13] = 1; hdr[14] = uint8_t(S);
        for (int i = 0; i < 4; ++i) hdr[16 + i] = uint8_t(seq >> (8 * i));
        for (int i = 0; i < 8; ++i) hdr[20 + i] = uint8_t(ts >> (8 * i));
        hdr[32] = uint8_t(meta.size()); hdr[33] = uint8_t(meta.size() >> 8);
        hdr[35] = uint8_t(lp.near);
        hdr[38] = lp.ac ? 1 : 0;                     // LOCO entropy back-end: 0 Golomb, 1 range coder
        hdr[36] = uint8_t(sr); hdr[37] = uint8_t(sr >> 8);
        out.insert(out.end(), hdr.begin(), hdr.end());
        if (!meta.empty()) {
            out.insert(out.end(), meta.begin(), meta.end());
            put16(out, crc16(meta.data(), meta.size()));
        }
        for (int k = 0; k < S; ++k) {
            //  FF E9 | sid | seq8 | y0 u16 | rows u16 | len u32 | crc16(previous 12 bytes)
            const auto& pay = pays[k];
            std::vector<uint8_t> sh = {0xFF, 0xE9, uint8_t(k), uint8_t(seq)};
            put16(sh, uint32_t(k * sr)); put16(sh, uint32_t(std::min(sr, H - k * sr)));
            put32(sh, uint32_t(pay.size()));
            put16(sh, crc16(sh.data(), sh.size()));
            out.insert(out.end(), sh.begin(), sh.end());
            for (size_t o = 0; o < pay.size(); o += kBlk) {
                const size_t len = std::min<size_t>(kBlk, pay.size() - o);
                out.insert(out.end(), pay.begin() + o, pay.begin() + o + len);
                if (bcrc) put16(out, crc16(pay.data() + o, len));
            }
        }
        const uint32_t total = uint32_t(out.size() - start);
        for (int i = 0; i < 4; ++i) out[start + 28 + i] = uint8_t(total >> (8 * i));
        const uint32_t c = crc32(out.data() + start, 40);
        for (int i = 0; i < 4; ++i) out[start + 40 + i] = uint8_t(c >> (8 * i));
        ++seq;
    }

    void encode(const uint16_t* frame, uint64_t ts, std::vector<uint8_t>& out, const std::vector<uint8_t>& meta) {
        if (meta.size() > 65535) throw std::invalid_argument("metadata too large");
        if (loco) { encode_loco(frame, ts, out, meta); return; }
        {
            const uint32_t maxv = (1u << cfg.bit_depth) - 1;
            for (size_t i = 0, n = size_t(g.W) * g.H; i < n; ++i)
                if (frame[i] > maxv) throw std::invalid_argument("sample exceeds bit_depth");
        }
        if (cfg.tile) { encode_tiled(frame, ts, out, meta); return; }
        const size_t N = size_t(g.Wp) * g.Hp;
        using tclk = std::chrono::steady_clock;
        auto T0 = tclk::now();
        double tm_me = 0, tm_mc = 0, tm_dwt = 0, tm_code = 0, tm_alloc = 0, tm_dec = 0;
        auto lap = [&](double& acc) { auto n = tclk::now(); acc += std::chrono::duration<double, std::milli>(n - T0).count(); T0 = n; };
        bool intra = cfg.gop <= 1 || !have_ref || force_intra || since_i + 1 >= uint32_t(cfg.gop);
        force_intra = false;
        std::vector<uint8_t> mvbytes;
        int32_t dc = 0;
        double e_inter = 0, e_intra = 0;
        if (!intra) {   // ---- motion-compensated prediction, then frame-level intra/inter decision
            g.pad_in(frame, cur.data(), 0);
            MotionParams mp;
            mp.bs = cfg.mv_block; mp.range = cfg.mv_range; mp.grange = cfg.global_range; mp.lambda = cfg.mv_lambda;
            estimate_motion(cur.data(), refp, mp, mf, cfg.threads);
            lap(tm_me);
            compensate_obmc(refp, mf, pred.data());
            lap(tm_mc);
            {
                // cost proxies: mean |inter residual| vs mean |MED (LOCO-I) intra residual|
                int64_t si = 0, sa = 0;
                const int Wp = g.Wp;
                for (int y = 1; y < g.H; y += 2) {
                    const int32_t* c = cur.data() + size_t(y) * Wp;
                    const int32_t* u = c - Wp;
                    const int32_t* pr = pred.data() + size_t(y) * Wp;
                    for (int x = 1; x < g.W; ++x) {
                        si += std::abs(c[x] - pr[x]);
                        const int32_t a = c[x - 1], b = u[x], cc = u[x - 1];
                        const int32_t mx = std::max(a, b), mn = std::min(a, b);
                        const int32_t med = cc >= mx ? mn : (cc <= mn ? mx : a + b - cc);
                        sa += std::abs(c[x] - med);
                    }
                }
                e_inter = double(si); e_intra = double(sa);
                if (cfg.scene_cut_q8 > 0 && si * 256 > sa * int64_t(cfg.scene_cut_q8)) intra = true;
            }
            if (!intra) {
                encode_motion(mf, mvbytes);
                for (size_t i = 0; i < N; ++i) coef[i] = cur[i] - pred[i];
            }
        }
        if (intra) {
            uint64_t sum = 0;
            for (size_t i = 0; i < size_t(g.W) * g.H; ++i) sum += frame[i];
            dc = int32_t((sum + uint64_t(g.W) * g.H / 2) / (uint64_t(g.W) * g.H));
            g.pad_in(frame, cur.data(), dc);
            std::copy(cur.begin(), cur.end(), coef.begin());
        }
        since_i = intra ? 0 : since_i + 1;
        // ---- budget: leaky-bucket (VBV) rate control. The channel drains frame_bytes per frame
        // period; the encoder buffer never holds more than vbv = vbv_frames * frame_bytes, so the
        // queueing delay is bounded by vbv_frames frame periods and every window of n frames
        // uses at most n * frame_bytes + vbv bytes. vbv_frames = 1 is strict per-frame CBR.
        const int64_t R = cfg.frame_bytes;
        const int64_t vbv = std::max<int64_t>(R, R * cfg.vbv_frames_q8 / 256);
        int64_t budget64;
        if (intra) budget64 = R * std::max(256, cfg.i_frame_scale_q8) / 256;
        else {
            const int64_t left = cfg.gop > 1 ? std::max<int64_t>(1, int64_t(cfg.gop) - int64_t(since_i)) : 1;
            budget64 = std::max<int64_t>(R / 4, R - vbv_fill / std::min<int64_t>(left, 8));
        }
        budget64 = std::min(budget64, vbv - vbv_fill);
        const uint32_t budget = uint32_t(std::max<int64_t>(budget64, 64));
        // ---- transform + weighting + zerotree maxima
        if (g.ps) for (size_t i = 0; i < N; ++i) coef[i] *= int32_t(1) << g.ps;
        dwt_forward(coef.data(), g.Wp, g.Hp, g.L, g.wv, tmp);
        for (size_t i = 0; i < N; ++i) {
            int32_t c = coef[i];
            uint64_t m = uint64_t(c < 0 ? -int64_t(c) : int64_t(c));
            mag[i] = uint32_t((m * uint64_t(g.wfix[g.lay.band[i]]) + 32768) >> 16);
            neg[i] = c < 0;
        }
        ecs.resize(N);
        build_trees();
        lap(tm_dwt);
        // ---- fixed overhead. Group payloads can exceed the u16 length field only when the
        // frame budget is >= 64 KiB or unlimited: then group lengths are coded as u32 (F_LEN32).
        const bool bcrc = cfg.block_crc;
        const bool len32 = cfg.lossless || cfg.frame_bytes == 0 || budget >= 65536u;
        grp_hdr = len32 ? kGrpHdr32 : kGrpHdr;
        const int red = intra && !cfg.lossless ? std::max(0, std::min(cfg.redundancy, 255)) : 0;
        const size_t fixed = kHdr + (meta.empty() ? 0 : meta.size() + 2) + (intra ? 0 : 4 + mvbytes.size()) +
                             (red ? kRedHdr + size_t(g.G) * (red + 4) : 0);
        // ---- embedded coding, bit-plane synchronous across groups (stop once over budget)
        for (int k = 0; k < g.G; ++k)
            genc[k].begin(&g.lay, &g.groups[k], mag.data(), neg.data(), dmax.data(), lmax.data(), &ecs, cfg.cp_interval);
        int top = -1;
        for (auto& e : genc) top = std::max(top, e.nmax());
        const bool unlimited = cfg.lossless || cfg.frame_bytes == 0;
        for (int n = top; n >= 0; --n) {
            parallel_for(g.G, cfg.threads, [&](int k) { if (genc[k].next_plane() == n) genc[k].code_plane(n); });
            if (!unlimited) {
                size_t tot = fixed;
                for (auto& e : genc) tot += grp_hdr + payload_wire_size(e.bytes_now(), bcrc);
                if (tot >= budget) break;
            }
        }
        lap(tm_code);
        // ---- rate allocation (equal distortion-rate slope across groups)
        std::vector<int> pick(g.G, 0);
        if (unlimited) for (int k = 0; k < g.G; ++k) pick[k] = int(genc[k].cps().size()) - 1;
        else allocate(budget, fixed, bcrc, pick, genc, size_t(grp_hdr));
        // ---- emit
        const size_t start = out.size();
        std::vector<uint8_t> hdr(kHdr, 0);
        std::memcpy(hdr.data(), kMagic, 4);
        hdr[4] = 1; hdr[5] = intra ? 0 : 1;
        hdr[6] = uint8_t((cfg.lossless ? F_LOSSLESS : 0) | (intra ? 0 : F_MOTION) | (bcrc ? F_BLOCKCRC : 0) | (len32 ? F_LEN32 : 0));
        hdr[7] = uint8_t(g.wv);
        hdr[8] = uint8_t(g.W); hdr[9] = uint8_t(g.W >> 8); hdr[10] = uint8_t(g.H); hdr[11] = uint8_t(g.H >> 8);
        hdr[12] = uint8_t(g.bd); hdr[13] = uint8_t(g.L); hdr[14] = uint8_t(g.G); hdr[15] = uint8_t(g.frac);
        for (int i = 0; i < 4; ++i) hdr[16 + i] = uint8_t(seq >> (8 * i));
        for (int i = 0; i < 8; ++i) hdr[20 + i] = uint8_t(ts >> (8 * i));
        hdr[32] = uint8_t(meta.size()); hdr[33] = uint8_t(meta.size() >> 8);
        hdr[34] = uint8_t(cfg.mv_block);
        hdr[36] = uint8_t(dc); hdr[37] = uint8_t(dc >> 8);
        hdr[38] = uint8_t(g.ps);                     // precision shift (0 = none; older streams)
        out.insert(out.end(), hdr.begin(), hdr.end());
        if (!meta.empty()) {
            out.insert(out.end(), meta.begin(), meta.end());
            put16(out, crc16(meta.data(), meta.size()));
        }
        if (!intra) {
            put16(out, uint32_t(mvbytes.size()));
            put16(out, crc16(mvbytes.data(), mvbytes.size()));
            out.insert(out.end(), mvbytes.begin(), mvbytes.end());
        }
        std::vector<std::vector<uint8_t>> pays(g.G);
        std::vector<uint32_t> nsyms(g.G);
        for (int k = 0; k < g.G; ++k) {
            const Checkpoint& cp = genc[k].cps()[pick[k]];
            pays[k] = genc[k].terminate_at(cp);
            nsyms[k] = cp.nsym;
            const auto& pay = pays[k];
            std::vector<uint8_t> gh;
            gh.push_back(0xFF); gh.push_back(0xE7); gh.push_back(uint8_t(k)); gh.push_back(uint8_t(int8_t(genc[k].nmax())));
            gh.push_back(uint8_t(seq));
            put32(gh, cp.nsym);
            if (len32) put32(gh, uint32_t(pay.size()));
            else put16(gh, uint32_t(pay.size()));
            put16(gh, crc16(gh.data(), gh.size()));
            out.insert(out.end(), gh.begin(), gh.end());
            for (size_t o = 0; o < pay.size(); o += kBlk) {
                size_t len = std::min<size_t>(kBlk, pay.size() - o);
                out.insert(out.end(), pay.begin() + o, pay.begin() + o + len);
                if (bcrc) put16(out, crc16(pay.data() + o, len));
            }
        }
        if (red) {
            std::vector<uint8_t> rh = {0xFF, 0xE8, uint8_t(seq), uint8_t(g.G)};
            put16(rh, crc16(rh.data(), rh.size()));
            out.insert(out.end(), rh.begin(), rh.end());
            for (int k = 0; k < g.G; ++k) {
                const size_t len = std::min<size_t>(size_t(red), pays[k].size());
                std::vector<uint8_t> e = {uint8_t(int8_t(genc[k].nmax())), uint8_t(len)};
                e.insert(e.end(), pays[k].begin(), pays[k].begin() + len);
                put16(e, crc16(e.data(), e.size()));
                out.insert(out.end(), e.begin(), e.end());
            }
        }
        const uint32_t total = uint32_t(out.size() - start);
        vbv_fill = std::max<int64_t>(0, vbv_fill + int64_t(total) - int64_t(cfg.frame_bytes));
        for (int i = 0; i < 4; ++i) out[start + 28 + i] = uint8_t(total >> (8 * i));
        const uint32_t c = crc32(out.data() + start, 40);
        for (int i = 0; i < 4; ++i) out[start + 40 + i] = uint8_t(c >> (8 * i));
        lap(tm_alloc);
        // ---- local decode (closed-loop reference): decode own group streams, reuse prediction
        if (cfg.gop > 1) {
            dcs.resize(N);
            parallel_for(g.G, cfg.threads, [&](int k) {
                decode_group(g.lay, g.groups[k], pays[k].data(), pays[k].size(), pays[k].size(), nsyms[k],
                             genc[k].nmax(), dcs);
            });
            g.reconstruct(dcs, intra ? nullptr : pred.data(), dc, img, tmp, recon);
            ref.resize(N);
            g.pad_in(recon.data(), ref.data(), 0);
            refp.build(ref.data(), g.Wp, g.Hp, kRefMargin);
            have_ref = true;
        }
        lap(tm_dec);
        if (std::getenv("EWC_TIME"))
            fprintf(stderr, "time seq %u %c e_inter/e_intra %.3f me %.2f mc %.2f dwt+trees %.2f code %.2f alloc+emit %.2f localdec %.2f mvbytes %zu\n",
                    seq, intra ? 'I' : 'P', e_intra > 0 ? e_inter / e_intra : 0.0, tm_me, tm_mc, tm_dwt, tm_code, tm_alloc, tm_dec, mvbytes.size());
        ++seq;
    }

    // Rate allocation over the coders `ge` (equal distortion-rate slope); `hdr` = header bytes per coder.
    // vidx: per-unit index is 1 + varint(nsym) + varint(len) bytes (compact tile index; `hdr` then 0)
    void allocate(uint32_t budget, size_t fixed, bool bcrc, std::vector<int>& pick, std::vector<GroupEncoder>& ge, size_t hdr,
                  bool vidx = false) {
        const int G = int(ge.size());
        auto rate_of = [&](int k, int i) {
            const auto& cp = ge[k].cps()[i];
            const size_t p = payload_wire_size(cp.nsym ? RCEncoder::terminated_size(cp.st) : 0, bcrc);
            return vidx ? p + 1 + vlen(cp.nsym) + vlen(uint32_t(p)) : p;
        };
        // upper convex hull per group over (rate, distortion reduction)
        struct HP { int idx; double r, d; };
        std::vector<std::vector<HP>> hull(G);
        for (int k = 0; k < G; ++k) {
            auto& Hh = hull[k];
            const auto& cps = ge[k].cps();
            for (int i = 0; i < int(cps.size()); ++i) {
                const double r = double(rate_of(k, i)), d = cps[i].dist;
                if (!Hh.empty() && r <= Hh.back().r) { if (d > Hh.back().d) Hh.back() = {i, Hh.back().r, d}; continue; }
                if (!Hh.empty() && d <= Hh.back().d) continue;
                Hh.push_back({i, r, d});
                while (Hh.size() >= 3) {
                    const HP& a = Hh[Hh.size() - 3]; const HP& b = Hh[Hh.size() - 2]; const HP& c = Hh.back();
                    if ((c.d - b.d) / (c.r - b.r) >= (b.d - a.d) / (b.r - a.r)) Hh.erase(Hh.end() - 2); else break;
                }
            }
        }
        auto choose = [&](double lam, std::vector<int>& sel) {
            size_t tot = fixed;
            for (int k = 0; k < G; ++k) {
                const auto& Hh = hull[k];
                int best = 0; double bv = -1e300;
                for (int i = 0; i < int(Hh.size()); ++i) {
                    double v = Hh[i].d - lam * Hh[i].r;
                    if (v > bv) { bv = v; best = i; }
                }
                sel[k] = best;
                tot += hdr + size_t(Hh[best].r);
            }
            return tot;
        };
        std::vector<int> sel(G);
        double lo = 0, hi = 1;
        while (choose(hi, sel) > budget && hi < 1e30) hi *= 4;
        for (int it = 0; it < 60; ++it) {
            double mid = 0.5 * (lo + hi);
            if (choose(mid, sel) > budget) lo = mid; else hi = mid;
        }
        choose(hi, sel);
        // fill leftover bytes greedily with any later raw checkpoint (best gain per byte)
        std::vector<int> raw(G);
        size_t tot = fixed;
        for (int k = 0; k < G; ++k) { raw[k] = hull[k][sel[k]].idx; tot += hdr + rate_of(k, raw[k]); }
        for (;;) {
            int bk = -1, bi = -1; double bs = 0;
            for (int k = 0; k < G; ++k) {
                const auto& cps = ge[k].cps();
                const size_t r0 = rate_of(k, raw[k]);
                const double d0 = cps[raw[k]].dist;
                for (int i = raw[k] + 1; i < int(cps.size()); ++i) {
                    const size_t r = rate_of(k, i);
                    if (tot - r0 + r > budget) break;
                    double s = r <= r0 ? (cps[i].dist > d0 ? 1e300 : 0) : (cps[i].dist - d0) / double(r - r0);
                    if (s > bs) { bs = s; bk = k; bi = i; }
                }
            }
            if (bk < 0) break;
            tot = tot - rate_of(bk, raw[bk]) + rate_of(bk, bi);
            raw[bk] = bi;
        }
        for (int k = 0; k < G; ++k) pick[k] = raw[k];
    }
};

Encoder::Encoder(const EncoderConfig& cfg) : p_(new Impl(cfg)) {}
Encoder::~Encoder() = default;
void Encoder::encode(const uint16_t* frame, uint64_t ts, std::vector<uint8_t>& out, const std::vector<uint8_t>& meta) {
    p_->encode(frame, ts, out, meta);
}
const std::vector<uint16_t>& Encoder::last_reconstruction() const { return p_->recon; }
void Encoder::request_intra() { p_->force_intra = true; }
const EncoderConfig& Encoder::config() const { return p_->cfg; }

// ============================================================================ decoder
struct Decoder::Impl {
    int threads = 1;
    Geom g;
    CoefState cs;
    std::vector<int32_t> img, tmp, pred, ref;
    RefPlane refp;
    std::vector<uint16_t> last;     // last output frame (for concealment)
    bool have_ref = false;
    uint32_t ref_seq = 0;           // sequence of the frame held in ref / refp (valid if have_ref)
    bool ref_reliable = false;      // that frame was decoded reliably (FrameInfo::reliable())
    MotionField mf;

    // Intra frame with lost / weak groups: replace their rows with the previous output frame.
    // (Without a previous frame the LL-domain interpolation done in reconstruct() is kept.)
    void conceal_intra(const std::vector<uint8_t>& lost, std::vector<uint16_t>& out, int W, int H, int L) {
        const int rows_per_ll = 1 << L;
        for (int k = 0; k < g.G; ++k) {
            if (!lost[k]) continue;
            const int y0 = g.groups[k].r0 * rows_per_ll, y1 = std::min(H, g.groups[k].r1 * rows_per_ll);
            for (int y = y0; y < y1; ++y)
                std::memcpy(out.data() + size_t(y) * W, last.data() + size_t(y) * W, sizeof(uint16_t) * W);
        }
    }

    // LOCO-I frame: stripes are located by marker, decoded in parallel; rows that could not be
    // decoded from CRC-verified bytes are concealed from the previous frame (or the row above).
    bool decode_loco(const uint8_t* data, size_t size, size_t p, size_t q, size_t end, const uint8_t* h,
                     std::vector<uint16_t>& out, FrameInfo& info) {
        (void)size;
        const int W = int(get16(h + 8)), H = int(get16(h + 10)), bd = h[12], S = h[14];
        const int sr = int(get16(h + 36));
        const bool bcrc = h[6] & F_BLOCKCRC;
        LocoParams lp;
        lp.maxval = (1 << bd) - 1;
        lp.near = h[35];
        lp.ac = h[38] == 1;
        const uint8_t seq8 = uint8_t(info.sequence);
        struct SC { bool found = false; int y0 = 0, rows = 0; size_t len = 0, valid = 0; std::vector<uint8_t> d; };
        std::vector<SC> st(static_cast<size_t>(S));
        while (q + kStripeHdr <= end) {
            const uint8_t* c = data + q;
            const bool ok = c[0] == 0xFF && c[1] == 0xE9 && c[2] < S && c[3] == seq8 && crc16(c, 12) == get16(c + 12);
            if (!ok) { ++q; continue; }
            const int sid = c[2], y0 = int(get16(c + 4)), rows = int(get16(c + 6));
            const size_t len = get32(c + 8);
            q += kStripeHdr;
            if (st[sid].found || y0 != sid * sr || rows < 1 || y0 + rows > H || len > (size_t(1) << 28)) continue;
            SC& x = st[sid];
            x.found = true; x.y0 = y0; x.rows = rows; x.len = len;
            bool broken = false;
            for (size_t o = 0; o < len;) {
                const size_t bl = std::min<size_t>(kBlk, len - o), need = bl + (bcrc ? 2 : 0);
                if (q + need > end) {
                    const size_t take = std::min(end > q ? end - q : 0, bl);
                    x.d.insert(x.d.end(), data + q, data + q + take);
                    if (!bcrc && !broken) x.valid += take;
                    broken = true; q = end; break;
                }
                x.d.insert(x.d.end(), data + q, data + q + bl);
                const bool good = !bcrc || crc16(data + q, bl) == get16(data + q + bl);
                if (good && !broken) x.valid += bl; else broken = true;
                q += need; o += bl;
            }
            x.d.resize(len, 0);
        }
        out.assign(size_t(W) * H, 0);
        std::vector<int> rows_ok(static_cast<size_t>(S), 0);
        parallel_for(S, threads, [&](int k) {
            const SC& x = st[k];
            if (!x.found) return;
            rows_ok[k] = loco_decode_stripe(x.d.data(), x.d.size(), x.valid, W, x.rows, lp, out.data() + size_t(x.y0) * W);
        });
        info.groups_total = S;
        std::vector<uint8_t> bad(static_cast<size_t>(H), 0);
        bool complete = true;
        for (int k = 0; k < S; ++k) {
            const int y0 = k * sr, n = std::min(sr, H - y0);
            if (!st[k].found) ++info.groups_lost;
            else if (rows_ok[k] < n) ++info.groups_partial;
            else ++info.groups_ok;
            for (int y = y0 + (st[k].found ? rows_ok[k] : 0); y < y0 + n; ++y) { bad[y] = 1; complete = false; }
        }
        const bool have_last = last.size() == out.size();
        for (int y = 0; y < H; ++y) {
            if (!bad[y]) continue;
            uint16_t* o = out.data() + size_t(y) * W;
            if (have_last) std::memcpy(o, last.data() + size_t(y) * W, sizeof(uint16_t) * W);
            else if (y > 0) std::memcpy(o, o - W, sizeof(uint16_t) * W);
            else std::fill(o, o + W, uint16_t(1 << (bd - 1)));
        }
        info.lossless_complete = lp.near == 0 && complete;
        info.bytes = uint32_t(end - p);
        last = out;
        have_ref = false;   // LOCO frames do not serve as wavelet-path references
        pos_out = std::max(end, p + 1);
        return true;
    }
    size_t pos_out = 0;

    // Tiled frame: tile rows located by marker; each tile decoded independently from the
    // CRC-verified prefix of its own bytes. Lost or weak tiles are taken from the previous frame.
    Geom tg;
    std::vector<CoefState> tcs;
    bool decode_tiled(const uint8_t* data, size_t p, size_t q, size_t end, const uint8_t* h, std::vector<uint16_t>& out, FrameInfo& info) {
        const int W = int(get16(h + 8)), H = int(get16(h + 10)), bd = h[12], L = h[13], frac = h[15];
        const Wavelet wv = Wavelet(h[7]);
        const bool bcrc = h[6] & F_BLOCKCRC;
        const int32_t dc = int32_t(get16(h + 36));
        const int lg = h[34];
        const bool have_last = last.size() == size_t(W) * H;
        auto conceal_all = [&]() {
            info.concealed = true;
            out = have_last ? last : std::vector<uint16_t>(size_t(W) * H, uint16_t(1 << (bd - 1)));
            last = out; have_ref = false; pos_out = std::max(end, p + 1);
            return true;
        };
        const int ps = h[38];
        if (lg < 4 || lg > 8 || L < 1 || (1 << lg) < (2 << L) || ps > 8) return conceal_all();
        const int T = 1 << lg;
        if (!tg.same(T, T, L, 1, bd, frac, wv, false, ps)) tg.init(T, T, L, 1, bd, frac, wv, false, ps);
        const int ntx = (W + T - 1) / T, nty = (H + T - 1) / T, nt = ntx * nty;
        if (ntx > 255) return conceal_all();
        const size_t A = size_t(T) * T;
        if (tcs.size() != size_t(nt)) tcs.resize(nt);
        struct TE { int nmax = -1; uint32_t nsym = 0; size_t len = 0, valid = 0; const uint8_t* ptr = nullptr; bool found = false; };
        std::vector<TE> te(nt);
        std::vector<std::vector<uint8_t>> rowbuf(nty);
        std::vector<uint8_t> rowfound(nty, 0);
        const uint8_t seq8 = uint8_t(info.sequence);
        const bool cidx = h[35] == 1;
        if (h[35] > 1) return conceal_all();
        const size_t hl = size_t(kTileRowHdr) + size_t(ntx) * kTileEnt;
        struct TR { std::vector<uint8_t> d; int nmax = -1; bool ok = false; };
        std::vector<TR> tred(nt);
        while (q + 7 <= end) {
            const uint8_t* c = data + q;
            if (c[0] == 0xFF && c[1] == 0xE5 && c[2] == seq8 && int(get16(c + 3)) == nt && crc16(c, 5) == get16(c + 5)) {
                size_t r = q + 7;
                for (int t = 0; t < nt && r + 2 <= end; ++t) {
                    const size_t len = data[r + 1];
                    if (r + 2 + len + 2 > end) break;
                    if (crc16(data + r, 2 + len) == get16(data + r + 2 + len) && int(int8_t(data[r])) <= 30) {
                        tred[t].nmax = int(int8_t(data[r]));
                        tred[t].d.assign(data + r + 2, data + r + 2 + len);
                        tred[t].ok = true;
                    }
                    r += 2 + len + 2;
                }
                q = r;
                continue;
            }
            size_t rhl = hl;                           // this row header's length (before its crc16)
            if (cidx) {
                if (q + kTileRowHdrC + 2 > end) { ++q; continue; }
                const size_t el = get16(c + 6);
                if (el < size_t(ntx) * 3 || el > size_t(ntx) * 9) { ++q; continue; }
                rhl = kTileRowHdrC + el;
            }
            if (q + rhl + 2 > end) { ++q; continue; }
            const int row = int(get16(c + 2));
            if (!(c[0] == 0xFF && c[1] == 0xE6 && c[4] == seq8 && c[5] == ntx && row < nty && !rowfound[row] &&
                  crc16(c, rhl) == get16(c + rhl))) { ++q; continue; }
            std::vector<size_t> offs(ntx), lens(ntx);
            std::vector<int> nmx(ntx);
            std::vector<uint32_t> nsy(ntx);
            size_t sum = 0;
            bool parsed = true;
            const uint8_t* p = c + kTileRowHdrC;
            for (int k = 0; k < ntx && parsed; ++k) {
                if (cidx) {
                    uint32_t ns = 0, ln = 0;
                    if (p >= c + rhl) { parsed = false; break; }
                    nmx[k] = int(int8_t(*p++));
                    parsed = getv(p, c + rhl, ns) && getv(p, c + rhl, ln);
                    nsy[k] = ns; lens[k] = ln;
                } else {
                    const uint8_t* e = c + kTileRowHdr + size_t(k) * kTileEnt;
                    nmx[k] = int(int8_t(e[0]));
                    nsy[k] = uint32_t(e[1]) | uint32_t(e[2]) << 8 | uint32_t(e[3]) << 16;
                    lens[k] = size_t(e[4]) | size_t(e[5]) << 8 | size_t(e[6]) << 16;
                }
                offs[k] = sum; sum += lens[k];
            }
            if (!parsed || (cidx && p != c + rhl)) { ++q; continue; }
            rowfound[row] = 1;
            q += rhl + 2;
            if (sum > (size_t(1) << 28)) continue;
            auto& rb = rowbuf[row];
            rb.assign(sum, 0);
            std::vector<uint8_t> good((sum + kBlk - 1) / kBlk, 0);
            for (size_t o = 0, b = 0; o < sum; ++b) {
                const size_t bl = std::min<size_t>(kBlk, sum - o), need = bl + (bcrc ? 2 : 0);
                if (q + need > end) {                  // unit truncated: keep what is there, unverified
                    const size_t take = std::min(end > q ? end - q : 0, bl);
                    std::memcpy(rb.data() + o, data + q, take);
                    q = end; break;
                }
                std::memcpy(rb.data() + o, data + q, bl);
                good[b] = !bcrc || crc16(data + q, bl) == get16(data + q + bl);
                q += need; o += bl;
            }
            for (int k = 0; k < ntx; ++k) {
                const int t = row * ntx + k;
                size_t v = 0;
                while (v < lens[k] && good[(offs[k] + v) / kBlk]) v = std::min(lens[k], ((offs[k] + v) / kBlk + 1) * kBlk - offs[k]);
                if (nmx[k] > 30) continue;
                te[t] = {nmx[k], nsy[k], lens[k], v, rb.data() + offs[k], true};
            }
        }
        out.assign(size_t(W) * H, 0);
        std::vector<GroupDecodeResult> gr(nt);
        std::vector<uint8_t> weak(nt, 0), used_red(nt, 0);
        std::vector<std::vector<uint16_t>> tout(nt);
        auto put_tile = [&](int t, const std::vector<uint16_t>* src_tile, bool from_last) {
            const int tx = t % ntx, ty = t / ntx;
            const int y1 = std::min(T, H - ty * T), x1 = std::min(T, W - tx * T);
            for (int y = 0; y < y1; ++y) {
                const size_t o = size_t(ty * T + y) * W + size_t(tx) * T;
                if (from_last) std::memcpy(out.data() + o, last.data() + o, sizeof(uint16_t) * size_t(x1));
                else std::memcpy(out.data() + o, src_tile->data() + size_t(y) * T, sizeof(uint16_t) * size_t(x1));
            }
        };
        // pass 1: decode every tile from its own bytes; weak = lost, or damaged so early that fewer
        // than 4 bit planes survived (a tile intact but coded coarsely by the allocator is not weak)
        parallel_for(nt, threads, [&](int t) {
            tcs[t].resize(A);
            if (te[t].found) gr[t] = decode_group(tg.lay, tg.groups[0], te[t].ptr, te[t].len, te[t].valid, te[t].nsym, te[t].nmax, tcs[t]);
            weak[t] = !te[t].found || (te[t].valid < te[t].len && !gr[t].complete && te[t].nmax >= 0 &&
                                       gr[t].last_plane > te[t].nmax - 4);
            std::vector<int32_t> img, tmp;
            tg.reconstruct(tcs[t], nullptr, dc, img, tmp, tout[t]);
            if (!weak[t]) put_tile(t, &tout[t], false);
        });
        // concealment policy for weak tiles: if the intact tiles show the scene barely changed since
        // the previous frame (mean |difference| below half the scene's own spread), copy weak tiles
        // from it; otherwise (scene change, unrelated stills) the previous frame is no guide: use the
        // tile's redundant prefix, or else its own partial decode.
        bool similar = false;
        if (have_last) {
            double sd = 0, s1 = 0, s2 = 0; size_t n = 0;
            for (int t = 0; t < nt; ++t) {
                if (weak[t]) continue;
                const int tx = t % ntx, ty = t / ntx;
                const int y1 = std::min(T, H - ty * T), x1 = std::min(T, W - tx * T);
                for (int y = 0; y < y1; y += 4)
                    for (int x = 0; x < x1; x += 4) {
                        const size_t o = size_t(ty * T + y) * W + size_t(tx) * T + size_t(x);
                        const double v = out[o];
                        sd += std::abs(v - double(last[o])); s1 += v; s2 += v * v; ++n;
                    }
            }
            if (n) {
                const double mean = s1 / double(n), spread = std::sqrt(std::max(0.0, s2 / double(n) - mean * mean));
                similar = sd / double(n) < 0.5 * std::max(spread, 1.0);
            }
        }
        for (int t = 0; t < nt; ++t) {
            if (!weak[t]) continue;
            if (similar) { put_tile(t, nullptr, true); continue; }
            if (tred[t].ok && (!te[t].found || te[t].valid < tred[t].d.size())) {
                CoefState cs; cs.resize(A);
                decode_group(tg.lay, tg.groups[0], tred[t].d.data(), tred[t].d.size(), tred[t].d.size(), 0xFFFFFFFFu, tred[t].nmax, cs);
                std::vector<int32_t> img, tmp;
                tg.reconstruct(cs, nullptr, dc, img, tmp, tout[t]);
                used_red[t] = 1;
            }
            put_tile(t, &tout[t], false);   // redundant prefix, or the tile's own partial decode (dc if lost)
        }
        info.groups_total = nt;
        for (int t = 0; t < nt; ++t) {
            if (!te[t].found && !used_red[t]) ++info.groups_lost;
            else if (used_red[t] || te[t].valid < te[t].len) ++info.groups_partial;
            else ++info.groups_ok;
        }
        info.bytes = uint32_t(end - p);
        last = out;
        have_ref = false;   // tiled frames are intra-only and do not serve as wavelet-path references
        pos_out = std::max(end, p + 1);
        return true;
    }

    bool decode_next(const uint8_t* data, size_t size, size_t& pos, std::vector<uint16_t>& out, FrameInfo& info,
                     int& width, int& height) {
        info = FrameInfo();
        // ---- locate a valid frame header (resynchronise if needed)
        size_t p = pos;
        while (p + kHdr <= size && !header_valid(data + p, size - p)) ++p;
        if (p + kHdr > size) { pos = size; return false; }
        info.header_ok = (p == pos);
        info.skipped_bytes = uint32_t(std::min<size_t>(p - pos, 0xFFFFFFFFu));
        const uint8_t* h = data + p;
        const int ftype = h[5], flags = h[6];
        const Wavelet wv = Wavelet(h[7]);
        const int W = int(get16(h + 8)), H = int(get16(h + 10));
        const int bd = h[12], L = h[13], G = h[14], frac = h[15];
        info.sequence = get32(h + 16);
        info.timestamp_us = uint64_t(get32(h + 20)) | (uint64_t(get32(h + 24)) << 32);
        const uint32_t total = get32(h + 28);
        const uint32_t meta_len = get16(h + 32);
        const int mvb = (h[34] == 8 || h[34] == 16 || h[34] == 32 || h[34] == 64) ? h[34] : 16;
        const bool lossless = flags & F_LOSSLESS, bcrc = flags & F_BLOCKCRC;
        const int32_t dc = int32_t(get16(h + 36));
        info.is_intra = ftype == 0;
        info.bit_depth = bd;
        width = W; height = H;
        const int ps = (flags & (F_LOCO | F_TILED)) || lossless ? 0 : std::min<int>(h[38], 8);
        if (!(flags & (F_LOCO | F_TILED)) && !g.same(W, H, L, G, bd, frac, wv, lossless, ps)) {
            g.init(W, H, L, G, bd, frac, wv, lossless, ps);
            have_ref = false;
        }
        size_t end = std::min(size, p + size_t(total));
        // a frame cannot overlap the next valid frame header (protects against deleted bytes)
        for (size_t s = p + kHdr; s + kHdr <= end; ++s)
            if (data[s] == 'E' && header_valid(data + s, size - s)) { end = s; break; }
        info.bytes = uint32_t(end - p);
        size_t q = p + kHdr;
        if (meta_len) {
            if (q + meta_len + 2 <= end && crc16(data + q, meta_len) == get16(data + q + meta_len))
                info.metadata.assign(data + q, data + q + meta_len);
            q += meta_len + 2;
        }
        if (flags & F_LOCO) { const bool r = decode_loco(data, size, p, q, end, h, out, info); pos = pos_out; return r; }
        if (flags & F_TILED) { const bool r = decode_tiled(data, p, q, end, h, out, info); pos = pos_out; return r; }
        const size_t N = size_t(g.Wp) * g.Hp;
        bool motion_ok = true;
        mf.init(g.Wp, g.Hp, mvb);
        if (ftype == 1) {
            motion_ok = false;
            if (q + 4 <= end) {
                const uint32_t ml = get16(data + q), mc = get16(data + q + 2);
                if (q + 4 + ml <= end && crc16(data + q + 4, ml) == mc) {
                    motion_ok = decode_motion(data + q + 4, ml, mf);
                    q += 4 + ml;
                }
            }
        }
        // lost / corrupt motion -> zero vectors (decode_motion resets on failure); reported below
        // ---- group chunks (scan for markers; tolerate deleted / inserted bytes)
        cs.resize(N);
        struct GChunk { size_t len = 0, valid = 0; uint32_t nsym = 0; int nmax = -1; bool found = false; };
        std::vector<GChunk> chunks(g.G);
        std::vector<std::vector<uint8_t>> payloads(g.G);
        const uint8_t seq8 = uint8_t(info.sequence);
        struct Red { std::vector<uint8_t> d; int nmax = -1; bool ok = false; };
        std::vector<Red> reds(g.G);
        const int gh = (flags & F_LEN32) ? kGrpHdr32 : kGrpHdr;
        while (q + size_t(gh) <= end) {
            const uint8_t* c = data + q;
            if (c[0] == 0xFF && c[1] == 0xE8 && c[2] == seq8 && c[3] == g.G && crc16(c, 4) == get16(c + 4)) {
                size_t r = q + kRedHdr;
                for (int k = 0; k < g.G && r + 2 <= end; ++k) {
                    const size_t len = data[r + 1];
                    if (r + 2 + len + 2 > end) break;
                    if (crc16(data + r, 2 + len) == get16(data + r + 2 + len)) {
                        reds[k].nmax = int(int8_t(data[r]));
                        reds[k].d.assign(data + r + 2, data + r + 2 + len);
                        reds[k].ok = reds[k].nmax <= 30;
                    }
                    r += 2 + len + 2;
                }
                q = r;
                continue;
            }
            const bool ok = c[0] == 0xFF && c[1] == 0xE7 && c[2] < g.G && c[4] == seq8 &&
                            crc16(c, size_t(gh - 2)) == get16(c + gh - 2) && int8_t(c[3]) <= 30;
            if (!ok) { ++q; continue; }
            const int gid = c[2];
            const int nmax = int(int8_t(c[3]));
            const uint32_t nsym = get32(c + 5);
            const size_t len = gh == kGrpHdr32 ? get32(c + 9) : get16(c + 9);
            q += size_t(gh);
            if (chunks[gid].found) continue;   // duplicate header: keep the first
            auto& pl = payloads[gid];
            pl.clear();
            size_t valid = 0;
            bool broken = false;
            for (size_t o = 0; o < len;) {
                const size_t bl = std::min<size_t>(kBlk, len - o);
                const size_t need = bl + (bcrc ? 2 : 0);
                if (q + need > end) {      // frame truncated
                    const size_t avail = end > q ? end - q : 0;
                    const size_t take = std::min(avail, bl);
                    pl.insert(pl.end(), data + q, data + q + take);
                    if (!bcrc && !broken) valid += take;
                    broken = true; q = end; break;
                }
                pl.insert(pl.end(), data + q, data + q + bl);
                const bool good = !bcrc || crc16(data + q, bl) == get16(data + q + bl);
                if (good && !broken) valid += bl; else broken = true;
                q += need; o += bl;
            }
            pl.resize(len, 0);
            chunks[gid] = {len, valid, nsym, nmax, true};
        }
        // ---- substitute the redundant prefix for groups that are missing or broken earlier
        std::vector<uint8_t> used_red(g.G, 0);
        for (int k = 0; k < g.G; ++k) {
            if (!reds[k].ok) continue;
            if (!chunks[k].found || chunks[k].valid < reds[k].d.size()) {
                payloads[k] = reds[k].d;
                chunks[k] = {reds[k].d.size(), reds[k].d.size(), 0xFFFFFFFFu, reds[k].nmax, true};
                used_red[k] = 1;
            }
        }
        // ---- decode groups (parallel)
        std::vector<GroupDecodeResult> gres(g.G);
        parallel_for(g.G, threads, [&](int k) {
            if (!chunks[k].found) return;
            gres[k] = decode_group(g.lay, g.groups[k], payloads[k].data(), chunks[k].len, chunks[k].valid,
                                   chunks[k].nsym, chunks[k].nmax, cs);
        });
        info.groups_total = g.G;
        bool all_complete = true;
        std::vector<uint8_t> lost(g.G, 0);
        for (int k = 0; k < g.G; ++k) {
            if (!chunks[k].found) { ++info.groups_lost; lost[k] = 1; all_complete = false; continue; }
            if (chunks[k].valid < chunks[k].len || used_red[k]) ++info.groups_partial; else ++info.groups_ok;
            if (!gres[k].complete) all_complete = false;
        }
        // ---- reconstruct (P frame without a reference: conceal with the last frame)
        const bool predicted = ftype == 1;
        if (predicted) {
            info.motion_ok = motion_ok;
            info.ref_sequence = ref_seq;
            info.ref_gap = have_ref && ref_seq != info.sequence - 1u;
            info.ref_ok = have_ref && !info.ref_gap && ref_reliable;
        }
        if (predicted && have_ref) {
            pred.resize(N);
            compensate_obmc(refp, mf, pred.data());
            g.reconstruct(cs, pred.data(), 0, img, tmp, out);
        } else if (predicted) {
            info.concealed = true;
            out = last.size() == size_t(W) * H ? last : std::vector<uint16_t>(size_t(W) * H, uint16_t(1 << (bd - 1)));
        } else {
            // LL rows of groups that are lost or decoded to fewer than 4 bit planes are unreliable
            std::vector<uint8_t> llbad(g.lay.hL, 0), lost_or_weak(g.G, 0);
            bool anybad = false;
            for (int k = 0; k < g.G; ++k) {
                const bool damaged = chunks[k].valid < chunks[k].len || used_red[k];
                const bool weak = !chunks[k].found ||
                                  (damaged && !gres[k].complete && chunks[k].nmax >= 0 && gres[k].last_plane > chunks[k].nmax - 4);
                if (!weak) continue;
                lost_or_weak[k] = 1; anybad = true;
                for (int r = g.groups[k].r0; r < g.groups[k].r1; ++r) llbad[r] = 1;
            }
            const bool all_bad = std::all_of(llbad.begin(), llbad.end(), [](uint8_t v) { return v != 0; });
            g.reconstruct(cs, nullptr, dc, img, tmp, out, anybad && !all_bad ? &llbad : nullptr);
            if (anybad && last.size() == out.size()) conceal_intra(lost_or_weak, out, W, H, L);
        }
        info.lossless_complete = lossless && all_complete;
        last = out;
        ref.resize(N);
        g.pad_in(out.data(), ref.data(), 0);
        refp.build(ref.data(), g.Wp, g.Hp, kRefMargin);
        have_ref = true;
        ref_seq = info.sequence;
        ref_reliable = info.reliable();
        pos = std::max(end, p + 1);
        return true;
    }
};

Decoder::Decoder() : p_(new Impl()) {}
Decoder::~Decoder() = default;
void Decoder::set_threads(int n) { p_->threads = std::max(1, n); }
bool Decoder::decode_next(const uint8_t* data, size_t size, size_t& pos, std::vector<uint16_t>& out, FrameInfo& info,
                          int& width, int& height) {
    return p_->decode_next(data, size, pos, out, info, width, height);
}

}  // namespace ewc
