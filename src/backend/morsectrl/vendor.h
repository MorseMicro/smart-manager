/**
 * Copyright 2023 Morse Micro
 * SPDX-License-Identifier: GPL-2.0-or-later OR LicenseRef-MorseMicroCommercial
 */
#pragma once

#define MORSE_OUI                           (0x0CBF74)

enum morse_vendor_cmds {
    MORSE_VENDOR_CMD_TO_MORSE = 0
};

enum morse_vendor_events {
    MORSE_VENDOR_EVENT_VENDOR_IE_FOUND = 0,
    MORSE_VENDOR_EVENT_OCS_DONE = 1,
    MORSE_VENDOR_EVENT_MGMT_VENDOR_IE_FOUND = 2,
    MORSE_VENDOR_EVENT_MESH_PEER_ADDR = 3,
    MORSE_VENDOR_EVENT_BSS_STATS = 4
};

enum morse_vendor_attributes {
    MORSE_VENDOR_ATTR_DATA = 0
};
