#include "dwt.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace ewc {

namespace {
// Q14 lifting constants for CDF 9/7 (JPEG 2000 irreversible factorisation, scaling step omitted;
// the per-subband synthesis norms absorb the scaling).
constexpr int64_t A97 = -25987;  // -1.586134342 * 16384
constexpr int64_t B97 = -868;    // -0.052980118 * 16384
constexpr int64_t C97 = 14466;   //  0.882911075 * 16384
constexpr int64_t D97 = 7266;    //  0.443506852 * 16384
inline int32_t rq(int64_t v) { return int32_t((v + 8192) >> 14); }

// ---- 1-D on a contiguous line (even length n), output [s | d] into `out`.
void fwd53_line(const int32_t* x, int n, int32_t* out) {
    const int h = n >> 1;
    int32_t* s = out;
    int32_t* d = out + h;
    for (int i = 0; i < h; ++i) {
        int32_t a = x[2 * i], b = (2 * i + 2 < n) ? x[2 * i + 2] : x[2 * i];
        d[i] = x[2 * i + 1] - ((a + b) >> 1);
    }
    for (int i = 0; i < h; ++i) {
        int32_t dl = i ? d[i - 1] : d[0];
        s[i] = x[2 * i] + ((dl + d[i] + 2) >> 2);
    }
}
void inv53_line(const int32_t* in, int n, int32_t* x) {
    const int h = n >> 1;
    const int32_t* s = in;
    const int32_t* d = in + h;
    for (int i = 0; i < h; ++i) {
        int32_t dl = i ? d[i - 1] : d[0];
        x[2 * i] = s[i] - ((dl + d[i] + 2) >> 2);
    }
    for (int i = 0; i < h; ++i) {
        int32_t a = x[2 * i], b = (2 * i + 2 < n) ? x[2 * i + 2] : x[2 * i];
        x[2 * i + 1] = d[i] + ((a + b) >> 1);
    }
}
void fwd97_line(const int32_t* x, int n, int32_t* out) {
    const int h = n >> 1;
    int32_t* s = out;
    int32_t* d = out + h;
    for (int i = 0; i < h; ++i) { s[i] = x[2 * i]; d[i] = x[2 * i + 1]; }
    for (int i = 0; i < h; ++i) d[i] += rq(A97 * (int64_t(s[i]) + s[i + 1 < h ? i + 1 : i]));
    for (int i = 0; i < h; ++i) s[i] += rq(B97 * (int64_t(d[i ? i - 1 : 0]) + d[i]));
    for (int i = 0; i < h; ++i) d[i] += rq(C97 * (int64_t(s[i]) + s[i + 1 < h ? i + 1 : i]));
    for (int i = 0; i < h; ++i) s[i] += rq(D97 * (int64_t(d[i ? i - 1 : 0]) + d[i]));
}
void inv97_line(const int32_t* in, int n, int32_t* x, int32_t* tmp) {
    const int h = n >> 1;
    int32_t* s = tmp;
    int32_t* d = tmp + h;
    std::memcpy(tmp, in, sizeof(int32_t) * n);
    for (int i = 0; i < h; ++i) s[i] -= rq(D97 * (int64_t(d[i ? i - 1 : 0]) + d[i]));
    for (int i = 0; i < h; ++i) d[i] -= rq(C97 * (int64_t(s[i]) + s[i + 1 < h ? i + 1 : i]));
    for (int i = 0; i < h; ++i) s[i] -= rq(B97 * (int64_t(d[i ? i - 1 : 0]) + d[i]));
    for (int i = 0; i < h; ++i) d[i] -= rq(A97 * (int64_t(s[i]) + s[i + 1 < h ? i + 1 : i]));
    for (int i = 0; i < h; ++i) { x[2 * i] = s[i]; x[2 * i + 1] = d[i]; }
}

// ---- vertical transforms operate on whole rows (vectorisable inner loops)
// rows: pointers to h rows of width w (in-place region of the image with given stride)
void fwd_vert(int32_t* img, int stride, int w, int h, Wavelet wv, std::vector<int32_t>& tmp) {
    const int hh = h >> 1;
    tmp.resize(size_t(w) * h);
    int32_t* S = tmp.data();             // hh rows
    int32_t* D = tmp.data() + size_t(w) * hh;
    auto srow = [&](int i) { return S + size_t(w) * std::min(std::max(i, 0), hh - 1); };
    auto drow = [&](int i) { return D + size_t(w) * std::min(std::max(i, 0), hh - 1); };
    for (int i = 0; i < hh; ++i) {
        std::memcpy(S + size_t(w) * i, img + size_t(2 * i) * stride, sizeof(int32_t) * w);
        std::memcpy(D + size_t(w) * i, img + size_t(2 * i + 1) * stride, sizeof(int32_t) * w);
    }
    if (wv == Wavelet::W53) {
        for (int i = 0; i < hh; ++i) {
            int32_t* d = drow(i); const int32_t* a = srow(i); const int32_t* b = srow(i + 1);
            for (int x = 0; x < w; ++x) d[x] -= (a[x] + b[x]) >> 1;
        }
        for (int i = 0; i < hh; ++i) {
            int32_t* s = srow(i); const int32_t* a = drow(i - 1); const int32_t* b = drow(i);
            for (int x = 0; x < w; ++x) s[x] += (a[x] + b[x] + 2) >> 2;
        }
    } else {
        auto stepD = [&](int64_t K) {
            for (int i = 0; i < hh; ++i) {
                int32_t* d = drow(i); const int32_t* a = srow(i); const int32_t* b = srow(i + 1);
                for (int x = 0; x < w; ++x) d[x] += rq(K * (int64_t(a[x]) + b[x]));
            }
        };
        auto stepS = [&](int64_t K) {
            for (int i = 0; i < hh; ++i) {
                int32_t* s = srow(i); const int32_t* a = drow(i - 1); const int32_t* b = drow(i);
                for (int x = 0; x < w; ++x) s[x] += rq(K * (int64_t(a[x]) + b[x]));
            }
        };
        stepD(A97); stepS(B97); stepD(C97); stepS(D97);
    }
    for (int i = 0; i < hh; ++i) {
        std::memcpy(img + size_t(i) * stride, S + size_t(w) * i, sizeof(int32_t) * w);
        std::memcpy(img + size_t(hh + i) * stride, D + size_t(w) * i, sizeof(int32_t) * w);
    }
}

void inv_vert(int32_t* img, int stride, int w, int h, Wavelet wv, std::vector<int32_t>& tmp) {
    const int hh = h >> 1;
    tmp.resize(size_t(w) * h);
    int32_t* S = tmp.data();
    int32_t* D = tmp.data() + size_t(w) * hh;
    auto srow = [&](int i) { return S + size_t(w) * std::min(std::max(i, 0), hh - 1); };
    auto drow = [&](int i) { return D + size_t(w) * std::min(std::max(i, 0), hh - 1); };
    for (int i = 0; i < hh; ++i) {
        std::memcpy(S + size_t(w) * i, img + size_t(i) * stride, sizeof(int32_t) * w);
        std::memcpy(D + size_t(w) * i, img + size_t(hh + i) * stride, sizeof(int32_t) * w);
    }
    if (wv == Wavelet::W53) {
        for (int i = 0; i < hh; ++i) {
            int32_t* s = srow(i); const int32_t* a = drow(i - 1); const int32_t* b = drow(i);
            for (int x = 0; x < w; ++x) s[x] -= (a[x] + b[x] + 2) >> 2;
        }
        for (int i = 0; i < hh; ++i) {
            int32_t* d = drow(i); const int32_t* a = srow(i); const int32_t* b = srow(i + 1);
            for (int x = 0; x < w; ++x) d[x] += (a[x] + b[x]) >> 1;
        }
    } else {
        auto stepD = [&](int64_t K) {
            for (int i = 0; i < hh; ++i) {
                int32_t* d = drow(i); const int32_t* a = srow(i); const int32_t* b = srow(i + 1);
                for (int x = 0; x < w; ++x) d[x] -= rq(K * (int64_t(a[x]) + b[x]));
            }
        };
        auto stepS = [&](int64_t K) {
            for (int i = 0; i < hh; ++i) {
                int32_t* s = srow(i); const int32_t* a = drow(i - 1); const int32_t* b = drow(i);
                for (int x = 0; x < w; ++x) s[x] -= rq(K * (int64_t(a[x]) + b[x]));
            }
        };
        stepS(D97); stepD(C97); stepS(B97); stepD(A97);
    }
    for (int i = 0; i < hh; ++i) {
        std::memcpy(img + size_t(2 * i) * stride, S + size_t(w) * i, sizeof(int32_t) * w);
        std::memcpy(img + size_t(2 * i + 1) * stride, D + size_t(w) * i, sizeof(int32_t) * w);
    }
}

// float reference (no rounding) for synthesis-norm computation
void inv_line_f(std::vector<double>& x, int n, Wavelet wv) {
    const int h = n >> 1;
    std::vector<double> s(x.begin(), x.begin() + h), d(x.begin() + h, x.begin() + n);
    auto S = [&](int i) { return s[std::min(std::max(i, 0), h - 1)]; };
    auto Dd = [&](int i) { return d[std::min(std::max(i, 0), h - 1)]; };
    if (wv == Wavelet::W53) {
        for (int i = 0; i < h; ++i) s[i] -= (Dd(i - 1) + d[i]) / 4.0;
        for (int i = 0; i < h; ++i) d[i] += (s[i] + S(i + 1)) / 2.0;
    } else {
        const double a = A97 / 16384.0, b = B97 / 16384.0, c = C97 / 16384.0, dd = D97 / 16384.0;
        for (int i = 0; i < h; ++i) s[i] -= dd * (Dd(i - 1) + d[i]);
        for (int i = 0; i < h; ++i) d[i] -= c * (s[i] + S(i + 1));
        for (int i = 0; i < h; ++i) s[i] -= b * (Dd(i - 1) + d[i]);
        for (int i = 0; i < h; ++i) d[i] -= a * (s[i] + S(i + 1));
    }
    for (int i = 0; i < h; ++i) { x[2 * i] = s[i]; x[2 * i + 1] = d[i]; }
}
}  // namespace

void dwt_forward(int32_t* img, int W, int H, int levels, Wavelet wv, std::vector<int32_t>& tmp) {
    std::vector<int32_t> line(std::max(W, H)), out(std::max(W, H));
    int w = W, h = H;
    for (int l = 0; l < levels; ++l) {
        for (int y = 0; y < h; ++y) {
            int32_t* row = img + size_t(y) * W;
            if (wv == Wavelet::W53) fwd53_line(row, w, out.data());
            else fwd97_line(row, w, out.data());
            std::memcpy(row, out.data(), sizeof(int32_t) * w);
        }
        fwd_vert(img, W, w, h, wv, tmp);
        w >>= 1; h >>= 1;
    }
}

void dwt_inverse(int32_t* img, int W, int H, int levels, Wavelet wv, std::vector<int32_t>& tmp) {
    std::vector<int32_t> out(std::max(W, H)), t2(std::max(W, H));
    for (int l = levels - 1; l >= 0; --l) {
        int w = W >> l, h = H >> l;
        inv_vert(img, W, w, h, wv, tmp);
        for (int y = 0; y < h; ++y) {
            int32_t* row = img + size_t(y) * W;
            if (wv == Wavelet::W53) inv53_line(row, w, out.data());
            else inv97_line(row, w, out.data(), t2.data());
            std::memcpy(row, out.data(), sizeof(int32_t) * w);
        }
    }
}

double synth_norm_1d(Wavelet wv, int level, int hi) {
    const int n = 1 << (level + 6);
    std::vector<double> x(n, 0.0);
    const int bandlen = n >> level;            // length of the band at this level
    const int off = hi ? bandlen : 0;          // high band sits after the low band
    x[off + bandlen / 2] = 1.0;
    for (int l = level - 1; l >= 0; --l) {
        int len = n >> l;
        std::vector<double> seg(x.begin(), x.begin() + len);
        inv_line_f(seg, len, wv);
        std::copy(seg.begin(), seg.end(), x.begin());
    }
    double e = 0;
    for (double v : x) e += v * v;
    return std::sqrt(e);
}

}  // namespace ewc
