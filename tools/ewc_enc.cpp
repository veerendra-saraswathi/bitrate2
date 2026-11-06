// ewc_enc — encode a raw image sequence (uint16 little-endian, row-major, 2 bytes/pixel)
// into a self-contained .ewc bitstream file. Reports per-frame size and encoder latency.
//
// Latency definition (documented in docs/LATENCY.md): time from the moment a complete input
// frame is available in memory to the moment its encoded unit has been fully produced.
#include "ewc.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

static void usage() {
    fprintf(stderr,
            "usage: ewc_enc -i in.raw -o out.ewc -W width -H height [options]\n"
            "  --bd N            bit depth (default 14)\n"
            "  --kbps K --fps F  constant bit rate (default 2000 kbps at 30 fps)\n"
            "  --frame-bytes B   per-frame byte budget (overrides --kbps)\n"
            "  --lossless        bit-exact profile (rate not constrained)\n"
            "  --near N          near-lossless: every sample within +-N (rate not constrained)\n"
            "  --gop N           1 = intra only (default); N>1 = I frame every N frames, P between\n"
            "  --iscale S        I-frame budget multiplier (P frames compensate), default 1.0\n"
            "  --groups G        independent groups per frame (default 8)\n"
            "  --levels L        wavelet levels (default 6)\n"
            "  --mvb B           motion block size 8/16/32/64 (default 32)\n"
            "  --threads T       worker threads (default 1)\n"
            "  --meta TEXT       application metadata stored with every frame\n"
            "  --fps-ts F        timestamp step = 1e6/F microseconds (default = --fps)\n"
            "  --stats file.csv  per-frame statistics\n"
            "  --frames N        encode at most N frames\n"
            "  --redundancy R    I frames: repeat first R bytes of each group stream (default 24, 0=off)\n"
            "  --tile T          tiled profile: independent TxT tiles (16..256), lossy, intra-only\n"
            "  --tile-levels L   wavelet levels inside a tile (default 4)\n"
            "  --poc             RCI proof-of-concept profile: 640x512, 14-bit, 2 Mbps CBR, intra-only,\n"
            "                    128x128 tiles, 2 threads (options after --poc override it, e.g. --tile 0)\n");
}

int main(int argc, char** argv) {
    ewc::EncoderConfig cfg;
    cfg.mv_block = 32;
    cfg.mv_lambda = 16;
    std::string in, out, stats, meta;
    double kbps = 2000, fps = 30;
    long frame_bytes = -1;
    long max_frames = -1;
    bool tile_set = false;   // --tile given explicitly (vs. inherited from --poc)
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto nx = [&]() -> const char* { if (i + 1 >= argc) { usage(); exit(2); } return argv[++i]; };
        if (a == "-i") in = nx();
        else if (a == "-o") out = nx();
        else if (a == "-W") cfg.width = atoi(nx());
        else if (a == "-H") cfg.height = atoi(nx());
        else if (a == "--bd") cfg.bit_depth = atoi(nx());
        else if (a == "--kbps") kbps = atof(nx());
        else if (a == "--fps") fps = atof(nx());
        else if (a == "--frame-bytes") frame_bytes = atol(nx());
        else if (a == "--lossless") cfg.lossless = true;
        else if (a == "--gop") cfg.gop = atoi(nx());
        else if (a == "--iscale") cfg.i_frame_scale_q8 = int(atof(nx()) * 256 + 0.5);
        else if (a == "--groups") cfg.groups = atoi(nx());
        else if (a == "--levels") cfg.levels = atoi(nx());
        else if (a == "--pshift") cfg.precision_shift = atoi(nx());
        else if (a == "--golomb") cfg.loco_ac = false;
        else if (a == "--fixed-stripes") cfg.loco_auto_stripes = false;
        else if (a == "--mvb") cfg.mv_block = atoi(nx());
        else if (a == "--threads") cfg.threads = atoi(nx());
        else if (a == "--meta") meta = nx();
        else if (a == "--stats") stats = nx();
        else if (a == "--frames") max_frames = atol(nx());
        else if (a == "--no-block-crc") cfg.block_crc = false;
        else if (a == "--redundancy") cfg.redundancy = atoi(nx());
        else if (a == "--tile") { cfg.tile = atoi(nx()); tile_set = true; }
        else if (a == "--tile-levels") cfg.tile_levels = atoi(nx());
        else if (a == "--tile-red") cfg.tile_redundancy = atoi(nx());
        else if (a == "--tile-fixed-index") cfg.tile_compact_index = false;
        else if (a == "--near") { cfg.lossless = true; cfg.near = atoi(nx()); }
        else if (a == "--poc") { cfg = ewc::poc_config(); kbps = 2000; frame_bytes = -1; }   // budget set from --kbps/--fps below
        else { usage(); return 2; }
    }
    if (in.empty() || out.empty()) { usage(); return 2; }
    if (cfg.lossless && cfg.tile > 0) {   // tiled coder is lossy-only; --poc --lossless falls back to whole-frame lossless
        if (tile_set) { fprintf(stderr, "error: --tile is lossy only; drop --tile or --lossless/--near\n"); return 2; }
        cfg.tile = 0;
        fprintf(stderr, "note: lossless/near-lossless selected, using whole-frame coding (tiles are lossy only)\n");
    }
    cfg.frame_bytes = frame_bytes > 0 ? uint32_t(frame_bytes) : uint32_t(kbps * 1000.0 / 8.0 / fps);
    std::ifstream fi(in, std::ios::binary);
    if (!fi) { fprintf(stderr, "cannot open %s\n", in.c_str()); return 1; }
    std::ofstream fo(out, std::ios::binary);
    if (!fo) { fprintf(stderr, "cannot create %s\n", out.c_str()); return 1; }
    FILE* fs = stats.empty() ? nullptr : fopen(stats.c_str(), "w");
    if (fs) fprintf(fs, "frame,type,bytes,encode_ms\n");
    try {
        ewc::Encoder enc(cfg);
        const size_t npix = size_t(cfg.width) * cfg.height;
        std::vector<uint16_t> frame(npix);
        std::vector<uint8_t> bs, md(meta.begin(), meta.end());
        std::vector<double> lat;
        size_t total = 0, maxb = 0;
        const uint16_t maxv = uint16_t((1u << cfg.bit_depth) - 1);
        long k = 0, clipped_frames = 0;
        size_t clipped_total = 0;
        while ((max_frames < 0 || k < max_frames) && fi.read(reinterpret_cast<char*>(frame.data()), std::streamsize(npix * 2))) {
            size_t clipped = 0;
            for (auto& v : frame) if (v > maxv) { v = maxv; ++clipped; }
            if (clipped && cfg.lossless) {   // clipping would silently break bit-exactness
                fprintf(stderr, "error: frame %ld has %zu samples above the %d-bit range; raise --bd (lossless profile)\n",
                        k, clipped, cfg.bit_depth);
                return 1;
            }
            if (clipped) { clipped_total += clipped; if (!clipped_frames++) fprintf(stderr, "warning: input exceeds %d-bit range, clipping\n", cfg.bit_depth); }
            bs.clear();
            auto t0 = std::chrono::steady_clock::now();       // frame available
            enc.encode(frame.data(), uint64_t(double(k) * 1e6 / fps), bs, md);
            auto t1 = std::chrono::steady_clock::now();       // encoded unit complete
            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            fo.write(reinterpret_cast<const char*>(bs.data()), std::streamsize(bs.size()));
            lat.push_back(ms);
            total += bs.size(); maxb = std::max(maxb, bs.size());
            const bool intra = bs.size() > 5 && bs[5] == 0;   // frame type from the unit header (scene cuts / LOCO)
            if (fs) fprintf(fs, "%ld,%c,%zu,%.3f\n", k, intra ? 'I' : 'P', bs.size(), ms);
            ++k;
        }
        if (fs) fclose(fs);
        if (!k) { fprintf(stderr, "no complete frame in input\n"); return 1; }
        if (clipped_total) fprintf(stderr, "warning: %zu samples clipped in %ld of %ld frames\n", clipped_total, clipped_frames, k);
        std::vector<double> s = lat;
        std::sort(s.begin(), s.end());
        const double period = 1000.0 / fps;
        printf("frames %ld  bytes %zu  avg %.1f B/frame  max %zu B  -> %.1f kbps @ %.2f fps\n", k, total, double(total) / k, maxb,
               double(total) * 8.0 * fps / k / 1000.0, fps);
        printf("encoder latency ms: median %.2f  p99 %.2f  max %.2f  (= %.3f frame periods at max)\n", s[s.size() / 2],
               s[std::min(s.size() - 1, size_t(0.99 * (s.size() - 1) + 0.5))], s.back(), s.back() / period);
    } catch (const std::exception& e) {
        fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
