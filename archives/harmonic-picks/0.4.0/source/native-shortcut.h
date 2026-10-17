/* SPDX-License-Identifier: GPL-3.0-only
 * Emit only the native BROWSE button through the SDK's mutable mixer input.
 * No LOAD, PLAY, filter toggle, pointer automation, library writes or firmware hooks.
 * Native browser/category/filter state stays owned by EP147.
 * request is shared; all remaining pulse state is owned by the one mixer bus thread.
 */
static int native_request;
static long native_requested_at;
static int native_phase;
static long native_until;
static int native_filter_ok;

static int native_request_browse(long now)
{
    /* UI thread is the only producer. Publish timestamp before the request. */
    if (__atomic_load_n(&native_request, __ATOMIC_ACQUIRE)) return 0;
    native_requested_at = now;
    __atomic_store_n(&native_request, 1, __ATOMIC_RELEASE);
    return 1;
}

static int native_frame_at(int bus, unsigned char *f, int len, long now)
{
    if (bus != AZ_BUS_MIXER_IN || !f || len < 98) return AZ_FRAME_OK;
    if (!__atomic_load_n(&native_request, __ATOMIC_ACQUIRE)) return AZ_FRAME_OK;
    /* Physical screen buttons, LOAD, BACK and knob clicks always win. */
    if (f[32] || f[33] || (f[31] & 1) ||
        now - native_requested_at > 1000 || now < native_requested_at) {
        native_phase = 0;
        __atomic_store_n(&native_request, 0, __ATOMIC_RELEASE);
        return AZ_FRAME_OK;
    }
    if (!native_phase) { native_phase = 1; native_until = now + 200; }
    if (now >= native_until) {
        native_phase = 0;
        __atomic_store_n(&native_request, 0, __ATOMIC_RELEASE);
        return AZ_FRAME_OK; /* Incoming idle frame releases our synthetic press. */
    }
    f[32] |= 0x40; /* BROWSE, never any LOAD bit */
    f[6] &= (unsigned char)~0x08; /* native panel press latch */
    return AZ_FRAME_OK;
}

static int native_frame(int bus, unsigned char *f, int len, void *user)
{
    (void)user;
    if (!__atomic_load_n(&native_request, __ATOMIC_ACQUIRE)) return AZ_FRAME_OK;
    return native_frame_at(bus, f, len, now_ms());
}
