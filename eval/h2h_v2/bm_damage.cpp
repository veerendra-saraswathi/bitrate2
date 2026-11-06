// bitrate-main decoder on damaged streams at the 2 Mbps operating point: compare to the CLEAN decode.
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <random>
#include <stdexcept>
#include <vector>
std::vector<uint8_t> encode_frame(const std::vector<uint8_t>&, int, int, int, int, int);
std::vector<uint8_t> decode_frame(const std::vector<uint8_t>&);
int main(int argc, char** argv) {
    std::vector<uint8_t> px(640 * 512);
    std::ifstream(argv[1], std::ios::binary).read(reinterpret_cast<char*>(px.data()), px.size());
    auto bs = encode_frame(px, 640, 512, atoi(argv[2]), atoi(argv[3]), 1);
    auto clean = decode_frame(bs);
    std::mt19937 rng(1);
    for (int flips : {1, 4, 16}) {
        int thrown = 0, wrong = 0, same = 0, crash = 0; const int trials = 200;
        for (int t = 0; t < trials; ++t) {
            auto c = bs;
            for (int k = 0; k < flips; ++k) c[rng() % c.size()] ^= uint8_t(1u << (rng() % 8));
            try { auto r = decode_frame(c); (r == clean) ? ++same : ++wrong; }
            catch (const std::exception&) { ++thrown; } catch (...) { ++crash; }
        }
        printf("%d flips: rejected %d, decoded silently wrong %d, unaffected %d (of %d)\n", flips, thrown, wrong, same, trials);
    }
}
