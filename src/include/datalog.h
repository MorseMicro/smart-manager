/**
 * Copyright 2023 Morse Micro
 * SPDX-License-Identifier: GPL-2.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#pragma once

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <libconfig.h>

/**
 * @brief Data log object
 *
 */
struct datalog
{
    /**
     * Underlying file pointer for the datalog
     */
    FILE *fptr;

    /** CSV specific params */
    struct
    {
        /** Number of fields in each CSV line */
        uint16_t n_fields;
    } csv;
};

/**
 * Create data log file
 *
 * @param Log file name
 * @return datalog object
 */
struct datalog *datalog_create(char *name);

/**
 * Set the root directory for datalog files
 *
 * @param path path to root directory
 */
void datalog_set_root_dir(const char *path);

/**
 * @brief Set the config setting object for datalogs, used to enable per module datalogs.
 *
 * Config setting should be a config object with children in the form
 *      "<datalog name>/enabled = <bool>"
 *
 * @param config pointer to config setting
 */
void datalog_set_config_settings(config_setting_t *config);

/**
 * Write string to the file
 *
 * @param dl The datalogger
 * @param length The data length
 * @param str, ...
 */
bool datalog_write_string(struct datalog *dl, const char *str, ...);

/**
 * @brief Initialise a datalog to output CSV values.
 *
 * For example, to initialise a set of unsigned integers:
 *
 *      struct datalog *datalog = datalog_create("csv_file.csv");
 *      datalog_init_csv(datalog, "value_one,value_two,another_value");
 *
 * Write data as csv with calls to datalog_write_csv()
 *
 *      datalog_write_csv(datalog, "uuu", 5, 3, 9);
 *      datalog_write_csv(datalog, "uuu", 10, 15, 30);
 *      datalog_write_csv(datalog, "u0u", 1000, 1550);
 *
 * csv_file.csv ->
 *      value_one,value_two,another_value
 *      5,3,9
 *      10,15,30
 *      1000,,1550
 *
 * @param dl The datalogger
 * @param heading csv field headings
 * @return true Successfully set, else false
 */
bool datalog_init_csv(struct datalog* dl, const char *heading);

/**
 * @brief Write a csv line to the datalogger.
 *
 * CSV format string contains a sequence of characters which describe how the following arguments
 * should be formatted
 * Valid arguments are:
 *      u - unsigned int
 *      d - signed int
 *      s - string
 *      S - string wrapped in quotes (eg. "String")
 *      b - boolean (output: True or False literals)
 *      t - timestamp in ISO format (argument is pointer to timestamp_t)
 *      0 - this field is empty and is not included in the argument list
 *
 * Datalogger must have had its fields initialised prior to calling this function.
 *
 * @param dl The datalogger
 * @param fmt CSV format string
 * @param ...
 * @return true Successfully written, else false
 */
bool datalog_write_csv(struct datalog* dl, const char *fmt, ...);

/**
 * Write plain data of hex bytes to the datalog
 *
 * @param dl The datalogger
 * @param data The data to be written
 * @param size The length of the data
 */
bool datalog_write_data(struct datalog *dl, uint8_t *data, uint32_t size);

/**
 * Close the datalog
 *
 * @param dl The datalogger
 */
bool datalog_close(struct datalog *dl);
