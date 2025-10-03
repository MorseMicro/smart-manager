/**
 * Copyright 2023 Morse Micro
 * SPDX-License-Identifier: GPL-2.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include <sys/time.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

#include "smart_manager.h"
#include "timestamp.h"

/**
 * Get the current timestamp structure.
 */
void timestamp_get(timestamp_t *timestamp)
{
    time_t rawtime;
    struct tm tm_time;

    time(&rawtime);
    (void)localtime_r(&rawtime, &tm_time);

    timestamp->year = tm_time.tm_year + 1900;
    timestamp->month = tm_time.tm_mon + 1; /* Looks odd but Jan is month zero */
    timestamp->day = tm_time.tm_mday;
    timestamp->hour = tm_time.tm_hour;
    timestamp->minute = tm_time.tm_min;
    timestamp->second = tm_time.tm_sec;

    struct timeval te;
    gettimeofday(&te, NULL);
    timestamp->millisecond = (te.tv_usec / 1000) % 1000;
}

/**
 * Get the current UTC timestamp in usecs.
 */
uint64_t get_timestamp_us()
{
    struct timeval tv;

    gettimeofday(&tv, NULL);

    return (uint64_t)tv.tv_sec * 1000000ULL + tv.tv_usec;
}

/**
 * Get the current UTC timestamp in msecs.
 */
uint64_t get_timestamp_ms()
{
    struct timeval tv;

    gettimeofday(&tv, NULL);

    return (uint64_t)tv.tv_sec * 1000ULL + tv.tv_usec / 1000;
}

bool
timestamp_from_iso_string(const char *str, timestamp_t *timestamp)
{
    unsigned int vals[7];

    if (sscanf(str, "%04u-%02u-%02uT%02u:%02u:%02u.%03u",
            vals, vals + 1, vals + 2, vals + 3, vals + 4, vals + 5, vals + 6) != ARRAY_SIZE(vals))
    {
        return false;
    }

    timestamp->year = vals[0];
    timestamp->month = vals[1];
    timestamp->day = vals[2];
    timestamp->hour = vals[3];
    timestamp->minute = vals[4];
    timestamp->second = vals[5];
    timestamp->millisecond = vals[6];

    return true;
}

void timestamp_write_to_file_as_iso(FILE* file, timestamp_t *timestamp)
{
    fprintf(file, "%04u-%02u-%02uT%02u:%02u:%02u.%03u",
        timestamp->year, timestamp->month, timestamp->day, timestamp->hour,
        timestamp->minute, timestamp->second, timestamp->millisecond);
}
