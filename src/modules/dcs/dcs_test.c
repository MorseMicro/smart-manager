/**
 * Copyright 2023 Morse Micro
 * SPDX-License-Identifier: GPL-2.0-or-later OR LicenseRef-MorseMicroCommercial
 *
 * DCS functions which allow replaying DCS measurements back into the algorithm
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <netlink/genl/genl.h>
#include <linux/nl80211.h>

#include "smart_manager.h"
#include "backend/backend.h"
#include "datalog.h"
#include "utils.h"
#include "logging.h"
#include "list.h"
#include "backend/morsectrl/command.h"
#include "timestamp.h"

#include "dcs.h"

/**
 *
 * In order to have decent performance with ~100s to 1000s of samples,
 * channel measurements are stored in per channel lists.
 *
 * ch_meas_per_ch_head
 *            |
 *            v
 *         ch1_list -> ch1_sample1 -> ch1_sample2 -> ch1_sample3 -> ...
 *            |
 *            v
 *         ch2_list -> ch2_sample1 -> ch2_sample2 -> ch2_sample3 -> ...
 *            |
 *            v
 *           ...
 */
struct channel_measurement_list_item
{
    list_entry_t list;
    struct channel_measurement *meas;
};

struct channel_measurement_list_per_ch
{
    list_entry_t list;
    struct morse_cmd_channel_info ch;
    list_head_t channel_sample_list;
};

/**
 * @brief Add a channel measurement item to the corresponding sample list
 *
 * @param list sample root list head
 * @param item individual channel measurement list item to add
 * @param ch The channel this measurement corresponds to
 */
static void add_channel_measurement_item(
        list_head_t *list, struct channel_measurement_list_item *item,
        struct morse_cmd_channel_info *ch)
{
    list_entry_t *pos;
    struct channel_measurement_list_per_ch *per_freq;

    list_for_each_entry(pos, list)
    {
        per_freq = list_get_item(per_freq, pos, list);

        if (per_freq->ch.frequency_khz == ch->frequency_khz)
        {
            list_add_tail(&per_freq->channel_sample_list, &item->list);
            return;
        }
    }

    /* No list for this frequency, make a new one */
    per_freq = calloc(1, sizeof(*per_freq));
    MMSM_ASSERT(per_freq);
    list_reset(&per_freq->channel_sample_list);
    list_add_tail(&per_freq->channel_sample_list, &item->list);
    list_add_tail(list, &per_freq->list);
    memcpy(&per_freq->ch, ch, sizeof(*ch));
}

/**
 * @brief Pop a channel measurement from the sample lists
 *
 * @param per_ch_list sample root list head
 * @param freq_khz frequency to pop a measurement from
 * @return Channel measurement for @ref freq_khz. Caller is responsible for freeing this.
 */
struct channel_measurement *pop_channel_measurement(list_head_t *per_ch_list, uint32_t freq_khz)
{
    list_entry_t *pos;
    struct channel_measurement_list_per_ch *per_ch;
    struct channel_measurement_list_item *sample = NULL;
    struct channel_measurement *meas = NULL;

    list_for_each_entry(pos, per_ch_list)
    {
        per_ch = list_get_item(per_ch, pos, list);

        if (per_ch->ch.frequency_khz == freq_khz)
        {
            sample = list_get_first_item(sample, &per_ch->channel_sample_list, list);

            list_remove(&sample->list);
            meas = sample->meas;
            free(sample);

            /* Frequency has no more samples, so remove it from the list */
            if (list_is_empty(&per_ch->channel_sample_list))
            {
                list_remove(&per_ch->list);
                free(per_ch);
            }

            break;
        }
    }

    if (list_is_empty(per_ch_list))
        mmsm_halt();

    return meas;
}

/**
 * @brief Load channel samples from a CSV file.
 * CSV format is the same that is outputted by DCS module.
 *
 * @param context DCS context object
 * @param file Open file object to read from
 * @return initial S1G channel number
 */
static int load_channel_measurement_samples_from_file(struct dcs *context, FILE *file)
{
    char *buff = NULL;
    size_t buff_len = 0;
    ssize_t n_read;
    int score, bw_mhz, s1g_chan;
    list_head_t *per_ch_sample_list = &context->test.per_ch_sample_list;
    struct channel_measurement_list_item *item;
    struct morse_cmd_channel_info ch = {0};
    int dummy, tmp;
    int initial_chan = 0;
    char *ret;
    timestamp_t sample_time;

    list_reset(per_ch_sample_list);

    /* Consume first line */
    n_read = getline(&buff, &buff_len, file);

    while ((n_read = getline(&buff, &buff_len, file)) > 0)
    {
        /*
         * time,frequency_khz,bandwidth_mhz,channel_s1g,metric,accumulated_score,
         * rounds_as_best_for_channel,current_channel
         */
        if (!timestamp_from_iso_string(buff, &sample_time))
        {
            LOG_ERROR("Invalid ISO time in samples %s\n", buff);
            return -1;
        }

        ret = strchr(buff, ',');
        if (ret == NULL)
        {
            LOG_WARN("Could not find first occurrence in buffer\n");
            continue;
        }

        item = malloc(sizeof(*item));
        MMSM_ASSERT(item);
        item->meas = calloc(1, sizeof(*(item->meas)));
        MMSM_ASSERT(item->meas);
        item->meas->sample_time = sample_time;

        sscanf(ret, ",%d,%d,%d,%d,%d,%d,%d",
            &ch.frequency_khz, &bw_mhz, &s1g_chan, &score, &dummy, &dummy, &tmp);

        if (initial_chan == 0)
        {
            initial_chan = tmp;
        }

        ch.bandwidth_mhz = bw_mhz;
        ch.channel_s1g = s1g_chan;
        item->meas->metric = score;
        add_channel_measurement_item(per_ch_sample_list, item, &ch);
    }

    free(buff);
    return initial_chan;
}

/**
 * @brief Initialise @ref all_channels and @ref current_channel for the test mode
 *
 * @param context dcs channel context
 * @return 0 if successfully initialised, else error code
 */
int initialise_channels_for_test(struct dcs *context)
{
    list_entry_t *pos;
    struct channel_measurement_list_per_ch *entry;
    int i = 0;
    int initial_chan;

    FILE *file = fopen(context->test.samples_filepath, "r");
    if (file == NULL)
    {
        LOG_ERROR("Could not open file\n");
        return -ENFILE;
    }

    initial_chan = load_channel_measurement_samples_from_file(context, file);
    fclose(file);

    if (initial_chan <= 0)
    {
        LOG_ERROR("Failed loading samples\n");
        return -EINVAL;
    }

    list_reset(&context->scan.list);

    context->num_chans = list_size(&context->test.per_ch_sample_list);
    context->all_channels = calloc(context->num_chans, sizeof(*context->all_channels));
    list_for_each_entry(pos, &context->test.per_ch_sample_list)
    {
        entry = list_get_item(entry, pos, list);
        memcpy(&context->all_channels[i].ch, &entry->ch, sizeof(entry->ch));
        context->all_channels[i].metric.accumulated_score = 100;
        list_add_tail(&context->scan.list, &context->all_channels[i].list);

        if (entry->ch.channel_s1g == initial_chan)
        {
            context->current_channel = &context->all_channels[i];
        }

        i++;
    }

    if (!context->current_channel)
    {
        LOG_ERROR("No current channel (%d)\n", initial_chan);
        return -EINVAL;
    }
    LOG_INFO("Loaded samples. Initial channel %d\n", context->current_channel->ch.channel_s1g);
    context->current_primary_ch_width = 1;
    context->current_prim_1mhz_ch_index = 0;
    return 0;
}

/**
 * @brief Get a channel measurement for a corresponding frequency
 *
 * @param context dcs context object
 * @param freq_khz Centre frequency to measure
 * @param bw_mhz Bandwidth to measure
 * @return Measurement of channel. Caller is responsible for freeing this
 */
struct channel_measurement *get_channel_measurement_for_test(
        struct dcs *context, struct dcs_channel *channel)
{
    return pop_channel_measurement(&context->test.per_ch_sample_list, channel->ch.frequency_khz);
}

void dcs_test_free_all_samples(struct dcs *context)
{
    list_entry_t *entry, *temp;
    list_for_each_entry_safe(entry, temp, &context->test.per_ch_sample_list)
    {
        struct channel_measurement_list_per_ch *per_ch = list_get_item(per_ch, entry, list);
        list_entry_t *entry2, *temp2;
        list_for_each_entry_safe(entry2, temp2, &per_ch->channel_sample_list)
        {
            struct channel_measurement_list_item *sample = list_get_item(sample, entry2, list);
            list_remove(entry2);
            free(sample->meas);
            free(sample);
        }
        list_remove(entry);
        free(per_ch);
    }
}
