// SPDX-License-Identifier: GPL-3.0-only
//
// Link on Deck, end to end on a computer: loads the host build of flow-beat.so with a small
// fake core whose decks play by the real clock, and checks what a second Link peer (standing
// in for Ableton) sees: tempo, beat, bar, start/stop, jog nudges, block-sized phase jitter,
// a "this is 1" press over UDP and a change of lead deck.
//
//   make -C tests link     (needs the Link submodule; runs on Linux, ~20 s)
#include <ableton/Link.hpp>

#include <arpa/inet.h>
#include <dlfcn.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "azmod-mod.h"

namespace {

double now_s()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);   // the clock Link uses on Linux
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

// ---- the simulated decks: beat position integrated from tempo, by the real clock -----------
struct Deck {
    bool loaded = false, playing = false;
    double bpm = 128.0;
    double beat = 0.0;      // beats played, at time t
    double t = 0.0;
    double block = 0.0;     // the playhead moves in steps of this many beats (audio blocks)
    unsigned start = 0;
    char title[32] = "";
};
std::mutex mu;
Deck decks[4];

void advance_locked(Deck &d, double t)
{
    if (d.playing) d.beat += (t - d.t) * d.bpm / 60.0;
    d.t = t;
}

double deck_beat(int i)
{
    std::lock_guard<std::mutex> g(mu);
    advance_locked(decks[i], now_s());
    return decks[i].beat;
}

template <typename F> void with_deck(int i, F f)
{
    std::lock_guard<std::mutex> g(mu);
    advance_locked(decks[i], now_s());
    f(decks[i]);
}

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


int c_cfg_get(const char *, const char *key, int def)
{
    if (!strcmp(key, "link")) return 1;   // no Mod Menu rows here: azmods.cfg turns Link on
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
    std::lock_guard<std::mutex> g(mu);
    Deck &d = decks[i];
    advance_locked(d, now_s());
    o->loaded = d.loaded;
    o->playing = d.playing;
    o->bpm = d.bpm;
    o->pos_s = d.beat * 60.0 / 128.0;
    o->len_s = 600;
    o->content_id = 100 + i;
    snprintf(o->title, sizeof o->title, "%s", d.title);
    snprintf(o->artist, sizeof o->artist, "Test");
    return 0;
}

int c_deck_grid(int i, double *beat, double *bar)
{
    if (i < 0 || i > 3) return -1;
    std::lock_guard<std::mutex> g(mu);
    Deck &d = decks[i];
    if (!d.loaded) return -1;
    advance_locked(d, now_s());
    double b = d.beat;
    if (d.block > 0) b = std::floor(b / d.block) * d.block;
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

// ---- checks -----------------------------------------------------------------------------------
int failures = 0;
void check(bool ok, const char *what, double v)
{
    std::printf("%s %-58s %.4f\n", ok ? "PASS" : "FAIL", what, v);
    if (!ok) failures++;
}

void sleep_s(double s) { usleep((useconds_t)(s * 1e6)); }

struct Peer {
    ableton::Link link{100.0};
    Peer()
    {
        link.enableStartStopSync(true);
        link.enable(true);
    }
    double tempo() { return link.captureAppSessionState().tempo(); }
    bool playing() { return link.captureAppSessionState().isPlaying(); }
    // the peer's beat now, and the deck's at the same moment
    double beat_err(int deck, double cycle, double deck_offset = 0.0)
    {
        auto s = link.captureAppSessionState();
        double want = deck_beat(deck) + deck_offset;
        double b = s.beatAtTime(link.clock().micros(), 4.0);
        double d = std::fmod(want - b, cycle);
        if (d > cycle / 2) d -= cycle;
        if (d < -cycle / 2) d += cycle;
        return d;
    }
    // the worst |error| over a stretch, sampled every 10 ms
    double worst(int deck, double secs, double cycle, double deck_offset = 0.0)
    {
        double w = 0;
        for (double t = 0; t < secs; t += 0.01) {
            w = std::max(w, std::fabs(beat_err(deck, cycle, deck_offset)));
            sleep_s(0.01);
        }
        return w;
    }
};

void press_one(bool late)
{
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(9101);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    char m[64];
    int n = snprintf(m, sizeof m, "{\"flowbeat\":1,\"type\":\"one\",\"late\":%s}", late ? "true" : "false");
    sendto(s, m, (size_t)n, 0, (sockaddr *)&a, sizeof a);
    close(s);
}

} // namespace

int main(int argc, char **argv)
{
    const char *so = argc > 1 ? argv[1] : "./flow-beat.so";
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

    Peer ableton;   // the stand-in for Ableton, joined before the deck starts
    double t0 = now_s();
    with_deck(0, [](Deck &d) { d.loaded = true; d.playing = true; d.bpm = 128.0; snprintf(d.title, sizeof d.title, "Track A"); });
    if (mod->init(&core) != 0) { std::printf("FAIL init\n"); return 1; }

    for (int i = 0; i < 300 && ableton.link.numPeers() < 1; i++) sleep_s(0.01);
    check(ableton.link.numPeers() == 1, "peer sees the deck in its Link session (peers)", (double)ableton.link.numPeers());
    std::printf("     (joined after %.2f s)\n", now_s() - t0);
    sleep_s(1.0);

    check(std::fabs(ableton.tempo() - 128.0) < 0.01, "tempo follows the deck: 128", ableton.tempo());
    check(ableton.playing(), "start/stop: the session plays", ableton.playing());
    check(ableton.worst(0, 1.0, 1.0) < 0.01, "beat lined up, worst error over 1 s (beats)", ableton.worst(0, 0.01, 1.0));

    // the pitch fader: 128 -> 124 over a second
    for (int i = 1; i <= 50; i++) {
        with_deck(0, [i](Deck &d) { d.bpm = 128.0 - 4.0 * i / 50; });
        sleep_s(0.02);
    }
    sleep_s(0.2);
    check(std::fabs(ableton.tempo() - 124.0) < 0.01, "tempo follows the pitch fader: 124", ableton.tempo());
    check(ableton.worst(0, 1.0, 1.0) < 0.015, "beat still lined up after the tempo ramp", ableton.worst(0, 0.01, 1.0));

    // a jog nudge: the deck jumps a third of a beat
    with_deck(0, [](Deck &d) { d.beat += 0.33; });
    sleep_s(0.15);
    check(std::fabs(ableton.beat_err(0, 1.0)) < 0.01, "a jog nudge is followed within 150 ms", ableton.beat_err(0, 1.0));

    // the playhead moving in audio blocks (0.04 beat, ~20 ms) must not keep moving the session
    with_deck(0, [](Deck &d) { d.block = 0.04; });
    sleep_s(0.5);
    double w = ableton.worst(0, 1.5, 1.0);
    // does the session's beat jump? sample it every 2 ms for 2 s and look for steps
    int jumps = 0;
    {
        double lb = -1, lt = 0;
        for (int i = 0; i < 1000; i++) {
            auto st = ableton.link.captureAppSessionState();
            double t = ableton.link.clock().micros().count() / 1e6;
            double b = st.beatAtTime(ableton.link.clock().micros(), 4.0);
            if (lb >= 0 && std::fabs((b - lb) - (t - lt) * st.tempo() / 60.0) > 0.01) jumps++;
            lb = b;
            lt = t;
            sleep_s(0.002);
        }
    }
    with_deck(0, [](Deck &d) { d.block = 0.0; });
    check(w < 0.045, "with block steps the peer stays within one block", w);
    check(jumps == 0, "and the session's beat doesn't jump about (jumps in 2 s)", jumps);

    // "this is 1": the next beat is the bar's 1
    sleep_s(0.3);
    double before = deck_beat(0);
    press_one(true);
    sleep_s(0.2);
    double one = std::ceil(before);   // the beat the press named
    // deck beat counting from the press: the peer's beat-of-bar should be (deck - one) mod 4
    double e = ableton.beat_err(0, 4.0, -one);
    check(std::fabs(e) < 0.02, "\"this is 1\" puts the peer's bar on the pressed beat", e);
    check(ableton.worst(0, 2.0, 4.0, -one) < 0.02, "and the bar holds for 2 s", ableton.worst(0, 0.01, 4.0, -one));

    // a second deck starts later at 140 and leads
    with_deck(1, [](Deck &d) { d.loaded = true; d.playing = true; d.bpm = 140.0; d.beat = 0.6; snprintf(d.title, sizeof d.title, "Track B"); });
    sleep_s(0.3);
    check(std::fabs(ableton.tempo() - 140.0) < 0.01, "the deck started last leads: tempo 140", ableton.tempo());
    check(ableton.worst(1, 1.0, 1.0) < 0.01, "and its beat", ableton.worst(1, 0.01, 1.0));

    // everything stops: the session stops, the tempo holds
    with_deck(0, [](Deck &d) { d.playing = false; });
    with_deck(1, [](Deck &d) { d.playing = false; });
    sleep_s(0.2);
    check(!ableton.playing(), "start/stop: the session stops with the decks", ableton.playing());
    check(std::fabs(ableton.tempo() - 140.0) < 0.01, "and the tempo holds at 140", ableton.tempo());

    // Ableton changes the tempo while the deck plays: the deck wins back within a step
    with_deck(0, [](Deck &d) { d.playing = true; });
    sleep_s(0.3);
    {
        auto s = ableton.link.captureAppSessionState();
        s.setTempo(90.0, ableton.link.clock().micros());
        ableton.link.commitAppSessionState(s);
    }
    sleep_s(0.3);
    check(std::fabs(ableton.tempo() - 124.0) < 0.01, "a tempo change from Ableton is overruled by the deck", ableton.tempo());

    std::printf("%s: %d failure%s\n", failures ? "FAILED" : "ALL PASS", failures, failures == 1 ? "" : "s");
    std::fflush(stdout);
    _exit(failures ? 1 : 0);   // the mod's threads never end, as on the deck
}
