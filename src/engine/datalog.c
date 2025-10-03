/**
 * Copyright 2023 Morse Micro
 * SPDX-License-Identifier: GPL-2.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include <time.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libconfig.h>

#include "datalog.h"
#include "logging.h"
#include "utils.h"
#include "timestamp.h"
#include "helpers.h"

/** Root datalog directory */
static char datalog_root[64] = "/var/log/smart_manager";

/** Datalog config settings */
static config_setting_t *dl_config;

void datalog_set_root_dir(const char *path)
{
    strncpy(datalog_root, path, sizeof(datalog_root) - 1);
    LOG_INFO("Datalog root directory set: %s\n", datalog_root);
}

void datalog_set_config_settings(config_setting_t *config)
{
    dl_config = config;
    datalog_set_root_dir(
        cfg_parse_string_with_default(config, "root_dir", datalog_root));
}

/**
 * @brief Check if the datalog is enabled based on the config setting previously passed in
 *
 * @note Datalogs will default to OFF unless explicitly set to enabled.
 *
 * @param name datalog name
 * @return true if enabled, else false
 */
static bool is_datalog_enabled(char *name)
{
    config_setting_t *member;

    if (!dl_config)
        return false;

    member = config_setting_get_member(dl_config, name);

    if (!member)
        return false;

    if (cfg_parse_bool_with_default(member, "enabled", false))
    {
        LOG_INFO("%s datalog enabled\n", name);
        return true;
    }

    return false;
}


struct datalog *datalog_create(char *name)
{
    struct datalog *dl;
    timestamp_t timestamp;
    char file_name[256];
    int ret;

    if (!is_datalog_enabled(name))
        return NULL;

    int status = mkdir(datalog_root, 0700);

    if (status == -1 && errno != EEXIST)
    {
        LOG_ERROR("Cannot create directory %s (%s)\n", datalog_root, strerror(errno));
        return NULL;
    }

    timestamp_get(&timestamp);
    ret = snprintf(file_name, sizeof(file_name), "%s/%04u_%02u_%02u_%02u_%02u_%02u/",
        datalog_root, timestamp.year, timestamp.month, timestamp.day, timestamp.hour,
        timestamp.minute, timestamp.second);

    if (ret < 0 || ret == sizeof(file_name))
    {
        LOG_ERROR("Error building datalog path\n");
        return NULL;
    }

    status = mkdir(file_name, 0700);

    if (status == -1 && errno != EEXIST)
    {
        LOG_ERROR("Cannot create directory %s (%s)\n", file_name, strerror(errno));
        return NULL;
    }

    dl = calloc(1, sizeof(*dl));
    if (!dl)
        return NULL;

    timestamp_get(&timestamp);
    snprintf(file_name + ret, sizeof(file_name) - ret, "%s.log", name);

    dl->fptr = fopen(file_name, "w");
    if (dl->fptr == NULL)
    {
        LOG_ERROR("Can't open data log file %s: %s\n",
                  file_name, strerror(errno));
        free(dl);
        return NULL;
    }
    return dl;
}

bool datalog_init_csv(struct datalog *dl, const char *heading)
{
    int num_fields = 1;
    const char *ptr = heading;

    if (!dl || !heading)
        return false;

    ptr = strchr(ptr, ',');

    while (ptr != NULL)
    {
        num_fields++;
        ptr = strchr((ptr + 1), ',');
    }

    dl->csv.n_fields = num_fields;

    fputs(heading, dl->fptr);
    fputc('\n', dl->fptr);
    fflush(dl->fptr);

    return true;
}

bool datalog_write_csv(struct datalog *dl, const char *fmt, ...)
{
    bool first = true;
    uint16_t n;

    if (!dl)
        return false;

    MMSM_ASSERT(dl->csv.n_fields != 0 && fmt != NULL);

    n = dl->csv.n_fields;

    va_list args;
    va_start(args, fmt);

    while (n-- > 0)
    {
        if (!first)
            fputc(',', dl->fptr);
        else
            first = false;

        switch (*fmt)
        {
            case 'u':
                fprintf(dl->fptr, "%u", va_arg(args, unsigned int));
                break;
            case 'd':
                fprintf(dl->fptr, "%d", va_arg(args, int));
                break;
            case 's':
                fprintf(dl->fptr, "%s", va_arg(args, char *));
                break;
            case 'S':
                fprintf(dl->fptr, "\"%s\"", va_arg(args, char *));
                break;
            case 'b':
                fprintf(dl->fptr, "%s", va_arg(args, int) ? "True" : "False");
                break;
            case 't':
                timestamp_write_to_file_as_iso(dl->fptr, va_arg(args, timestamp_t *));
                break;

            /* 0 means skip this field (aka arg not included)*/
            case '0':
                break;

            /* No more formatters. Just continue so we dont increment fmt */
            case '\0':
                continue;

            default:
                MMSM_ASSERT("Invalid CSV formatter\n");
        }

        fmt += 1;
    }

    va_end(args);

    fprintf(dl->fptr, "\n");
    fflush(dl->fptr);
    return true;
}

bool datalog_write_string(struct datalog *dl, const char *str, ...)
{
    timestamp_t timestamp;
    timestamp_get(&timestamp);

    if (!dl)
        return false;

    fprintf(dl->fptr, "%04u/%02u/%02u:%02u:%02u:%02u:%03u ",
        timestamp.year, timestamp.month, timestamp.day, timestamp.hour,
        timestamp.minute, timestamp.second, timestamp.millisecond);

    va_list args;
    va_start(args, str);
    vfprintf(dl->fptr, str, args);
    va_end(args);
    fflush(dl->fptr);
    return true;
}


bool datalog_write_data(struct datalog *dl, uint8_t *data, uint32_t size)
{
    timestamp_t timestamp;
    timestamp_get(&timestamp);

    if (!dl)
        return false;

    fprintf(dl->fptr, "%04u/%02u/%02u:%02u:%02u:%02u:%03u \n",
        timestamp.year, timestamp.month, timestamp.day, timestamp.hour,
        timestamp.minute, timestamp.second, timestamp.millisecond);
    for (int i = 0; i < size; i++)
    {
        if (i % 16 == 0)
            fprintf(dl->fptr, "\t");

        fprintf(dl->fptr, "%02x ", data[i]);

        if (i % 16 == 7)
            fprintf(dl->fptr, " ");
        else if (i % 16 == 15)
            fprintf(dl->fptr, "\n");
    }
    fprintf(dl->fptr, "\n");
    fflush(dl->fptr);
    return true;
}


bool datalog_close(struct datalog *dl)
{
    if (!dl)
    {
        return false;
    }
    if (!dl->fptr)
    {
        LOG_ERROR("Can't close data log file as File descriptor is NULL\n");
        return false;
    }

    if (fclose(dl->fptr) > 0)
    {
        LOG_ERROR("Can't close data log file: %s\n", strerror(errno));
        return false;
    }

    free(dl);

    return true;
}
