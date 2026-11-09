// ewc_gst_loop — experiment: carry ewc frame units through a real GStreamer RTP/UDP pipeline on
// localhost and measure what GStreamer adds (latency, CPU, loss behaviour). ewc still does the
// compression; GStreamer only transports.
//
//   sender:   appsrc (application/x-ewc) ! rtpgstpay mtu=1400 [! rtpulpfecenc percentage=P]
//             [! identity drop-probability=L] ! udpsink 127.0.0.1:PORT
//   receiver: udpsrc PORT ! rtpjitterbuffer latency=J [! rtpulpfecdec] ! rtpgstdepay ! appsink
//
// Frames are pushed in real time (one every 1/fps s). For every frame the program records
// push -> pull latency, whether the received unit is byte-identical, and the ewc decode result.
// Not part of the core codec (GStreamer is an external library); built only if found.
//
// usage: ewc_gst_loop input.u16 frames fps fec_percent drop_probability jitter_ms port
#include "ewc.h"

#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <sys/resource.h>

using clk = std::chrono::steady_clock;

int main(int argc, char** argv) {
    if (argc < 8) { fprintf(stderr, "usage: ewc_gst_loop input.u16 frames fps fec_percent drop_prob jitter_ms port\n"); return 2; }
    gst_init(&argc, &argv);
    const char* in = argv[1];
    const int nf = atoi(argv[2]);
    const double fps = atof(argv[3]);
    const int fec = atoi(argv[4]);
    const double drop = atof(argv[5]);
    const int jit = atoi(argv[6]);
    const int port = atoi(argv[7]);

    // ---- encode all frames first (PoC profile) so only transport is timed
    ewc::EncoderConfig cfg = ewc::poc_config(fps);
    ewc::Encoder enc(cfg);
    std::ifstream f(in, std::ios::binary);
    std::vector<std::vector<uint8_t>> units;
    std::vector<uint16_t> fr(640 * 512);
    for (int k = 0; k < nf && f.read(reinterpret_cast<char*>(fr.data()), std::streamsize(fr.size() * 2)); ++k) {
        units.emplace_back();
        enc.encode(fr.data(), uint64_t(k * 1e6 / fps), units.back());
    }
    const int n = int(units.size());

    // ---- pipelines
    std::string s = "appsrc name=src is-live=true format=time do-timestamp=false caps=application/x-ewc ! "
                    "rtpgstpay mtu=1400 config-interval=1 ssrc=4660 pt=96 ";
    if (fec > 0) s += "! rtpulpfecenc percentage=" + std::to_string(fec) + " pt=122 ";
    if (drop > 0) s += "! identity drop-probability=" + std::to_string(drop) + " ";
    s += "! udpsink host=127.0.0.1 port=" + std::to_string(port) + " sync=false async=false";
    // ULPFEC receive side (RFC 5109) as GStreamer expects it: SSRC in the caps, an rtpstorage that
    // keeps recent media packets, and rtpulpfecdec pointed at that storage (set in code below).
    std::string r = "udpsrc port=" + std::to_string(port) +
                    " caps=\"application/x-rtp,media=application,clock-rate=90000,encoding-name=X-GST,payload=96,ssrc=(uint)4660\" ";
    if (fec > 0) r += "! rtpstorage name=st size-time=" + std::to_string(uint64_t(jit + 200) * 1000000ull) + " ";
    r += "! rtpjitterbuffer latency=" + std::to_string(jit) + " do-lost=true ";
    if (fec > 0) r += "! rtpulpfecdec name=fd pt=122 ";
    r += "! rtpgstdepay ! appsink name=sink sync=false emit-signals=false max-buffers=1000";
    GError* err = nullptr;
    GstElement* tx = gst_parse_launch(s.c_str(), &err);
    if (!tx) { fprintf(stderr, "sender: %s\n", err->message); return 1; }
    GstElement* rx = gst_parse_launch(r.c_str(), &err);
    if (!rx) { fprintf(stderr, "receiver: %s\n", err->message); return 1; }
    if (fec > 0) {
        GstElement* st = gst_bin_get_by_name(GST_BIN(rx), "st");
        GstElement* fd = gst_bin_get_by_name(GST_BIN(rx), "fd");
        GObject* storage = nullptr;
        g_object_get(st, "internal-storage", &storage, nullptr);
        g_object_set(fd, "storage", storage, nullptr);
        g_object_unref(storage); gst_object_unref(st); gst_object_unref(fd);
    }
    GstAppSrc* src = GST_APP_SRC(gst_bin_get_by_name(GST_BIN(tx), "src"));
    GstAppSink* sink = GST_APP_SINK(gst_bin_get_by_name(GST_BIN(rx), "sink"));
    gst_element_set_state(rx, GST_STATE_PLAYING);
    gst_element_set_state(tx, GST_STATE_PLAYING);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    std::vector<clk::time_point> sent(static_cast<size_t>(n));
    std::mutex mu;
    // received units keyed by the ewc sequence number in the unit header (receiver PTS are re-stamped)
    std::map<uint32_t, std::pair<clk::time_point, std::vector<uint8_t>>> got;
    std::atomic<bool> done{false};
    std::thread rxth([&] {
        while (!done) {
            GstSample* smp = gst_app_sink_try_pull_sample(sink, 50 * GST_MSECOND);
            if (!smp) continue;
            const auto t = clk::now();
            GstBuffer* b = gst_sample_get_buffer(smp);
            GstMapInfo mi;
            gst_buffer_map(b, &mi, GST_MAP_READ);
            std::lock_guard<std::mutex> lk(mu);
            if (mi.size >= 20) {
                const uint32_t sq = uint32_t(mi.data[16]) | uint32_t(mi.data[17]) << 8 | uint32_t(mi.data[18]) << 16 | uint32_t(mi.data[19]) << 24;
                if (!got.count(sq)) got[sq] = {t, std::vector<uint8_t>(mi.data, mi.data + mi.size)};
            }
            gst_buffer_unmap(b, &mi);
            gst_sample_unref(smp);
        }
    });

    rusage r0, r1;
    getrusage(RUSAGE_SELF, &r0);
    const auto t0 = clk::now();
    for (int k = 0; k < n; ++k) {
        std::this_thread::sleep_until(t0 + std::chrono::microseconds(int64_t(k * 1e6 / fps)));
        GstBuffer* b = gst_buffer_new_allocate(nullptr, units[k].size(), nullptr);
        gst_buffer_fill(b, 0, units[k].data(), units[k].size());
        GST_BUFFER_PTS(b) = GST_BUFFER_DTS(b) = guint64(k * 1e9 / fps);
        GST_BUFFER_DURATION(b) = guint64(1e9 / fps);
        sent[k] = clk::now();
        gst_app_src_push_buffer(src, b);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500 + jit));
    getrusage(RUSAGE_SELF, &r1);
    done = true;
    rxth.join();
    const double wall = std::chrono::duration<double>(clk::now() - t0).count();
    const double cpu = (r1.ru_utime.tv_sec - r0.ru_utime.tv_sec) + (r1.ru_stime.tv_sec - r0.ru_stime.tv_sec) +
                       ((r1.ru_utime.tv_usec - r0.ru_utime.tv_usec) + (r1.ru_stime.tv_usec - r0.ru_stime.tv_usec)) / 1e6;

    // ---- results
    std::vector<double> lat;
    int exact = 0, wrong = 0, decoded_reliable = 0;
    ewc::Decoder dec;
    for (int k = 0; k < n; ++k) {
        auto it = got.find(uint32_t(k));
        if (it == got.end()) continue;
        lat.push_back(std::chrono::duration<double, std::milli>(it->second.first - sent[k]).count());
        if (it->second.second == units[k]) ++exact; else ++wrong;
        size_t pos = 0; std::vector<uint16_t> out; ewc::FrameInfo fi; int w, h;
        if (dec.decode_next(it->second.second.data(), it->second.second.size(), pos, out, fi, w, h) && fi.reliable()) ++decoded_reliable;
    }
    std::sort(lat.begin(), lat.end());
    auto pct = [&](double p) { return lat.empty() ? 0.0 : lat[std::min(lat.size() - 1, size_t(p * double(lat.size() - 1) + 0.5))]; };
    guint rec_pk = 0, unrec_pk = 0;
    if (fec > 0) {
        GstElement* fd = gst_bin_get_by_name(GST_BIN(rx), "fd");
        g_object_get(fd, "recovered", &rec_pk, "unrecovered", &unrec_pk, nullptr);
        gst_object_unref(fd);
    }
    printf("FEC packets recovered %u unrecovered %u\n", rec_pk, unrec_pk);
    printf("SUMMARY frames %d received %zu exact %d wrong %d reliable %d lost %d lat_med_ms %.3f lat_p99_ms %.3f lat_max_ms %.3f "
           "cpu_pct_of_one_core %.2f fec %d drop %.3f jitter_ms %d\n",
           n, lat.size(), exact, wrong, decoded_reliable, n - int(lat.size()), pct(0.5), pct(0.99), lat.empty() ? 0.0 : lat.back(),
           100.0 * cpu / wall, fec, drop, jit);
    gst_element_set_state(tx, GST_STATE_NULL);
    gst_element_set_state(rx, GST_STATE_NULL);
    gst_object_unref(tx); gst_object_unref(rx);
    return 0;
}
