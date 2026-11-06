// ewc — UDP packetisation of frame units with optional forward error correction
// (transport-independent part, pure C++17; the caller owns the sockets).
//
// Datagram layout (protocol version 2, little endian, 24-byte header):
//    0 'E' 'U'
//    2 version = 2
//    3 flags: bit0 last data datagram of the unit, bit1 parity datagram
//    4 datagram sequence u32 (per sender, informational)
//    8 frame (unit) sequence u32
//   12 data: byte offset of the payload in the unit; parity: parity index * pay
//   16 unit size u32
//   20 pay u16: payload size of every data datagram of this unit (the last one may be shorter)
//   22 k u8: data datagrams per parity datagram (FEC overhead ~ 1/k); 0 when fec = none
//   23 fec u8: 0 none | 1 xor | 2 xor-interleaved | 3 rs (Reed-Solomon erasure code)
//   24 payload
//
// Payload sizing: a unit of S bytes is cut into nd = ceil(S / (mtu - 24)) datagrams of equal
// size pay = ceil(S / nd), so parity datagrams (always `pay` bytes) are never larger than
// the data they protect and small units do not pay for a full-MTU parity datagram.
//
// FEC modes (np = number of parity datagrams of a unit; all modes send np = ceil(nd / k)
// except rs on very large units, see below, so overhead is equal for equal k):
//   xor              parity j = XOR of data [j*k, j*k + k): repairs 1 loss per k consecutive
//                    datagrams. A burst of 2 inside a group is unrecoverable.
//   xor-interleaved  parity j = XOR of data {i : i % np == j}: any burst of up to np
//                    consecutive data datagrams is repairable (1 loss per parity group).
//   rs               systematic Cauchy Reed-Solomon over GF(256): ANY np lost datagrams of the
//                    unit (data or parity) are repairable. Units with nd + np > 255 are split
//                    into interleaved RS blocks (data i -> block i % nb), each with its own
//                    ceil(nd_b / k) parity datagrams.
// FEC operates within one unit (frame): it adds no latency beyond the frame itself, but it
// cannot repair a burst that removes more datagrams than the unit has parity.
//
// Reception: payloads are placed at their offsets. Bytes that are neither received nor
// repaired are left as zeros and reported (Unit::missing_ranges), so the unit keeps its
// layout. The codec must be run with block CRCs (EncoderConfig::block_crc, the default): the
// zero-filled ranges then fail their CRC and are never decoded as valid data.
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace ewc {

constexpr size_t kDgramHdr = 24;
constexpr uint8_t kNetVersion = 2;

enum class Fec : uint8_t { None = 0, Xor = 1, XorInterleaved = 2, RS = 3 };

// Geometry of the parity datagrams of one unit (shared by sender and receiver).
struct FecLayout {
    Fec fec = Fec::None;
    int k = 0;
    size_t nd = 0;                        // data datagrams
    size_t nb = 1;                        // RS blocks (1 for the XOR modes)
    std::vector<size_t> pstart;           // per block: index of its first parity datagram
    size_t np = 0;                        // parity datagrams in total
    void init(Fec f, int k, size_t nd);
    size_t block_of_data(size_t i) const { return fec == Fec::RS ? i % nb : 0; }
};

class Packetizer {
public:
    // mtu: maximum datagram size (header + payload). fec_k: data datagrams per parity
    // datagram (0 = no FEC). fec: parity scheme (ignored when fec_k == 0).
    explicit Packetizer(size_t mtu = 1400, int fec_k = 0, Fec fec = Fec::Xor);
    // Appends the datagrams of one frame unit to `out` (data first, then parity).
    void packetize(const uint8_t* unit, size_t size, uint32_t frame_seq, std::vector<std::vector<uint8_t>>& out);
    // Datagram bytes (headers + parity) sent for a unit of `size` bytes.
    size_t wire_bytes(size_t size) const;
    size_t datagrams(size_t size) const;   // data + parity datagrams for a unit
    Fec fec() const { return fec_; }
    int k() const { return k_; }
private:
    size_t mtu_, maxpay_;
    int k_;
    Fec fec_;
    uint32_t dseq_ = 0;
};

class Reassembler {
public:
    struct Unit {
        uint32_t frame_seq = 0;
        std::vector<uint8_t> data;   // zero-filled where datagrams were lost and not repaired
        size_t pay = 0;
        int k = 0;
        Fec fec = Fec::None;
        FecLayout lay;
        std::vector<uint8_t> got;    // per data datagram
        std::map<uint32_t, std::vector<uint8_t>> parity;   // parity index -> payload
        int recovered = 0;           // data datagrams repaired by FEC
        bool complete() const;
        size_t missing() const;      // data datagrams neither received nor repaired
        // Unrecoverable byte ranges (offset, length), merged.
        std::vector<std::pair<size_t, size_t>> missing_ranges() const;
    };
    // Feed one datagram (invalid ones are ignored and counted). Units are released in frame
    // order once complete (after FEC repair), or when more than `max_pending` units are open
    // (the oldest is then released with its gaps).
    void push(const uint8_t* d, size_t n);
    // Moves released units to `out`. flush = release everything still pending.
    void pop(std::vector<Unit>& out, bool flush = false);
    size_t max_pending = 2;
    uint64_t datagrams_ok = 0, datagrams_bad = 0, datagrams_late = 0, datagrams_dup = 0, datagrams_recovered = 0;
private:
    void try_recover(Unit& u);
    void release(std::map<uint32_t, Unit>::iterator it);
    std::map<uint32_t, Unit> open_;
    std::vector<Unit> ready_;
    bool have_last_ = false;
    uint32_t last_released_ = 0;
};

const char* fec_name(Fec f);
bool fec_from_name(const char* s, Fec& f);

}  // namespace ewc
