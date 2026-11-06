// ewc — embedded set-partitioning (SPIHT-style) coder with context-adaptive binary arithmetic
// coding. Coefficient trees are partitioned into independent "groups" (horizontal stripes of
// LL roots), each with its own range coder and context state, so that:
//   * groups can be coded in parallel,
//   * a lost / corrupted group only degrades its own image region,
//   * every group stream is embedded: any prefix decodes to a coarser approximation.
// The bit-plane algorithm is written once (templated on encode/decode) so that encoder and
// decoder can never diverge.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include "rc.h"

namespace ewc {

struct Layout {
    int W = 0, H = 0, L = 0;     // padded size, levels
    int wL = 0, hL = 0;          // LL size
    struct Band { int lvl, orient, oy, ox, bh, bw; };  // lvl 0 = LL, 1 = coarsest detail .. L = finest
    std::vector<Band> bands;     // index 0 = LL, then (coarsest..finest) x (HL, LH, HH)
    std::vector<uint8_t> band;   // band index per coefficient
    void init(int W, int H, int L);
    inline bool has_children(int y, int x) const {
        return (y < hL && x < wL) || (y < (H >> 1) && x < (W >> 1));
    }
    inline int children(int y, int x, uint32_t* out) const {
        if (y < hL && x < wL) {
            out[0] = uint32_t(y) * W + (x + wL);
            out[1] = uint32_t(y + hL) * W + x;
            out[2] = uint32_t(y + hL) * W + (x + wL);
            return 3;
        }
        if (y < (H >> 1) && x < (W >> 1)) {
            const uint32_t b = uint32_t(2 * y) * W + 2 * x;
            out[0] = b; out[1] = b + 1; out[2] = b + W; out[3] = b + W + 1;
            return 4;
        }
        return 0;
    }
    inline int parent(int y, int x) const {  // -1 for LL
        if (y < hL && x < wL) return -1;
        if (y < 2 * hL && x < 2 * wL) return (y >= hL ? y - hL : y) * W + (x >= wL ? x - wL : x);
        return (y >> 1) * W + (x >> 1);
    }
};

struct GroupDef {
    int r0 = 0, r1 = 0;               // LL root rows [r0, r1)
    std::vector<int> ylo, yhi;        // absolute row range of this group inside each band
    void init(const Layout& lay, int r0, int r1);
};

struct Checkpoint {
    RCState st;
    uint32_t nsym;
    double dist;     // cumulative distortion reduction (weighted coefficient domain)
};

struct Contexts {
    static constexpr int kLv = 8;   // level classes (0 = LL, 1.. = detail coarse->fine)
    Prob lip[kLv][2][9];            // [level][parent sig][EBCOT neighbourhood ctx]
    Prob off[kLv][2][3][9];         // [level][parent sig][siblings sig so far][EBCOT ctx]
    Prob setA[kLv][2][3][3];        // [level][node sig][nbr sig 0/1/2+][nbr set-sig 0/1/2+]
    Prob setB[kLv][3];              // [level][#children sig 0/1/2+]
    Prob ref[3][2][2];              // [LL/coarse/fine][first refinement][any nbr sig]
    Prob sign[5];                   // EBCOT sign contexts
};

// Per-coefficient state shared by all groups of a frame (groups touch disjoint entries).
struct CoefState {
    std::vector<uint32_t> a;     // magnitude lower bound (0 = insignificant)
    std::vector<int8_t> p;       // last decoded plane for significant coefficients
    std::vector<uint8_t> neg;    // sign
    std::vector<uint8_t> sig;    // significance flags
    std::vector<uint8_t> dsig;   // descendant-set significance flags (context modelling)
    void resize(size_t n) {
        a.assign(n, 0); p.assign(n, 0); neg.assign(n, 0); sig.assign(n, 0); dsig.assign(n, 0);
    }
};

// Encoder side of one group for one frame.
class GroupEncoder {
public:
    GroupEncoder();
    ~GroupEncoder();
    GroupEncoder(const GroupEncoder&) = delete;
    GroupEncoder& operator=(const GroupEncoder&) = delete;
    void begin(const Layout* lay, const GroupDef* g, const uint32_t* mag, const uint8_t* neg,
               const uint32_t* dmax, const uint32_t* lmax, CoefState* cs, int cp_interval);
    int nmax() const;
    void code_plane(int n);
    int next_plane() const;
    size_t bytes_now() const;
    const std::vector<Checkpoint>& cps() const;
    std::vector<uint8_t> terminate_at(const Checkpoint& c) const;
private:
    struct Impl;
    Impl* p_;
};

struct GroupDecodeResult { uint32_t nsym_decoded = 0; bool complete = false; int last_plane = -1; };

GroupDecodeResult decode_group(const Layout& lay, const GroupDef& g, const uint8_t* data, size_t size,
                               size_t valid, uint32_t nsym, int nmax, CoefState& cs);

extern int g_recon_q5;   // reconstruction offset inside the uncertainty interval, Q5 (16 = midpoint)
inline int32_t recon_value(uint32_t a, int p, bool neg) {
    if (a == 0) return 0;
    int32_t v = int32_t(a + (p > 0 ? uint32_t((uint64_t(g_recon_q5) << p) >> 5) : 0u));
    return neg ? -v : v;
}

}  // namespace ewc
