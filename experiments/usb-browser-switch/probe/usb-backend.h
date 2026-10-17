/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef USB_SWITCH_PROBE_BACKEND_H
#define USB_SWITCH_PROBE_BACKEND_H
#include <stdint.h>
#include "azmod-mod.h"
#define USP_FW_TAG "ep147-9e9454e357389ece"
#define USP_MAX_ROWS 16
#define USP_LABEL_SIZE 64

enum usp_result {
    USP_OK = 0, USP_BUSY = -1, USP_NOT_READY = -2,
    USP_OBSERVATION_ONLY = -3, USP_BAD_PORT = -4
};
enum usp_capture_status {
    USP_NO_CONTROLLER = 0, USP_ROWS_VALID = 1,
    USP_BAD_CONTROLLER = 2, USP_BAD_MODEL = 3, USP_BAD_BOUNDS = 4,
    USP_WRONG_THREAD = 5, USP_IDLE = 6
};
typedef struct usp_row {
    uint32_t kind;
    uint16_t media_id;
    uint8_t media_valid;
    uint8_t focused, source_active;
    char label[USP_LABEL_SIZE];
} usp_row;
typedef struct usp_snapshot {
    uint64_t serial, generation;
    uintptr_t controller, model, widget;
    unsigned process_id, timer_tid, source_tid, ctor_tid, destructor_tid;
    unsigned timer_calls, source_calls, ctor_calls, destructor_calls;
    int status, row_count, last_clicked_row, captured_by_ctor;
    usp_row rows[USP_MAX_ROWS];
} usp_snapshot;

/* Call exactly once from the owning mod's init, after other fallible setup.
 * A partial hook claim remains a pass-through until EP147 restarts. */
int usp_backend_init(const az_core_v1 *core, const char *mod_id);
/* Owner's tick keeps the observer alive. Missing ticks idle it within 2.5 s. */
void usp_backend_tick(void);
void usp_backend_enable(int enabled);
/* Copies values only, never native pointers dereferenced by the caller. */
int usp_backend_snapshot(usp_snapshot *out);
int usp_backend_request_observation(void);
/* Reserved port API, 1=USB1 / 2=USB2. This build ALWAYS refuses activation. */
int usp_backend_request_port(int port);
#endif
