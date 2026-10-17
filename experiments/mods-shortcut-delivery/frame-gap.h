/* SPDX-License-Identifier: GPL-3.0-only
 * Test transport adapter, never a production navigation implementation.
 * Suppress delivery to the shortcut for1250ms after each accepted touch, then
 * let its unchanged deadline logic inspect the first delayed frame. */
#ifndef MODS_TEST_FRAME_GAP_H
#define MODS_TEST_FRAME_GAP_H
static int delivery_gap_blocks(int pending, long requested_ms, long now_ms)
{
    return pending && now_ms >= requested_ms && now_ms-requested_ms < 1250;
}
#endif
