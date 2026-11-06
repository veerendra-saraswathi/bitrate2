// ewc_bench — end-to-end benchmark: encode a raw uint16 sequence frame by frame, send every
// frame unit through the UDP packetiser and a simulated lossy channel, reassemble, decode,
// align decoded frames to the source by sequence number, and report rate / quality / timing /
// reliability. Frames that never decode are replaced by the last displayed frame (freeze).
//
// Rates are reported separately: codec payload, FEC parity, datagram headers, total wire.
// With --link-kbps the codec budget is the largest whose datagrams (headers + parity) fit the
// link per frame; with the default strict VBV no frame exceeds it.
//
// Channel: Gilbert-Elliott two-state loss process with stationary loss rate --udp-loss and
// mean burst length --burst (datagrams). Bursts continue across frame boundaries.
#include "ewc.h"
#include "ewc_net.h"
namespace ewc { extern int g_recon_q5; }
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using clk = std::chrono::steady_clock;

int main(int argc, char** argv) {
    ewc::EncoderConfig cfg;
    std::string in, obs, orec, csv;
    int n = 1 << 30, skip = 0, dec_threads = -1;
    bool quiet = false;
    double udp_loss = 0, fps = 30, kbps = -1, burst = 1, link_kbps = 0;
    int mtu = 1400, fec_k = 0, feedback = -1, max_pending = 2;
    ewc::Fec fec = ewc::Fec::Xor;
    bool freeze_on_error = false;
    unsigned seed = 1;
    bool tile_set = false;   // --tile given explicitly (vs. inherited from --poc)
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto nx = [&]() -> const char* { if (i + 1 >= argc) { fprintf(stderr, "missing value for %s\n", a.c_str()); exit(2); } return argv[++i]; };
        if (a == "--in") in = nx();
        else if (a == "--w") cfg.width = atoi(nx());
        else if (a == "--h") cfg.height = atoi(nx());
        else if (a == "--n") n = atoi(nx());
        else if (a == "--skip") skip = atoi(nx());
        else if (a == "--bd") cfg.bit_depth = atoi(nx());
        else if (a == "--fps") fps = atof(nx());
        else if (a == "--budget") cfg.frame_bytes = uint32_t(atoi(nx()));
        else if (a == "--kbps") kbps = atof(nx());            // applied after parsing (order-independent)
        else if (a == "--levels") cfg.levels = atoi(nx());
        else if (a == "--groups") cfg.groups = atoi(nx());
        else if (a == "--wavelet") cfg.wavelet = atoi(nx());
        else if (a == "--frac") cfg.frac_bits = atoi(nx());
        else if (a == "--pshift") cfg.precision_shift = atoi(nx());
        else if (a == "--golomb") cfg.loco_ac = false;
        else if (a == "--fixed-stripes") cfg.loco_auto_stripes = false;
        else if (a == "--cp") cfg.cp_interval = atoi(nx());
        else if (a == "--gop") cfg.gop = atoi(nx());
        else if (a == "--iscale") cfg.i_frame_scale_q8 = int(std::lround(atof(nx()) * 256));
        else if (a == "--vbv") cfg.vbv_frames_q8 = int(std::lround(atof(nx()) * 256));
        else if (a == "--mvb") cfg.mv_block = atoi(nx());
        else if (a == "--mvr") cfg.mv_range = atoi(nx());
        else if (a == "--gr") cfg.global_range = atoi(nx());
        else if (a == "--lam") cfg.mv_lambda = atoi(nx());
        else if (a == "--scut") cfg.scene_cut_q8 = int(std::lround(atof(nx()) * 256));
        else if (a == "--threads") cfg.threads = atoi(nx());
        else if (a == "--dec-threads") dec_threads = atoi(nx());
        else if (a == "--lossless") cfg.lossless = true;
        else if (a == "--near") { cfg.lossless = true; cfg.near = atoi(nx()); }
        else if (a == "--embedded") cfg.embedded_lossless = true;
        else if (a == "--stripe") cfg.stripe_rows = atoi(nx());
        else if (a == "--noblockcrc") cfg.block_crc = false;
        else if (a == "--red") cfg.redundancy = atoi(nx());
        else if (a == "--tile") { cfg.tile = atoi(nx()); tile_set = true; }
        else if (a == "--tile-levels") cfg.tile_levels = atoi(nx());
        else if (a == "--tile-red") cfg.tile_redundancy = atoi(nx());
        else if (a == "--tile-fixed-index") cfg.tile_compact_index = false;
        else if (a == "--udp-loss") udp_loss = atof(nx());
        else if (a == "--burst") burst = atof(nx());
        else if (a == "--mtu") mtu = atoi(nx());
        else if (a == "--fec") fec_k = atoi(nx());
        else if (a == "--fec-mode") { if (!ewc::fec_from_name(nx(), fec)) { fprintf(stderr, "--fec-mode none|xor|xori|rs\n"); return 2; } }
        else if (a == "--link-kbps") link_kbps = atof(nx());   // budget incl. datagram headers + FEC
        else if (a == "--max-pending") max_pending = atoi(nx());
        else if (a == "--feedback") feedback = atoi(nx());    // receiver -> encoder intra request after D frames (-1 off)
        else if (a == "--on-error") { std::string m = nx(); freeze_on_error = m == "freeze"; if (m != "freeze" && m != "show") { fprintf(stderr, "--on-error show|freeze\n"); return 2; } }
        else if (a == "--seed") seed = unsigned(atoi(nx()));
        else if (a == "--poc") { cfg = ewc::poc_config(); if (kbps < 0) kbps = 2000; }   // RCI PoC profile; later options override
        else if (a == "--out-bs") obs = nx();
        else if (a == "--out-rec") orec = nx();
        else if (a == "--csv") csv = nx();
        else if (a == "-q") quiet = true;
        else if (a == "--rq5") ewc::g_recon_q5 = atoi(nx());
        else { fprintf(stderr, "unknown arg %s\n", a.c_str()); return 2; }
    }
    if (fps <= 0) { fprintf(stderr, "--fps must be > 0\n"); return 2; }
    if (cfg.lossless && cfg.tile > 0) {   // tiled coder is lossy-only; --poc --lossless falls back to whole-frame lossless
        if (tile_set) { fprintf(stderr, "error: --tile is lossy only; drop --tile or --lossless/--near\n"); return 2; }
        cfg.tile = 0;
        if (!quiet) fprintf(stderr, "note: lossless/near-lossless selected, using whole-frame coding (tiles are lossy only)\n");
    }
    if (kbps >= 0) cfg.frame_bytes = uint32_t(kbps * 1000 / 8 / fps);
    if (udp_loss > 0 && !cfg.block_crc) {
        fprintf(stderr, "error: --udp-loss requires block CRCs (lost bytes are zero-filled and would be decoded as data)\n");
        return 2;
    }
    if (udp_loss < 0 || udp_loss >= 1 || burst < 1) { fprintf(stderr, "need 0 <= --udp-loss < 1 and --burst >= 1\n"); return 2; }
    std::ifstream f(in, std::ios::binary);
    if (!f) { fprintf(stderr, "cannot open %s\n", in.c_str()); return 1; }
    const size_t fs = size_t(cfg.width) * cfg.height;
    f.seekg(std::streamoff(skip) * std::streamoff(fs * 2));
    std::vector<std::vector<uint16_t>> frames;
    for (int k = 0; k < n; ++k) {
        std::vector<uint16_t> fr(fs);
        if (!f.read(reinterpret_cast<char*>(fr.data()), std::streamsize(fs * 2))) break;
        frames.push_back(std::move(fr));
    }
    if (frames.empty()) { fprintf(stderr, "no frames\n"); return 1; }
    ewc::Packetizer pk(static_cast<size_t>(mtu), fec_k, fec);
    if (link_kbps > 0) {   // largest codec budget whose datagrams (headers, parity) fit the link budget
        const size_t L = size_t(link_kbps * 1000 / 8 / fps);
        size_t lo = 64, hi = L;
        while (lo < hi) { const size_t mid = (lo + hi + 1) / 2; if (pk.wire_bytes(mid) <= L) lo = mid; else hi = mid - 1; }
        cfg.frame_bytes = uint32_t(lo);
    }
    const int nfr = int(frames.size());
    const double peak = double((1 << cfg.bit_depth) - 1);
    const uint16_t gray = uint16_t(1u << (cfg.bit_depth - 1));

    // channel: Gilbert-Elliott, P(bad) = udp_loss, mean bad run = burst
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> U(0, 1);
    const double p_bg = 1.0 / burst, p_gb = udp_loss > 0 ? udp_loss * p_bg / (1 - udp_loss) : 0;
    bool bad_state = false;
    auto drop_next = [&]() {
        if (udp_loss <= 0) return false;
        bad_state = bad_state ? (U(rng) >= p_bg) : (U(rng) < p_gb);
        return bad_state;
    };

    ewc::Encoder enc(cfg);
    ewc::Decoder dec;
    dec.set_threads(dec_threads > 0 ? dec_threads : cfg.threads);
    ewc::Reassembler ra;
    ra.max_pending = size_t(std::max(0, max_pending));
    std::ofstream bsf;
    if (!obs.empty()) bsf.open(obs, std::ios::binary);

    // per-frame records (index = source frame = sequence number)
    struct Rec { char type = '?'; size_t bytes = 0, wire = 0; double enc_ms = 0, dec_ms = -1; int status = 0; bool exact = false;
                 double psnr = 0, mse = 0, mae = 0; int maxerr = 0; int release_delay = -1; };
    enum { ST_LOST = 0, ST_OK = 1, ST_DAMAGED = 2, ST_REFBAD = 3 };
    static const char* stname[] = {"lost", "ok", "damaged", "ref_invalid"};
    std::vector<Rec> rec(static_cast<size_t>(nfr));
    std::vector<std::vector<uint16_t>> decoded(static_cast<size_t>(nfr));
    std::vector<uint8_t> have(static_cast<size_t>(nfr), 0);
    long dgrams = 0, dropped = 0;
    size_t codec_bytes = 0, wire_bytes = 0, hdr_bytes = 0, par_bytes = 0;
    int requests = 0;
    int request_at = -1;            // encode index at which to call request_intra()
    uint32_t refresh_from = 0;      // problems on frames older than this are already handled
    std::vector<ewc::Reassembler::Unit> units;
    std::vector<uint16_t> out;
    ewc::FrameInfo info;
    int w = 0, h = 0;
    bool roundtrip_ok = true;
    std::vector<std::vector<uint8_t>> sent;

    auto decode_units = [&](int now) {
        ra.pop(units, now < 0);
        for (auto& u : units) {
            const uint32_t sq = u.frame_seq;
            if (sq >= uint32_t(nfr)) continue;
            if (udp_loss <= 0 && (sq >= sent.size() || u.data != sent[sq])) roundtrip_ok = false;
            rec[sq].release_delay = (now < 0 ? nfr : now) - int(sq);
            size_t pos = 0;
            const auto t0 = clk::now();
            const bool ok = dec.decode_next(u.data.data(), u.data.size(), pos, out, info, w, h);
            const auto t1 = clk::now();
            if (!ok || info.sequence != sq || w != cfg.width || h != cfg.height) continue;   // header lost: frame lost
            rec[sq].dec_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            rec[sq].type = info.is_intra ? 'I' : 'P';
            const bool dmg = info.groups_lost || info.groups_partial || info.concealed || !info.motion_ok;
            rec[sq].status = info.reliable() ? ST_OK : dmg ? ST_DAMAGED : ST_REFBAD;
            decoded[sq] = out; have[sq] = 1;
        }
        units.clear();
    };
    auto feedback_check = [&](int now) {   // receiver: first problem not yet covered by a refresh
        if (feedback < 0 || request_at >= 0) return;
        for (int s = int(refresh_from); s < now && s < nfr; ++s) {
            const bool problem = (rec[s].release_delay >= 0 && (!have[s] || rec[s].status != ST_OK));
            if (problem) { request_at = now + feedback; return; }
        }
    };

    for (int k = 0; k < nfr; ++k) {
        if (request_at >= 0 && k >= request_at) { enc.request_intra(); ++requests; refresh_from = uint32_t(k); request_at = -1; }
        std::vector<uint8_t> unit;
        const auto t0 = clk::now();
        enc.encode(frames[k].data(), uint64_t(std::llround(double(k) * 1e6 / fps)), unit);
        const auto t1 = clk::now();
        rec[k].enc_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        rec[k].bytes = unit.size();
        rec[k].type = unit.size() > 5 && unit[5] == 0 ? 'I' : 'P';
        codec_bytes += unit.size();
        if (bsf) bsf.write(reinterpret_cast<const char*>(unit.data()), std::streamsize(unit.size()));
        std::vector<std::vector<uint8_t>> dg;
        pk.packetize(unit.data(), unit.size(), uint32_t(k), dg);
        for (auto& d : dg) {
            const bool par = d[3] & 2;
            rec[k].wire += d.size(); wire_bytes += d.size(); hdr_bytes += ewc::kDgramHdr;
            if (par) par_bytes += d.size() - ewc::kDgramHdr;
            ++dgrams;
            if (drop_next()) { ++dropped; continue; }
            ra.push(d.data(), d.size());
        }
        if (udp_loss <= 0) sent.push_back(unit);   // zero loss: the transport must return units unchanged
        decode_units(k);
        feedback_check(k + 1);
    }
    decode_units(-1);
    if (!roundtrip_ok) { fprintf(stderr, "error: transport round trip changed a unit with zero loss\n"); return 1; }

    // ---- display: decoded frame, or freeze (lost; or unreliable with --on-error freeze)
    std::ofstream rf;
    if (!orec.empty()) rf.open(orec, std::ios::binary);
    FILE* cf = csv.empty() ? nullptr : fopen(csv.c_str(), "w");
    if (cf) fprintf(cf, "frame,type,bytes,wire_bytes,psnr,rmse,mae,maxerr,enc_ms,dec_ms,status,release_delay\n");
    std::vector<uint16_t> shown(fs, gray);
    bool shown_any = false;
    double psum = 0, pmin = 1e9, sse_all = 0, sae_all = 0, psum_ok = 0;
    int maxerr = 0, cnt[4] = {0, 0, 0, 0}, displayed_stale = 0;
    bool exact = true;
    for (int k = 0; k < nfr; ++k) {
        Rec& r = rec[k];
        if (!have[k]) r.status = ST_LOST;
        if (have[k] && !(freeze_on_error && r.status != ST_OK)) { shown = decoded[k]; shown_any = true; }
        else displayed_stale++;
        (void)shown_any;
        double sse = 0, sae = 0; int me = 0;
        for (size_t i = 0; i < fs; ++i) {
            const int d = int(shown[i]) - int(frames[k][i]);
            sse += double(d) * d; sae += std::abs(d); me = std::max(me, std::abs(d));
        }
        r.mse = sse / double(fs); r.mae = sae / double(fs); r.maxerr = me; r.exact = me == 0;
        r.psnr = r.mse > 0 ? 10 * std::log10(peak * peak / r.mse) : 199.0;
        psum += r.psnr; pmin = std::min(pmin, r.psnr); sse_all += sse; sae_all += sae; maxerr = std::max(maxerr, me);
        if (r.status == ST_OK) psum_ok += r.psnr;
        if (me) exact = false;
        ++cnt[r.status];
        if (!quiet) printf("frame %3d %c bytes %6zu wire %6zu psnr %7.3f rmse %8.3f mae %7.3f maxerr %5d enc %7.2f ms dec %7.2f ms %s\n", k, r.type,
                           r.bytes, r.wire, r.psnr, std::sqrt(r.mse), r.mae, me, r.enc_ms, r.dec_ms, stname[r.status]);
        if (cf) fprintf(cf, "%d,%c,%zu,%zu,%.4f,%.4f,%.4f,%d,%.3f,%.3f,%s,%d\n", k, r.type, r.bytes, r.wire, r.psnr, std::sqrt(r.mse), r.mae, me,
                        r.enc_ms, r.dec_ms, stname[r.status], r.release_delay);
        if (rf) rf.write(reinterpret_cast<const char*>(shown.data()), std::streamsize(fs * 2));
    }
    if (cf) fclose(cf);
    std::vector<double> te, td;
    size_t smax = 0, wmax = 0;
    double rd = 0; int rdn = 0;
    for (auto& r : rec) {
        te.push_back(r.enc_ms); if (r.dec_ms >= 0) td.push_back(r.dec_ms);
        smax = std::max(smax, r.bytes); wmax = std::max(wmax, r.wire);
        if (r.release_delay >= 0) { rd += r.release_delay; ++rdn; }
    }
    auto med = [](std::vector<double> v) { std::sort(v.begin(), v.end()); return v.empty() ? 0.0 : v[v.size() / 2]; };
    auto pct = [](std::vector<double> v, double p) { std::sort(v.begin(), v.end()); return v.empty() ? 0.0 : v[std::min(v.size() - 1, size_t(p * (v.size() - 1) + 0.5))]; };
    const double npx = double(fs) * nfr, per = fps / nfr / 1000 * 8;
    printf("SUMMARY frames %d ok %d damaged %d ref_invalid %d lost %d stale_displayed %d avg_bytes %.1f max_bytes %zu kbps %.1f "
           "fec_kbps %.1f hdr_kbps %.1f wire_kbps %.1f peak_wire_kbps %.1f bpp %.4f psnr_avg %.3f psnr_min %.3f psnr_ok_avg %.3f "
           "psnr_global %.3f rmse %.3f mae %.3f maxerr %d exact %d enc_med %.2f enc_p99 %.2f enc_max %.2f dec_med %.2f dec_p99 %.2f "
           "dgrams %ld dropped %ld loss_actual %.4f recovered %llu intra_requests %d release_delay_avg %.3f codec_budget %u fps %.3f fec %s k %d\n",
           nfr, cnt[ST_OK], cnt[ST_DAMAGED], cnt[ST_REFBAD], cnt[ST_LOST], displayed_stale, double(codec_bytes) / nfr, smax,
           double(codec_bytes) * per, double(par_bytes) * per, double(hdr_bytes) * per, double(wire_bytes) * per,
           double(wmax) * 8 * fps / 1000, double(codec_bytes) * 8 / npx, psum / nfr, pmin, cnt[ST_OK] ? psum_ok / cnt[ST_OK] : 0.0,
           sse_all > 0 ? 10 * std::log10(peak * peak / (sse_all / npx)) : 199.0, std::sqrt(sse_all / npx), sae_all / npx, maxerr,
           int(exact), med(te), pct(te, 0.99), *std::max_element(te.begin(), te.end()), med(td), pct(td, 0.99), dgrams, dropped,
           dgrams ? double(dropped) / dgrams : 0.0, (unsigned long long)ra.datagrams_recovered, requests, rdn ? rd / rdn : 0.0,
           cfg.frame_bytes, fps, ewc::fec_name(pk.fec()), pk.k());
    return 0;
}
