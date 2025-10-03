/**
 * Copyright 2023 Morse Micro
 * SPDX-License-Identifier: GPL-2.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <libconfig.h>

#include "mmsm_data.h"


/**
 * Asserts that the statement x is MMSM_SUCCESS, or exits
 */
#define ASSERT_SUCCESS(x)                               \
    do {                                                \
        mmsm_error_code err = x;                        \
        if (err != MMSM_SUCCESS) {                      \
            printf("FATAL: "#x " failed: %d\n", err);   \
            exit(1);                                    \
        }                                               \
    } while (0);



/** Iterate on a data item with multiple values */
#define for_each_data_item(_iter, _head) \
    for (_iter = _head; _iter != NULL; _iter = _iter->mmsm_next)

/**
 * Dumps the given data item to stdout
 *
 * Use the macro MMSM_DUMP_DATA_ITEM instead, which uses the local file's log
 * level.
 */
void
_mmsm_dump_data_item(mmsm_data_item_t *result, int log_level);


/**
 * Dumps the nl80211 data item to stdout
 *
 * @param result The data to dump
 */
void
mmsm_dump_nl80211_data_item(mmsm_data_item_t *result);


/**
 * Finds a string key within the data item and returns the associated value
 *
 * @param head The list to search
 * @param key The key to search for
 *
 * @returns the value if found, otherwise @c NULL
 */
uint8_t *
mmsm_find_value_by_key(mmsm_data_item_t *head, const char *key);

static inline uint32_t mmsm_find_value_by_key_u32(mmsm_data_item_t *head, const char *key)
{
    uint32_t *val = (uint32_t *)mmsm_find_value_by_key(head, key);
    return val ? *val : -1;
}

static inline uint16_t mmsm_find_value_by_key_u16(mmsm_data_item_t *head, const char *key)
{
    uint16_t *val = (uint16_t *)mmsm_find_value_by_key(head, key);
    return val ? *val : -1;
}

static inline uint8_t mmsm_find_value_by_key_u8(mmsm_data_item_t *head, const char *key)
{
    uint8_t *val = mmsm_find_value_by_key(head, key);
    return val ? *val : -1;
}


/**
 * Finds a key within the data item and returns the associated data_item
 *
 * @param head The list to search
 * @param key The mmsm_key_t to search for
 *
 * @returns a pointer to the data item that includes this key
 */
mmsm_data_item_t *
mmsm_find_key(mmsm_data_item_t *head, const mmsm_key_t *key);


/**
 * Finds an integer key within the data item and returns the associated value
 *
 * @param head The list to search
 * @param key The key to search for
 *
 * @returns the value if found, otherwise @c NULL
 */
uint8_t *
mmsm_find_value_by_intkey(mmsm_data_item_t *head, uint32_t key);


/**
 * Returns the nth value in the list
 *
 * @param head The head of the list
 * @param n Zero-based index of which item to find
 *
 * @returns the value if found, otherwise @c NULL
 */
uint8_t *
mmsm_find_nth_value(mmsm_data_item_t *head, uint32_t n);


/**
 * Find the item by keys in nl80211 data list
 *
 * @param head The head of the list
 * @param layers of keys to be matched tree data structure
 *
 * @returns the value if found, otherwise @c NULL
 */
uint8_t *
mmsm_find_by_nested_intkeys(mmsm_data_item_t *head, ...);


/**
 * Checks if the given flag is set within the value of the given key.
 *
 * Flags are in the format as provided by hostapd ctrl, for example, with a
 * result in the format like:
 *
 *  result = {
 *    "flags": "[AUTH][CONNECTED]"
 *  }
 *
 *  mmsm_is_flag_set_in(result, "flags", "CONNECTED") == true
 *  mmsm_is_flag_set_in(result, "flags", "ASSOC") == false
 *
 *  @param result The result structure to look at
 *  @param key The key to look for the flags within
 *  @param flag The flag to look for
 *
 *  @returns @c true if the flag exists, or @c false
 */
bool
mmsm_is_flag_set_in(mmsm_data_item_t *result, const char *key, const char *flag);


/**
 * Frees the item and all children
 *
 * mmsm_data_item_free(NULL) returns with no action.
 *
 * @param item The item to free
 */
void
mmsm_data_item_free(mmsm_data_item_t *item);

/* config helpers */

/**
 * Config helper function to parse an integer. Will print an error message and increment
 * @ref error_counter on parse failure
 *
 * @param cfg - pointer to config_setting_t to parse from
 * @param config_str - constant string of the config setting to lookup
 * @param error_counter - pointer to error counter which is incremented if the setting can't
 *                        be found
 * @return Parsed integer, or 0 on failure
 */
int cfg_parse_int(struct config_setting_t *cfg, const char *config_str, int *error_counter);

/**
 * Config helper function to parse an integer. If the setting cant be found, will return
 * @ref on_fail
 *
 * @param cfg - pointer to config_setting_t to parse from
 * @param config_str - constant string of the config setting to lookup
 * @param on_fail - default value to return on parse failure
 * @return Parsed integer, or 0 on failure
 */
int cfg_parse_int_with_default(struct config_setting_t *cfg, const char * config_str, int on_fail);

/**
 * @brief Helper function to parse a bool. Will print an error message and increment
 * @ref error_counter on parse failure
 *
 * @param cfg - pointer to config_setting_t to parse from
 * @param config_str - constant string of the config setting to lookup
 * @param error_counter - pointer to error counter which is incremented if the setting can't
 *                        be found
 * @return parsed bool, or false on parse fail
 */
bool cfg_parse_bool(struct config_setting_t *cfg, const char *config_str, int *error_counter);

/**
 * @brief Helper function to parse a bool.
 *
 * @param cfg - pointer to config_setting_t to parse from
 * @param config_str - constant string of the config setting to lookup
 * @param on_fail - default value to return on parse failure
 * @return parsed bool, or @ref on_fail if parse fails
 */
bool cfg_parse_bool_with_default(
        struct config_setting_t *cfg, const char *config_str, bool on_fail);

/**
 * Config helper function to parse a string. Will print an error message and increment
 * @ref error_counter on parse failure
 *
 * @param cfg - pointer to config_setting_t to parse from
 * @param config_str - constant string of the config setting to lookup
 * @param error_counter - pointer to error counter which is incremented if the setting can't
 *                        be found
 * @return Parsed string, or 0 on failure
 */
const char *cfg_parse_string(
        struct config_setting_t *cfg, const char *config_str, int *error_counter);

/**
 * Config helper function to parse a string. If the setting cant be found, will return
 * @ref on_fail
 *
 * @param cfg - pointer to config_setting_t to parse from
 * @param config_str - constant string of the config setting to lookup
 * @param on_fail - default value to return on parse failure
 * @return Parsed string, or 0 on failure
 */
const char *cfg_parse_string_with_default(
        struct config_setting_t *cfg, const char *config_str, const char *on_fail);
