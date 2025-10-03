/*
 * Copyright 2023 Morse Micro
 * SPDX-License-Identifier: GPL-2.0-or-later OR LicenseRef-MorseMicroCommercial
 */

/*
 * Helpers for parsing config settings
 */

#include <stdbool.h>
#include <string.h>
#include <libconfig.h>

#include "helpers.h"
#include "logging.h"

/**
 * @brief Build the fully qualified setting name from a config setting
 *
 * @param cfg config setting that was looked into
 * @param child Name of looked up setting
 * @param buff Buffer to store fully qualified setting name
 * @param buff_len length of buff
 * @return char* pointer to end of buffer. Used internally
 */
static char* build_full_setting_name(
        struct config_setting_t *cfg, const char *child, char *buff, int buff_len)
{
    struct config_setting_t *parent;
    char *new_buff = buff;

    if (!cfg)
        return buff;

    parent = config_setting_parent(cfg);

    /* If there is a parent, and its not the root, recurse into the parent */
    if (parent && !config_setting_is_root(parent))
    {
        new_buff = build_full_setting_name(parent, NULL, buff, buff_len);
    }

    /* Write this stage (if not root node) */
    if (!config_setting_is_root(cfg))
    {
        new_buff +=
            snprintf(new_buff, buff_len - (new_buff - buff), "%s.", config_setting_name(cfg));
    }

    if (child)
    {
        /* First invocation, write the setting we actually looked up */
        new_buff += snprintf(new_buff, buff_len - (new_buff - buff), "%s", child);
    }
    return new_buff;
}

/**
 * @brief Get the fully qualified setting name from a config setting. Replaces any '/' with '.'
 *
 * @param cfg config setting that was looked into
 * @param child Name of looked up setting
 * @param buff Buffer to store fully qualified setting name
 * @param buff_len length of buff
 */
static void get_full_setting_name(
        struct config_setting_t *cfg, const char *child, char *buff, int buff_len)
{
    if (!cfg)
    {
        strncpy(buff, child, buff_len);
        /* strncpy is a badly designed function, and will forget to leave a null if SRC is larger
         * than buff len.
         */
        buff[buff_len - 1] = '\0';
    }
    else
    {
        build_full_setting_name(cfg, child, buff, buff_len);
    }

    /* replace any '/' with '.' */
    while ((buff = strchr(buff, '/')) != NULL)
    {
        *buff++ = '.';
    }
}



int cfg_parse_int(struct config_setting_t *cfg, const char * config_str, int *error_counter)
{
    char buff[256];
    int out;

    MMSM_ASSERT(cfg != NULL);

    get_full_setting_name(cfg, config_str, buff, sizeof(buff));

    if (!config_setting_lookup_int(cfg, config_str, &out)) {
        LOG_ERROR("Error in reading config value: %s (line %d)\n", buff,
            config_setting_source_line(cfg));
        if (error_counter)
            (*error_counter)++;
        return 0;
    }
    LOG_INFO_ALWAYS("For %s found %d\n", buff, out);
    return out;
}


int cfg_parse_int_with_default(struct config_setting_t *cfg, const char * config_str, int on_fail)
{
    char buff[256];
    int out;

    get_full_setting_name(cfg, config_str, buff, sizeof(buff));

    if (!cfg)
        goto exit;

    if (config_setting_lookup_int(cfg, config_str, &out))
    {
        LOG_INFO_ALWAYS("For %s found %d\n", buff, out);
        return out;
    }

exit:
    LOG_INFO_ALWAYS("Could not find %s : defaulting to %d\n", buff, on_fail);
    return on_fail;
}


bool cfg_parse_bool(struct config_setting_t *cfg, const char *config_str, int *error_counter)
{
    char buff[256];
    int out;

    MMSM_ASSERT(cfg != NULL);

    get_full_setting_name(cfg, config_str, buff, sizeof(buff));

    if (!config_setting_lookup_bool(cfg, config_str, &out))
    {
        LOG_ERROR("Error in reading config value: %s (line %d)\n", buff,
            config_setting_source_line(cfg));
        if (error_counter)
            (*error_counter)++;
        return false;
    }

    LOG_INFO_ALWAYS("For %s found %s\n", buff, out ? "True" : "False");
    return !!out;
}


bool cfg_parse_bool_with_default(struct config_setting_t *cfg, const char *config_str, bool on_fail)
{
    char buff[256];
    int out;

    get_full_setting_name(cfg, config_str, buff, sizeof(buff));

    if (!cfg)
        goto exit;

    if (config_setting_lookup_bool(cfg, config_str, &out))
    {
        LOG_INFO_ALWAYS("For %s found %s\n", buff, out ? "True" : "False");
        return !!out;
    }

exit:
    LOG_INFO_ALWAYS("Could not find %s : defaulting to %s\n", buff, on_fail ? "True" : "False");
    return on_fail;
}



const char *cfg_parse_string(
        struct config_setting_t *cfg, const char *config_str, int *error_counter)
{
    char buff[256];
    const char *out;

    MMSM_ASSERT(cfg != NULL);

    get_full_setting_name(cfg, config_str, buff, sizeof(buff));

    if (!config_setting_lookup_string(cfg, config_str, &out)) {
        LOG_ERROR("Error in reading config value: %s (line %d)\n", buff,
            config_setting_source_line(cfg));
        if (error_counter)
            (*error_counter)++;
        return NULL;
    }
    LOG_INFO_ALWAYS("For %s found %s\n", buff, out);
    return out;
}


const char *cfg_parse_string_with_default(
        struct config_setting_t *cfg, const char *config_str, const char *on_fail)
{
    char buff[256];
    const char *out;

    get_full_setting_name(cfg, config_str, buff, sizeof(buff));

    if (!cfg)
        goto exit;

    if (config_setting_lookup_string(cfg, config_str, &out))
    {
        LOG_INFO_ALWAYS("For %s found %s\n", buff, out);
        return out;
    }

exit:
    LOG_INFO_ALWAYS("Could not find %s : defaulting to %s\n", buff, on_fail);
    return on_fail;
}
