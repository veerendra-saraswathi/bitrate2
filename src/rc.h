// ewc — binary adaptive range coder (LZMA-style carry handling), pure integer, deterministic.
// Encoder state can be snapshotted at any symbol boundary and terminated from the snapshot,
// which is what makes post-hoc rate allocation (PCRD) possible without re-encoding.
#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <cstring>

namespace ewc {

// 15-bit probability of bit==0, two-rate estimator (fast + slow) averaged.
struct Prob {
    uint16_t f = 1u << 14;  // fast adapting
    uint16_t s = 1u << 14;  // slow adapting
    inline uint32_t p() const { return (uint32_t(f) + uint32_t(s)) >> 1; }
    inline void update(int bit) {
        if (bit) { f -= f >> 4; s -= s >> 7; }
        else     { f += (32768 - f) >> 4; s += (32768 - s) >> 7; }
    }
};

constexpr uint32_t kTop = 1u << 24;
constexpr int kPB = 15;

struct RCState {           // everything needed to terminate the stream at this point
    uint64_t low;
    uint32_t range;
    uint32_t cache_size;
    uint8_t cache;
    bool first;            // leading (always-zero) byte not yet emitted/dropped
    size_t out_size;
};

class RCEncoder {
public:
    std::vector<uint8_t> out;
    RCEncoder() { reset(); }
    void reset() { low_ = 0; range_ = 0xFFFFFFFFu; cache_ = 0; cache_size_ = 1; out.clear(); first_ = true; }

    inline void encode(Prob& pr, int bit) {
        uint32_t p = pr.p();
        if (p < 32) p = 32; else if (p > 32768 - 32) p = 32768 - 32;
        uint32_t bound = (range_ >> kPB) * p;
        if (!bit) range_ = bound;
        else { low_ += bound; range_ -= bound; }
        pr.update(bit);
        while (range_ < kTop) { range_ <<= 8; shift_low(); }
    }
    // raw probability (15-bit, of bit == 0) for models that keep their own state
    inline void encode_p(uint32_t p, int bit) {
        if (p < 32) p = 32; else if (p > 32768 - 32) p = 32768 - 32;
        uint32_t bound = (range_ >> kPB) * p;
        if (!bit) range_ = bound;
        else { low_ += bound; range_ -= bound; }
        while (range_ < kTop) { range_ <<= 8; shift_low(); }
    }
    inline void encode_bypass(int bit) {
        range_ >>= 1;
        if (bit) low_ += range_;
        while (range_ < kTop) { range_ <<= 8; shift_low(); }
    }
    RCState snapshot() const { return RCState{low_, range_, cache_size_, cache_, first_, out.size()}; }

    // Bytes the stream would have if terminated at snapshot s.
    static size_t terminated_size(const RCState& s) {
        // terminate = 5 x shift_low; each shift_low eventually emits one byte; the leading
        // byte of the whole stream is always 0 and is dropped (see shift_low).
        return s.out_size + s.cache_size + 4 - (s.first ? 1 : 0);
    }
    // Produce the terminated stream for snapshot s (prefix of `out` + flush bytes).
    std::vector<uint8_t> terminate_at(const RCState& s) const {
        RCEncoder t;
        t.out.assign(out.begin(), out.begin() + s.out_size);
        t.low_ = s.low; t.range_ = s.range; t.cache_ = s.cache; t.cache_size_ = s.cache_size;
        t.first_ = s.first;
        for (int i = 0; i < 5; ++i) t.shift_low();
        return t.out;
    }
    void finish() { for (int i = 0; i < 5; ++i) shift_low(); }

private:
    uint64_t low_;
    uint32_t range_;
    uint8_t cache_;
    uint32_t cache_size_;
    bool first_;
    inline void put(uint8_t b) {
        if (first_) { first_ = false; return; }  // leading byte is always 0: drop it
        out.push_back(b);
    }
    inline void shift_low() {
        if (uint32_t(low_) < 0xFF000000u || (low_ >> 32) != 0) {
            uint8_t carry = uint8_t(low_ >> 32);
            uint8_t temp = cache_;
            do { put(uint8_t(temp + carry)); temp = 0xFF; } while (--cache_size_ != 0);
            cache_ = uint8_t(low_ >> 24);
        }
        ++cache_size_;
        low_ = (low_ & 0x00FFFFFFu) << 8;
    }
};

class RCDecoder {
public:
    // valid = number of bytes known to be correct (<= size). Decoding of a symbol is
    // refused (ok()==false) once the decoder would depend on bytes at or beyond `valid`.
    void init(const uint8_t* buf, size_t size, size_t valid) {
        buf_ = buf; size_ = size; valid_ = valid < size ? valid : size; pos_ = 0;
        range_ = 0xFFFFFFFFu; code_ = 0;
        for (int i = 0; i < 4; ++i) code_ = (code_ << 8) | next();
    }
    inline bool ok() const { return pos_ <= valid_; }
    inline int decode(Prob& pr) {
        uint32_t p = pr.p();
        if (p < 32) p = 32; else if (p > 32768 - 32) p = 32768 - 32;
        uint32_t bound = (range_ >> kPB) * p;
        int bit;
        if (code_ < bound) { range_ = bound; bit = 0; }
        else { code_ -= bound; range_ -= bound; bit = 1; }
        pr.update(bit);
        while (range_ < kTop) { range_ <<= 8; code_ = (code_ << 8) | next(); }
        return bit;
    }
    inline int decode_p(uint32_t p) {
        if (p < 32) p = 32; else if (p > 32768 - 32) p = 32768 - 32;
        uint32_t bound = (range_ >> kPB) * p;
        int bit;
        if (code_ < bound) { range_ = bound; bit = 0; }
        else { code_ -= bound; range_ -= bound; bit = 1; }
        while (range_ < kTop) { range_ <<= 8; code_ = (code_ << 8) | next(); }
        return bit;
    }
    inline int decode_bypass() {
        range_ >>= 1;
        int bit = 0;
        if (code_ >= range_) { code_ -= range_; bit = 1; }
        while (range_ < kTop) { range_ <<= 8; code_ = (code_ << 8) | next(); }
        return bit;
    }
    size_t pos() const { return pos_; }
private:
    const uint8_t* buf_ = nullptr;
    size_t size_ = 0, valid_ = 0, pos_ = 0;
    uint32_t range_ = 0, code_ = 0;
    inline uint8_t next() { uint8_t b = pos_ < valid_ ? buf_[pos_] : 0; ++pos_; return b; }
};

}  // namespace ewc
