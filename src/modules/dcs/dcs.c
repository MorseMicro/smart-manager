/*
 * Copyright 2023 Morse Micro
 * SPDX-License-Identifier: GPL-2.0-or-later OR LicenseRef-MorseMicroCommercial
 */

/*
 * Dynamic channel selection main module
 */
#include <endian.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <inttypes.h>
#include <netlink/genl/genl.h>
#include <linux/nl80211.h>

#include "smart_manager.h"
#include "backend/backend.h"
#include "mmsm_data.h"
#include "helpers.h"
#include "datalog.h"
#include "utils.h"
#include "logging.h"
#include "list.h"
#include "backend/morsectrl/command.h"
#include "backend/morsectrl/vendor.h"

#include "dcs.h"
#include "algo.h"

/** Number of seconds to wait for OCS / CSA before timing out */
#define WAIT_TIMEOUT_SEC         (10)

/** Number of times to wait on hostapd to come up before giving up */
#define MAX_RETRIES               (10)

/* Number of times to retry getting the channel from hostapd, if receiving an invalid */
#define MAX_CHANNEL_UPDATE_RETRIES  (3)

/* Number of times to retry a failing channel measurement */
#define MAX_CHANNEL_MEASURE_RETRIES (3)

/*
 * Channel switch grace period, to account for post channel switch operations like beacon update,
 * bss change notification and any delay in receiving channel switch complete event.
 */
#define DCS_CHAN_SWITCH_GRACE_SECS (5)

/** Test-mode specific functions */
extern struct channel_measurement *get_channel_measurement_for_test(struct dcs *context,
        struct dcs_channel *channel);
extern int initialise_channels_for_test(struct dcs *context);
extern void dcs_test_free_all_samples(struct dcs *context);

/* Forward declarations */
static uint32_t calculate_new_prim_ch_center_freq(struct dcs *context, struct dcs_channel *channel);

/**
 * @brief Set a timespec object to be @ref sec seconds in the future
 *
 * @param ts Timespecs to set
 * @param sec Seconds in future to set it to
 */
static inline void set_timespec_for_future(struct timespec *ts, time_t sec)
{
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec += sec;
}

/**
 * @brief Update the current channel in the DCS context
 *
 * @param dcs_context dcs context structure
 */
static int update_current_channel(struct dcs *dcs_context)
{
    /* Get current channel, and its BW */
    mmsm_data_item_t *item;
    char *val;
    int32_t s1g_freq;
    int32_t s1g_bw;

    item = mmsm_request(dcs_context->hostapd_intf, "STATUS");

    if (!item)
    {
        LOG_ERROR("No status\n");
        goto err;
    }

    val = (char *)mmsm_find_value_by_key(item, "s1g_freq");
    if (!val)
    {
        LOG_ERROR("No S1G frequency\n");
        goto err;
    }
    s1g_freq = strtol(val, NULL, 10);

    /* Hostapd does not have a valid channel yet, try again.. */
    if (s1g_freq == -1)
        return -EAGAIN;

    val = (char *)mmsm_find_value_by_key(item, "freq");
    if (!val)
    {
        LOG_ERROR("No 5g frequency\n");
        goto err;
    }

    dcs_context->current_5g_freq = strtoul(val, NULL, 10);

    val = (char *)mmsm_find_value_by_key(item, "s1g_bw");
    if (!val)
    {
        LOG_ERROR("No op bandwidth\n");
        goto err;
    }

    s1g_bw = strtol(val, NULL, 10);

    val = (char *)mmsm_find_value_by_key(item, "s1g_prim_chwidth");
    if (!val)
    {
        LOG_ERROR("No primary channel width\n");
        goto err;
    }

    dcs_context->current_primary_ch_width = strtoul(val, NULL, 10);

    val = (char *)mmsm_find_value_by_key(item, "s1g_prim_1mhz_chan_index");
    if (!val)
    {
        LOG_ERROR("No primary channel index\n");
        goto err;
    }

    dcs_context->current_prim_1mhz_ch_index = strtoul(val, NULL, 10);

    val = (char *)mmsm_find_value_by_key(item, "beacon_int");
    if (!val)
    {
        LOG_ERROR("No beacon interval\n");
        goto err;
    }

    dcs_context->beacon_interval = strtol(val, NULL, 10);
    if (!dcs_context->beacon_interval)
    {
        LOG_ERROR("Invalid beacon interval\n");
        goto err;
    }

    val = (char *)mmsm_find_value_by_key(item, "dtim_period");
    if (!val)
    {
        LOG_ERROR("No DTIM period\n");
        goto err;
    }

    dcs_context->dtim_period = strtol(val, NULL, 10);
    if (!dcs_context->dtim_period)
    {
        LOG_ERROR("Invalid DTIM period\n");
        goto err;
    }

    for (int i = 0; i < dcs_context->num_chans; i++)
    {
        if (dcs_context->all_channels[i].ch.frequency_khz == s1g_freq &&
                dcs_context->all_channels[i].ch.bandwidth_mhz == s1g_bw)
        {
            LOG_INFO("Current channel is ch %u (freq: %u kHz)\n",
                dcs_context->all_channels[i].ch.channel_s1g,
                dcs_context->all_channels[i].ch.frequency_khz);
            dcs_context->current_channel = &dcs_context->all_channels[i];
            mmsm_data_item_free(item);
            return 0;
        }
    }

    LOG_ERROR(
        "Could not find new channel in channel map. freq: %d bw: %d prim_bw: %u prim_idx: %u\n",
            s1g_freq, s1g_bw, dcs_context->current_primary_ch_width,
            dcs_context->current_prim_1mhz_ch_index);

err:
    mmsm_data_item_free(item);
    dcs_context->current_channel = NULL;
    LOG_ERROR("Could not update current channel\n");
    return -EINVAL;
}

/**
 * @brief Poll and wait for a hostapd backend to be in a specific state.
 *
 * @param hostapd_intf hostapd backend
 * @param state state to wait for, as reported by 'STATUS' command
 * @param wait_sec seconds to wait between polling
 * @param num_retries number of times to retry before giving up
 * @return false if timed out or error, otherwise true
 */
static bool wait_for_hostapd_state(mmsm_backend_intf_t *hostapd_intf, const char *state,
        uint8_t wait_sec, uint8_t num_retries)
{
    char *val;
    mmsm_data_item_t *item;
    uint8_t retries = 0;
    const struct timespec sleep_time = {
        .tv_sec = wait_sec
    };

    while (retries++ < num_retries)
    {
        item = mmsm_request(hostapd_intf, "STATUS");

        if (!item)
            return false;

        val = (char *)mmsm_find_value_by_key(item, "state");

        if (strcmp(val, state) == 0)
        {
            mmsm_data_item_free(item);
            return true;
        }

        LOG_INFO("Hostapd is not in state %s yet (current state: %s), retry %d/%d\n", state, val,
                retries, num_retries);
        mmsm_data_item_free(item);
        nanosleep(&sleep_time, NULL);
    }
    return false;
}

/**
 * @brief Initialise the list of all available channels, and the current channel from a running chip
 *
 * @param dcs_context dcs context object
 * @return 0 if successful, otherwise error code
 */
static int initialise_channels_from_driver(struct dcs *dcs_context)
{
    mmsm_data_item_t *resp;
    struct morse_cmd_resp_get_available_channels *cfm;

    resp = mmsm_request(dcs_context->mctrl_intf, MORSE_CMD_ID_GET_AVAILABLE_CHANNELS, 0, 0, -1);

    if (!resp)
    {
        LOG_ERROR("Null response\n");
        return -ENODATA;
    }

    mmsm_data_item_to_mctrl_response(resp, &cfm);

    dcs_context->all_channels = calloc(cfm->num_channels, sizeof(*dcs_context->all_channels));
    if (!dcs_context->all_channels)
    {
        LOG_ERROR("Failed to allocate channels\n");
        return -ENOMEM;
    }

    struct dcs_channel *channel = dcs_context->all_channels;
    for (int i = 0; i < cfm->num_channels; i++)
    {
        memcpy(&channel->ch, &cfm->channels[i], sizeof(channel->ch));
        channel += 1;
    }

    dcs_context->num_chans = cfm->num_channels;

    mmsm_data_item_free(resp);

    if (update_current_channel(dcs_context))
    {
        LOG_ERROR("Couldn't find current channel\n");
        return -ENAVAIL;
    }
    return 0;
}

/**
 * @brief Determine if the primary channel is an available operating channel
 *
 * @param context DCS context, including primary bandwidth and index
 * @param channel Operating channel to be scanned
 * @return True if primary channel coincides with an enabled channel
 */
static bool primary_channel_is_available(struct dcs *context, struct dcs_channel channel)
{
    /* List of channels from driver contains all channels that are not disabled in the BCF */
    struct dcs_channel *all_channels = context->all_channels;
    uint32_t primary_freq_khz = calculate_new_prim_ch_center_freq(context, &channel);
    uint8_t primary_bw_mhz = context->current_primary_ch_width;

    for (int i = 0; i < context->num_chans; i++)
    {
        if (all_channels[i].ch.frequency_khz == primary_freq_khz &&
            all_channels[i].ch.bandwidth_mhz == primary_bw_mhz)
        {
            return true;
        }
    }
    LOG_INFO("Could not find available primary channel, skipping.\n"
             "Channel %u, primary BW %u MHz, primary channel index %u\n",
             channel.ch.channel_s1g,
             primary_bw_mhz,
             context->current_prim_1mhz_ch_index);
    return false;
}

/**
 * @brief Initialise the list of channels to scan based on our current channel.
 * Channels to scan are all available channels with the same BW as our current channel.
 *
 * @param dcs_context - dcs context structure
 */
static void init_scan_list(struct dcs *context)
{
    uint8_t bw = context->current_channel->ch.bandwidth_mhz;
    struct dcs_channel *chans = context->all_channels;

    list_reset(&context->scan.list);
    for (int i = 0; i < context->num_chans; i++)
    {
        LOG_DEBUG("Channel %u: %u kHz %u MHz BW loaded\n",
            context->all_channels[i].ch.channel_s1g,
            context->all_channels[i].ch.frequency_khz,
            context->all_channels[i].ch.bandwidth_mhz);

        if (chans[i].ch.bandwidth_mhz == bw && primary_channel_is_available(context, chans[i]))
        {
            LOG_INFO("Channel %u: %u kHz %u MHz BW added to scan list\n",
                context->all_channels[i].ch.channel_s1g,
                context->all_channels[i].ch.frequency_khz,
                context->all_channels[i].ch.bandwidth_mhz);
            list_add_tail(&context->scan.list, &chans[i].list);
        }
    }
}

/**
 * @brief Initialise all channel objects in the dcs context structure, depending on mode
 *
 * @param context dcs context
 * @return true if successfully initialised, else false
 */
static int initialise_channels(struct dcs *context)
{
    bool ret;

    if (context->test.enabled)
    {
        ret = initialise_channels_for_test(context);
    }
    else
    {
        ret = initialise_channels_from_driver(context);
    }

    if (ret == 0)
    {
        init_scan_list(context);
        LOG_INFO("Channels initialised\n");
    }

    return ret;
}

/**
 * @brief Calculate the new primary channel center frequency, based on the current primary channel
 * width and index
 *
 * @param context DCS context
 * @param channel The channel we want to switch to
 * @return Primary channel frequency in kHz
 */
static uint32_t calculate_new_prim_ch_center_freq(struct dcs *context, struct dcs_channel *channel)
{
    uint32_t bottom_freq = channel->ch.frequency_khz - (MHZ_TO_KHZ(channel->ch.bandwidth_mhz) / 2);
    uint32_t top_freq = channel->ch.frequency_khz + (MHZ_TO_KHZ(channel->ch.bandwidth_mhz) / 2);
    int prim_1mhz_idx = context->current_prim_1mhz_ch_index;
    uint32_t prim_ch_center_khz = 0;

    if (context->current_primary_ch_width == 1)
    {
        /* 1MHz channel center is offset by 500KHz from bottom freq + 1MHz prim index */
        prim_ch_center_khz = bottom_freq + MHZ_TO_KHZ(prim_1mhz_idx) + 500;
    }
    else if (context->current_primary_ch_width == 2)
    {
        /* 2MHz center is bottom freq + (floor(1mhz_idx / 2) * 2) + 1MHz. */
        prim_ch_center_khz = bottom_freq + (MHZ_TO_KHZ(prim_1mhz_idx / 2) * 2) + 1000;
    }
    else
    {
        MMSM_ASSERT(false);
    }

    MMSM_ASSERT(prim_ch_center_khz < top_freq);

    return prim_ch_center_khz;
}

/**
 * @brief Calculate the sec_channel_offset parameter to pass to the ECSA command, to preserve the
 * current primary channel index.
 *
 * @param context DCS context
 * @param channel The channel we are switching to
 * @return sec_channel_offset corresponding to channel conditions
 */
static int8_t calculate_sec_channel_offset(struct dcs *context, struct dcs_channel *channel)
{
    if (channel->ch.bandwidth_mhz == 1) {
        return 0;
    }

    /* Set to 1 if the prim index is even (0, 2, 4, 6) or -1 if its odd (1, 3, 5, 7) */
    return ((context->current_prim_1mhz_ch_index & 0x1) == 0) ? 1 : -1;
}

/**
 * @brief Callback function on ECSA done event
 *
 * @param param context parameter
 * @param intf nl80211 backend interface
 * @param result data item containing ECSA done event
 */
static void ecsa_done_callback(void *param, mmsm_backend_intf_t *intf, mmsm_data_item_t *result)
{
    struct dcs *context = (struct dcs *)param;
    mmsm_data_item_t *item;
    int ret;
    int retries = 0;
    const mmsm_key_t key = {
        .d.u32 = NL80211_ATTR_WIPHY_FREQ,
        .type = MMSM_KEY_TYPE_U32
    };
    struct timespec retry_sleep = {
        .tv_sec = 1,
        .tv_nsec = 0
    };

    MMSM_ASSERT(result->mmsm_key.d.u32 == NL80211_CMD_CH_SWITCH_NOTIFY);
    MMSM_ASSERT(pthread_mutex_lock(&context->csa.mutex) == 0);

    item = mmsm_find_key(result->mmsm_sub_values, &key);

    if (item)
    {
        context->csa.freq_5g = mmsm_data_item_get_val_u32(item);
        LOG_DEBUG("CSA Finished: %u\n", context->csa.freq_5g);
    }
    else
    {
        LOG_ERROR("Could not find frequency in CSA completed message\n");
        MMSM_DUMP_DATA_ITEM(result, LOG_LEVEL_DEBUG);
        context->csa.freq_5g = 0;
    }

    /* Hostapd might not be updated by the time we process the netlink event and ask for the new
     * channel. Retry if we get an invalid frequency
     */
    while (retries < MAX_CHANNEL_UPDATE_RETRIES)
    {
        ret = update_current_channel(context);
        if (ret != -EAGAIN)
            break;
        retries++;

        /* have to release mutex before going to sleep*/
        MMSM_ASSERT(pthread_mutex_unlock(&context->csa.mutex) == 0);
        nanosleep(&retry_sleep, NULL);
        MMSM_ASSERT(pthread_mutex_lock(&context->csa.mutex) == 0);
    }

    if (retries)
        LOG_DEBUG("Took %d tries to retrieve channel\n", retries);

    if (ret)
    {
        LOG_ERROR("Could not retrieve new channel\n");
        context->csa.freq_5g = 0;
    }

    if (context->csa.in_progress == false)
    {
        LOG_WARN("CSA was not in progress, but completed\n");
    }
    else
    {
        pthread_cond_signal(&context->csa.done);
    }

    MMSM_ASSERT(pthread_mutex_unlock(&context->csa.mutex) == 0);
}

/**
 * @brief Trigger a ECSA to switch to a new channel - returns when channel has switched
 *
 * @param context DCS context
 * @param channel Channel to switch to
 */
static int do_channel_switch(struct dcs *context, struct dcs_channel *channel)
{
    int ret = 0;
    mmsm_data_item_t *result;
    char ecsa_cmd[512];
    struct timespec wait_timeout;
    uint32_t chan_switch_time;

    MMSM_ASSERT(context);
    MMSM_ASSERT(channel);

    if (!context->config.csa_enabled)
    {
        return ret;
    }

    MMSM_ASSERT(pthread_mutex_lock(&context->csa.mutex) == 0);

    LOG_INFO("Triggering channel switch - new operating frequency: %u kHz, s1g chan: %u\n",
        channel->ch.frequency_khz, channel->ch.channel_s1g);

    snprintf(ecsa_cmd, sizeof(ecsa_cmd),
        "CHAN_SWITCH %d %u prim_bandwidth=%u sec_channel_offset=%d center_freq1=%u bandwidth=%u",
        context->config.dtims_for_csa, calculate_new_prim_ch_center_freq(context, channel),
        context->current_primary_ch_width, calculate_sec_channel_offset(context, channel),
        channel->ch.frequency_khz, channel->ch.bandwidth_mhz);

    result = mmsm_request(context->hostapd_intf, ecsa_cmd);
    if (!result)
    {
        LOG_ERROR("Failed to request channel switch\n");
        ret = -ENODATA;
        goto exit;
    }

    if (strcmp(result->mmsm_key.d.string, "OK") != 0)
    {
        LOG_ERROR("ECSA Failed: %s\n", result->mmsm_key.d.string);
        ret = -EBADR;
        goto exit;
    }

    /*
     * Calculate channel switch time using beacon interval, dtim period and
     * channel switch count.
     */
    chan_switch_time = TU_TO_SEC(context->beacon_interval * context->dtim_period *
        context->config.dtims_for_csa) + DCS_CHAN_SWITCH_GRACE_SECS;

    LOG_INFO(
        "channel switch time=%u seconds, beacon interval=%u, dtim period=%u, dtims for csa=%u\n",
        chan_switch_time, context->beacon_interval, context->dtim_period,
        context->config.dtims_for_csa);

    context->csa.in_progress = true;
    set_timespec_for_future(&wait_timeout, chan_switch_time);

    if (pthread_cond_timedwait(&context->csa.done, &context->csa.mutex, &wait_timeout)
        == ETIMEDOUT)
    {
        LOG_WARN("CSA has timed out\n");
        ret = -ETIMEDOUT;
        goto exit;
    }

    /* Sanity check have ended up where we were supposed to */
    if (context->csa.freq_5g == context->current_5g_freq)
    {
        LOG_INFO("Channel switched successfully\n");
    }
    else
    {
        LOG_WARN("CSA freq %d does not match current freq %d\n",
            context->csa.freq_5g, context->current_5g_freq);
        ret = -EPROTO;
    }

exit:
    context->csa.in_progress = false;
    context->csa.freq_5g = 0;
    mmsm_data_item_free(result);
    MMSM_ASSERT(pthread_mutex_unlock(&context->csa.mutex) == 0);

    return ret;
}

/**
 * @brief Perform a channel measurement on the chip.
 *
 * @param context DCS context object
 * @param channel The channel to scan
 * @return Pointer to channel measurement or NULL if measurement failed.
 *      The caller is responsible for freeing this object.
 */
static struct channel_measurement *get_channel_measurement_from_chip(
        struct dcs *context, struct dcs_channel *channel)
{
    mmsm_data_item_t *result;
    struct timespec wait_timeout;
    struct channel_measurement *meas = calloc(1, sizeof(*meas));

    if (!meas)
    {
        LOG_ERROR("Failed to allocate measurement\n");
        return NULL;
    }

    struct morse_cmd_req_ocs_driver req = {
        .subcmd = htole32(1),
        .config = {
            .op_channel_freq_hz = htole32(channel->ch.frequency_khz * 1000),
            .op_channel_bw_mhz = channel->ch.bandwidth_mhz,
            .pri_channel_bw_mhz = context->current_primary_ch_width,
            .pri_1mhz_channel_index = context->current_prim_1mhz_ch_index,
        }
    };

    /* Pass the allocated measurement object to the done callback (or NULL if error) */
    context->scan.result = meas;

    result = mmsm_request(context->mctrl_intf, MORSE_CMD_ID_OCS_DRIVER, sizeof(req), &req, -1);
    if (!result)
    {
        LOG_ERROR("No result\n");
        free(meas);
        meas = NULL;
        goto exit;
    }

    LOG_DEBUG("Measurement scheduled %u\n", context->scan.channel->ch.frequency_khz);

    mmsm_data_item_free(result);

    set_timespec_for_future(&wait_timeout, WAIT_TIMEOUT_SEC);

    /* Wait for the scan to complete (this will unlock the mutex while asleep) */
    if (pthread_cond_timedwait(&context->scan.done, &context->scan.mutex, &wait_timeout)
        == ETIMEDOUT)
    {
        LOG_ERROR("Measurement timed out\n");
        free(meas);
        meas = NULL;
    }

exit:

    return meas;
}

/**
 * @brief Get a channel measurement, depending on mode
 *
 * @param context DCS context object
 * @param channel The channel to scan
 * @return Pointer to channel measurement, or NULL if measurement failed.
 *          The caller is responsible for freeing this object if not NULL
 */
static struct channel_measurement *get_channel_measurement(
        struct dcs *context, struct dcs_channel *channel)
{
    struct channel_measurement *result;

    MMSM_ASSERT(pthread_mutex_lock(&context->scan.mutex) == 0);

    /* Make sure no scan is currently in progress. */
    MMSM_ASSERT(context->scan.result == NULL);

    context->scan.channel = channel;

    if (context->test.enabled)
    {
        result = get_channel_measurement_for_test(context, channel);
    }
    else
    {
        result = get_channel_measurement_from_chip(context, channel);
    }

    context->scan.result = NULL;
    MMSM_ASSERT(pthread_mutex_unlock(&context->scan.mutex) == 0);

    return result;
}

/**
 * @brief Validate that the vendor event is a morse OCS done event
 *
 * @param result data item containing the vendor event
 * @return true if OCS done event, else false
 */
static bool is_morse_ocs_done_vendor_event(mmsm_data_item_t *result)
{
    uint8_t *data;
    data = mmsm_find_by_nested_intkeys(result, NL80211_CMD_VENDOR, NL80211_ATTR_VENDOR_ID, -1);
    if (!data || *((uint32_t*)data) != MORSE_OUI)
        return false;

    data = mmsm_find_by_nested_intkeys(result, NL80211_CMD_VENDOR, NL80211_ATTR_VENDOR_SUBCMD, -1);
    if (!data || *((uint32_t*)data) != MORSE_VENDOR_EVENT_OCS_DONE)
        return false;

    return true;
}

/**
 * @brief Get the ocs done from vendor event object
 *
 * @param result data item containing the vendor event
 * @return OCS done event or NULL if no data
 */
static struct morse_cmd_evt_ocs_done* get_ocs_done_from_vendor_event(mmsm_data_item_t *result)
{
    return (struct morse_cmd_evt_ocs_done*) mmsm_find_by_nested_intkeys(result,
            NL80211_CMD_VENDOR, NL80211_ATTR_VENDOR_DATA, MORSE_VENDOR_ATTR_DATA, -1);
}

/**
 * @brief Callback on vendor event for when a channel scan is complete
 *
 * @param arg context argument
 * @param intf backend interface vendor event was received on
 * @param result The vendor event
 */
static void measurement_done_callback(void *arg, mmsm_backend_intf_t *intf, mmsm_data_item_t
                    *result)
{
    struct dcs *context = (struct dcs *)arg;
    struct channel_measurement *meas;
    struct morse_cmd_evt_ocs_done *ocs_done;

    if (!is_morse_ocs_done_vendor_event(result))
    {
        /* Not our event */
        return;
    }

    /* Grab the scan mutex */
    MMSM_ASSERT(pthread_mutex_lock(&context->scan.mutex) == 0);

    meas = context->scan.result;

    if (meas == NULL)
    {
        LOG_ERROR("Measurement completed after it timed out\n");
        MMSM_ASSERT(pthread_mutex_unlock(&context->scan.mutex) == 0);
        return;
    }

    ocs_done = get_ocs_done_from_vendor_event(result);
    if (!ocs_done)
    {
        /* Invalidate the result pointer to signal that the measurement failed */
        free(context->scan.result);
        context->scan.result = NULL;
        goto exit;
    }

    timestamp_get(&meas->sample_time);
    meas->metric = ocs_done->metric;
    meas->noise = ocs_done->noise;
    meas->time_listen_us = ocs_done->time_listen;
    meas->time_rx_us = ocs_done->time_rx;

exit:
    /* Signal our scan has finished */
    pthread_cond_signal(&context->scan.done);
    MMSM_ASSERT(pthread_mutex_unlock(&context->scan.mutex) == 0);
}

/**
 * @brief Thread function to trigger and evaluate channel measurements
 *
 * @param arg context variable
 * @return void* ignored
 */
static void *measurement_schedule_thread_fn(void *arg)
{
    struct dcs *context = (struct dcs *)arg;
    struct channel_measurement *meas;
    struct dcs_channel *channel = list_get_first_item(channel, &context->scan.list, list);
    int attempt_count = 0;

    datalog_init_csv(context->datalog,
        "time,frequency_khz,bandwidth_mhz,channel_s1g,metric,accumulated_score,"
        "rounds_as_best_for_channel,current_channel");

    while (1)
    {
        /* Wait for scan period */
        nanosleep(&context->config.sec_per_scan, NULL);

        meas = get_channel_measurement(context, channel);

        if (meas)
        {
            dcs_algo_ops_process_measurement(context, meas, channel);

            LOG_DEBUG("Measurement done (ch %u) - listen time: %"PRIu64", rx time: %"PRIu64", "
                        "noise: %d, metric: %u, accumulated score: %u\n",
                    channel->ch.channel_s1g, meas->time_listen_us, meas->time_rx_us, meas->noise,
                    meas->metric, channel->metric.accumulated_score);

            datalog_write_csv(context->datalog, "tuuuuuuu", &meas->sample_time,
                channel->ch.frequency_khz, channel->ch.bandwidth_mhz, channel->ch.channel_s1g,
                meas->metric, channel->metric.accumulated_score,
                channel->metric.rounds_as_best,
                context->current_channel->ch.channel_s1g);

            /* Free the measurement */
            free(meas);

            /* Get the next channel to scan */
            channel = list_get_next_item_or_null(channel, &context->scan.list, list);
            attempt_count = 0;
        }
        else
        {
            attempt_count++;
            LOG_WARN("Measurement failed on channel %u (attempt %d)\n",
                channel->ch.channel_s1g, attempt_count);

            if (attempt_count >= MAX_CHANNEL_MEASURE_RETRIES) {
                LOG_WARN("Removing channel %u from scan list\n", channel->ch.channel_s1g);
                struct dcs_channel *failing_channel = channel;

                /* Continue to next channel and remove old channel */
                channel = list_get_next_item_or_null(channel, &context->scan.list, list);
                attempt_count = 0;
                list_remove(&failing_channel->list);
            }
        }

        /* Full scan round completed. Evaluate the current best one. */
        if (channel == NULL)
        {
            struct dcs_channel *candidate_chan;

            LOG_DEBUG("Evaluating channels... \n");

            candidate_chan = dcs_algo_ops_evaluate_channels(context);

            if (candidate_chan && candidate_chan != context->current_channel)
            {
                if (!do_channel_switch(context, candidate_chan))
                {
                    dcs_algo_ops_post_csa_hook(context, candidate_chan);
                }
            }

            nanosleep(&context->config.sec_per_round, NULL);
            channel = list_get_first_item(channel, &context->scan.list, list);
        }
    }
    return NULL;
}

/**
 * @brief Initialise the thread responsible for scheduling and evaluating the channel measurements
 *
 * @param context DCS context object
 */
static void init_scan_thread(struct dcs *context)
{
    MMSM_ASSERT(context != NULL);
    MMSM_ASSERT(!list_is_empty(&context->scan.list));

    pthread_mutex_init(&context->scan.mutex, NULL);
    pthread_cond_init(&context->scan.done, NULL);

    MMSM_ASSERT(pthread_create(
            &context->scan.thread,
            NULL,
            measurement_schedule_thread_fn,
            context) == 0);

    if (!context->test.enabled)
    {
        mmsm_monitor_pattern(context->nl80211_intf, "", measurement_done_callback, context,
                NL80211_CMD_VENDOR, 0, -1);
    }
}

/**
 * @brief Apply config values from config file, and initialise the algorithm
 *
 * @param config libconfig struct
 * @param dcs DCS context object
 * @return 0 on success, else error code
 */
static int apply_configs_and_init_algo(config_setting_t *config, struct dcs *dcs)
{
    int errors = 0;
    int ret;

    MMSM_ASSERT(config != NULL);

    ret = dcs_algo_initialise(dcs, config);

    if (ret)
    {
        LOG_ERROR("Failed to initalise algorithm - %d\n", ret);
        dcs_algo_deinitialise(dcs);
        return -EINVAL;
    }

    /* Only disable if explicitly set to false, otherwise default to true */
    dcs->config.csa_enabled = cfg_parse_bool_with_default(config, "trigger_csa", true);
    dcs->config.dtims_for_csa = cfg_parse_int(config, "dtims_for_csa", &errors);

    return errors ? -EINVAL : 0;
}

struct dcs *dcs_create(config_t *config)
{
    int ret;
    struct dcs *context;
    char buff[128];
    const char *if_name;
    const char *hostapd_ctrl_path;
    int errors = 0;
    config_setting_t *cfg_root = config_root_setting(config);
    config_setting_t *test_settings;
    config_setting_t *hostapd_settings;
    config_setting_t *dcs_settings;

    context = calloc(1, sizeof(*context));

    LOG_INFO_ALWAYS("Initialising DCS\n");

    if (!context)
    {
        LOG_ERROR("Failed to allocate DCS context\n");
        return NULL;
    }

    if (!cfg_root)
    {
        LOG_ERROR("Can't find config root\n");
        goto err;
    }

    if_name = cfg_parse_string(cfg_root, "interface_name", &errors);

    hostapd_settings = config_lookup(config, "backends.hostapd");
    if (!hostapd_settings)
    {
        LOG_ERROR("Cant find settings for hostapd backend\n");
        goto err;
    }
    hostapd_ctrl_path = cfg_parse_string(hostapd_settings, "control_path", &errors);

    if (errors)
    {
        goto err;
    }

    snprintf(buff, sizeof(buff), "%s/%s", hostapd_ctrl_path, if_name);

    context->mctrl_intf = mmsm_backend_morsectrl_create(if_name);
    if (context->mctrl_intf == NULL)
    {
        LOG_ERROR("Failed to initialise morsectrl backend\n");
        goto err;
    }

    context->nl80211_intf = mmsm_backend_nl80211_create();
    if (context->nl80211_intf == NULL)
    {
        LOG_ERROR("Failed to initialise nl80211 backend\n");
        goto err;
    }

    context->hostapd_intf = mmsm_backend_hostapd_ctrl_create(buff);
    if (context->hostapd_intf == NULL)
    {
        LOG_ERROR("Failed to initialise hostapd backend\n");
        goto err;
    }

    test_settings = config_lookup(config, "dcs.test");

    if (test_settings)
    {
        context->test.enabled = cfg_parse_bool_with_default(test_settings, "enabled", false);
        if (context->test.enabled)
        {
            context->test.samples_filepath = cfg_parse_string(test_settings, "filepath", &errors);
        }
    }
    else
    {
        context->test.enabled = false;
    }

    LOG_INFO_ALWAYS("Waiting for hostapd to start\n");
    /* wait for hostapd to come up if not in test mode */
    if (!context->test.enabled &&
        !wait_for_hostapd_state(context->hostapd_intf, "ENABLED", WAIT_TIMEOUT_SEC, MAX_RETRIES))
    {
        goto err;
    }

    ret = initialise_channels(context);
    if (ret)
    {
        LOG_ERROR("Failed to initialise channels - %d\n", ret);
        goto err;
    }

    dcs_settings = config_lookup(config, "dcs");
    if (!dcs_settings)
    {
        LOG_ERROR("Could not find DCS settings\n");
        goto err;
    }

    ret = apply_configs_and_init_algo(dcs_settings, context);
    if (ret)
    {
        LOG_ERROR("Failed to apply configs - %d\n", ret);
        goto err;
    }

    context->datalog = datalog_create("dcs");

    init_scan_thread(context);

    /* CSA in progress condition */
    pthread_mutex_init(&context->csa.mutex, NULL);
    pthread_cond_init(&context->csa.done, NULL);

    /* Start a monitor to detect when the CSA completes */
    mmsm_monitor_pattern(context->nl80211_intf, "",
            ecsa_done_callback, context, NL80211_CMD_CH_SWITCH_NOTIFY, 0, -1);

    return context;

err:
    mmsm_backend_morsectrl_destroy(context->mctrl_intf);
    mmsm_backend_nl80211_destroy(context->nl80211_intf);
    mmsm_backend_hostapd_ctrl_destroy(context->hostapd_intf);
    free(context);
    return NULL;
}

void dcs_destroy(struct dcs *context)
{
    if (!context)
        return;

    pthread_cancel(context->scan.thread);

    /* wait for the thread to stop */
    pthread_join(context->scan.thread, NULL);

    if (context->test.enabled)
    {
        LOG_INFO("freeing samples\n");
        dcs_test_free_all_samples(context);
    }

    dcs_algo_deinitialise(context);

    mmsm_backend_hostapd_ctrl_destroy(context->hostapd_intf);
    mmsm_backend_nl80211_destroy(context->nl80211_intf);
    mmsm_backend_morsectrl_destroy(context->mctrl_intf);

    datalog_close(context->datalog);

    if (context->all_channels)
        free(context->all_channels);

    free(context);
}

const char * dcs_get_version(void)
{
    return MORSE_VERSION;
}
