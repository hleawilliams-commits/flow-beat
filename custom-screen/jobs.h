/* SPDX-License-Identifier: GPL-3.0-only
 * The mod's fourth and last thread_start: one slow-work thread that runs every registered job
 * about five times a second. Jobs do the disk and library work that must never run on the UI
 * thread (ANLZ files today; a library BPM lookup can be added the same way). Register from
 * init() before cs_jobs_start; a job runs to completion before the next one, so a slow job
 * only delays the others, never the screen. */
#ifndef CS_JOBS_H
#define CS_JOBS_H
#include "azmod-mod.h"

#define CS_JOBS_MAX 8
#define CS_JOBS_PERIOD_MS 200

typedef void (*cs_job_fn)(void *user);
static struct { cs_job_fn fn; void *user; } cs_jobs[CS_JOBS_MAX];
static int cs_njobs;

static int cs_job_add(cs_job_fn fn, void *user)
{
    if (!fn || cs_njobs >= CS_JOBS_MAX) return -1;
    cs_jobs[cs_njobs].fn = fn; cs_jobs[cs_njobs].user = user;
    cs_njobs++;
    return 0;
}

static void *cs_jobs_worker(void *arg)
{
    (void)arg;
    for (;;) {
        for (int i = 0; i < cs_njobs; i++) cs_jobs[i].fn(cs_jobs[i].user);
        gp_sleep_ms(CS_JOBS_PERIOD_MS);
    }
    return NULL;
}

/* 0 = running (or nothing to run). */
static int cs_jobs_start(const az_core_v1 *core, const char *id)
{
    static int started;   /* threads outlive an init: start it once per process */
    if (!cs_njobs || started) return 0;
    int rc = core->thread_start(id, "screen jobs", cs_jobs_worker, NULL);
    if (!rc) started = 1;
    return rc;
}
#endif
