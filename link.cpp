/* SPDX-License-Identifier: GPL-3.0-only
 *
 * Link on Deck: Ableton Link from the deck itself (replaces the Mac's FLOW Link bridge).
 *
 * The lead deck (the playing deck that started last) leads the session:
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
 * Ableton Link (third_party/link) is GPL-2.0-or-later, so it may be built into this GPL-3.0
 * mod. Link runs two threads of its own (network and callbacks, normal priority, quiet when
 * idle); turning the row off disables Link, which closes its sockets.
 *
 * Everything below runs on flow-beat's worker thread, never on the UI, bus or audio paths, and
 * no exception leaves this file.
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
#include <ableton/Link.hpp>
#pragma GCC diagnostic pop

#include <chrono>
#include <cmath>
#include <cstring>

namespace {

constexpr double kQuantum = 4.0;     /* beats in a bar */
constexpr double kAvgK = 0.1;        /* averaging of small errors: about 50 ms at 5 ms steps */
constexpr double kAvgMax = 0.04;     /* averaged error that moves the session (~19 ms at 128) */
constexpr double kJump = 0.25;       /* an error this big for kJumpSteps steps in a row is a jump */
constexpr int kJumpSteps = 3;

ableton::Link *deck_link = nullptr;       /* made once; the core never unloads a mod */
double avg_err = 0.0;
int jump_steps = 0;
int sent_playing = -1;               /* the start/stop state last sent: -1 none yet */
std::size_t last_peers = 0;
long long resend_us[3];              /* when to send it again after a peer joins (0 = no) */

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
            deck_link = new ableton::Link(120.0);
            deck_link->enableStartStopSync(true);
        }
        if (deck_link->isEnabled() != (on != 0)) {
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
