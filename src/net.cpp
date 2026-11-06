#include "ewc_net.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>

namespace ewc {
namespace {
inline void put16(uint8_t* p, uint32_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }
inline void put32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * i)); }
inline uint32_t get16(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8; }
inline uint32_t get32(const uint8_t* p) { return get16(p) | get16(p + 2) << 16; }
constexpr uint32_t kMaxUnit = 64u << 20;
inline size_t ndata(size_t total, size_t pay) { return total == 0 ? 1 : (total + pay - 1) / pay; }
inline size_t cdiv(size_t a, size_t b) { return (a + b - 1) / b; }

// ---------------------------------------------------------------- GF(256), polynomial 0x11d
struct GF {
    std::array<uint8_t, 512> exp{};
    std::array<int, 256> log{};
    GF() {
        int x = 1;
        for (int i = 0; i < 255; ++i) {
            exp[i] = uint8_t(x); log[x] = i;
            x <<= 1; if (x & 0x100) x ^= 0x11d;
        }
        for (int i = 255; i < 512; ++i) exp[i] = exp[i - 255];
        log[0] = -1;
    }
    uint8_t mul(uint8_t a, uint8_t b) const { return (a && b) ? exp[log[a] + log[b]] : 0; }
    uint8_t inv(uint8_t a) const { return exp[255 - log[a]]; }
};
const GF& gf() { static const GF g; return g; }

// dst[0..n) ^= c * src[0..n)
void axpy(uint8_t* dst, const uint8_t* src, size_t n, uint8_t c) {
    if (!c) return;
    if (c == 1) { for (size_t i = 0; i < n; ++i) dst[i] ^= src[i]; return; }
    const GF& g = gf();
    std::array<uint8_t, 256> t;
    for (int v = 0; v < 256; ++v) t[v] = g.mul(c, uint8_t(v));
    for (size_t i = 0; i < n; ++i) dst[i] ^= t[src[i]];
}

// Cauchy coefficient of parity row r for data column t of an RS block (t < nd_b, r < m_b,
// nd_b + m_b <= 255, so 255 - r and t are distinct and the XOR is non-zero).
inline uint8_t cauchy(size_t r, size_t t) { return gf().inv(uint8_t((255 - r) ^ t)); }

// Data datagram indices covered by XOR parity j.
void xor_members(const FecLayout& L, size_t j, std::vector<size_t>& m) {
    m.clear();
    if (L.fec == Fec::Xor) for (size_t i = j * size_t(L.k); i < std::min(L.nd, (j + 1) * size_t(L.k)); ++i) m.push_back(i);
    else for (size_t i = j; i < L.nd; i += L.np) m.push_back(i);
}
size_t rs_block_size(const FecLayout& L, size_t b) { return L.nd / L.nb + (b < L.nd % L.nb ? 1 : 0); }
}  // namespace

const char* fec_name(Fec f) {
    switch (f) { case Fec::Xor: return "xor"; case Fec::XorInterleaved: return "xori"; case Fec::RS: return "rs"; default: return "none"; }
}
bool fec_from_name(const char* s, Fec& f) {
    const std::string n(s);
    if (n == "none") f = Fec::None; else if (n == "xor") f = Fec::Xor; else if (n == "xori") f = Fec::XorInterleaved;
    else if (n == "rs") f = Fec::RS; else return false;
    return true;
}

void FecLayout::init(Fec f, int k_, size_t nd_) {
    fec = k_ > 0 ? f : Fec::None; k = fec == Fec::None ? 0 : k_; nd = nd_;
    nb = 1; pstart.assign(1, 0); np = 0;
    if (fec == Fec::None) return;
    if (fec != Fec::RS) { np = cdiv(nd, size_t(k)); return; }
    while (cdiv(nd, nb) + cdiv(cdiv(nd, nb), size_t(k)) > 255) ++nb;
    pstart.resize(nb + 1);
    pstart[0] = 0;
    for (size_t b = 0; b < nb; ++b) pstart[b + 1] = pstart[b] + cdiv(rs_block_size(*this, b), size_t(k));
    np = pstart[nb];
}

// ---------------------------------------------------------------- sender
Packetizer::Packetizer(size_t mtu, int fec_k, Fec fec) {
    mtu_ = std::max<size_t>(mtu, kDgramHdr + 16);
    maxpay_ = std::min<size_t>(mtu_ - kDgramHdr, 65535);
    k_ = std::max(0, std::min(fec_k, 255));
    fec_ = k_ ? fec : Fec::None;
    if (fec_ == Fec::None) k_ = 0;
}

size_t Packetizer::datagrams(size_t size) const {
    FecLayout L; L.init(fec_, k_, ndata(size, maxpay_));
    return L.nd + L.np;
}

size_t Packetizer::wire_bytes(size_t size) const {
    const size_t nd = ndata(size, maxpay_);
    const size_t pay = size ? cdiv(size, nd) : 1;
    FecLayout L; L.init(fec_, k_, nd);
    return size + (nd + L.np) * kDgramHdr + L.np * pay;
}

void Packetizer::packetize(const uint8_t* unit, size_t size, uint32_t frame_seq, std::vector<std::vector<uint8_t>>& out) {
    const size_t nd = ndata(size, maxpay_);
    const size_t pay = size ? cdiv(size, nd) : 1;
    FecLayout L; L.init(fec_, k_, nd);
    auto header = [&](std::vector<uint8_t>& d, uint8_t flags, size_t off) {
        d[0] = 'E'; d[1] = 'U'; d[2] = kNetVersion; d[3] = flags;
        put32(&d[4], dseq_++);
        put32(&d[8], frame_seq);
        put32(&d[12], uint32_t(off));
        put32(&d[16], uint32_t(size));
        put16(&d[20], uint32_t(pay));
        d[22] = uint8_t(k_); d[23] = uint8_t(fec_);
    };
    auto dlen = [&](size_t i) { const size_t off = i * pay; return off >= size ? size_t(0) : std::min(pay, size - off); };
    for (size_t i = 0; i < nd; ++i) {
        const size_t n = dlen(i);
        std::vector<uint8_t> d(kDgramHdr + n);
        header(d, i + 1 == nd ? 1 : 0, i * pay);
        if (n) std::memcpy(&d[kDgramHdr], unit + i * pay, n);
        out.push_back(std::move(d));
    }
    if (L.fec == Fec::None) return;
    std::vector<uint8_t> par(pay);
    std::vector<size_t> mem;
    for (size_t j = 0; j < L.np; ++j) {
        std::fill(par.begin(), par.end(), 0);
        if (L.fec == Fec::RS) {
            const size_t b = size_t(std::upper_bound(L.pstart.begin(), L.pstart.end(), j) - L.pstart.begin()) - 1;
            const size_t r = j - L.pstart[b], nbk = rs_block_size(L, b);
            for (size_t t = 0; t < nbk; ++t) {
                const size_t i = b + t * L.nb;
                axpy(par.data(), unit + i * pay, dlen(i), cauchy(r, t));
            }
        } else {
            xor_members(L, j, mem);
            for (size_t i : mem) axpy(par.data(), unit + i * pay, dlen(i), 1);
        }
        std::vector<uint8_t> p(kDgramHdr + pay);
        header(p, 2, j * pay);
        std::memcpy(&p[kDgramHdr], par.data(), pay);
        out.push_back(std::move(p));
    }
}

// ---------------------------------------------------------------- receiver
bool Reassembler::Unit::complete() const {
    return std::all_of(got.begin(), got.end(), [](uint8_t v) { return v != 0; });
}
size_t Reassembler::Unit::missing() const { return size_t(std::count(got.begin(), got.end(), uint8_t(0))); }
std::vector<std::pair<size_t, size_t>> Reassembler::Unit::missing_ranges() const {
    std::vector<std::pair<size_t, size_t>> r;
    for (size_t i = 0; i < got.size(); ++i) {
        if (got[i]) continue;
        const size_t off = i * pay;
        if (off >= data.size()) continue;
        const size_t n = std::min(pay, data.size() - off);
        if (!r.empty() && r.back().first + r.back().second == off) r.back().second += n;
        else r.emplace_back(off, n);
    }
    return r;
}

void Reassembler::try_recover(Unit& u) {
    const FecLayout& L = u.lay;
    if (L.fec == Fec::None || u.complete()) return;
    auto dptr = [&](size_t i) { return u.data.data() + i * u.pay; };
    auto dlen = [&](size_t i) { const size_t off = i * u.pay; return off >= u.data.size() ? size_t(0) : std::min(u.pay, u.data.size() - off); };
    if (L.fec != Fec::RS) {
        std::vector<size_t> mem;
        for (auto& kv : u.parity) {
            xor_members(L, kv.first, mem);
            int miss = 0; size_t mi = 0;
            for (size_t i : mem) if (!u.got[i]) { ++miss; mi = i; }
            if (miss != 1) continue;
            std::vector<uint8_t> x = kv.second;
            for (size_t i : mem) if (i != mi) axpy(x.data(), dptr(i), dlen(i), 1);
            std::memcpy(dptr(mi), x.data(), dlen(mi));
            u.got[mi] = 1; ++u.recovered; ++datagrams_recovered;
        }
        return;
    }
    const GF& g = gf();
    for (size_t b = 0; b < L.nb; ++b) {
        const size_t nbk = rs_block_size(L, b), m = L.pstart[b + 1] - L.pstart[b];
        std::vector<size_t> miss;                // column indices t of missing data
        for (size_t t = 0; t < nbk; ++t) if (!u.got[b + t * L.nb]) miss.push_back(t);
        if (miss.empty()) continue;
        std::vector<size_t> rows;                // received parity rows of this block
        for (size_t r = 0; r < m && rows.size() < miss.size(); ++r)
            if (u.parity.count(uint32_t(L.pstart[b] + r))) rows.push_back(r);
        const size_t e = miss.size();
        if (rows.size() < e) continue;
        // syndromes: s_a = parity(r_a) - sum over received columns of C(r_a, t) d_t
        std::vector<std::vector<uint8_t>> s(e);
        for (size_t a = 0; a < e; ++a) {
            s[a] = u.parity[uint32_t(L.pstart[b] + rows[a])];
            for (size_t t = 0; t < nbk; ++t) {
                const size_t i = b + t * L.nb;
                if (u.got[i]) axpy(s[a].data(), dptr(i), dlen(i), cauchy(rows[a], t));
            }
        }
        // solve A x = s with A[a][c] = C(r_a, miss_c): Gauss-Jordan inverse over GF(256)
        std::vector<uint8_t> A(e * e), I(e * e, 0);
        for (size_t a = 0; a < e; ++a) { I[a * e + a] = 1; for (size_t c = 0; c < e; ++c) A[a * e + c] = cauchy(rows[a], miss[c]); }
        bool singular = false;
        for (size_t c = 0; c < e && !singular; ++c) {
            size_t piv = c;
            while (piv < e && !A[piv * e + c]) ++piv;
            if (piv == e) { singular = true; break; }
            for (size_t x = 0; x < e; ++x) { std::swap(A[c * e + x], A[piv * e + x]); std::swap(I[c * e + x], I[piv * e + x]); }
            const uint8_t iv = g.inv(A[c * e + c]);
            for (size_t x = 0; x < e; ++x) { A[c * e + x] = g.mul(A[c * e + x], iv); I[c * e + x] = g.mul(I[c * e + x], iv); }
            for (size_t r = 0; r < e; ++r) {
                if (r == c || !A[r * e + c]) continue;
                const uint8_t f = A[r * e + c];
                for (size_t x = 0; x < e; ++x) { A[r * e + x] ^= g.mul(f, A[c * e + x]); I[r * e + x] ^= g.mul(f, I[c * e + x]); }
            }
        }
        if (singular) continue;   // cannot happen for a Cauchy matrix; defensive
        std::vector<uint8_t> x(u.pay);
        for (size_t c = 0; c < e; ++c) {
            std::fill(x.begin(), x.end(), 0);
            for (size_t a = 0; a < e; ++a) axpy(x.data(), s[a].data(), u.pay, I[c * e + a]);
            const size_t i = b + miss[c] * L.nb;
            std::memcpy(dptr(i), x.data(), dlen(i));
            u.got[i] = 1; ++u.recovered; ++datagrams_recovered;
        }
    }
}

void Reassembler::release(std::map<uint32_t, Unit>::iterator it) {
    try_recover(it->second);
    last_released_ = it->first; have_last_ = true;
    ready_.push_back(std::move(it->second));
    open_.erase(it);
}

void Reassembler::push(const uint8_t* d, size_t n) {
    if (n < kDgramHdr || d[0] != 'E' || d[1] != 'U' || d[2] != kNetVersion) { ++datagrams_bad; return; }
    const bool parity = d[3] & 2;
    const uint32_t fseq = get32(d + 8), off = get32(d + 12), total = get32(d + 16), pay = get16(d + 20);
    const int k = d[22];
    const uint8_t fecb = d[23];
    const size_t len = n - kDgramHdr;
    if (total > kMaxUnit || pay < 1 || fecb > 3 || off % pay) { ++datagrams_bad; return; }
    const Fec fec = Fec(fecb);
    if ((fec == Fec::None) != (k == 0) || (parity && fec == Fec::None)) { ++datagrams_bad; return; }
    const size_t nd = ndata(total, pay), idx = off / pay;
    if (nd > (size_t(1) << 20)) { ++datagrams_bad; return; }
    if (parity) {
        FecLayout L; L.init(fec, k, nd);
        if (idx >= L.np || len != pay) { ++datagrams_bad; return; }
    } else {
        const size_t expect = off >= total ? 0 : std::min<size_t>(pay, total - off);   // exact length: truncation is detected
        if (idx >= nd || len != expect) { ++datagrams_bad; return; }
    }
    if (have_last_ && int32_t(fseq - last_released_) <= 0) { ++datagrams_late; return; }   // late / duplicate unit
    auto it = open_.find(fseq);
    if (it == open_.end()) {
        it = open_.emplace(fseq, Unit{}).first;
        Unit& u = it->second;
        u.frame_seq = fseq; u.pay = pay; u.k = k; u.fec = fec;
        u.data.assign(total, 0);
        u.got.assign(nd, 0);
        u.lay.init(fec, k, nd);
    }
    Unit& u = it->second;
    if (u.data.size() != total || u.pay != pay || u.k != k || u.fec != fec) { ++datagrams_bad; return; }
    if (parity) {
        if (u.parity.count(uint32_t(idx))) { ++datagrams_dup; return; }
        u.parity[uint32_t(idx)].assign(d + kDgramHdr, d + kDgramHdr + len);
    } else {
        if (u.got[idx]) { ++datagrams_dup; return; }
        if (len) std::memcpy(u.data.data() + off, d + kDgramHdr, len);
        u.got[idx] = 1;
    }
    ++datagrams_ok;
    if (!u.complete()) try_recover(u);
    // release in frame order: complete head units, or the oldest when too many are open
    while (!open_.empty()) {
        auto h = open_.begin();
        if (!(h->second.complete() || open_.size() > max_pending)) break;
        release(h);
    }
}

void Reassembler::pop(std::vector<Unit>& out, bool flush) {
    if (flush) while (!open_.empty()) release(open_.begin());
    for (auto& u : ready_) out.push_back(std::move(u));
    ready_.clear();
}

}  // namespace ewc
