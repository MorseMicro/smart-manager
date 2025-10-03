/*
 * Copyright 2023 Morse Micro
 * SPDX-License-Identifier: GPL-2.0-or-later OR LicenseRef-MorseMicroCommercial
 */
#pragma once

#include <libconfig.h>

#include "dcs.h"

/** Algorithm operation table */
struct algo_ops {
    /**
     * Passed DCS context, and the config setting that matches the algorithm's name.
     * Returns 0 on success, else error code.
     */
    int (*init)(struct dcs *, config_setting_t *);

    /**
     * Uninitalise and clean up algorithm specific context. Called in DCS destroy, or if init fails.
     */
    void (*deinit)(struct dcs *);

    /**
     * Called after a scan round, to evaluate the channels and determine whether a channel
     * switch is required. Returns the channel to switch to, or NULL if no channel switch is
     * required.
     */
    struct dcs_channel* (*evaluate_channels)(struct dcs *);

    /**
     * Called after each measurement, to process the received measurement.
     * Passed the DCS context, the channel measurement to process, and the channel that was just
     * measured.
     */
    void (*process_measurement)(struct dcs *, struct channel_measurement *, struct dcs_channel *);

    /**
     * Called after a channel switch has completed. Is passed DCS context and the channel switched
     * into.
     */
    void (*post_csa_hook)(struct dcs *, struct dcs_channel *);
};


/** Algorithm definition */
struct algo {
    /** Name of algorithm. Must match config file section. */
    const char *name;
    /** Operation table with callbacks for the particular algorithm. */
    struct algo_ops *ops;
};

/**
 * @brief Assign a DCS algorithm and initialise it
 *
 * @param dcs DCS context
 * @param cfg root config setting
 *            must contain 'algo_type' and a child setting with a matching name
 * @return 0 if successful, else error code
 */
int dcs_algo_initialise(struct dcs *dcs, config_setting_t *cfg);

/**
 * @brief Call the deinit op to clean up the algorithm context.
 *
 * @param dcs DCS context
 */
void dcs_algo_deinitialise(struct dcs *dcs);

/**
 * @brief Call the evaluate channels op for the assigned algorithm.
 * This function is called at the end of each scan round.
 *
 * @param dcs DCS context
 * @return The channel that the algorithm wants to switch to, or NULL if no switch is required
 */
struct dcs_channel *dcs_algo_ops_evaluate_channels(struct dcs *dcs);

/**
 * @brief Call the process measurement op for the assigned algorithm.
 * This function is called after each measurement has completed.
 *
 * @param dcs DCS context
 * @param meas The channel measurement to process
 * @param chan The channel that the measurement was taken on
 */
void dcs_algo_ops_process_measurement(struct dcs *dcs, struct channel_measurement *meas,
        struct dcs_channel *chan);

/**
 * @brief Call the post CSA hook op for the assigned algorithm.
 * This function is called once a channel switch has completed.
 *
 * @param dcs DCS context
 * @param chan channel that was switched into
 */
void dcs_algo_ops_post_csa_hook(struct dcs *dcs, struct dcs_channel *chan);

/**
 * @brief Utility function to get the channel currently with the highest accumulated score.
 *
 * @param dcs DCS context object
 * @return pointer to dcs channel
 */
struct dcs_channel *dcs_algo_get_channel_with_highest_score(struct dcs *dcs);

/**
 * @brief Utility function to reset all accumulated scores in the scan list.
 * Also sets n_samples to 0.
 *
 * @param dcs DCS context
 * @param reset_val Value to set accumulated_score to
 */
void dcs_algo_reset_accumulated_scores(struct dcs *dcs, int reset_val);

/**
 * @brief Utility function to calculate the score threshold.
 *
 * @param current_score Current score to calculate against
 * @param threshold_percentage Threshold percentage to apply
 * @return adjusted score
 */
static inline uint32_t dcs_algo_calculate_threshold(uint32_t current_score,
        uint8_t threshold_percentage)
{
    return (current_score * (100 + (uint32_t)threshold_percentage)) / 100;
}

/**
 * @brief Generic deinit function to free the opaque algorithm context
 *
 * @param context DCS context
 */
void dcs_algo_free_context(struct dcs *context);
