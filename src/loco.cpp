// ewc — LOCO-I / JPEG-LS-style coder. See loco.h. The modelling loop is written once,
// templated on encode/decode, so encoder and decoder cannot diverge.
#include "loco.h"
#include "rc.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace ewc {
namespace {

constexpr int kJ[32] = {0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
                        4, 4, 5, 5, 6, 6, 7, 7, 8, 9, 10, 11, 12, 13, 14, 15};
constexpr int kReset = 64;
constexpr int kMaxK = 24;     // Golomb parameter cap: keeps N << k in range for any (corrupt) input

class BitWriter {
public:
    explicit BitWriter(std::vector<uint8_t>& o) : out_(o), start_(o.size()) {}
    inline void put(uint32_t v, int n) {   // n <= 32
        if (!n) return;
        acc_ = (acc_ << n) | (uint64_t(v) & ((uint64_t(1) << n) - 1));
        nb_ += n;
        while (nb_ >= 8) { nb_ -= 8; out_.push_back(uint8_t(acc_ >> nb_)); }
    }
    inline void zeros(int n) { while (n > 24) { put(0, 24); n -= 24; } put(0, n); }
    size_t finish() {
        if (nb_) { out_.push_back(uint8_t(acc_ << (8 - nb_))); nb_ = 0; }
        return out_.size() - start_;
    }
private:
    std::vector<uint8_t>& out_;
    size_t start_;
    uint64_t acc_ = 0;
    int nb_ = 0;
};

class BitReader {
public:
    BitReader(const uint8_t* d, size_t n) : d_(d), n_(n) {}
    inline uint32_t get(int n) {   // n <= 24; past the end reads zeros
        if (!n) return 0;
        while (nb_ < n) {
            acc_ = (acc_ << 8) | (pos_ < n_ ? d_[pos_] : 0u);
            ++pos_; nb_ += 8;
        }
        nb_ -= n;
        return uint32_t(acc_ >> nb_) & ((1u << n) - 1);
    }
    inline int bit() { return int(get(1)); }
    // unary prefix: number of zeros before the terminating 1, at most `cap`; the 1 is consumed
    inline int zeros(int cap) {
        int z = 0;
        while (z < cap && !bit()) ++z;
        if (z == cap) bit();
        return z;
    }
    size_t bits_consumed() const { return pos_ * 8 - size_t(nb_); }
private:
    const uint8_t* d_;
    size_t n_, pos_ = 0;
    uint64_t acc_ = 0;
    int nb_ = 0;
};

struct Model {
    int maxval, near, range, qbpp, limit, t1, t2, t3, step;
    int A[367], B[367], C[367], N[367], Nn[367];
    int ri = 0;
    void init(const LocoParams& p) {
        maxval = p.maxval; near = p.near; step = 2 * near + 1;
        range = (maxval + 2 * near) / step + 1;
        qbpp = 0; while ((1 << qbpp) < range) ++qbpp;
        int bpp = 0; while ((1 << bpp) < maxval + 1) ++bpp;
        bpp = std::max(2, bpp);
        limit = 2 * (bpp + std::max(8, bpp));
        // default thresholds of T.87 (C.2.4.1.1)
        const int f = (std::min(maxval, 4095) + 128) >> 8;
        t1 = f * (3 - 2) + 2 + 3 * near; if (t1 > maxval || t1 < near + 1) t1 = near + 1;
        t2 = f * (7 - 3) + 3 + 5 * near; if (t2 > maxval || t2 < t1) t2 = t1;
        t3 = f * (21 - 4) + 4 + 7 * near; if (t3 > maxval || t3 < t2) t3 = t2;
        const int a0 = std::max(2, (range + 32) / 64);
        for (int i = 0; i < 367; ++i) { A[i] = a0; B[i] = 0; C[i] = 0; N[i] = 1; Nn[i] = 0; }
        ri = 0;
    }
    inline int quant(int d) const {
        if (d <= -t2) return d <= -t3 ? -4 : -3;
        if (d <= -t1) return -2;
        if (d < -near) return -1;
        if (d <= near) return 0;
        if (d < t1) return 1;
        if (d < t2) return 2;
        return d < t3 ? 3 : 4;
    }
    // quantise a prediction error (near-lossless) and reduce it modulo RANGE
    inline int qerr(int e) const {
        if (near) e = e > 0 ? (e + near) / step : -((near - e) / step);
        if (e < 0) e += range;
        if (e >= (range + 1) / 2) e -= range;
        return e;
    }
    inline int recon(int px, int sign, int e) const {
        int r = px + sign * e * step;
        if (r < -near) r += range * step;
        else if (r > maxval + near) r -= range * step;
        return r < 0 ? 0 : (r > maxval ? maxval : r);
    }
};

// Entropy back-ends: AC = false is JPEG-LS (limited-length Golomb + raw run bits);
// AC = true keeps the same modelling but codes every symbol with the adaptive binary range
// coder of the wavelet path: Golomb-style binarisation (unary prefix + k low bits) whose prefix
// bits and the top low bit are adaptive per Golomb parameter k, run bits adaptive per run index.
constexpr int kU = 20;          // unary prefix length before an escape (AC)
// Probability with a warm-up: adaptation rate 1/2, 1/4, ... until it settles at 1/2^kWShift.
// Each stripe starts from p = 1/2, so fast early learning cuts the cost of independent stripes.
#ifndef EWC_LOCO_WSHIFT
#define EWC_LOCO_WSHIFT 6
#endif
constexpr int kWShift = EWC_LOCO_WSHIFT;
struct WProb {
    uint16_t p = 1u << 14;
    uint8_t n = 0;
    inline void update(int bit) {
        const int sh = n < 2 ? 1 : n < 4 ? 2 : n < 8 ? 3 : n < 16 ? 4 : n < 32 ? 5 : kWShift;
        if (n < 255) ++n;
        const int s2 = std::min(sh, kWShift);
        if (bit) p -= p >> s2; else p += (32768 - p) >> s2;
    }
};
struct AcModel {
    WProb pre[2][16][kU];        // [regular / interruption][k][prefix position]
    WProb lo[2][16];            // most significant low bit
    WProb run[32];              // run segment continuation, per run index
};

template <bool ENC, bool AC = false>
struct Stripe {
    Model m;
    BitWriter* bw = nullptr;
    BitReader* br = nullptr;
    RCEncoder* re = nullptr;
    RCDecoder* rd = nullptr;
    AcModel* am = nullptr;

    // ---- entropy primitives shared by both back-ends
    inline void ac_put(WProb& p, int b) { re->encode_p(p.p, b); p.update(b); }
    inline int ac_get(WProb& p) { const int b = rd->decode_p(p.p); p.update(b); return b; }
    inline void ac_val_put(int v, int k, int set) {
        const int kc = std::min(k, 15), hi = v >> k;
        if (hi < kU) {
            for (int i = 0; i < hi; ++i) ac_put(am->pre[set][kc][i], 1);
            ac_put(am->pre[set][kc][hi], 0);
            if (k) {
                ac_put(am->lo[set][kc], (v >> (k - 1)) & 1);
                for (int b = k - 2; b >= 0; --b) re->encode_bypass((v >> b) & 1);
            }
        } else {
            for (int i = 0; i < kU; ++i) ac_put(am->pre[set][kc][std::min(i, kU - 1)], 1);
            for (int b = 17; b >= 0; --b) re->encode_bypass((v >> b) & 1);
        }
    }
    inline int ac_val_get(int k, int set) {
        const int kc = std::min(k, 15);
        int hi = 0;
        while (hi < kU && ac_get(am->pre[set][kc][std::min(hi, kU - 1)])) ++hi;
        if (hi == kU) { int v = 0; for (int b = 17; b >= 0; --b) v = (v << 1) | rd->decode_bypass(); return v; }
        int v = hi << k;
        if (k) {
            v |= ac_get(am->lo[set][kc]) << (k - 1);
            for (int b = k - 2; b >= 0; --b) v |= rd->decode_bypass() << b;
        }
        return v;
    }
    inline void put_val(int v, int k, int glimit, int set) { if constexpr (AC) ac_val_put(v, k, set); else put_lg(v, k, glimit); }
    inline int get_val(int k, int glimit, int set) { if constexpr (AC) return ac_val_get(k, set); else return get_lg(k, glimit); }
    inline void put_bit(WProb& p, int b) { if constexpr (AC) ac_put(p, b); else bw->put(uint32_t(b), 1); }
    inline int get_bit(WProb& p) { if constexpr (AC) return ac_get(p); else return br->bit(); }
    inline void put_raw(uint32_t v, int n) {
        if constexpr (AC) { for (int b = n - 1; b >= 0; --b) re->encode_bypass((v >> b) & 1); } else bw->put(v, n);
    }
    inline uint32_t get_raw(int n) {
        if constexpr (AC) { uint32_t v = 0; for (int b = n - 1; b >= 0; --b) v = (v << 1) | uint32_t(rd->decode_bypass()); return v; }
        else return br->get(n);
    }
    inline bool input_ok(size_t valid_bits) const {
        if constexpr (AC) return rd->ok(); else return br->bits_consumed() <= valid_bits;
    }

    WProb dummy_;
    inline WProb& am_run(int ri) { if constexpr (AC) return am->run[ri]; else return dummy_; }
    // limited-length Golomb code
    inline void put_lg(int v, int k, int glimit) {
        const int high = v >> k, cap = glimit - m.qbpp - 1;
        if (high < cap) { bw->zeros(high); bw->put(1, 1); bw->put(uint32_t(v) & ((1u << k) - 1), k); }
        else { bw->zeros(cap); bw->put(1, 1); bw->put(uint32_t(v - 1), m.qbpp); }
    }
    inline int get_lg(int k, int glimit) {
        const int cap = glimit - m.qbpp - 1;
        const int z = br->zeros(cap);
        if (z < cap) return (z << k) | int(br->get(k));
        return int(br->get(m.qbpp)) + 1;
    }

    // regular-mode sample. x: value to code (encoder) / ignored (decoder). Returns reconstruction.
    inline int regular(int x, int a, int b, int c, int d) {
        int q1 = m.quant(d - b), q2 = m.quant(b - c), q3 = m.quant(c - a);
        int q = 81 * q1 + 9 * q2 + q3, sign = 1;
        if (q < 0) { q = -q; sign = -1; }
        const int mx = std::max(a, b), mn = std::min(a, b);
        int px = c >= mx ? mn : (c <= mn ? mx : a + b - c);
        px += sign * m.C[q];
        px = px < 0 ? 0 : (px > m.maxval ? m.maxval : px);
        int k = 0;
        while (k < kMaxK && (m.N[q] << k) < m.A[q]) ++k;   // bounded: A can be large on corrupt input
        int e;
        if (ENC) {
            e = m.qerr(sign * (x - px));
            int me;
            if (!m.near && k == 0 && 2 * m.B[q] <= -m.N[q]) me = e >= 0 ? 2 * e + 1 : -2 * (e + 1);
            else me = e >= 0 ? 2 * e : -2 * e - 1;
            put_val(me, k, m.limit, 0);
        } else {
            const int me = get_val(k, m.limit, 0);
            if (!m.near && k == 0 && 2 * m.B[q] <= -m.N[q]) e = (me & 1) ? (me - 1) >> 1 : -(me >> 1) - 1;
            else e = (me & 1) ? -((me + 1) >> 1) : me >> 1;
        }
        // context update (A.6) and bias correction
        m.B[q] += e * m.step;
        m.A[q] += std::abs(e);
        if (m.N[q] == kReset) {
            m.A[q] >>= 1;
            m.B[q] = m.B[q] >= 0 ? m.B[q] >> 1 : -((1 - m.B[q]) >> 1);
            m.N[q] >>= 1;
        }
        ++m.N[q];
        if (m.B[q] <= -m.N[q]) {
            m.B[q] += m.N[q];
            if (m.C[q] > -128) --m.C[q];
            if (m.B[q] <= -m.N[q]) m.B[q] = -m.N[q] + 1;
        } else if (m.B[q] > 0) {
            m.B[q] -= m.N[q];
            if (m.C[q] < 127) ++m.C[q];
            if (m.B[q] > 0) m.B[q] = 0;
        }
        return m.recon(px, sign, e);
    }

    // run-interruption sample (ra, rb: reconstructed left / above)
    inline int interruption(int x, int ra, int rb) {
        const int rit = std::abs(ra - rb) <= m.near ? 1 : 0;
        const int px = rit ? ra : rb;
        const int sign = (!rit && ra > rb) ? -1 : 1;
        const int q = 365 + rit;
        const int temp = rit ? m.A[q] + (m.N[q] >> 1) : m.A[q];
        int k = 0;
        while (k < kMaxK && (m.N[q] << k) < temp) ++k;
        const int glimit = m.limit - kJ[m.ri] - 1;
        int e, em;
        if (ENC) {
            e = m.qerr(sign * (x - px));
            const int map = (k == 0 && e > 0 && 2 * m.Nn[q] < m.N[q]) || (e < 0 && 2 * m.Nn[q] >= m.N[q]) ||
                            (e < 0 && k != 0);
            em = 2 * std::abs(e) - rit - map;
            put_val(em, k, glimit, 1);
        } else {
            em = get_val(k, glimit, 1);
            const int t = em + rit;
            const int map = t & 1;
            const int mag = (t + map) >> 1;
            if (k == 0 && 2 * m.Nn[q] < m.N[q]) e = map ? mag : -mag;
            else e = map ? -mag : mag;
        }
        if (e < 0) ++m.Nn[q];
        m.A[q] += (em + 1 - rit) >> 1;
        if (m.N[q] == kReset) { m.A[q] >>= 1; m.N[q] >>= 1; m.Nn[q] >>= 1; }
        ++m.N[q];
        return m.recon(px, sign, e);
    }

    // rows: src (encoder) / dst (decoder output and encoder reconstruction)
    int code(const uint16_t* src, uint16_t* dst, int W, int nrows, size_t valid_bits) {
        std::vector<int> prev(size_t(W) + 2), curr(size_t(W) + 2);
        std::fill(prev.begin(), prev.end(), (m.maxval + 1) >> 1);   // virtual row above the stripe
        int rows_ok = 0;
        for (int y = 0; y < nrows; ++y) {
            int* P = prev.data() + 1;
            int* R = curr.data() + 1;
            P[-1] = P[0]; P[W] = P[W - 1];
            R[-1] = P[0];
            const uint16_t* s = ENC ? src + size_t(y) * W : nullptr;
            for (int x = 0; x < W;) {
                const int a = R[x - 1], b = P[x], c = P[x - 1], d = P[x + 1];
                if (std::abs(d - b) <= m.near && std::abs(b - c) <= m.near && std::abs(c - a) <= m.near) {
                    // ---- run mode
                    const int rv = a, rem = W - x;
                    int cnt = 0;
                    bool eol;
                    if (ENC) {
                        while (cnt < rem && std::abs(int(s[x + cnt]) - rv) <= m.near) ++cnt;
                        eol = cnt == rem;
                        int left = cnt;
                        while (left >= (1 << kJ[m.ri])) {
                            put_bit(am_run(m.ri), 1);
                            left -= 1 << kJ[m.ri];
                            if (m.ri < 31) ++m.ri;
                        }
                        if (eol) { if (left > 0) put_bit(am_run(m.ri), 1); }
                        else { put_bit(am_run(m.ri), 0); put_raw(uint32_t(left), kJ[m.ri]); }
                    } else {
                        eol = false;
                        for (;;) {
                            if (cnt == rem) { eol = true; break; }
                            if (get_bit(am_run(m.ri))) {
                                const int seg = 1 << kJ[m.ri];
                                if (cnt + seg > rem) { cnt = rem; eol = true; break; }
                                cnt += seg;
                                if (m.ri < 31) ++m.ri;
                            } else {
                                cnt += int(get_raw(kJ[m.ri]));
                                if (cnt >= rem) { cnt = rem; eol = true; }   // only on corrupt data
                                break;
                            }
                        }
                    }
                    for (int i = 0; i < cnt; ++i) R[x + i] = rv;
                    x += cnt;
                    if (eol) break;
                    R[x] = interruption(ENC ? int(s[x]) : 0, R[x - 1], P[x]);
                    if (m.ri > 0) --m.ri;
                    ++x;
                } else {
                    R[x] = regular(ENC ? int(s[x]) : 0, a, b, c, d);
                    ++x;
                }
            }
            uint16_t* o = dst + size_t(y) * W;
            for (int x = 0; x < W; ++x) o[x] = uint16_t(R[x]);
            if (!ENC && input_ok(valid_bits)) rows_ok = y + 1;
            std::swap(prev, curr);
        }
        return ENC ? nrows : rows_ok;
    }
};

}  // namespace

size_t loco_encode_stripe(const uint16_t* src, int W, int nrows, const LocoParams& p, std::vector<uint8_t>& out,
                          uint16_t* recon) {
    if (p.ac) {
        RCEncoder rc;
        auto am = std::make_unique<AcModel>();
        Stripe<true, true> st;
        st.m.init(p); st.re = &rc; st.am = am.get();
        st.code(src, recon, W, nrows, 0);
        rc.finish();
        out.insert(out.end(), rc.out.begin(), rc.out.end());
        return rc.out.size();
    }
    BitWriter bw(out);
    Stripe<true> st;
    st.m.init(p);
    st.bw = &bw;
    st.code(src, recon, W, nrows, 0);
    return bw.finish();
}

int loco_decode_stripe(const uint8_t* data, size_t size, size_t valid, int W, int nrows, const LocoParams& p,
                       uint16_t* out) {
    if (p.ac) {
        RCDecoder rc;
        rc.init(data, size, valid);
        auto am = std::make_unique<AcModel>();
        Stripe<false, true> st;
        st.m.init(p); st.rd = &rc; st.am = am.get();
        return st.code(nullptr, out, W, nrows, 0);
    }
    BitReader br(data, size);
    Stripe<false> st;
    st.m.init(p);
    st.br = &br;
    return st.code(nullptr, out, W, nrows, valid * 8);
}

}  // namespace ewc
