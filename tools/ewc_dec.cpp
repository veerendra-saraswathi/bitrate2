// ewc_dec — decode a .ewc bitstream into a raw image sequence (uint16 little-endian,
// row-major, 2 bytes per pixel). Validates every unit (CRC-32 frame headers, CRC-16 group
// headers and payload blocks), conceals damaged regions, and keeps the output frame count
// aligned with the encoder's sequence numbers (missing frames are replaced by the last frame).
// Optional: --ref original.raw computes reconstruction error per frame.
#include "ewc.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    std::string in, out, ref, report;
    int threads = 1;
    long expect = -1;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto nx = [&]() -> const char* { if (i + 1 >= argc) exit(2); return argv[++i]; };
        if (a == "-i") in = nx();
        else if (a == "-o") out = nx();
        else if (a == "--ref") ref = nx();
        else if (a == "--report") report = nx();
        else if (a == "--threads") threads = atoi(nx());
        else if (a == "--frames") expect = atol(nx());
        else { fprintf(stderr, "usage: ewc_dec -i in.ewc -o out.raw [--ref orig.raw] [--report r.csv] [--frames N]\n"); return 2; }
    }
    if (in.empty() || out.empty()) { fprintf(stderr, "usage: ewc_dec -i in.ewc -o out.raw [--ref orig.raw]\n"); return 2; }
    std::ifstream fi(in, std::ios::binary);
    if (!fi) { fprintf(stderr, "cannot open %s\n", in.c_str()); return 1; }
    std::vector<uint8_t> bs((std::istreambuf_iterator<char>(fi)), std::istreambuf_iterator<char>());
    std::ofstream fo(out, std::ios::binary);
    std::ifstream fr;
    if (!ref.empty()) fr.open(ref, std::ios::binary);
    FILE* rp = report.empty() ? nullptr : fopen(report.c_str(), "w");
    if (rp) fprintf(rp, "frame,seq,type,bytes,header_ok,skipped_bytes,groups_ok,groups_partial,groups_lost,concealed,motion_ok,"
                        "ref_gap,ref_ok,reliable,decode_ms,psnr,maxerr\n");

    ewc::Decoder dec;
    dec.set_threads(threads);
    size_t pos = 0;
    std::vector<uint16_t> frame, last, orig;
    ewc::FrameInfo info;
    int W = 0, H = 0, bd = 14;
    long written = 0, decoded = 0, repeated = 0, damaged = 0, ref_invalid = 0, reliable = 0;
    double psum = 0, pmin = 1e9;
    int maxerr = 0;
    std::vector<double> dt;
    auto emit = [&](const std::vector<uint16_t>& f, const char* type, const ewc::FrameInfo* fi_, double ms) {
        fo.write(reinterpret_cast<const char*>(f.data()), std::streamsize(f.size() * 2));
        double ps = -1; int me = -1;
        if (fr.is_open()) {
            orig.resize(f.size());
            if (fr.read(reinterpret_cast<char*>(orig.data()), std::streamsize(orig.size() * 2))) {
                double mse = 0; me = 0;
                for (size_t i = 0; i < f.size(); ++i) { int d = int(f[i]) - int(orig[i]); mse += double(d) * d; me = std::max(me, std::abs(d)); }
                mse /= double(f.size());
                const double peak = double((1 << bd) - 1);
                ps = mse > 0 ? 10 * std::log10(peak * peak / mse) : 199.0;
                psum += ps; pmin = std::min(pmin, ps); maxerr = std::max(maxerr, me);
            }
        }
        if (rp) {
            if (fi_) fprintf(rp, "%ld,%u,%s,%u,%d,%u,%d,%d,%d,%d,%d,%d,%d,%d,%.3f,%.3f,%d\n", written, fi_->sequence, type, fi_->bytes,
                             fi_->header_ok, fi_->skipped_bytes, fi_->groups_ok, fi_->groups_partial, fi_->groups_lost, fi_->concealed,
                             fi_->motion_ok, fi_->ref_gap, fi_->ref_ok, int(fi_->reliable()), ms, ps, me);
            else fprintf(rp, "%ld,-1,%s,0,0,0,0,0,0,1,0,0,0,0,0,%.3f,%d\n", written, type, ps, me);
        }
        ++written;
    };
    long next_seq = 0;
    for (;;) {
        auto t0 = std::chrono::steady_clock::now();
        bool ok = dec.decode_next(bs.data(), bs.size(), pos, frame, info, W, H);
        auto t1 = std::chrono::steady_clock::now();
        if (!ok) break;
        const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        dt.push_back(ms);
        ++decoded;
        bd = info.bit_depth;
        if (long(info.sequence) < next_seq) continue;            // duplicate / stale unit
        while (next_seq < long(info.sequence)) {                  // lost frames -> repeat last
            if (last.empty()) last.assign(frame.size(), uint16_t(1u << (bd - 1)));   // lost before first frame
            emit(last, "LOST", nullptr, 0); ++repeated; ++next_seq;
        }
        // classification: reliable / damaged (own data lost or corrupt) / ref_invalid (own data intact,
        // but predicted from a missing or unreliable reference). A resync before an intact frame
        // (header_ok == false) does not make that frame damaged.
        if (info.reliable()) ++reliable;
        else if (info.groups_lost || info.groups_partial || info.concealed || !info.motion_ok) ++damaged;
        else ++ref_invalid;
        emit(frame, info.is_intra ? "I" : "P", &info, ms);
        last = frame;
        next_seq = long(info.sequence) + 1;
    }
    while (expect > 0 && written < expect && !last.empty()) { emit(last, "LOST", nullptr, 0); ++repeated; }
    if (expect > 0 && written < expect)
        fprintf(stderr, "warning: no frame could be decoded; %ld of %ld frames missing from output\n", expect - written, expect);
    if (rp) fclose(rp);
    std::sort(dt.begin(), dt.end());
    printf("decoded %ld units, wrote %ld frames (%dx%d), repeated(lost) %ld, damaged %ld, ref_invalid %ld, reliable %ld\n", decoded,
           written, W, H, repeated, damaged, ref_invalid, reliable);
    if (!dt.empty())
        printf("decoder latency ms: median %.2f  p99 %.2f  max %.2f\n", dt[dt.size() / 2],
               dt[std::min(dt.size() - 1, size_t(0.99 * (dt.size() - 1) + 0.5))], dt.back());
    if (fr.is_open() && written) printf("vs reference: psnr avg %.3f dB  min %.3f dB  max abs error %d\n", psum / written, pmin, maxerr);
    printf("SUMMARY written %ld decoded %ld reliable %ld damaged %ld ref_invalid %ld lost %ld dec_med_ms %.3f dec_max_ms %.3f\n", written,
           decoded, reliable, damaged, ref_invalid, repeated, dt.empty() ? 0.0 : dt[dt.size() / 2], dt.empty() ? 0.0 : dt.back());
    return 0;
}
