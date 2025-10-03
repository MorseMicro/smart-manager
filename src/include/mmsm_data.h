/**
 * Copyright 2023 Morse Micro
 * SPDX-License-Identifier: GPL-2.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "utils.h"

/**
 * Identifies the type of data item in a mmsm data item key.
 */
typedef enum mmsm_key_type_t
{
    MMSM_KEY_TYPE_U32,
    MMSM_KEY_TYPE_STRING,
} mmsm_key_type_t;


/**
 * The key which is used to identify a data item in a list of many
 */
typedef struct mmsm_key_t
{
    /** The type of the data stored in the below union */
    mmsm_key_type_t type;
    union {
        /** The data as a string. Valid if type == MMSM_KEY_TYPE_STRING */
        char *string;
        /** The data as a uint32_t. Valid if type == MMSM_KEY_TYPE_U32 */
        uint32_t u32;
    } d;
} mmsm_key_t;


/**
 * Contains data for a series of data items that can be passed around smart
 * manager.
 *
 * This struct acts as a generic form of data transfer and can be used to
 * represent a number of different data structures.
 *
 * It's generally expected that the user of this data structure accesses it in a
 * way that depends on what they expect to be contained within it. The data that
 * is contained is context-sensitive, depending on the command that was sent,
 * and on what backend it was sent on.
 */
typedef struct mmsm_data_item_t
{
    /** The key which identifies the item. May be NULL if these data items are
     * more of a list structure */
    mmsm_key_t mmsm_key;

    /** The value being encoded. Should be accessed using the relevant helper
     * functions */
    uint8_t *mmsm_value;

    /** The length of mmsm_value. Take care that this is the actual data
     *  length so that if the mmsm_value is a string, this must include
     *  the terminating null in the length - so you cannot use the output
     *  of strlen to set this up you must add 1. */
    uint32_t mmsm_value_len;

    /** Contains a sub-list of values for nested data items */
    struct mmsm_data_item_t *mmsm_sub_values;

    /** The next value in this list of data items */
    struct mmsm_data_item_t *mmsm_next;
} mmsm_data_item_t;

static inline mmsm_data_item_t *mmsm_data_item_alloc(void)
{
    mmsm_data_item_t *item = calloc(1, sizeof(*item));
    return item;
}

static inline mmsm_data_item_t *mmsm_data_item_alloc_next(mmsm_data_item_t *item)
{
    MMSM_ASSERT(item->mmsm_next == NULL);
    item->mmsm_next = mmsm_data_item_alloc();
    return item->mmsm_next;
}

static inline mmsm_data_item_t *mmsm_data_item_alloc_sub_value(mmsm_data_item_t *item)
{
    MMSM_ASSERT(item->mmsm_sub_values == NULL);
    item->mmsm_sub_values = mmsm_data_item_alloc();
    return item->mmsm_sub_values;
}

static inline void mmsm_data_item_set_key_u32(mmsm_data_item_t *item, uint32_t key)
{
    item->mmsm_key.type = MMSM_KEY_TYPE_U32;
    item->mmsm_key.d.u32 = key;
}

static inline void mmsm_data_item_set_key_str(mmsm_data_item_t *item, const char *str)
{
    item->mmsm_key.type = MMSM_KEY_TYPE_STRING;
    item->mmsm_key.d.string = strdup(str);
}

static inline void mmsm_data_item_set_val_u32(mmsm_data_item_t *item, uint32_t val)
{
    item->mmsm_value_len = sizeof(val);
    item->mmsm_value = calloc(1, item->mmsm_value_len);
    memcpy(item->mmsm_value, &val, item->mmsm_value_len);
}

static inline void mmsm_data_item_set_val_bytes(mmsm_data_item_t *item, uint8_t *buff, size_t len)
{
    item->mmsm_value_len = len;
    if (len)
    {
        item->mmsm_value = calloc(1, item->mmsm_value_len);
        memcpy(item->mmsm_value, buff, item->mmsm_value_len);
    }
}

static inline void mmsm_data_item_set_val_string(mmsm_data_item_t *item, const char *str)
{
    item->mmsm_value = (uint8_t *)strdup(str);
    item->mmsm_value_len = strlen(str) + 1;
}

static inline uint32_t mmsm_data_item_get_val_u32(mmsm_data_item_t *item)
{
    uint32_t val;
    memcpy(&val, item->mmsm_value, sizeof(val));
    return val;
}

static inline const char *mmsm_data_item_get_val_string(mmsm_data_item_t *item)
{
    return (const char *) item->mmsm_value;
}

static inline uint32_t mmsm_data_item_get_val_len(mmsm_data_item_t *item)
{
    return item->mmsm_value_len;
}

/**
 *
 * @enum    mmsm_error_code
 *
 * @brief   enum for the error codes returned by mmsm functions
 **/
typedef enum mmsm_error_code {
    MMSM_SUCCESS = 0,
    MMSM_COMMAND_FAILED,
    MMSM_UNKNOWN_ERROR,
} mmsm_error_code;
