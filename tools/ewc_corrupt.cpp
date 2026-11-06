// ewc_corrupt — inject losses into a stored bitstream (RCI SoW: "robustness to channel errors",
// evaluated "by introducing losses in the stored bitstream"). Works on any file; deterministic
// for a given seed. Same error models as eval/scripts/stored_loss.py.
//   ber     flip each bit with probability p
//   pktdel  cut the file into packets (default 1400 bytes), delete each with probability p
//   pktzero same packets, overwrite each with zeros with probability p (length kept)
//   burst   overwrite 512-byte bursts with zeros; p = expected bursts per 100 kB
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 5) {
        fprintf(stderr, "usage: ewc_corrupt in.ewc out.ewc ber|pktdel|pktzero|burst p [seed] [packet_bytes]\n");
        return 2;
    }
    const std::string mode = argv[3];
    const double p = atof(argv[4]);
    const unsigned seed = argc > 5 ? unsigned(atoi(argv[5])) : 1u;
    const size_t pkt = argc > 6 ? size_t(atol(argv[6])) : 1400;
    std::ifstream fi(argv[1], std::ios::binary);
    if (!fi) { fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
    std::vector<uint8_t> a((std::istreambuf_iterator<char>(fi)), std::istreambuf_iterator<char>());
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> U(0, 1);
    std::vector<uint8_t> out;
    size_t changed = 0, events = 0;
    if (mode == "ber") {
        out = a;
        if (p > 0) {
            std::geometric_distribution<uint64_t> gap(p);   // distance to the next flipped bit
            for (uint64_t bit = gap(rng); bit < uint64_t(out.size()) * 8; bit += gap(rng) + 1) {
                out[bit / 8] ^= uint8_t(1u << (bit % 8)); ++events;
            }
        }
        for (size_t i = 0; i < a.size(); ++i) changed += a[i] != out[i];
    } else if (mode == "pktdel" || mode == "pktzero") {
        for (size_t o = 0; o < a.size(); o += pkt) {
            const size_t n = std::min(pkt, a.size() - o);
            const bool hit = U(rng) < p;
            events += hit;
            if (hit && mode == "pktdel") { changed += n; continue; }
            if (hit) { out.insert(out.end(), n, 0); changed += n; }
            else out.insert(out.end(), a.begin() + long(o), a.begin() + long(o + n));
        }
    } else if (mode == "burst") {
        out = a;
        std::poisson_distribution<int> np(p * double(a.size()) / 1e5);
        const int nb = np(rng);
        for (int b = 0; b < nb && a.size() > 0; ++b) {
            const size_t s = size_t(U(rng) * double(a.size() > 512 ? a.size() - 512 : 1));
            for (size_t i = s; i < std::min(a.size(), s + 512); ++i) out[i] = 0;
            ++events;
        }
        for (size_t i = 0; i < a.size(); ++i) changed += a[i] != out[i];
    } else {
        fprintf(stderr, "unknown mode %s\n", mode.c_str());
        return 2;
    }
    std::ofstream fo(argv[2], std::ios::binary);
    fo.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
    printf("%s p=%g seed=%u: %zu -> %zu bytes, %zu events, %zu bytes changed or removed\n", mode.c_str(), p, seed, a.size(), out.size(),
           events, changed);
    return 0;
}
