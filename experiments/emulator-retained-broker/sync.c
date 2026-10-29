/* SPDX-License-Identifier: GPL-2.0-only */
#define _POSIX_C_SOURCE 200809L
#include "sync.h"

int mbrs_attr_init(pthread_mutexattr_t *attr)
{
    return pthread_mutexattr_init(attr);
}
int mbrs_attr_settype(pthread_mutexattr_t *attr, int type)
{
    return pthread_mutexattr_settype(attr, type);
}
int mbrs_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
    return pthread_mutex_init(mutex, attr);
}
int mbrs_mutex_destroy(pthread_mutex_t *mutex)
{
    return pthread_mutex_destroy(mutex);
}
int mbrs_attr_destroy(pthread_mutexattr_t *attr)
{
    return pthread_mutexattr_destroy(attr);
}
