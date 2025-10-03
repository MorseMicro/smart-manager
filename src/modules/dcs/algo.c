/*
 * Copyright 2023 Morse Micro
 * SPDX-License-Identifier: GPL-2.0-or-later OR LicenseRef-MorseMicroCommercial
 */

/*
 * DCS Algorithm common file.
 *
 * All supported / enabled algorithms should have a corresponding entry in algo_table
 */
#include <libconfig.h>
#include <string.h>

#include "utils.h"
#include "dcs.h"
#include "algo.h"


extern struct algo_ops ewma_ops;
extern struct algo_ops sample_and_hold_ops;

/** Table of supported algorithms */
static struct algo algo_table[] = {
    {
        .name = "ewma",
        .ops = &ewma_ops
    },
    {
        .name = "sample_and_hold",
        .ops = &sample_and_hold_ops
    }
};

int dcs_algo_initialise(struct dcs *context, config_setting_t *cfg)
{
    const char *algo_name;
    int errors = 0;

    algo_name = cfg_parse_string(cfg, "algo_type", &errors);

    if (errors)
    {
        LOG_ERROR("No algorithm specified\n");
        return -EINVAL;
    }

    for (int i = 0; i < ARRAY_SIZE(algo_table); i++)
    {
        if (strcmp(algo_table[i].name, algo_name) == 0)
        {
            LOG_INFO("Using algorithm: %s\n", algo_name);
            context->algo.ops = algo_table[i].ops;

            if (context->algo.ops->init)
            {
                return context->algo.ops->init(context, config_setting_get_member(cfg, algo_name));
            }
            return 0;
        }
    }

    LOG_ERROR("No matching algorithm for %s\n", algo_name);
    return -EINVAL;
}

void dcs_algo_deinitialise(struct dcs *context)
{
    if (context->algo.ops && context->algo.ops->deinit)
    {
        context->algo.ops->deinit(context);
    }
}

struct dcs_channel *dcs_algo_ops_evaluate_channels(struct dcs *context)
{
    if (context->algo.ops->evaluate_channels)
    {
        return context->algo.ops->evaluate_channels(context);
    }
    return NULL;
}

void dcs_algo_ops_process_measurement(struct dcs *context,
        struct channel_measurement *meas, struct dcs_channel *chan)
{
    if (context->algo.ops->process_measurement)
    {
        context->algo.ops->process_measurement(context, meas, chan);
    }
}

void dcs_algo_ops_post_csa_hook(struct dcs *context, struct dcs_channel *chan)
{
    if (context->algo.ops->post_csa_hook)
    {
        context->algo.ops->post_csa_hook(context, chan);
    }
}

struct dcs_channel *dcs_algo_get_channel_with_highest_score(struct dcs *context)
{
    struct dcs_channel *best = NULL;
    struct dcs_channel *next;
    list_entry_t *pos;

    list_for_each_entry(pos, &context->scan.list) {
        next = list_get_item(next, pos, list);
        if (!best)
        {
            best = next;
        }
        else if (next->metric.accumulated_score > best->metric.accumulated_score)
        {
            best = next;
        }
        /*
         * In the case were there are mulitple channels with the same score, in the real world
         * it makes no difference which channel we choose. For testing however, as we know that
         * typically the inteferers are close to our current channel, it makes specifying allowed
         * switch channels simpler if we always move to the furthest away.
         */
        else if (next->metric.accumulated_score == best->metric.accumulated_score)
        {
            /* See how far away the next and best channels are from the current chan */
            int32_t diff_next = context->current_channel->ch.frequency_khz - next->ch.frequency_khz;
            int32_t diff_best = context->current_channel->ch.frequency_khz - best->ch.frequency_khz;

            /* If the best is already the current channel, do nothing */
            if (diff_best == 0)
                continue;

            /* If the next chan is further away from the current (or is the current), choose that */
            if (abs(diff_next) > abs(diff_best) || diff_next == 0)
                best = next;
        }
    }
    return best;
}

void dcs_algo_reset_accumulated_scores(struct dcs *context, int reset_val)
{
    struct dcs_channel *next;
    list_entry_t *pos;

    list_for_each_entry(pos, &context->scan.list) {
        next = list_get_item(next, pos, list);

        next->metric.accumulated_score = reset_val;
        next->metric.n_samples = 0;
    }
}

void dcs_algo_free_context(struct dcs *context)
{
    free(context->algo.context);
}
