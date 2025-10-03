/*
 * Copyright 2023 Morse Micro
 * SPDX-License-Identifier: GPL-2.0-or-later OR LicenseRef-MorseMicroCommercial
 */

/**
 * Sample and Hold DCS algorithm.
 *
 * Will simply accumulate the score for @ref rounds_for_eval number of rounds, then evaluate.
 * When evaluating, will switch to the channel with the highest score
 * (providing its score is @ref threshold_percentage % above the current channel)
 *
 * After each evaluation round, scores are reset.
 */
#include "dcs.h"
#include "algo.h"

/**
 * Sample and hold context
 */
struct sample_and_hold
{
    struct {
        /** Number of scan rounds until time to evaluate channels (ie. hold time) */
        int rounds_for_eval;
        /** Percentage above current channel to warrent a channel switch */
        uint8_t threshold_percentage;
    } config;

    /** Total number of scan rounds */
    int num_full_scans;
};

/**
 * @brief Initialise sample and hold DCS algorithm
 *
 * @param dcs DCS context
 * @param cfg Sample and hold configuration
 * @return 0 on success, else error code
 */
static int sample_hold_op_init(struct dcs *context, config_setting_t *cfg)
{
    int errors = 0;
    struct sample_and_hold *sh_ctx;

    if (!cfg)
    {
        LOG_ERROR("Could not find config settings for sample and hold\n");
        return -EINVAL;
    }

    sh_ctx = calloc(1, sizeof(*sh_ctx));

    if (!sh_ctx)
    {
        LOG_ERROR("Failed to allocate sample and hold context\n");
        return -ENOMEM;
    }

    context->algo.context = sh_ctx;

    sh_ctx->config.rounds_for_eval = cfg_parse_int(cfg, "rounds_for_eval", &errors);
    sh_ctx->config.threshold_percentage = cfg_parse_int(cfg, "threshold_percentage", &errors);
    context->config.sec_per_scan.tv_sec = cfg_parse_int(cfg, "sec_per_scan", &errors);
    context->config.sec_per_round.tv_sec = cfg_parse_int(cfg, "sec_per_round", &errors);

    return errors ? -EINVAL : 0;
}

/**
 * @brief Function called by DCS to evaluate channels
 *
 * @param context DCS context
 * @return channel to switch to, or NULL
 */
struct dcs_channel *sample_hold_op_evaluate_channels(struct dcs * context)
{
    struct sample_and_hold *sh_ctx = context->algo.context;
    struct dcs_channel *best = dcs_algo_get_channel_with_highest_score(context);

    /* increment the channels lifetime counter as best channel */
    best->metric.rounds_as_best++;
    sh_ctx->num_full_scans++;

    /* switch if current num rounds as best is past threshold */
    if (sh_ctx->num_full_scans % sh_ctx->config.rounds_for_eval == 0)
    {
        uint32_t threshold = dcs_algo_calculate_threshold(
            context->current_channel->metric.accumulated_score,
            sh_ctx->config.threshold_percentage);

        LOG_INFO("Channel eval - best: %d, avg metric: %u, accum metric: %u, accum threshold: %u\n",
            best->ch.channel_s1g, best->metric.accumulated_score / best->metric.n_samples,
            best->metric.accumulated_score, threshold);

        /** to ponder - current best or chan with highest rounds as best ? */
        if (best->metric.accumulated_score > threshold)
        {
            return best;
        }
        else
        {
            dcs_algo_reset_accumulated_scores(context, 0);
            return NULL;
        }
    }
    return NULL;
}

/**
 * @brief Function called by DCS to process a measurement
 *
 * @param context DCS context
 * @param meas Measurement to process
 * @param channel Channel this measurement was performed on
 */
static void sample_hold_op_process_measurement(struct dcs *context,
        struct channel_measurement *meas, struct dcs_channel *channel)
{
    UNUSED(context);
    channel->metric.accumulated_score += meas->metric;
    channel->metric.n_samples++;
}

/**
 * @brief Called after a channel switch has completed by DCS
 *
 * @param context DCS context
 * @param channel Channel that was switched into
 */
static void sample_hold_op_post_csa_hook(struct dcs *context, struct dcs_channel *chan)
{
    UNUSED(chan);
    dcs_algo_reset_accumulated_scores(context, 0);
}

/**
 * @brief op table for sample and hold DCS algorithm
 */
struct algo_ops sample_and_hold_ops = {
    .init = sample_hold_op_init,
    .deinit = dcs_algo_free_context,
    .evaluate_channels = sample_hold_op_evaluate_channels,
    .process_measurement = sample_hold_op_process_measurement,
    .post_csa_hook = sample_hold_op_post_csa_hook,
};
