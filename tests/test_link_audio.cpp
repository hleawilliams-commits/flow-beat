// SPDX-License-Identifier: GPL-3.0-only
//
// Link Audio from the deck, end to end on a computer: loads the host build of flow-beat.so with a
// fake core whose four audio taps deliver 480-frame stereo blocks at exactly the given rate, and checks
// what a second LinkAudio peer (standing in for Ableton Live 12.4) receives: the channels, the
// samples (a counter pattern, so a lost, reordered or corrupted frame shows), the format, and
// where each block sits on Link's beat timeline, and the sample rate it is sent at.
//
//   make -C tests audio     (needs the Link submodule; runs on Linux, ~45 s): three runs,
//                           taps at 48000, taps at 44100 (as on the emulator), and taps at 44100
//                           with flow-beat:link_audio_rate = 48000 forcing the rate
//   test_link_audio <flow-beat.so> [tap rate] [forced link_audio_rate, 0 = measure]
#include <ableton/LinkAudio.hpp>
#include <ableton/util/FloatIntConversion.hpp>

#include <dlfcn.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "azmod-mod.h"

namespace {

int kRate = 48000;                    // the taps' rate (argv[2])
int kForced = 0;                      // flow-beat:link_audio_rate (argv[3]), 0 = measure
int send_rate() { return kForced ? kForced : kRate; }
constexpr int kFrames = 480;          // flow-beat asks for 480-frame blocks (TAP_FRAMES)
constexpr int kPattern = 16384;       // left = k % kPattern (in int16 steps), right = -left

long long now_us()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);   // the clock Link uses on Linux
    return ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}

void sleep_s(double s) { usleep((useconds_t)(s * 1e6)); }

// ---- the fake core ---------------------------------------------------------------------------
std::mutex log_mu;
std::vector<std::string> logs;

void c_log(const char *, const char *fmt, ...)
{
    char b[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    std::lock_guard<std::mutex> g(log_mu);
    logs.push_back(b);
    std::printf("    [flow-beat] %s\n", b);
}

bool logged(const char *part)
{
    std::lock_guard<std::mutex> g(log_mu);
    for (auto &l : logs)
        if (l.find(part) != std::string::npos) return true;
    return false;
}

std::atomic<int> cfg_audio{1};

int c_cfg_get(const char *, const char *key, int def)
{
    if (!strcmp(key, "link-audio")) return cfg_audio.load();   // no Mod Menu rows here
    if (!strcmp(key, "link_audio_rate")) return kForced;
    if (!strcmp(key, "link")) return 0;   // Link Audio alone must bring Link up
    if (!strcmp(key, "send")) return 0;   // keep the FLOW packets off the test network
    return def;
}

int c_cfg_get_str(const char *, const char *, char *buf, int n, const char *def)
{
    snprintf(buf, (size_t)n, "%s", def ? def : "");
    return 0;
}

int c_deck_info(int i, az_deck_info *o)
{
    if (i < 0 || i > 3) return -1;
    std::memset(o, 0, sizeof *o);
    o->loaded = i == 0;
    o->playing = i == 0;
    o->bpm = 128.0;
    o->pos_s = (now_us() / 1e6);
    o->len_s = 600;
    o->content_id = 100 + i;
    snprintf(o->title, sizeof o->title, "Track %d", i + 1);
    return 0;
}

int c_deck_grid(int i, double *beat, double *bar)
{
    if (i != 0) return -1;
    double b = now_us() / 1e6 * 128.0 / 60.0;
    *beat = b - std::floor(b);
    *bar = -1;
    return 0;
}

int c_thread_start(const char *, const char *, void *(*fn)(void *), void *arg)
{
    pthread_t t;
    if (pthread_create(&t, nullptr, fn, arg) != 0) return -1;
    pthread_detach(t);
    return 0;
}

int c_on_deck_event(const char *, void (*)(int, int, void *), void *) { return 0; }

// the taps: the mod's fn for each deck, fed by one thread at exactly 48 kHz (absolute deadlines)
struct Tap {
    void (*fn)(const float *, int, void *) = nullptr;
    void *user = nullptr;
};
Tap taps[4];
// for deck 1, the host time each frame's block was handed over at (end of the block), by k % kPattern
std::unique_ptr<std::atomic<long long>[]> fed_end_us(new std::atomic<long long>[kPattern]());
std::unique_ptr<std::atomic<long long>[]> fed_block_end_k(new std::atomic<long long>[kPattern]());
std::atomic<bool> taps_run{true};

int c_audio_tap(const char *, int source, int frames, void (*fn)(const float *, int, void *), void *user)
{
    if (source < 0 || source > 3 || frames != kFrames || taps[source].fn) return -1;
    taps[source].fn = fn;
    taps[source].user = user;
    return 0;
}

void *tap_thread(void *)
{
    std::vector<float> lr(2 * kFrames);
    long long k = 0;   // frames delivered
    timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    while (taps_run.load()) {
        // block n is due when its 480 frames have played at kRate (10 ms at 48 kHz, 10.88 at 44.1)
        long long due_ns = t0.tv_sec * 1000000000LL + t0.tv_nsec + (k + kFrames) * 1000000000LL / kRate;
        timespec next = {(time_t)(due_ns / 1000000000LL), (long)(due_ns % 1000000000LL)};
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, nullptr);
        for (int i = 0; i < kFrames; i++) {
            float v = (float)((k + i) % kPattern) / 32768.0f;
            lr[2 * i] = v;
            lr[2 * i + 1] = -v;
        }
        long long end = now_us();
        for (int i = 0; i < kFrames; i++) {
            fed_end_us[(k + i) % kPattern] = end;
            fed_block_end_k[(k + i) % kPattern] = k + kFrames;
        }
        for (int d = 0; d < 4; d++)
            if (taps[d].fn) taps[d].fn(lr.data(), kFrames, taps[d].user);
        k += kFrames;
    }
    return nullptr;
}

// ---- checks -----------------------------------------------------------------------------------
int failures = 0;
void check(bool ok, const char *what, double v)
{
    std::printf("%s %-62s %.4f\n", ok ? "PASS" : "FAIL", what, v);
    if (!ok) failures++;
}

// what the stand-in for Ableton receives on one channel
struct Rx {
    std::mutex mu;
    long buffers = 0, frames = 0, bad_samples = 0, gaps = 0, bad_format = 0;
    long beat_n = 0;
    double beat_worst = 0.0;
    int last = -1;
    void reset()
    {
        std::lock_guard<std::mutex> g(mu);
        buffers = frames = bad_samples = gaps = bad_format = beat_n = 0;
        beat_worst = 0.0;
        last = -1;
    }
};

} // namespace

int main(int argc, char **argv)
{
    const char *so = argc > 1 ? argv[1] : "./flow-beat.so";
    if (argc > 2) kRate = atoi(argv[2]);
    if (argc > 3) kForced = atoi(argv[3]);
    std::printf("== taps at %d Hz, link_audio_rate = %d (%s)\n", kRate, kForced, kForced ? "forced" : "measure");
    void *h = dlopen(so, RTLD_NOW | RTLD_LOCAL);
    if (!h) { std::printf("FAIL dlopen %s\n", dlerror()); return 1; }
    auto entry = (const az_mod_v1 *(*)(void))dlsym(h, "az_mod_v1_entry");
    const az_mod_v1 *mod = entry();

    static az_core_v1 core;
    std::memset(&core, 0, sizeof core);
    core.abi = AZ_MOD_ABI;
    core.size = sizeof core;
    core.log = c_log;
    core.cfg_get = c_cfg_get;
    core.cfg_get_str = c_cfg_get_str;
    core.deck_info = c_deck_info;
    core.deck_grid = c_deck_grid;
    core.thread_start = c_thread_start;
    core.on_deck_event = c_on_deck_event;
    core.audio_tap = c_audio_tap;

    ableton::LinkAudio ableton(100.0, "Ableton");
    ableton.enable(true);
    ableton.enableLinkAudio(true);

    if (mod->init(&core) != 0) { std::printf("FAIL init\n"); return 1; }
    check(taps[0].fn && taps[3].fn, "the mod taps all four decks", taps[3].fn ? 4 : 0);
    pthread_t feeder;
    pthread_create(&feeder, nullptr, tap_thread, nullptr);

    // the channels show up in the session
    std::vector<ableton::LinkAudio::Channel> ch;
    long long t0 = now_us();
    for (int i = 0; i < 500; i++) {
        ch = ableton.channels();
        if (ch.size() >= 4) break;
        sleep_s(0.01);
    }
    std::printf("     (channels after %.2f s)\n", (now_us() - t0) / 1e6);
    int named = 0;
    for (int d = 0; d < 4; d++)
        for (auto &c : ch)
            if (c.name == "Deck " + std::to_string(d + 1) && c.peerName == "XDJ-AZ") named++;
    check(ch.size() == 4 && named == 4, "Ableton sees XDJ-AZ's channels Deck 1..Deck 4", (double)named);
    check(ableton.numPeers() == 1, "and the deck is a Link peer (Link came on with Link Audio)", (double)ableton.numPeers());
    sleep_s(1.0);
    check(std::fabs(ableton.captureAppSessionState().tempo() - 128.0) < 0.01, "the session tempo is the deck's: 128",
          ableton.captureAppSessionState().tempo());

    // listen to Deck 1
    Rx rx;
    std::unique_ptr<ableton::LinkAudioSource> src;
    for (auto &c : ch)
        if (c.name == "Deck 1")
            src.reset(new ableton::LinkAudioSource(ableton, c.id, [&](ableton::LinkAudioSource::BufferHandle b) {
                auto st = ableton.captureAppSessionState();
                std::lock_guard<std::mutex> g(rx.mu);
                rx.buffers++;
                rx.frames += (long)b.info.numFrames;
                if (b.info.numChannels != 2 || b.info.sampleRate != (uint32_t)send_rate()) rx.bad_format++;
                for (size_t i = 0; i < b.info.numFrames; i++) {
                    int l = b.samples[2 * i], r = b.samples[2 * i + 1];
                    if (l < 0 || l >= kPattern || r != -l) { rx.bad_samples++; continue; }
                    if (rx.last >= 0 && l != (rx.last + 1) % kPattern) rx.gaps++;
                    rx.last = l;
                }
                // where the buffer's first frame was meant to be heard: its block's hand-over time
                // minus the rest of the block (link_ms and link_audio_ms are 0 here)
                int k0 = b.samples[0];
                if (k0 >= 0 && k0 < kPattern) {
                    long long blk_end_k = fed_block_end_k[(size_t)k0];
                    long long end = fed_end_us[(size_t)k0];
                    long long frames_left = (blk_end_k % kPattern - k0 + kPattern) % kPattern;   // 1..480
                    // (the mod times the block by the rate it sends at)
                    auto t = std::chrono::microseconds(end - (long long)std::llround(frames_left * 1e6 / send_rate()));
                    auto got = b.info.beginBeats(st, 4.0);
                    if (got) {
                        double e = *got - st.beatAtTime(t, 4.0);
                        rx.beat_n++;
                        rx.beat_worst = std::max(rx.beat_worst, std::fabs(e));
                    }
                }
            }));
    sleep_s(1.0);   // let the deck's clock map settle
    rx.reset();
    sleep_s(3.0);
    {
        std::lock_guard<std::mutex> g(rx.mu);
        double secs = 3.0;
        check(rx.buffers > 0, "Deck 1's audio arrives (buffers in 3 s)", (double)rx.buffers);
        check(std::fabs(rx.frames - secs * kRate) < 0.05 * secs * kRate, "as many frames a second arrive as the tap gives",
              rx.frames / secs);
        check(rx.bad_format == 0, kForced ? "every buffer is stereo at the forced rate (bad buffers)"
                                          : "every buffer is stereo at the tap's own rate (bad buffers)",
              (double)rx.bad_format);
        check(rx.bad_samples == 0, "every sample is what the tap gave, L and R (bad samples)", (double)rx.bad_samples);
        check(rx.gaps == 0, "no frame lost or out of order (gaps)", (double)rx.gaps);
        check(rx.beat_n > 0 && rx.beat_worst < 0.01,
              "each buffer sits on Link's beat where it was played (worst, beats)", rx.beat_worst);
    }
    check(logged("deck 1 is being received"), "the log says deck 1 is being received", logged("deck 1 is being received"));
    check(!logged("deck 3 is being received"), "decks nobody listens to send nothing (deck 3 not received)",
          logged("deck 3 is being received"));
    {
        char want[96];
        snprintf(want, sizeof want, "Link Audio: deck 1 sends at %d Hz", send_rate());
        check(logged(want), "the log names the rate deck 1 is sent at", send_rate());
        snprintf(want, sizeof want, "sends at %d Hz", kForced ? kRate : (kRate == 48000 ? 44100 : 48000));
        check(!logged(want), "and never the wrong one", 0);
    }
    sleep_s(3.5);
    bool rate_ok = false;
    {
        std::lock_guard<std::mutex> g(log_mu);
        for (auto &l : logs) {
            double r;
            if (sscanf(l.c_str(), "Link Audio: deck 1 tap runs at %lf Hz", &r) == 1 && std::fabs(r - kRate) < 200) rate_ok = true;
        }
    }
    check(rate_ok, "the log reports the tap's measured rate", rate_ok);

    // the row goes off: Link Audio and Link stop, nothing more arrives
    cfg_audio.store(0);
    sleep_s(1.0);
    rx.reset();
    sleep_s(1.0);
    long after;
    {
        std::lock_guard<std::mutex> g(rx.mu);
        after = rx.buffers;
    }
    check(after == 0, "turned off: no more audio (buffers in 1 s)", (double)after);
    check(logged("Link Audio off") || logged("Link off"), "and the log says so", 1);
    int left = 0;
    for (int i = 0; i < 300; i++) {
        left = 0;
        for (auto &c : ableton.channels())
            if (c.peerName == "XDJ-AZ") left++;
        if (!left) break;
        sleep_s(0.01);
    }
    check(left == 0, "and the deck's channels leave the session", left);

    // on again: it comes back
    cfg_audio.store(1);
    for (int i = 0; i < 500; i++) {
        int n = 0;
        for (auto &c : ableton.channels())
            if (c.peerName == "XDJ-AZ") n++;
        if (n == 4) break;
        sleep_s(0.01);
    }
    src.reset();
    ch = ableton.channels();
    int n = 0;
    for (auto &c : ch)
        if (c.peerName == "XDJ-AZ") n++;
    check(n == 4, "on again: the four channels are back", n);

    taps_run.store(false);
    pthread_join(feeder, nullptr);
    std::printf("%s: %d failure%s\n", failures ? "FAILED" : "ALL PASS", failures, failures == 1 ? "" : "s");
    std::fflush(stdout);
    _exit(failures ? 1 : 0);   // the mod's threads never end, as on the deck
}
