/* SPDX-License-Identifier: GPL-2.0-only */
/* Separate production-wrapper smoke. Actual pthread lifecycle/lock calls,
 * synthetic value backend only; no injected synchronization results. */
#include "broker_owner.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

struct smoke_backend { struct mb_state state; unsigned snapshots, submits; };
static int snapshot(void *context, struct mb_state *out)
{
    struct smoke_backend *backend = context;
    backend->snapshots++;
    *out = backend->state;
    return 0;
}
static struct mb_backend_reply submit(void *context, enum mb_operation operation,
                                      const struct mb_command *command)
{
    struct smoke_backend *backend = context;
    (void)operation; (void)command;
    backend->submits++;
    return (struct mb_backend_reply){MB_OUTCOME_UNKNOWN, 0};
}
int main(void)
{
    struct smoke_backend backend = {.state = {
        .instance = {11, 23}, .run_id = 37, .epoch = 5, .sequence = 0,
        .flags = MB_REQUIRED_FLAGS, .crc_len = 96}};
    for (unsigned i=0; i<MB_FRAME_BYTES; i++) backend.state.frame[i]=(uint8_t)(i*19+7);
    struct mb_backend callbacks={&backend, snapshot, submit};
    struct mbro_owner *owner=NULL;
    struct mb_broker *broker=NULL;
    struct mbro_evidence journal={0};
    struct mb_status state={0};
    int created=mbro_create(&callbacks,&backend.state,&owner);
    int initialized=owner?mbro_initialize(owner):MB_ERR_INVALID;
    int validated=owner?mbro_validate(owner):MB_ERR_INVALID;
    int inspected=owner?mbro_inspect(owner,&journal):MB_ERR_INVALID;
    int exported=owner?mbro_get_broker(owner,&broker):MB_ERR_INVALID;
    int operational=broker?mb_inspect(broker,&state):MB_ERR_INVALID;
    bool ok=created==MB_OK && initialized==MB_OK && validated==MB_OK &&
        inspected==MB_OK && exported==MB_OK && operational==MB_OK &&
        journal.phase==MBRO_READY && journal.attr_held && journal.mutex_held &&
        journal.validation_matched && backend.snapshots==1 && !backend.submits &&
        !state.admission_open && !state.grant_issued && !state.unresolved &&
        state.next_request_id==1 && state.next_restore_id==1 &&
        !memcmp(state.expected.frame,backend.state.frame,MB_FRAME_BYTES);
    /* No exported broker pointer is used after this explicit quiescent point. */
    broker=NULL;
    struct mbro_dispose_report report={0}, again={0};
    int disposed=mbro_dispose(&owner,&report);
    int repeated=owner?MB_ERR_STATE:mbro_dispose(&owner,&again);
    ok=ok && disposed==MB_OK && repeated==MB_OK && !owner &&
        report.attempt_number==1 && report.completed && report.owner_released &&
        report.mutex_attempted && report.mutex_returned && !report.mutex_result &&
        report.attr_attempted && report.attr_returned && !report.attr_result &&
        !report.result && again.completed && !again.attempt_number &&
        !again.mutex_attempted && !again.attr_attempted && !again.owner_released &&
        backend.snapshots==1 && !backend.submits;
    if(!ok) fprintf(stderr,"FAIL production_sync_runs_real_pthread_lifecycle\n");
    printf("checks=1 failures=%u\n",ok?0u:1u);
    return ok?0:1;
}
