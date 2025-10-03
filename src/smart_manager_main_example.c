/**
 * Copyright 2023 Morse Micro
 * SPDX-License-Identifier: GPL-2.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <netlink/genl/genl.h>
#include <linux/nl80211.h>

#include "smart_manager.h"
#include "backend/backend.h"
#include "utils.h"
#include "logging.h"
#include "backend/morsectrl/command.h"


void
my_callback(void *context, mmsm_backend_intf_t *intf, mmsm_data_item_t *result)
{
    uint32_t val;
    uint8_t *ptr;

    UNUSED(context);
    UNUSED(intf);

    LOG_INFO("-------- CALLBACK -------\n");

    if (!result)
    {
        LOG_INFO("Callback called, but no stations connected\n");
        return;
    }


    LOG_INFO("Connected MAC address is: %s\n", result->mmsm_key.d.string);

    ptr = mmsm_find_value_by_key(result, "connected_time");
    if (ptr) {
        val = strtoul((char *)ptr, NULL, 0);
        LOG_INFO("STA has been connected for: %lu seconds\n", val);
    }

    LOG_INFO("Is STA authorised? %s\n",
           mmsm_is_flag_set_in(result, "flags", "AUTH") ? "Yes!" : "No");

    LOG_INFO("The 6th value is: %s\n", mmsm_find_nth_value(result, 6));

    LOG_INFO("-------- END -------\n");
}


void
my_other_callback(void *context, mmsm_backend_intf_t *intf,
                  mmsm_data_item_t *result)
{
    UNUSED(context);
    UNUSED(intf);
    UNUSED(result);

    printf("-------- CALLBACK -------\n");
    MMSM_DUMP_DATA_ITEM(result, LOG_LEVEL_INFO);
    printf("-------- END -------\n");
}


void
my_other_other_callback(void *context, mmsm_backend_intf_t *intf,
                        mmsm_data_item_t *result)
{
    UNUSED(context);
    UNUSED(intf);
    UNUSED(result);

    printf("-------- Second CALLBACK -------\n");
    MMSM_DUMP_DATA_ITEM(result, LOG_LEVEL_INFO);
    printf("-------- END -------\n");
}


void
my_nl80211_callback(void *context, mmsm_backend_intf_t *intf, mmsm_data_item_t
                    *result)
{
    UNUSED(context);
    UNUSED(intf);
    UNUSED(result);

    printf("-------- nl80211 CALLBACK -------\n");
    MMSM_DUMP_DATA_ITEM(result, LOG_LEVEL_INFO);
    printf("-------- END -------\n");
}

extern void dcs_init(const char *ifname);

int main(int argc, char **argv)
{
    mmsm_data_item_t *result = NULL;
    /* This may be updated depending on the device name on your system */
    mmsm_backend_intf_t *hostapd;
    uint32_t if_index = 0;

    mmsm_init();

    if (argc < 2)
    {
        LOG_ERROR("Usage: smart_manager <hostapd_control_path>\n");
        return 1;
    }

    hostapd = mmsm_backend_hostapd_ctrl_create(argv[1]);
    mmsm_backend_intf_t *nl80211 = mmsm_backend_nl80211_create();

    mmsm_backend_intf_t *morsectrl = mmsm_backend_morsectrl_create("wlan0");

    /* We can set up polling monitors with certain frequency using the
     * mmsm_monitor_polling API */
    // mmsm_monitor_polling(hostapd, 1000, my_callback, NULL, "STA-FIRST");
    // /* Ww can also setup pattern matching monitors to look for certain
    //  * events. */
    // mmsm_monitor_pattern(hostapd, "", my_other_callback, NULL,
    //                      "AP-STA-CONNECTED");
    // mmsm_monitor_pattern(hostapd, "", my_other_other_callback, NULL,
    //                     "AP-STA-DISCONNECTED");

    // mmsm_monitor_pattern(nl80211, "", my_nl80211_callback, NULL,
    //                      NL80211_CMD_NEW_STATION, 0, -1);



    /* Here, calls to get and set data via the hostapd_ctrl backend may be added
     * as required */
    // LOG_INFO("Requesting hostapd station list\n");
    // result = mmsm_request(hostapd, "STATUS");
    // if (!result)
    // {
    //     LOG_ERROR("No stations\n");
    // }
    // MMSM_DUMP_DATA_ITEM(result, LOG_LEVEL_INFO);
    // mmsm_data_item_free(result);

    LOG_INFO("Initialising DCS\n");
    dcs_init("wlan0");

    /* Monitors aren't started until mmsm_start is called */
    /* Once mmsm_start is called, no additional pattern monitors can be added */

    LOG_INFO("Start monitors\n");
    mmsm_start();

    /* Usually you'd run this throughout the duration of your program.
     * For testing and ililustration, we're just going to run it for
     * a few seconds */
    // sleep(20);

    // mmsm_stop();
    // LOG_INFO("Monitors stopped\n");

    /* Monitors are now stopped. NL80211 testing begins. */

    /* The way that NL80211 works is that you can send a request
     * with a series of arguments of various types. Here we have some
     * examples of requests that can be made with and without
     * arguments.
     *
     * Note that all NL80211 responses are considered to be array-like
     * where each item in the array corresponds to the packet received
     * on nl80211. In the instance of the GET_INTERFACE command, you
     * will get one packet per interface. To go directly to the first
     * packet and inspect the contents, you can use result->mmsm_sub_values
     */
    LOG_INFO("Getting interfaces over nl80211\n");
    result = mmsm_request(nl80211, NL80211_CMD_GET_INTERFACE, NLM_F_DUMP, -1);
    if (!result)
    {
        LOG_ERROR("No stations\n");
        return 0;
    }
    MMSM_DUMP_DATA_ITEM(result, LOG_LEVEL_INFO);

    uint8_t *item;

    LOG_INFO("Searching nl80211 results\n");

    /*
     * As an example here we can select a named interface by iterating down the
     * result tree and searching for a match.
     */

    mmsm_data_item_t *iter = result;
    const char *search_interface = "wlan0";
    char *intf = NULL;
    while (iter)
    {
        intf = (char *)mmsm_find_value_by_intkey(iter->mmsm_sub_values,
                                                 NL80211_ATTR_IFNAME);
        if (intf && strcmp(intf, search_interface) == 0)
            break;

        iter = iter->mmsm_next;
    }
    if (iter == NULL)
    {
        LOG_ERROR("Specified interface not found\n");
        goto cleanup;
    }

    char *name = strdup(intf);
    if (name)
    {
        LOG_INFO("if_name: %s\n", name);
    }

    /* Look for the index of the second interface */
    item = mmsm_find_value_by_intkey(iter->mmsm_sub_values,
                                     NL80211_ATTR_IFINDEX);

    if (item)
    {
        if_index = *(uint32_t *)item;
        LOG_INFO("if_index: %d\n", if_index);
    }
    else
    {
        LOG_ERROR("Can't find the item. Key:%d\n", NL80211_ATTR_IFINDEX);
    }

    mmsm_data_item_free(result);
    result = NULL;

    result = mmsm_request(morsectrl, MORSE_CMD_ID_GET_VERSION, 0, 0, -1);

    if (!result)
    {
        LOG_ERROR("No result\n");
    }
    else
    {
        struct morse_cmd_resp_get_version *rsp;

        mmsm_data_item_to_mctrl_response(result, &rsp);

        LOG_INFO("FW Vers [%d]: %.*s\n", rsp->length, rsp->length, rsp->version);

        mmsm_data_item_free(result);
    }

    if (if_index != 0)
    {
        LOG_INFO("Getting stations for interface index=%d name=%s\n",
                 if_index, name);
        /* A slightly more complicated request, this time with an argument.
         * Again, more than one station will be represented by a list of
         * results. Here, we just access the first station. */
        printf("if_index is %d\n", if_index);
        result = mmsm_request(nl80211, NL80211_CMD_GET_STATION, NLM_F_DUMP,
                              NL80211_ATTR_IFINDEX, NLA_U32, if_index,
                              -1);
        if (result)
        {
            item = mmsm_find_by_nested_intkeys(
                result->mmsm_sub_values,
                NL80211_ATTR_STA_INFO, NL80211_STA_INFO_SIGNAL, -1);
            if (item)
            {
                LOG_INFO("signal: %d dB\n", 100 + *(int8_t *)item);
            }
            else
            {
                LOG_ERROR("Can't find the item. Keys:%d,%d\n",
                          NL80211_ATTR_STA_INFO, NL80211_ATTR_IFINDEX);
            }
        }
        else
        {
            LOG_INFO("No stations\n");
        }
    }
    free(name);


    sleep(100);
cleanup:
    if (result)
        mmsm_data_item_free(result);

    mmsm_backend_hostapd_ctrl_destroy(hostapd);
    mmsm_backend_nl80211_destroy(nl80211);
    mmsm_backend_morsectrl_destroy(morsectrl);
}
