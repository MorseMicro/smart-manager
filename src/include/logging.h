/**
 * Copyright 2023 Morse Micro
 * SPDX-License-Identifier: GPL-2.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <libconfig.h>

#include "timestamp.h"

#define LOG_COLOUR_ENABLED

enum log_levels {
    LOG_LEVEL_NONE  = 0,
    LOG_LEVEL_ERROR = 1,
    LOG_LEVEL_WARN  = 2,
    LOG_LEVEL_INFO  = 3,
    LOG_LEVEL_DEBUG = 4,
    LOG_LEVEL_VERBOSE  = 5,

    LOG_LEVEL_MAX
};


/* Set our global log level, allowing local log levels to take precedence. */
#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_ERROR
#endif

/* Use milliseconds for the log time. */
#define LOG_TIME() (mmsm_get_run_time_ms())

unsigned int mmsm_get_log_colours_enabled(void);

unsigned int mmsm_get_log_level(void);

static const char *log_colours_end[2] __attribute__((unused)) = {
    "",
    "\x1b[0m"
};

/* Add fancy colours to the logs, if enabled. */
static const char *log_colours_start[2][LOG_LEVEL_MAX] __attribute__((unused)) = {
    {
        "",
        "ERR",
        "WRN",
        "INF",
        "DBG",
        "VBS",
    },
    {
        "",
        "\x1b[31mERR",
        "\x1b[33mWRN",
        "\x1b[34mINF",
        "\x1b[0mDBG",
        "\x1b[35mVBS",
    }
};

#define LOG_PREFIX(level) \
    do {\
        timestamp_t _ts; \
        timestamp_get(&_ts); \
        printf("%s  %4u-%02u-%02u %02u:%02u:%02u %s %s", \
            log_colours_start[mmsm_get_log_colours_enabled()][level], \
            _ts.year, _ts.month, _ts.day, \
            _ts.hour, _ts.minute, _ts.second, \
            LOG_FILENAME, \
            log_colours_end[mmsm_get_log_colours_enabled()]); \
    } while (0)


#define LOG(level, ...)                                     \
    do {                                                    \
        LOG_PREFIX(level);                                  \
        printf(__VA_ARGS__);                                \
        fflush(stdout);                                     \
    } while (0)


#define LOG_NP(level, ...) printf(__VA_ARGS__)


#define LOG_VAR(level, debug_level, ...)                            \
    do {                                                            \
        if ((debug_level) >= (level))                               \
            LOG(level, __VA_ARGS__);                                \
    } while (0)


#define LOG_VAR_NP(level, debug_level, ...)     \
    do {                                        \
        if ((debug_level) >= (level))           \
            LOG_NP(__VA_ARGS__);                \
    } while (0)


#define LOG_ERROR(...) LOG_VAR(LOG_LEVEL_ERROR, mmsm_get_log_level(), __VA_ARGS__)
#define LOG_ERROR_NP(...) LOG_VAR_NP(LOG_LEVEL_ERROR, mmsm_get_log_level(), __VA_ARGS__)

#define LOG_WARN(...) LOG_VAR(LOG_LEVEL_WARN, mmsm_get_log_level(), __VA_ARGS__)
#define LOG_WARN_NP(...) LOG_VAR_NP(LOG_LEVEL_WARN, mmsm_get_log_level(), __VA_ARGS__)

#define LOG_INFO(...) LOG_VAR(LOG_LEVEL_INFO, mmsm_get_log_level(), __VA_ARGS__)
#define LOG_INFO_NP(...) LOG_VAR_NP(LOG_LEVEL_INFO, mmsm_get_log_level(), __VA_ARGS__)

#define LOG_DEBUG(...) LOG_VAR(LOG_LEVEL_DEBUG, mmsm_get_log_level(), __VA_ARGS__)
#define LOG_DEBUG_NP(...) LOG_VAR_NP(LOG_LEVEL_DEBUG, mmsm_get_log_level(), __VA_ARGS__)

#define LOG_VERBOSE(...) LOG_VAR(LOG_LEVEL_VERBOSE, mmsm_get_log_level(), __VA_ARGS__)
#define LOG_VERBOSE_NP(...) \
    LOG_VAR_NP(LOG_LEVEL_VERBOSE, mmsm_get_log_level(), __VA_ARGS__)


#define LOG_INFO_ALWAYS(...) LOG(LOG_LEVEL_INFO, __VA_ARGS__)

#define LOG_DATA(level, data, size)                                   \
    do {                                                              \
        if (mmsm_get_log_level() >= (level)) {                            \
            mmsm_dump_data(data, size);                               \
            fflush(stdout);                                           \
        }                                                             \
    } while (0)


/**
 * Dumps the given data item to the log
 *
 * @param result The data to dump
 * @param level The log level to dump the data at
 */
#define MMSM_DUMP_DATA_ITEM(result, level) do       \
    {                                               \
        if (mmsm_get_log_level() >= (level)) {          \
            _mmsm_dump_data_item(result, level);    \
            fflush(stdout);                         \
        }                                           \
    } while (0)


/**
 * Initialize program running time. It is to be called at begginning of
 * the program starts.
 */
void
mmsm_init_time(void);

/**
 * @brief Set log settings from config file
 *
 * @param cfg logging config setting
 */
void mmsm_set_log_config(config_setting_t *cfg);

/**
 * Set the current log level
 *
 * @param level Runtime log level
 */
void mmsm_set_log_level(unsigned int level);

/**
 * @brief Set whether to use colours in the log
 *
 * @param colour True to use colours, else false
 */
void mmsm_set_log_colours(bool colour);

/**
 * Get program's current run time in milliseconds.
 *
 *  @returns current run time in milliseconds
 */
uint32_t
mmsm_get_run_time_ms(void);


/**
 * Dump the packet data.
 *
 * @param length The data length
 * @param data The data to dump
 */
void
mmsm_dump_data(uint8_t *data, uint32_t size);
