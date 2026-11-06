#include "motion.h"
#include "rc.h"
#include <algorithm>
#include <atomic>
#include <thread>
#include <climits>
#include <cmath>
#include <cstdlib>

namespace ewc {

namespace {

inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// bilinear quarter-pel sample (rounded), coordinates clamped (edge replication)
inline int32_t sample_qpel(const int32_t* ref, int S, int W, int H, int xq, int yq) {
    const int xi = xq >> 2, yi = yq >> 2;
    const int fx = xq & 3, fy = yq & 3;
    const int x0 = clampi(xi, 0, W - 1), x1 = clampi(xi + 1, 0, W - 1);
    const int y0 = clampi(yi, 0, H - 1), y1 = clampi(yi + 1, 0, H - 1);
    const int32_t a = ref[ptrdiff_t(y0) * S + x0], b = ref[ptrdiff_t(y0) * S + x1];
    const int32_t c = ref[ptrdiff_t(y1) * S + x0], d = ref[ptrdiff_t(y1) * S + x1];
    const int32_t v = (4 - fx) * (4 - fy) * a + fx * (4 - fy) * b + (4 - fx) * fy * c + fx * fy * d;
    return (v + 8) >> 4;
}

// Motion-compensated rectangle [x0,x0+w) x [y0,y0+h) at quarter-pel vector (mx,my) -> dst (w*h).
// Fast path when the source footprint lies inside the edge-replicated margin of the reference
// plane; the slow path clamps coordinates. Both produce identical values.
void mc_rect(const RefPlane& r, int x0, int y0, int w, int h, int mx, int my, int32_t* dst) {
    const int dx = mx >> 2, dy = my >> 2, fx = mx & 3, fy = my & 3;
    const int sx0 = x0 + dx, sy0 = y0 + dy, S = r.S;
    const bool inside = sx0 >= -r.M && sy0 >= -r.M && sx0 + w + 1 <= r.W + r.M && sy0 + h + 1 <= r.H + r.M;
    if (!inside) {
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                dst[size_t(y) * w + x] = sample_qpel(r.p, S, r.W, r.H, 4 * (x0 + x) + mx, 4 * (y0 + y) + my);
        return;
    }
    if (fx == 0 && fy == 0) {
        for (int y = 0; y < h; ++y) {
            const int32_t* s = r.p + ptrdiff_t(sy0 + y) * S + sx0;
            std::copy(s, s + w, dst + size_t(y) * w);
        }
        return;
    }
    const int wa = (4 - fx) * (4 - fy), wb = fx * (4 - fy), wc = (4 - fx) * fy, wd = fx * fy;
    for (int y = 0; y < h; ++y) {
        const int32_t* s0 = r.p + ptrdiff_t(sy0 + y) * S + sx0;
        const int32_t* s1 = s0 + S;
        int32_t* d = dst + size_t(y) * w;
        for (int x = 0; x < w; ++x) d[x] = (wa * s0[x] + wb * s0[x + 1] + wc * s1[x] + wd * s1[x + 1] + 8) >> 4;
    }
}

struct SadCtx {
    const int32_t* cur; const RefPlane* ref; int W, H;
    std::vector<int32_t> buf;
};

// SAD of the block at (x0,y0) size bw x bh for a quarter-pel vector, early termination at `best`
int64_t block_sad(SadCtx& c, int x0, int y0, int bw, int bh, int mx, int my, int64_t best) {
    const RefPlane& r = *c.ref;
    const int W = c.W, S = r.S;
    const int dx = mx >> 2, dy = my >> 2, fx = mx & 3, fy = my & 3;
    int64_t s = 0;
    const bool inside = x0 + dx >= -r.M && y0 + dy >= -r.M && x0 + dx + bw + 1 <= r.W + r.M && y0 + dy + bh + 1 <= r.H + r.M;
    if (inside && fx == 0 && fy == 0) {
        for (int y = 0; y < bh; ++y) {
            const int32_t* a = c.cur + size_t(y0 + y) * W + x0;
            const int32_t* q = r.p + ptrdiff_t(y0 + y + dy) * S + x0 + dx;
            int32_t acc = 0;
            for (int x = 0; x < bw; ++x) acc += std::abs(a[x] - q[x]);
            s += acc;
            if (s >= best) return s;
        }
        return s;
    }
    if (inside) {
        // fused bilinear interpolation + SAD (identical values to mc_rect), early exit per row
        const int wa = (4 - fx) * (4 - fy), wb = fx * (4 - fy), wc = (4 - fx) * fy, wd = fx * fy;
        for (int y = 0; y < bh; ++y) {
            const int32_t* a = c.cur + size_t(y0 + y) * W + x0;
            const int32_t* s0 = r.p + ptrdiff_t(y0 + y + dy) * S + x0 + dx;
            const int32_t* s1 = s0 + S;
            int32_t acc = 0;
            for (int x = 0; x < bw; ++x)
                acc += std::abs(a[x] - ((wa * s0[x] + wb * s0[x + 1] + wc * s1[x] + wd * s1[x + 1] + 8) >> 4));
            s += acc;
            if (s >= best) return s;
        }
        return s;
    }
    c.buf.resize(size_t(bw) * bh);
    mc_rect(r, x0, y0, bw, bh, mx, my, c.buf.data());
    for (int y = 0; y < bh; ++y) {
        const int32_t* a = c.cur + size_t(y0 + y) * W + x0;
        const int32_t* q = c.buf.data() + size_t(y) * bw;
        int32_t acc = 0;
        for (int x = 0; x < bw; ++x) acc += std::abs(a[x] - q[x]);
        s += acc;
        if (s >= best) return s;
    }
    return s;
}

void downsample(const int32_t* src, int S, int W, int H, int f, std::vector<int32_t>& dst, int& w, int& h) {
    w = W / f; h = H / f;
    dst.assign(size_t(w) * h, 0);
    const int sh = f == 2 ? 2 : (f == 4 ? 4 : 6);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int64_t s = 0;
            for (int j = 0; j < f; ++j)
                for (int i = 0; i < f; ++i) s += src[ptrdiff_t(f * y + j) * S + f * x + i];
            dst[size_t(y) * w + x] = int32_t((s + (1 << (sh - 1))) >> sh);
        }
}

inline int median3(int a, int b, int c) { return std::max(std::min(a, b), std::min(std::max(a, b), c)); }

void mv_pred(const MotionField& mf, int bx, int by, int& px, int& py) {
    const int nbx = mf.nbx;
    auto at = [&](int x, int y, int comp) {
        size_t i = size_t(y) * nbx + x;
        return comp ? mf.mvy[i] : mf.mvx[i];
    };
    if (by == 0 && bx == 0) { px = py = 0; return; }
    if (by == 0) { px = at(bx - 1, 0, 0); py = at(bx - 1, 0, 1); return; }
    if (bx == 0) { px = at(0, by - 1, 0); py = at(0, by - 1, 1); return; }
    const int tr = bx + 1 < nbx ? bx + 1 : bx - 1;
    px = median3(at(bx - 1, by, 0), at(bx, by - 1, 0), at(tr, by - 1, 0));
    py = median3(at(bx - 1, by, 1), at(bx, by - 1, 1), at(tr, by - 1, 1));
}

// approximate bits of an MV-difference component (for the rate term)
inline int mvd_bits(int d) {
    if (!d) return 1;
    unsigned m = unsigned(std::abs(d));
    int nb = 0;
    while (m >> (nb + 1)) ++nb;
    return 2 + 2 * nb + 1;
}

}  // namespace

void RefPlane::build(const int32_t* src, int w, int h, int margin) {
    W = w; H = h; M = margin; S = W + 2 * M;
    buf.resize(size_t(S) * (H + 2 * M));
    for (int y = -M; y < H + M; ++y) {
        const int32_t* s = src + size_t(clampi(y, 0, H - 1)) * W;
        int32_t* d = buf.data() + size_t(y + M) * S;
        std::fill(d, d + M, s[0]);
        std::copy(s, s + W, d + M);
        std::fill(d + M + W, d + S, s[W - 1]);
    }
    p = buf.data() + size_t(M) * S + M;
}

void estimate_motion(const int32_t* cur, const RefPlane& ref, const MotionParams& mp, MotionField& mf, int threads) {
    const int W = ref.W, H = ref.H;
    mf.init(W, H, mp.bs);
    const int bs = mp.bs;
    // ---- 1. global translation: coarse-to-fine on 8x and 2x downsampled frames
    int gx = 0, gy = 0;
    {
        std::vector<int32_t> c8, r8;
        int w8, h8;
        downsample(cur, W, W, H, 8, c8, w8, h8);
        downsample(ref.p, ref.S, W, H, 8, r8, w8, h8);
        const int gr = std::max(1, mp.grange / 8);
        int64_t best = LLONG_MAX;
        for (int dy = -gr; dy <= gr; ++dy)
            for (int dx = -gr; dx <= gr; ++dx) {
                int64_t s = 0;
                for (int y = gr; y < h8 - gr && s < best; ++y) {
                    const int32_t* c = c8.data() + size_t(y) * w8;
                    const int32_t* r = r8.data() + size_t(y + dy) * w8;
                    for (int x = gr; x < w8 - gr; ++x) s += std::abs(c[x] - r[x + dx]);
                }
                s += std::abs(dx) + std::abs(dy);
                if (s < best) { best = s; gx = dx; gy = dy; }
            }
        gx *= 8; gy *= 8;
        // refine +-4 at 2x resolution (8 px full-res)
        std::vector<int32_t> c2, r2;
        int w2, h2;
        downsample(cur, W, W, H, 2, c2, w2, h2);
        downsample(ref.p, ref.S, W, H, 2, r2, w2, h2);
        int bx = gx / 2, by = gy / 2;
        best = LLONG_MAX;
        const int m = std::abs(bx) + std::abs(by) + 6;
        int nx = bx, ny = by;
        for (int dy = by - 4; dy <= by + 4; ++dy)
            for (int dx = bx - 4; dx <= bx + 4; ++dx) {
                int64_t s = 0;
                for (int y = m; y < h2 - m && s < best; y += 2) {
                    const int32_t* c = c2.data() + size_t(y) * w2;
                    const int32_t* r = r2.data() + size_t(y + dy) * w2;
                    for (int x = m; x < w2 - m; x += 2) s += std::abs(c[x] - r[x + dx]);
                }
                if (s < best) { best = s; nx = dx; ny = dy; }
            }
        gx = nx * 2; gy = ny * 2;
    }
    // ---- 2. per-block: candidate set, integer diamond search, half/quarter-pel refinement.
    // Block (bx,by) reads the final vectors of (bx-1,by), (bx,by-1) and (bx+1,by-1) (median
    // predictor), so rows are processed as a wavefront: row r may handle block bx once row r-1
    // has finished block bx+1. Results are identical for any thread count.
    const int lam = mp.lambda;
    auto search_block = [&](SadCtx& sc, int bx, int by) {
            const int x0 = bx * bs, y0 = by * bs;
            const int bw = std::min(bs, W - x0), bh = std::min(bs, H - y0);
            int px, py;
            mv_pred(mf, bx, by, px, py);
            auto cost = [&](int mx, int my, int64_t best) -> int64_t {
                int64_t pen = int64_t(lam) * (mvd_bits(mx - px) + mvd_bits(my - py));
                if (pen >= best) return pen;
                return pen + block_sad(sc, x0, y0, bw, bh, mx, my, best - pen);
            };
            int bmx = px, bmy = py;
            int64_t best = cost(px, py, LLONG_MAX);
            auto tryv = [&](int mx, int my) {
                int64_t s = cost(mx, my, best);
                if (s < best) { best = s; bmx = mx; bmy = my; }
            };
            tryv(0, 0);
            tryv(4 * gx, 4 * gy);
            if (bx) tryv(mf.mvx[size_t(by) * mf.nbx + bx - 1], mf.mvy[size_t(by) * mf.nbx + bx - 1]);
            if (by) tryv(mf.mvx[size_t(by - 1) * mf.nbx + bx], mf.mvy[size_t(by - 1) * mf.nbx + bx]);
            // integer large-diamond then small-diamond around the best candidate
            int cx = (bmx + 2) & ~3, cy = (bmy + 2) & ~3;
            tryv(cx, cy);
            const int maxd = 4 * mp.range;
            for (int step = 8; step >= 1; step >>= 1) {
                for (int it = 0; it < 32; ++it) {
                    const int ox = bmx & ~3, oy = bmy & ~3;
                    const int pts[8][2] = {{step, 0}, {-step, 0}, {0, step}, {0, -step},
                                           {step, step}, {-step, step}, {step, -step}, {-step, -step}};
                    bool moved = false;
                    for (auto& p : pts) {
                        int mx = ox + 4 * p[0], my = oy + 4 * p[1];
                        if (std::abs(mx - px) > maxd + 4 * mp.grange || std::abs(my - py) > maxd + 4 * mp.grange) continue;
                        int64_t s = cost(mx, my, best);
                        if (s < best) { best = s; bmx = mx; bmy = my; moved = true; }
                    }
                    if (!moved) break;
                }
            }
            // sub-pel refinement: half then quarter
            for (int step = 2; step >= 1; step >>= 1) {
                const int ix = bmx, iy = bmy;
                for (int dy = -step; dy <= step; dy += step)
                    for (int dx = -step; dx <= step; dx += step)
                        if (dx || dy) tryv(ix + dx, iy + dy);
            }
            mf.mvx[size_t(by) * mf.nbx + bx] = int16_t(bmx);
            mf.mvy[size_t(by) * mf.nbx + bx] = int16_t(bmy);
    };
    const int nt = std::max(1, std::min(threads, mf.nby));
    if (nt == 1) {
        SadCtx sc{cur, &ref, W, H, {}};
        for (int by = 0; by < mf.nby; ++by)
            for (int bx = 0; bx < mf.nbx; ++bx) search_block(sc, bx, by);
        return;
    }
    std::vector<std::atomic<int>> done(size_t(mf.nby));   // blocks finished per row
    for (auto& d : done) d.store(0);
    std::atomic<int> next_row{0};
    auto worker = [&]() {
        SadCtx sc{cur, &ref, W, H, {}};
        for (int by; (by = next_row.fetch_add(1)) < mf.nby;) {
            for (int bx = 0; bx < mf.nbx; ++bx) {
                if (by > 0) {
                    const int need = std::min(bx + 2, mf.nbx);
                    while (done[size_t(by - 1)].load(std::memory_order_acquire) < need) std::this_thread::yield();
                }
                search_block(sc, bx, by);
                done[size_t(by)].store(bx + 1, std::memory_order_release);
            }
        }
    };
    std::vector<std::thread> th;
    for (int t = 0; t < nt; ++t) th.emplace_back(worker);
    for (auto& t : th) t.join();
}

void compensate_obmc(const RefPlane& ref, const MotionField& mf, int32_t* pred) {
    const int W = ref.W, H = ref.H;
    const int bs = mf.bs, hb = bs / 2, ws = 2 * bs;
    // 1-D raised-cosine window of length 2*bs, Q7, w[i] + w[i+bs] == 128
    std::vector<int32_t> win(ws);
    for (int i = 0; i < bs; ++i) {
        double t = std::sin(M_PI * (i + 0.5) / (2.0 * bs));
        int v = int(std::lround(128.0 * t * t));
        win[i] = v; win[i + bs] = 128 - v;
    }
    // Accumulate weighted window predictions. Window of block (bx,by) covers
    // [bx*bs-hb, bx*bs-hb+2bs). Outside the frame, missing neighbour blocks are replaced by the
    // nearest block (clamped index), which is equivalent to extending its window: we fold the
    // weight of clamped windows into edge blocks by giving edge windows full weight there.
    std::vector<int64_t> acc(size_t(W) * H, 0);
    std::vector<int32_t> buf(size_t(ws) * ws);
    std::vector<int32_t> wx(ws), wy(ws);
    for (int by = 0; by < mf.nby; ++by)
        for (int bx = 0; bx < mf.nbx; ++bx) {
            const int x0 = bx * bs - hb, y0 = by * bs - hb;
            // effective 1-D weights (edge blocks absorb the weight of the missing neighbour)
            for (int i = 0; i < ws; ++i) {
                wx[i] = win[i]; wy[i] = win[i];
                if (bx == 0 && i < bs) wx[i] = 128;            // left of first window centre
                if (bx == mf.nbx - 1 && i >= bs) wx[i] = 128;
                if (by == 0 && i < bs) wy[i] = 128;
                if (by == mf.nby - 1 && i >= bs) wy[i] = 128;
            }
            const int cx0 = std::max(x0, 0), cy0 = std::max(y0, 0);
            const int cx1 = std::min(x0 + ws, W), cy1 = std::min(y0 + ws, H);
            const int w = cx1 - cx0, h = cy1 - cy0;
            if (w <= 0 || h <= 0) continue;
            const size_t bi = size_t(by) * mf.nbx + bx;
            mc_rect(ref, cx0, cy0, w, h, mf.mvx[bi], mf.mvy[bi], buf.data());
            for (int y = 0; y < h; ++y) {
                const int32_t wyv = wy[cy0 + y - y0];
                int64_t* a = acc.data() + size_t(cy0 + y) * W + cx0;
                const int32_t* s = buf.data() + size_t(y) * w;
                const int32_t* wxp = wx.data() + (cx0 - x0);
                for (int x = 0; x < w; ++x) a[x] += int64_t(wyv * wxp[x]) * s[x];
            }
        }
    for (size_t i = 0; i < acc.size(); ++i) pred[i] = int32_t((acc[i] + 8192) >> 14);
}

// --------------------------------------------------------------------------- MV entropy coding
namespace {
struct MvCtx {
    Prob zero[2][3];      // [component][ctx: neighbours' residual zero count]
    Prob prefix[2][12];
};
void enc_comp(RCEncoder& rc, MvCtx& c, int comp, int v, int nctx) {
    rc.encode(c.zero[comp][nctx], v != 0);
    if (!v) return;
    rc.encode_bypass(v < 0);
    unsigned m = unsigned(std::abs(v));   // >= 1, Exp-Golomb style: prefix = floor(log2 m)
    int nb = 0;
    while (m >> (nb + 1)) ++nb;
    for (int i = 0; i < nb; ++i) rc.encode(c.prefix[comp][std::min(i, 11)], 1);
    rc.encode(c.prefix[comp][std::min(nb, 11)], 0);
    for (int i = nb - 1; i >= 0; --i) rc.encode_bypass((m >> i) & 1);
}
int dec_comp(RCDecoder& rc, MvCtx& c, int comp, int nctx) {
    if (!rc.decode(c.zero[comp][nctx])) return 0;
    int neg = rc.decode_bypass();
    int nb = 0;
    while (nb < 20 && rc.decode(c.prefix[comp][std::min(nb, 11)])) ++nb;
    unsigned v = 1;
    for (int i = nb - 1; i >= 0; --i) v = (v << 1) | unsigned(rc.decode_bypass());
    return neg ? -int(v) : int(v);
}
}  // namespace

void encode_motion(const MotionField& mf, std::vector<uint8_t>& out) {
    RCEncoder rc;
    MvCtx* c = new MvCtx();
    std::vector<uint8_t> zres(size_t(mf.nbx) * mf.nby, 0);
    for (int by = 0; by < mf.nby; ++by)
        for (int bx = 0; bx < mf.nbx; ++bx) {
            int px, py;
            mv_pred(mf, bx, by, px, py);
            size_t i = size_t(by) * mf.nbx + bx;
            int nz = (bx ? zres[i - 1] : 1) + (by ? zres[i - mf.nbx] : 1);
            int dx = mf.mvx[i] - px, dy = mf.mvy[i] - py;
            enc_comp(rc, *c, 0, dx, nz);
            enc_comp(rc, *c, 1, dy, nz);
            zres[i] = (dx == 0 && dy == 0);
        }
    rc.finish();
    delete c;
    out = rc.out;
}

bool decode_motion(const uint8_t* data, size_t size, MotionField& mf) {
    RCDecoder rc;
    rc.init(data, size, size);
    MvCtx* c = new MvCtx();
    std::vector<uint8_t> zres(size_t(mf.nbx) * mf.nby, 0);
    bool ok = true;
    for (int by = 0; by < mf.nby && ok; ++by)
        for (int bx = 0; bx < mf.nbx; ++bx) {
            int px, py;
            mv_pred(mf, bx, by, px, py);
            size_t i = size_t(by) * mf.nbx + bx;
            int nz = (bx ? zres[i - 1] : 1) + (by ? zres[i - mf.nbx] : 1);
            int dx = dec_comp(rc, *c, 0, nz);
            int dy = dec_comp(rc, *c, 1, nz);
            if (!rc.ok()) { ok = false; break; }
            mf.mvx[i] = int16_t(px + dx); mf.mvy[i] = int16_t(py + dy);
            zres[i] = (dx == 0 && dy == 0);
        }
    delete c;
    if (!ok) { std::fill(mf.mvx.begin(), mf.mvx.end(), 0); std::fill(mf.mvy.begin(), mf.mvy.end(), 0); }
    return ok;
}

}  // namespace ewc
