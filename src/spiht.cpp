#include "spiht.h"
#include <algorithm>
#include <climits>

namespace ewc {

int g_recon_q5 = 16;

void Layout::init(int W_, int H_, int L_) {
    W = W_; H = H_; L = L_;
    wL = W >> L; hL = H >> L;
    bands.clear();
    bands.push_back({0, 1, 0, 0, hL, wL});
    for (int d = 1; d <= L; ++d) {            // d = 1 coarsest detail level
        int l = L - d + 1;                     // decomposition level (1 = finest)
        int bw = W >> l, bh = H >> l;
        bands.push_back({d, 0, 0, bw, bh, bw});   // HL: horizontally high-pass
        bands.push_back({d, 1, bh, 0, bh, bw});   // LH
        bands.push_back({d, 2, bh, bw, bh, bw});  // HH
    }
    band.assign(size_t(W) * H, 0);
    for (size_t b = 0; b < bands.size(); ++b) {
        const Band& B = bands[b];
        for (int y = B.oy; y < B.oy + B.bh; ++y)
            for (int x = B.ox; x < B.ox + B.bw; ++x) band[size_t(y) * W + x] = uint8_t(b);
    }
}

void GroupDef::init(const Layout& lay, int r0_, int r1_) {
    r0 = r0_; r1 = r1_;
    ylo.resize(lay.bands.size()); yhi.resize(lay.bands.size());
    for (size_t b = 0; b < lay.bands.size(); ++b) {
        const auto& B = lay.bands[b];
        int sh = B.lvl == 0 ? 0 : B.lvl - 1;   // band rows per LL row: 2^(lvl-1)
        ylo[b] = B.oy + (r0 << sh);
        yhi[b] = B.oy + (r1 << sh);
    }
}

namespace {

inline int ebcot_ctx(int h, int v, int d, int orient) {
    if (orient == 2) {
        int hv = h + v;
        if (d >= 3) return 8;
        if (d == 2) return hv >= 1 ? 7 : 6;
        if (d == 1) return hv >= 2 ? 5 : (hv == 1 ? 4 : 3);
        return hv >= 2 ? 2 : (hv == 1 ? 1 : 0);
    }
    if (orient == 0) std::swap(h, v);
    if (h == 2) return 8;
    if (h == 1) return v >= 1 ? 7 : (d >= 1 ? 6 : 5);
    if (v == 2) return 4;
    if (v == 1) return 3;
    return d >= 2 ? 2 : (d == 1 ? 1 : 0);
}

inline int lvcls(int lvl) { return lvl < Contexts::kLv ? lvl : Contexts::kLv - 1; }
inline int refcls(int lvl, int L) { return lvl == 0 ? 0 : (2 * lvl <= L ? 1 : 2); }
inline int c2(int v) { return v >= 2 ? 2 : v; }
constexpr uint32_t DEAD = 0xFFFFFFFFu;

// --------------------------------------------------------------------------- shared core
template <bool ENC>
struct Core {
    const Layout& lay;
    const GroupDef& g;
    CoefState& cs;
    Contexts ctx{};
    std::vector<uint32_t> lip, lis, lsp;
    // encoder inputs
    const uint32_t* mag = nullptr;
    const uint8_t* ineg = nullptr;
    const uint32_t* dmax = nullptr;
    const uint32_t* lmax = nullptr;
    RCEncoder* enc = nullptr;
    std::vector<Checkpoint>* cps = nullptr;
    int cpi = 32;
    double dist = 0;
    // decoder inputs
    RCDecoder* dec = nullptr;
    uint32_t limit = UINT32_MAX;
    uint32_t nsym = 0, since = 0;

    Core(const Layout& l, const GroupDef& gd, CoefState& c) : lay(l), g(gd), cs(c) {}

    void init_lists() {
        const int W = lay.W;
        for (int y = g.r0; y < g.r1; ++y)
            for (int x = 0; x < lay.wL; ++x) {
                uint32_t i = uint32_t(y) * W + x;
                lip.push_back(i);
                lis.push_back(i << 1);
            }
    }

    inline bool live() const {
        if constexpr (ENC) return true;
        else return nsym < limit && dec->ok();
    }
    inline int code(Prob& pr, int v) {
        if constexpr (ENC) enc->encode(pr, v);
        else v = dec->decode(pr);
        ++nsym; ++since;
        return v;
    }
    inline void cp() {
        if constexpr (ENC) {
            if (since >= uint32_t(cpi)) { cps->push_back({enc->snapshot(), nsym, dist}); since = 0; }
        }
    }

    struct Nbr { int h, v, d, ds; };
    inline Nbr nbr(int y, int x, int b, bool want_ds) const {
        const auto& B = lay.bands[b];
        const int ylo = g.ylo[b], yhi = g.yhi[b], xlo = B.ox, xhi = B.ox + B.bw;
        const int W = lay.W;
        const bool up = y - 1 >= ylo, dn = y + 1 < yhi, lf = x - 1 >= xlo, rt = x + 1 < xhi;
        const size_t i = size_t(y) * W + x;
        const uint8_t* r = cs.sig.data() + i;
        Nbr n{0, 0, 0, 0};
        if (lf) n.h += r[-1];
        if (rt) n.h += r[1];
        if (up) { n.v += r[-W]; if (lf) n.d += r[-W - 1]; if (rt) n.d += r[-W + 1]; }
        if (dn) { n.v += r[W]; if (lf) n.d += r[W - 1]; if (rt) n.d += r[W + 1]; }
        if (want_ds) {
            const uint8_t* s = cs.dsig.data() + i;
            if (lf) n.ds += s[-1];
            if (rt) n.ds += s[1];
            if (up) { n.ds += s[-W]; if (lf) n.ds += s[-W - 1]; if (rt) n.ds += s[-W + 1]; }
            if (dn) { n.ds += s[W]; if (lf) n.ds += s[W - 1]; if (rt) n.ds += s[W + 1]; }
        }
        return n;
    }

    inline int sgn_at(int yy, int xx, int b) const {
        const auto& B = lay.bands[b];
        if (yy < g.ylo[b] || yy >= g.yhi[b] || xx < B.ox || xx >= B.ox + B.bw) return 0;
        const size_t i = size_t(yy) * lay.W + xx;
        return cs.sig[i] ? (cs.neg[i] ? -1 : 1) : 0;
    }

    // coefficient becomes significant at plane n: code sign (EBCOT sign contexts), update state
    bool become_sig(uint32_t idx, int y, int x, int b, int n) {
        if (!live()) return false;
        int hc = sgn_at(y, x - 1, b) + sgn_at(y, x + 1, b);
        int vc = sgn_at(y - 1, x, b) + sgn_at(y + 1, x, b);
        hc = hc > 0 ? 1 : (hc < 0 ? -1 : 0);
        vc = vc > 0 ? 1 : (vc < 0 ? -1 : 0);
        // rows: hc = -1, 0, +1 ; columns: vc = +1, 0, -1
        static const int tctx[3][3] = {{2, 3, 4}, {1, 0, 1}, {4, 3, 2}};
        static const int txor[3][3] = {{1, 1, 1}, {0, 0, 1}, {0, 0, 0}};
        const int ri = hc + 1, ci = 1 - vc;
        const int sc = tctx[ri][ci], xr = txor[ri][ci];
        int s = 0;
        if constexpr (ENC) s = ineg[idx];
        s = code(ctx.sign[sc], s ^ xr) ^ xr;
        cs.neg[idx] = uint8_t(s);
        cs.sig[idx] = 1;
        cs.a[idx] = 1u << n;
        cs.p[idx] = int8_t(n);
        lsp.push_back(idx);
        if constexpr (ENC) {
            const double m = mag[idx];
            const double rec = recon_value(1u << n, n, false);
            dist += m * m - (m - rec) * (m - rec);
        }
        return true;
    }

    // one bit plane; returns false when (decoder) data ran out
    bool plane(int n) {
        const int W = lay.W;
        const uint32_t T = 1u << n;
        const size_t lsp_prev = lsp.size();
        // ---- 1. LIP
        size_t w = 0;
        for (size_t k = 0; k < lip.size(); ++k) {
            const uint32_t idx = lip[k];
            if (!live()) return false;
            cp();
            const int y = int(idx / W), x = int(idx % W);
            const int b = lay.band[idx];
            const auto& B = lay.bands[b];
            const Nbr nb = nbr(y, x, b, false);
            const int par = lay.parent(y, x);
            const int ps = par >= 0 ? cs.sig[par] : 0;
            int v = 0;
            if constexpr (ENC) v = mag[idx] >= T;
            v = code(ctx.lip[lvcls(B.lvl)][ps][ebcot_ctx(nb.h, nb.v, nb.d, B.orient)], v);
            if (v) { if (!become_sig(idx, y, x, b, n)) return false; }
            else lip[w++] = idx;
        }
        lip.resize(w);
        // ---- 2. LIS
        uint32_t ch[4];
        for (size_t q = 0; q < lis.size(); ++q) {
            const uint32_t e = lis[q];
            const uint32_t idx = e >> 1;
            const int y = int(idx / W), x = int(idx % W);
            const int b = lay.band[idx];
            const int lv = lvcls(lay.bands[b].lvl);
            if (!live()) return false;
            cp();
            if ((e & 1) == 0) {
                const Nbr nb = nbr(y, x, b, true);
                int v = 0;
                if constexpr (ENC) v = dmax[idx] >= T;
                v = code(ctx.setA[lv][cs.sig[idx]][c2(nb.h + nb.v + nb.d)][c2(nb.ds)], v);
                if (v) {
                    cs.dsig[idx] = 1;
                    const int nc = lay.children(y, x, ch);
                    const int cy0 = int(ch[0] / W), cx0 = int(ch[0] % W);
                    const bool leaf = !lay.has_children(cy0, cx0);
                    int sib = 0;
                    for (int c = 0; c < nc; ++c) {
                        const uint32_t ci = ch[c];
                        const int cy = int(ci / W), cx = int(ci % W);
                        const int cb = lay.band[ci];
                        const auto& CB = lay.bands[cb];
                        int cv;
                        if (leaf && c == nc - 1 && sib == 0) cv = 1;      // implied by D(idx) significant
                        else {
                            if (!live()) return false;
                            cp();
                            const Nbr cn = nbr(cy, cx, cb, false);
                            cv = 0;
                            if constexpr (ENC) cv = mag[ci] >= T;
                            cv = code(ctx.off[lvcls(CB.lvl)][cs.sig[idx]][c2(sib)]
                                             [ebcot_ctx(cn.h, cn.v, cn.d, CB.orient)], cv);
                        }
                        if (cv) { ++sib; if (!become_sig(ci, cy, cx, cb, n)) return false; }
                        else lip.push_back(ci);
                    }
                    if (!leaf) lis.push_back((idx << 1) | 1);
                    lis[q] = DEAD;
                }
            } else {
                const int nc = lay.children(y, x, ch);
                int csig = 0;
                for (int c = 0; c < nc; ++c) csig += cs.sig[ch[c]];
                int v = 0;
                if constexpr (ENC) v = lmax[idx] >= T;
                v = code(ctx.setB[lv][c2(csig)], v);
                if (v) {
                    for (int c = 0; c < nc; ++c) lis.push_back(ch[c] << 1);
                    lis[q] = DEAD;
                }
            }
        }
        {
            size_t o = 0;
            for (size_t q = 0; q < lis.size(); ++q) if (lis[q] != DEAD) lis[o++] = lis[q];
            lis.resize(o);
        }
        // ---- 3. refinement
        for (size_t k = 0; k < lsp_prev; ++k) {
            if (!live()) return false;
            cp();
            const uint32_t idx = lsp[k];
            const int y = int(idx / W), x = int(idx % W);
            const int b = lay.band[idx];
            const Nbr nb = nbr(y, x, b, false);
            const int any = (nb.h + nb.v + nb.d) > 0;
            const int first = cs.p[idx] == n + 1;
            int v = 0;
            if constexpr (ENC) v = (mag[idx] >> n) & 1;
            v = code(ctx.ref[refcls(lay.bands[b].lvl, lay.L)][first][any], v);
            const uint32_t a0 = cs.a[idx];
            const int p0 = cs.p[idx];
            cs.a[idx] = a0 | (uint32_t(v) << n);
            cs.p[idx] = int8_t(n);
            if constexpr (ENC) {
                const double m = mag[idx];
                const double ro = recon_value(a0, p0, false), rn = recon_value(cs.a[idx], n, false);
                dist += (m - ro) * (m - ro) - (m - rn) * (m - rn);
            }
        }
        if constexpr (ENC) { cps->push_back({enc->snapshot(), nsym, dist}); since = 0; }
        return true;
    }
};

}  // namespace

// --------------------------------------------------------------------------------- encoder
struct GroupEncoder::Impl {
    RCEncoder rc;
    std::vector<Checkpoint> cps;
    Core<true>* core = nullptr;
    int nmax = -1, plane = -1;
    ~Impl() { delete core; }
};

GroupEncoder::GroupEncoder() : p_(new Impl()) {}
GroupEncoder::~GroupEncoder() { delete p_; }

void GroupEncoder::begin(const Layout* lay, const GroupDef* g, const uint32_t* mag, const uint8_t* neg,
                         const uint32_t* dmax, const uint32_t* lmax, CoefState* cs, int cp_interval) {
    delete p_->core;
    p_->core = new Core<true>(*lay, *g, *cs);
    auto& c = *p_->core;
    c.mag = mag; c.ineg = neg; c.dmax = dmax; c.lmax = lmax;
    p_->rc.reset();
    p_->cps.clear();
    c.enc = &p_->rc; c.cps = &p_->cps; c.cpi = cp_interval;
    c.init_lists();
    uint32_t mx = 0;
    for (uint32_t i : c.lip) mx = std::max(mx, std::max(mag[i], dmax[i]));
    int nm = -1;
    while (mx) { ++nm; mx >>= 1; }
    p_->nmax = nm; p_->plane = nm;
    p_->cps.push_back({p_->rc.snapshot(), 0, 0.0});
}
int GroupEncoder::nmax() const { return p_->nmax; }
int GroupEncoder::next_plane() const { return p_->plane; }
void GroupEncoder::code_plane(int n) { p_->core->plane(n); p_->plane = n - 1; }
size_t GroupEncoder::bytes_now() const { return RCEncoder::terminated_size(p_->rc.snapshot()); }
const std::vector<Checkpoint>& GroupEncoder::cps() const { return p_->cps; }
std::vector<uint8_t> GroupEncoder::terminate_at(const Checkpoint& c) const {
    if (!c.nsym) return {};
    return p_->rc.terminate_at(c.st);
}

// --------------------------------------------------------------------------------- decoder
GroupDecodeResult decode_group(const Layout& lay, const GroupDef& g, const uint8_t* data, size_t size,
                               size_t valid, uint32_t nsym, int nmax, CoefState& cs) {
    GroupDecodeResult res;
    if (nmax < 0) { res.complete = true; return res; }
    RCDecoder rc;
    rc.init(data, size, valid);
    Core<false>* c = new Core<false>(lay, g, cs);
    c->dec = &rc;
    c->limit = nsym;
    c->init_lists();
    int n = nmax;
    for (; n >= 0; --n)
        if (!c->plane(n)) break;
    res.nsym_decoded = c->nsym;
    res.complete = n < 0;
    res.last_plane = n;
    delete c;
    return res;
}

}  // namespace ewc
