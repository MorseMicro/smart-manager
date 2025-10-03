/*
 * Copyright 2020 Morse Micro
 * SPDX-License-Identifier: GPL-2.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef MORSE_WIN_BUILD
#include <winsock2.h>
#include <windows.h>
#endif

#include "portable_endian.h"
#include "morse_commands.h"
#include "utils.h"

#define BIT(N) (1UL << (N))

#define mmsm_data_item_to_mctrl_response(_data_item, _resp_ptr) \
        do                                                  \
        {                                                   \
            *_resp_ptr = ((typeof(*_resp_ptr)) _data_item->mmsm_value); \
        } while (0);

/**
 * Command request format for MM driver
 */
struct PACKED request
{
    /** The request command starts with a header */
    struct morse_cmd_header hdr;
    /** An opaque data pointer */
    uint8_t data[0];
};

/**
 * Command response format for MM driver
 */
struct PACKED response
{
    /** The confirm header */
    struct morse_cmd_header hdr;
    /** The status of the of the command. @see morse_error_t */
    uint32_t status;
    /** An opaque data pointer */
    uint8_t data[0];
};
