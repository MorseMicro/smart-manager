/**
 * Copyright 2022 Morse Micro
 * SPDX-License-Identifier: GPL-2.0-or-later OR LicenseRef-MorseMicroCommercial
 *
 */
#include <stddef.h>
#include <netlink/genl/genl.h>
#include <linux/nl80211.h>
#include <net/if.h>
#include <endian.h>

#include "smart_manager.h"
#include "mmsm_data.h"
#include "backend.h"
#include "utils.h"
#include "logging.h"
#include "helpers.h"
#include "datalog.h"

#include "backend/morsectrl/command.h"
#include "backend/morsectrl/vendor.h"

/**
 * The morsectrl backend object
 */
typedef struct backend_morsectrl_t
{
    mmsm_backend_intf_t intf;

    mmsm_backend_intf_t *nl80211_intf;

    char *ifname;

    struct datalog *datalog;
} backend_morsectrl_t;

/**
 * @brief Perform the morsectrl command over netlink
 *
 * @param intf morsectrl instance
 * @param command data_item containing the command/s
 * @param result location to store the data_items containing the result
 * @return MMSM_SUCCESS on success, otherwise error code
 */
static mmsm_error_code
backend_morsectrl_sync_command(mmsm_backend_intf_t *intf,
                             mmsm_data_item_t *command,
                             mmsm_data_item_t **result)
{
    backend_morsectrl_t *morsectrl = get_container_from_intf(morsectrl, intf);
    mmsm_data_item_t *item;
    mmsm_data_item_t *resp_item, *iter = NULL;
    struct response *resp;
    int16_t ret;
    mmsm_error_code err = MMSM_SUCCESS;

    uint32_t ifnum = if_nametoindex(morsectrl->ifname);

    for_each_data_item(item, command)
    {
        resp_item = mmsm_request(morsectrl->nl80211_intf, NL80211_CMD_VENDOR, 0,
            NL80211_ATTR_IFINDEX, NLA_U32, ifnum,
            NL80211_ATTR_VENDOR_ID, NLA_U32, MORSE_OUI,
            NL80211_ATTR_VENDOR_SUBCMD, NLA_U32, MORSE_VENDOR_CMD_TO_MORSE,
            NL80211_ATTR_VENDOR_DATA, NLA_BINARY, item->mmsm_value_len, item->mmsm_value, -1);

        if (!resp_item)
        {
            LOG_ERROR("Failed to execute vendor command\n");
            return MMSM_UNKNOWN_ERROR;
        }

        resp = (struct response *)mmsm_find_value_by_intkey(
                    resp_item->mmsm_sub_values, NL80211_ATTR_VENDOR_DATA);

        if (resp)
        {
            if (iter == NULL)
            {
                iter = mmsm_data_item_alloc();
                *result = iter;
            }
            else
            {
                iter = mmsm_data_item_alloc_next(iter);
            }

            mmsm_data_item_set_key_u32(iter, le16toh(resp->hdr.message_id));

            ret = le16toh(resp->status);

            if (!ret)
            {
                mmsm_data_item_set_val_bytes(iter, resp->data, le16toh(resp->hdr.len));
            }
            else
            {
                LOG_WARN("morsectrl command %u failed %d\n", resp->hdr.message_id, ret);
                err = MMSM_COMMAND_FAILED;
            }
        }
        else
        {
            LOG_ERROR("No vendor data in response\n");
        }

        mmsm_data_item_free(resp_item);
    }

    return err;
}


/**
 * @brief Process request arguments for a morsectrl request. Multiple commands can be made in one
 * request, with the arguments terminated with a 0.
 *
 * Arguments are expected in the following format:
 *      <command parameters>, [... <command parameters>], -1
 * where
 *  command parameters - 3 arguments which specify the command, in the following format
 *      <command id> <command length> <pointer to command body>
 *
 * Note that a -1 for command id indicates no more commands.
 *
 * @param intf morsectrl backend instance
 * @param args arguments for the command
 * @return data item containing the response
 */
static mmsm_data_item_t *
backend_morsectrl_process_request_args(mmsm_backend_intf_t *intf,
                                     va_list args)
{
    mmsm_data_item_t *item;
    mmsm_data_item_t *first = NULL;
    mmsm_data_item_t *last = NULL;
    struct request *request;
    uint8_t *cmd_req;
    uint16_t cmd_len;
    int command_id;

    /* Arguments are <command_id> <command_len> <ptr to command> */
    command_id = va_arg(args, int);

    while (command_id != -1) /* -1 indicates no more commands */
    {
        cmd_len = (uint16_t) va_arg(args, int);
        cmd_req = (uint8_t *) va_arg(args, char *);

        request = calloc(1, sizeof(*request) + cmd_len);

        request->hdr.message_id = htole16(command_id);
        request->hdr.len = htole16(cmd_len);
        request->hdr.flags = htole16(MORSE_CMD_TYPE_REQ);
        memcpy(request->data, cmd_req, cmd_len);

        item = mmsm_data_item_alloc();

        mmsm_data_item_set_key_u32(item, command_id);

        item->mmsm_value = (uint8_t *) request;
        item->mmsm_value_len = sizeof(*request) + cmd_len;

        if (last)
        {
            last->mmsm_next = item;
        }
        else
        {
            first = item;
        }

        last = item;
        command_id = va_arg(args, int);
    }
    return first;
}

/**
 * Morsectrl backend interface instance
 */
static const mmsm_backend_intf_t morsectrl_intf =
{
    .req_blocking = backend_morsectrl_sync_command,
    .req_async = NULL,
    .process_request_args = backend_morsectrl_process_request_args,
};

mmsm_backend_intf_t *
mmsm_backend_morsectrl_create(const char *ifname)
{
    backend_morsectrl_t *module;
    LOG_INFO("Instantiating morsectrl backend\n");

    module = calloc(1, sizeof(*module));
    if (!module)
        return NULL;

    memcpy(&module->intf, &morsectrl_intf, sizeof(module->intf));
    module->datalog = datalog_create("morsectrl");
    module->nl80211_intf = mmsm_backend_nl80211_create();
    module->ifname = strdup(ifname);

    return &module->intf;
}


void
mmsm_backend_morsectrl_destroy(mmsm_backend_intf_t *handle)
{
    backend_morsectrl_t *morsectrl;

    if (!handle)
        return;

    morsectrl = get_container_from_intf(morsectrl, handle);
    datalog_close(morsectrl->datalog);
    morsectrl->datalog = NULL;
    if (morsectrl->nl80211_intf)
    {
        mmsm_backend_nl80211_destroy(morsectrl->nl80211_intf);
    }
    free(morsectrl->ifname);
    free(morsectrl);
}
