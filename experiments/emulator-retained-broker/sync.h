/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef EMULATOR_RETAINED_BROKER_SYNC_H
#define EMULATOR_RETAINED_BROKER_SYNC_H
#include <pthread.h>
/* Lifecycle-only boundary. Production calls pthread once and returns its raw
 * integer result. The offline harness supplies separate scripted definitions;
 * frozen broker operation lock/unlock calls are not replaced through this API. */
int mbrs_attr_init(pthread_mutexattr_t *attr);
int mbrs_attr_settype(pthread_mutexattr_t *attr, int type);
int mbrs_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr);
int mbrs_mutex_destroy(pthread_mutex_t *mutex);
int mbrs_attr_destroy(pthread_mutexattr_t *attr);
#endif
