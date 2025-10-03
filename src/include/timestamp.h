/**
 * Copyright 2023 Morse Micro
 * SPDX-License-Identifier: GPL-2.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#pragma once

#include <stdio.h>
#include <stdint.h>

typedef struct timestamp_t
{
    uint16_t year;
    uint16_t month;
    uint16_t day;
    uint16_t hour;
    uint16_t minute;
    uint16_t second;
    uint16_t millisecond;
} timestamp_t;

/**
 * @brief Read a timestamp from an ISO string in the following format: "YYYY-MM-DDThh:mm:ss.SS"
 * where Y = year, M = month, D = day, h = hour, m = minute, s = second, S = millisecond
 *
 * @param str String to read
 * @param timestamp Timestamp to fill
 * @return true if successfully parsed, else false
 */
bool timestamp_from_iso_string(const char *str, timestamp_t *timestamp);

/**
 * @brief Write a timestamp to a file in ISO format: "YYYY-MM-DDThh:mm:ss.SS"
 *
 * @param file File to write to
 * @param timestamp timestamp to write
 */
void timestamp_write_to_file_as_iso(FILE* file, timestamp_t *timestamp);

/**
 * @brief Fill the timestamp structure with the current system time
 *
 * @param timestamp Timestamp to fill
 */
void timestamp_get(timestamp_t *timestamp);

/**
 * @brief Get the current system time in usecs
 *
 * @return current timestamp in usecs
 */
uint64_t get_timestamp_us();

/**
 * @brief Get the current system time in msecs
 *
 * @return current timestamp in msecs
 */
uint64_t get_timestamp_ms();
