/*
 * Copyright 2023 Morse Micro
 * SPDX-License-Identifier: GPL-2.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#pragma once

#include <errno.h>
#include <libconfig.h>
#include <pthread.h>

#include "backend/backend.h"
#include "smart_manager.h"
#include "backend/morsectrl/command.h"
#include "list.h"
#include "timestamp.h"

/**
 * @brief A single instance of a channel measurement. Captures the instantaneous quality of a
 * channel, at a point in time.
 *
 * Can come from the chip, or loaded as a sample from a file
 */
struct channel_measurement
{
    /** Time we received the measurement */
    timestamp_t sample_time;
    /** Throughput grading metric from the phy */
    uint8_t metric;
    /** Noise RSSI */
    int8_t noise;
    /** Listen time in us*/
    uint64_t time_listen_us;
    /** Time in RX in us*/
    uint64_t time_rx_us;
};

/**
 * @brief Per channel accumulated metric. Stores the information over time which defines the quality
 * of a channel
 */
struct channel_metric
{
    /** Accumulated score for the channel */
    uint32_t accumulated_score;
    /** Total number of samples taken so far */
    int n_samples;
    /** Number of scan rounds this channel has been considered the best */
    int rounds_as_best;
};

/**
 * @brief Channel object used within DCS module
 */
struct dcs_channel
{
    /** List entry used to link all scannable channels together */
    list_entry_t list;
    /** Parameters of the channel (freq, bw etc.) */
    struct morse_cmd_channel_info ch;
    /** Current channel metric. Defines the quality of the channel over time*/
    struct channel_metric metric;
};

/** DCS context structure */
struct dcs
{
    /** Backends */
    mmsm_backend_intf_t *mctrl_intf;
    mmsm_backend_intf_t *nl80211_intf;
    mmsm_backend_intf_t *hostapd_intf;

    /** Datalog used to dump channel measurements */
    struct datalog *datalog;

    /** Total number of channels in @ref all_channels */
    uint8_t num_chans;
    /** Array of all available channels for current country code */
    struct dcs_channel *all_channels;
    /** Points to the current operating channel*/
    struct dcs_channel *current_channel;
    /** Primary channel width */
    int current_primary_ch_width;
    /** 1MHz primary channel index */
    int current_prim_1mhz_ch_index;
    /** Current 5g frequency, used to validate CSA. This is required until we get S1G Linux */
    uint32_t current_5g_freq;
    /** AP DTIM period */
    uint8_t dtim_period;
    /**
     * AP Beacon interval together with above dtim period and channel switch count used
     * to calculate the channel switch time.
     */
    uint16_t beacon_interval;

    /** Scan related parameters */
    struct {
        /** List of channels available to scan (usually a subset of @ref all_channels)*/
        list_head_t list;
        /** Current channel being scanned */
        struct dcs_channel *channel;
        /** Scan thread reference */
        pthread_t thread;
        /** Mutex used to syncronise the scan thread and measurement done callback */
        pthread_mutex_t mutex;
        /** Condition used to syncronise the scan thread and measurement done callback */
        pthread_cond_t done;
        /**
         * Internal result pointer used to pass the scan result back
         * If NULL, no scan is in progress
         */
        struct channel_measurement *result;
    } scan;

    struct {
        struct algo_ops *ops;
        void *context;
    } algo;

    struct {
        /** Mutex used to syncronise the channel switch and its callback */
        pthread_mutex_t mutex;
        /** Condition used to syncronise the channel switch and its callback */
        pthread_cond_t done;
        /** Flag if CSA is in progress */
        bool in_progress;
        /** 5g frequency of the switched to channel */
        uint32_t freq_5g;
    } csa;

    struct {
        /** Time to wait between scanning each channel within a scan round */
        struct timespec sec_per_scan;
        /** Time to wait before starting another scan round */
        struct timespec sec_per_round;

        /** Number of DTIMs to include the channel switch announcement before switching */
        int dtims_for_csa;
        /** Trigger a CSA if we find a better channel */
        bool csa_enabled;
    } config;

    /** Test mode parameters */
    struct {
        /** Test mode is enabled */
        bool enabled;
        /** The path to the CSV file containing the channel measurement samples to use */
        const char *samples_filepath;
        /**
         * List of frequencies, containing lists of samples for each frequency.
         * See dcs_test.c for more info
         */
        list_head_t per_ch_sample_list;
    } test;
};

/**
 * @brief Create a Dynamic Channel Selection instance
 *
 * @param config config object
 * @return Initialised DCS context structure
 */
struct dcs *dcs_create(config_t *config);

/**
 * @brief Destroy a Dynamic channel selection instance
 *
 * @param context DCS context to destroy
 */
void dcs_destroy(struct dcs *context);
