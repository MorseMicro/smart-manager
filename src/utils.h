/**
 * Copyright 2023 Morse Micro
 * SPDX-License-Identifier: GPL-2.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#pragma once

#include <stddef.h>
#include <stdio.h>
#include <errno.h>
#include "logging.h"

/**
 * Gets a pointer to the container that contains the provided interface.
 *
 * The container must have a mmsm_backend_intf_t member called intf inside of
 * it, and ptr must point to an instance of mmsm_backend_intf_t. For example:
 *
 * typedef struct backend_hostapd_ctrl_t {
 *     mmsm_backend_intf_t intf;
 *     // etc.
 * }
 *
 * ... then convert from (mmsm_backend_intf_t *) to (backend_hostapd_ctrl_t *)
 * like this:
 *
 * backend_hostapd_ctrl_t *hostapd = get_container_from_intf(hostapd, intf)
 */
#define get_container_from_intf(container, ptr)                         \
    (typeof(container))(((void *)(ptr)) - offsetof(typeof(*container), intf))


/**
 * Return the pointer to the start of a container when a pointer within
 * the container is known
*/
#define container_of(ptr, type, member) ({\
        const typeof(((type *)0)->member) *__mptr = (const typeof(((type *)0)->member) *)(ptr);\
        (type *)( (char *)__mptr - offsetof(type, member) );})

/**
 * Called on assert failure to log error and exit. Do not call directly
 */
void mmsm_assert_failed(const char *cond, const char *func, int line);

/**
 * Asserts that condition x evaluates to true
 *
 * Prints out some useful information on assert failure and exits
 */
#define MMSM_ASSERT(_x)   do { if (!(_x)) mmsm_assert_failed(#_x, __func__, __LINE__); } while (0)

/**
 * Helper to mark the provided value as unused
 */
#define UNUSED(x) (void)(x)

/**
 * Helper to mark a structure as packed
 */
#define PACKED __attribute__((packed))

/**
 * Return the number of elements in an array
 */
#define ARRAY_SIZE(_a) (sizeof(_a) / sizeof((_a)[0]))

/* Macros to convert between frequency units */
#define MHZ_TO_KHZ(_freq) ((_freq) * 1000)
#define KHZ_TO_MHZ(_freq) ((_freq) / 1000)

/** Convert microseconds to seconds */
#define USEC_TO_SEC(x) ((x) / 1000000)

/** Convert time in time units to seconds. (1 TU = 1024us) */
#define TU_TO_SEC(_val) USEC_TO_SEC(((_val) * 1024))

/** MAC format string */
#define MACF "%02x:%02x:%02x:%02x:%02x:%02x"

/** MAC string for debug logs */
#define MACSTR(mac) (mac)[0], (mac)[1], (mac)[2], (mac)[3], (mac)[4], (mac)[5]

/** Maximum element */
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

/** Convert milliseconds to microseconds */
#define MSEC_TO_USEC(msec)      ((msec) * 1000U)
