/*
 * Copyright 2023 Morse Micro
 * SPDX-License-Identifier: GPL-2.0-or-later OR LicenseRef-MorseMicroCommercial
 */

/**
 * Exponential weighted moving average DCS algorithm
 *
 * Averages subsequent scores using an EWMA function. Evaluates channels after each scan round.
 * Will switch channels if the current channel is not the best channel for @ref rounds_for_csa
 * rounds in a row.
 * If the current channel is the best for the round, the @ref rounds_for_csa count will be reset.
 * If the current channel is within @ref threshold_percentage of the best channel,
 * @ref rounds_for_csa count will stay the same.
 *
 * @ref ewma_alpha is the 'smoothing' coefficent, and represents how heavily we bias the current
 * measurement, vs the last sum. It takes a range of 1 - 100, with 1 being the most smooth
 * (99% history), and 100 being the least smooth (no history)
 */
#include <stdlib.h>
#include "helpers.h"
#include "dcs.h"
#include "algo.h"

#define EWMA_ALPHA_MIN          (1)
#define EWMA_ALPHA_MAX          (100)

/* Initialise metric at max */
#define METRIC_INIT_VALUE       (100)

/** Context for EWMA algorithm */
struct ewma_context
{
    struct {
        /** Alpha, or smoothing co-efficent, of EWMA function */
        uint8_t ewma_alpha;
        /**
         * Percentage of score a candidate channel must be above the best channel to be considered
         * the new best
         */
        uint8_t threshold_percentage;
        /**
         * Number of consecutive scan rounds if the best channel is not the current channel to
         * trigger a channel switch, or 0 to disable channel switching
         */
        int rounds_for_csa;
    } config;

    /** Number of consecutive scan rounds there has been a better channel */
    int rounds_with_a_better_channel;
};

/**
 * @brief Exponential Weighted Moving Average function.
 *
 *          S[t] = a*X[t] + (1-a)*S[t-1]
 *
 * @param alpha EWMA weight between 1-100, with 100 being least smooth and 1 being the most smooth
 * @param new_score New sample to apply to EWMA, X[t]
 * @param last_score Last EWMA value, S[t-1]
 * @return uint8_t computed new EWMA value, S[t]
 */
static uint16_t apply_ewma(uint8_t alpha, uint32_t new_score, uint32_t last_score)
{
    uint32_t out;

    uint32_t alpha_cur = (uint32_t)alpha;
    uint32_t alpha_last = (uint32_t)(EWMA_ALPHA_MAX - alpha);

    out = ((alpha_cur * (new_score)) + (alpha_last * (last_score))) / 100;
    return (uint16_t) out;
}

/**
 * @brief Function called by DCS to evaluate channels
 *
 * @param context DCS context
 * @return channel to switch to, or NULL
 */
static struct dcs_channel *ewma_op_evaluate_channels(struct dcs *context)
{
    struct dcs_channel *candidate_chan = dcs_algo_get_channel_with_highest_score(context);
    struct ewma_context *ewma = context->algo.context;

    uint32_t threshold = dcs_algo_calculate_threshold(
            context->current_channel->metric.accumulated_score,
            ewma->config.threshold_percentage);

    LOG_INFO("Candidate chan (ch %d): score %d, threshold %d\n",
            candidate_chan->ch.channel_s1g, candidate_chan->metric.accumulated_score, threshold);

    if (candidate_chan == context->current_channel)
    {
        LOG_INFO("Candidate is current channel\n");
        ewma->rounds_with_a_better_channel = 0;
    }
    else if (candidate_chan->metric.accumulated_score > threshold)
    {
        ewma->rounds_with_a_better_channel++;
        LOG_INFO("Candidate is a different channel (%d time(s) in a row)\n",
                ewma->rounds_with_a_better_channel);
    }
    else
    {
        LOG_INFO("Candidate is a different channel, but not above the threshold\n");
    }

    /* increment the channels lifetime counter as best channel */
    candidate_chan->metric.rounds_as_best++;

    /* switch if current num rounds as best is past threshold */
    if (ewma->config.rounds_for_csa != 0 &&
        ewma->rounds_with_a_better_channel >= ewma->config.rounds_for_csa)
    {
        return candidate_chan;
    }
    return NULL;
}

/**
 * @brief Called after a channel switch has completed by DCS
 *
 * @param context DCS context
 * @param channel Channel that was switched into
 */
static void ewma_op_post_csa_hook(struct dcs *context, struct dcs_channel *channel)
{
    UNUSED(channel);
    struct ewma_context *ewma = context->algo.context;

    ewma->rounds_with_a_better_channel = 0;
}

/**
 * @brief Function called by DCS to process a measurement
 *
 * @param context DCS context
 * @param meas Measurement to process
 * @param channel Channel this measurement was performed on
 */
static void ewma_op_process_measurement(struct dcs *context, struct channel_measurement *meas,
        struct dcs_channel *channel)
{
    struct ewma_context *ewma = context->algo.context;

    /* Save timestamp and score */
    channel->metric.n_samples++;

    /* Update current score using EWMA function */
    channel->metric.accumulated_score = apply_ewma(ewma->config.ewma_alpha,
            meas->metric, channel->metric.accumulated_score);
}

/**
 * @brief Called to initialise EWMA algorithm
 *
 * @param dcs DCS context
 * @param config EWMA configuration
 * @return 0 on success, else error code
 */
static int ewma_op_init(struct dcs *context, config_setting_t *cfg)
{
    int val;
    int errors = 0;
    struct ewma_context *ewma = calloc(1, sizeof(*ewma));

    if (!ewma)
    {
        LOG_ERROR("Failed to allocate EWMA context\n");
        return -ENOMEM;
    }

    if (!cfg)
    {
        LOG_ERROR("Could not find config settings for EWMA\n");
        free(ewma);
        return -EINVAL;
    }

    context->algo.context = ewma;

    ewma->config.threshold_percentage = cfg_parse_int(cfg, "threshold_percentage", &errors);

    val = cfg_parse_int(cfg, "ewma_alpha", &errors);
    if ((val > EWMA_ALPHA_MAX) || (val < EWMA_ALPHA_MIN))
    {
        LOG_ERROR("EWMA alpha out of bounds (min: %d, max: %d, actual: %d)\n",
                EWMA_ALPHA_MIN, EWMA_ALPHA_MAX, val);
        errors++;
    }
    ewma->config.ewma_alpha = val;

    val = cfg_parse_int(cfg, "rounds_for_csa", &errors);
    if (val <= 0)
    {
        LOG_ERROR("Rounds as best must be greater than 0\n");
        errors++;
    }
    ewma->config.rounds_for_csa = val;

    context->config.sec_per_scan.tv_sec = cfg_parse_int(cfg, "sec_per_scan", &errors);
    context->config.sec_per_round.tv_sec = cfg_parse_int(cfg, "sec_per_round", &errors);

    dcs_algo_reset_accumulated_scores(context, METRIC_INIT_VALUE);
    return errors ? -EINVAL : 0;
}

/**
 * @brief op table for EWMA algorithm
 */
struct algo_ops ewma_ops = {
    .init = ewma_op_init,
    .deinit = dcs_algo_free_context,
    .evaluate_channels = ewma_op_evaluate_channels,
    .process_measurement = ewma_op_process_measurement,
    .post_csa_hook = ewma_op_post_csa_hook,
};
