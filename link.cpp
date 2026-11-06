/* SPDX-License-Identifier: GPL-3.0-only
 *
 * Link on Deck: Ableton Link from the deck itself (replaces the Mac's FLOW Link bridge).
 *
 * The lead deck (flow-beat.c on_air_deck: the playing deck whose fader is up most, else the one
 * started last) leads the session:
 *   tempo  Link's tempo is the lead deck's BPM, pitch included, set whenever it moves.
 *   beat   Link's beat phase follows the deck's own grid phase (deck_grid), read every 5 ms on
 *          the deck instead of guessed from packet arrival times on a computer.
 *   bar    Link's bar (quantum 4) sits on the track's "1": from a "this is 1" press, else the
 *          track's rekordbox beat grid (PQTZ) that flow-beat already reads off the USB.
 *   play   Link's start/stop follows the decks: on when one starts, off when all stop (for apps
 *          with Start Stop Sync on).
 * Small differences are averaged first, so the playhead's block-sized steps don't move every
 * peer; a real jump (a jog nudge, a new lead deck, the wrong bar) forces the session over.
 *
 * Link Audio (0.5.2): the deck is the peer "XDJ-AZ" and offers four channels, "Deck 1".."Deck 4",
 * each deck's own sound from its audio tap (before the hardware mixer, so no faders, EQ or
 * master). A channel sends only while a peer (Ableton's Link Audio input) listens to it. Each
 * block is placed on Link's timeline at the time it is heard: the tap's sample count is mapped
 * to the clock with Link's own host-time filter, plus link_ms (and link_audio_ms).
 *
 * Ableton Link (third_party/link) is GPL-2.0-or-later, so it may be built into this GPL-3.0
 * mod. Link runs two threads of its own (network and callbacks, normal priority, quiet when
 * idle); turning the row off disables Link, which closes its sockets.
 *
 * Everything below runs on flow-beat's worker thread, never on the UI, bus or audio paths, and
 * no exception leaves this file. flowlink_audio_send runs on the core's tap threads (not the
 * audio thread: a slow tap loses blocks, it never delays the sound).
 */
/* Link's internal asserts would abort EP147: compile them out */
#ifndef NDEBUG
#define NDEBUG 1
#endif
#include "link.h"

#ifndef LINK_PLATFORM_LINUX
#define LINK_PLATFORM_LINUX 1
#endif
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmultichar"   /* Link's four-letter payload keys (clang) */
/* Link Audio's own code (Link 4.1.1) trips these under -Wextra */
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wuninitialized"         /* GCC 13: PCMEncoder's output buffer */
#if !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include <ableton/LinkAudio.hpp>
#include <ableton/link/HostTimeFilter.hpp>
#include <ableton/util/FloatIntConversion.hpp>
#pragma GCC diagnostic pop

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>

namespace {

constexpr double kQuantum = 4.0;     /* beats in a bar */
constexpr double kAvgK = 0.1;        /* averaging of small errors: about 50 ms at 5 ms steps */
constexpr double kAvgMax = 0.04;     /* averaged error that moves the session (~19 ms at 128) */
constexpr double kJump = 0.25;       /* an error this big for kJumpSteps steps in a row is a jump */
constexpr int kJumpSteps = 3;

ableton::LinkAudio *deck_link = nullptr;  /* made once; the core never unloads a mod */
double avg_err = 0.0;
int jump_steps = 0;
int sent_playing = -1;               /* the start/stop state last sent: -1 none yet */
std::size_t last_peers = 0;
long long resend_us[3];              /* when to send it again after a peer joins (0 = no) */

/* ---- Link Audio: one channel per deck --------------------------------------------------- */
constexpr int kDecks = 4;
constexpr std::size_t kMaxSamples = 2 * 8192;   /* stereo, the tap's largest block */
constexpr long long kGapUs = 200000;            /* a tap silent this long starts a fresh clock map */

struct DeckSend {
    std::atomic<ableton::LinkAudioSink *> sink{nullptr};   /* made once, by the worker */
    /* the rest is touched only by this deck's tap */
    ableton::link::HostTimeFilter<ableton::link::platform::Clock> filter;
    double samples = 0.0;          /* frames delivered since the clock map started */
    long long first_us = 0, last_us = 0;
    double first_frames = 0.0;
    unsigned gen = 0;              /* audio_gen this deck last started at */
    std::atomic<double> rate_seen{0.0};
    std::atomic<unsigned> rate_used{0};   /* the rate blocks are sent at; 0 = not known yet */
    std::atomic<unsigned long> sent{0};
};
DeckSend sends[kDecks];
std::atomic<bool> audio_on{false};
std::atomic<unsigned> audio_gen{0};             /* bumped when Link Audio comes on */
std::atomic<unsigned> audio_rate{0};             /* forced sample rate, 0 = the tap's, measured */
constexpr long long kRateUs = 500000;           /* tap time needed before the rate is trusted */
std::atomic<long long> audio_offset_us{0};      /* ahead_ms + extra_ms */
std::atomic<long long> ahead_us{0};             /* the latest ahead_ms from flowlink_step */
long long extra_us = 0;

/* A measured tap rate snapped to the standard rate it is (the deck's taps run at one of these;
 * the emulator's at 44100), else rounded. 480-frame blocks over 0.5 s measure within ~2 %. */
unsigned snap_rate(double measured)
{
    static const unsigned kStd[] = {16000, 22050, 24000, 32000, 44100, 48000, 88200, 96000, 176400, 192000};
    unsigned best = kStd[0];
    for (unsigned r : kStd)
        if (std::fabs(measured / r - 1.0) < std::fabs(measured / best - 1.0)) best = r;
    return std::fabs(measured / best - 1.0) <= 0.02 ? best : (unsigned)std::llround(measured);
}

/* d wrapped into -cycle/2 .. cycle/2 */
double wrap(double d, double cycle)
{
    d = std::fmod(d, cycle);
    if (d > cycle / 2) d -= cycle;
    if (d < -cycle / 2) d += cycle;
    return d;
}

} // namespace

extern "C" int flowlink_enable(int on)
{
    try {
        if (!deck_link) {
            if (!on) return 0;
            deck_link = new ableton::LinkAudio(120.0, "XDJ-AZ");
            deck_link->enableStartStopSync(true);
        }
        if (deck_link->isEnabled() != (on != 0)) {
            if (!on) {
                audio_on.store(false);
                deck_link->enableLinkAudio(false);
            }
            deck_link->enable(on != 0);
            avg_err = 0.0;
            jump_steps = 0;
            sent_playing = -1;
            last_peers = 0;
        }
        return 0;
    } catch (...) {
        return -1;
    }
}

extern "C" int flowlink_peers(void)
{
    try {
        return deck_link && deck_link->isEnabled() ? (int)deck_link->numPeers() : -1;
    } catch (...) {
        return -1;
    }
}

extern "C" void flowlink_step(const flowlink_in *in, flowlink_out *out)
{
    std::memset(out, 0, sizeof *out);
    try {
        if (!deck_link || !deck_link->isEnabled()) return;
        {
            const long long a = std::llround(in->ahead_ms * 1000.0);
            if (a != ahead_us.load(std::memory_order_relaxed)) {
                ahead_us.store(a, std::memory_order_relaxed);
                audio_offset_us.store(a + extra_us, std::memory_order_relaxed);
            }
        }
        /* the beat the deck is playing now is heard ahead_ms later: that is the moment it
         * belongs to on Link's timeline */
        const auto at = deck_link->clock().micros() +
                        std::chrono::microseconds((long long)std::llround(in->ahead_ms * 1000.0));
        auto s = deck_link->captureAppSessionState();
        bool dirty = false;

        /* start/stop: sent when the decks start or all stop, and again when a peer joins (a
         * joining session drops it; sent at once and twice more as the sessions settle). An app
         * that stops its own transport while the deck plays is left alone until the decks change. */
        const std::size_t peers = deck_link->numPeers();
        const long long now_us = at.count();
        if (peers > last_peers) {
            resend_us[0] = now_us;
            resend_us[1] = now_us + 500000;
            resend_us[2] = now_us + 2000000;
        }
        last_peers = peers;
        bool resend = false;
        for (auto &r : resend_us)
            if (r && now_us >= r) {
                resend = true;
                r = 0;
            }
        if (in->playing != sent_playing || resend) {
            s.setIsPlaying(in->playing != 0, at);
            sent_playing = in->playing;
            dirty = true;
        }

        if (!in->playing) {
            if (dirty) deck_link->commitAppSessionState(s);
            avg_err = 0.0;
            jump_steps = 0;
            return;    /* nothing plays: hold the last tempo and bar */
        }
        if (in->bpm >= 20.0 && in->bpm <= 999.0 && std::fabs(s.tempo() - in->bpm) > 0.005) {
            s.setTempo(in->bpm, at);
            out->tempo = 1;
            dirty = true;
        }
        if (in->ph >= 0.0 && in->ph <= 1.0) {
            const double b = s.beatAtTime(at, kQuantum);
            /* with the bar known, line up the beat of the bar; without, only the beat */
            const double cycle = in->bar_beat >= 0 ? kQuantum : 1.0;
            const double want = in->bar_beat >= 0 ? in->bar_beat + in->ph : in->ph;
            const double d = wrap(want - b, cycle);
            out->err = d;
            jump_steps = std::fabs(d) >= kJump ? jump_steps + 1 : 0;
            avg_err += kAvgK * (d - avg_err);
            if (in->new_lead || jump_steps >= kJumpSteps || std::fabs(avg_err) > kAvgMax) {
                s.forceBeatAtTime(b + d, at, kQuantum);
                out->forced = 1;
                dirty = true;
                avg_err = 0.0;
                jump_steps = 0;
            }
        }
        if (dirty) deck_link->commitAppSessionState(s);
    } catch (...) {
    }
}

extern "C" int flowlink_audio_enable(int on, unsigned rate, double extra_ms)
{
    try {
        if (!deck_link || !deck_link->isEnabled()) return on ? -1 : 0;
        if (on) {
            for (int d = 0; d < kDecks; d++)
                if (!sends[d].sink.load()) {
                    sends[d].sink.store(
                        new ableton::LinkAudioSink(*deck_link, "Deck " + std::to_string(d + 1), kMaxSamples));
                }
            audio_rate.store(rate >= 8000 && rate <= 192000 ? rate : 0);
            extra_us = std::llround(extra_ms * 1000.0);
            audio_offset_us.store(ahead_us.load() + extra_us);
            for (auto &s : sends) {
                s.rate_seen.store(0.0);
                s.rate_used.store(0);     /* measured afresh: the first half second sends nothing */
                s.sent.store(0);
            }
            audio_gen.fetch_add(1);
            if (!deck_link->isLinkAudioEnabled()) deck_link->enableLinkAudio(true);
            audio_on.store(true);
        } else {
            audio_on.store(false);
            if (deck_link->isLinkAudioEnabled()) deck_link->enableLinkAudio(false);
        }
        return 0;
    } catch (...) {
        audio_on.store(false);
        return -1;
    }
}

extern "C" void flowlink_audio_send(int d, const float *lr, int frames)
{
    if (d < 0 || d >= kDecks || !lr || frames <= 0 || !audio_on.load(std::memory_order_acquire)) return;
    try {
        DeckSend &s = sends[d];
        ableton::LinkAudioSink *sink = s.sink.load(std::memory_order_acquire);
        if (!sink) return;
        const long long now = deck_link->clock().micros().count();
        const unsigned gen = audio_gen.load(std::memory_order_relaxed);
        if (gen != s.gen || now - s.last_us > kGapUs) {   /* (re)started or the tap paused */
            s.filter.reset();
            s.samples = 0.0;
            s.first_us = now;
            s.gen = gen;
        }
        s.last_us = now;
        /* the tap hands over a block once it is rendered: its last sample is "now" */
        const auto end = s.filter.sampleTimeToHostTime(s.samples + frames);
        s.samples += frames;
        if (s.samples == frames) s.first_frames = frames;   /* the first block ends at first_us */
        const long long span = now - s.first_us;
        const double measured = span > 0 ? (s.samples - s.first_frames) * 1e6 / (double)span : 0.0;
        if (span >= 2000000) s.rate_seen.store(measured, std::memory_order_relaxed);
        /* the rate sent: a forced one, else the tap's own, measured (kept across pauses until
         * there is a fresh measurement), so peers play it at the right speed on any hardware */
        unsigned rate = audio_rate.load(std::memory_order_relaxed);
        if (!rate) {
            rate = span >= kRateUs && measured >= 8000.0 && measured <= 200000.0
                       ? snap_rate(measured) : s.rate_used.load(std::memory_order_relaxed);
            if (!rate) return;                         /* the first half second: not known yet */
        }
        if (rate != s.rate_used.load(std::memory_order_relaxed)) s.rate_used.store(rate, std::memory_order_relaxed);

        ableton::LinkAudioSink::BufferHandle buf(*sink);
        if (!buf) return;                              /* nobody listens to this deck */
        const std::size_t n = 2 * (std::size_t)frames;
        if (buf.maxNumSamples < n) {
            sink->requestMaxNumSamples(n);
            return;
        }
        for (std::size_t i = 0; i < n; i++) {
            const float x = lr[i];
            buf.samples[i] = ableton::util::floatToInt16(x == x ? x : 0.0f);   /* NaN -> silence */
        }
        const auto begin = end - std::chrono::microseconds(std::llround(frames * 1e6 / rate)) +
                           std::chrono::microseconds(audio_offset_us.load(std::memory_order_relaxed));
        const auto st = deck_link->captureAppSessionState();
        if (buf.commit(st, st.beatAtTime(begin, kQuantum), kQuantum, (std::size_t)frames, 2, rate))
            s.sent.fetch_add(1, std::memory_order_relaxed);
    } catch (...) {
    }
}

extern "C" double flowlink_audio_rate_seen(int d)
{
    return d >= 0 && d < kDecks ? sends[d].rate_seen.load(std::memory_order_relaxed) : 0.0;
}

extern "C" unsigned flowlink_audio_rate_used(int d)
{
    return d >= 0 && d < kDecks ? sends[d].rate_used.load(std::memory_order_relaxed) : 0;
}

extern "C" unsigned long flowlink_audio_sent(int d)
{
    return d >= 0 && d < kDecks ? sends[d].sent.load(std::memory_order_relaxed) : 0;
}
