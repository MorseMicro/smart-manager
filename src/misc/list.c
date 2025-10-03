/**
 * Copyright 2023 Morse Micro
 * SPDX-License-Identifier: GPL-2.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#include <stddef.h>

#include "list.h"

void list_add(list_entry_t *item, list_entry_t *prev, list_entry_t *next)
{
    item->prev = prev;
    item->next = next;
    prev->next = item;
    next->prev = item;
}

void list_add_tail(list_head_t *head, list_entry_t *item)
{
    list_add(item, head->prev, (list_entry_t *) head);
}

void list_add_head(list_head_t *head, list_entry_t *item)
{
    list_add(item, (list_entry_t *) head, head->next);
}

void list_reset(list_head_t *head)
{
    head->next = (list_entry_t *) head;
    head->prev = (list_entry_t *) head;
}

void list_remove(list_entry_t *item)
{
    list_entry_t *prev = item->prev;
    list_entry_t *next = item->next;

    if (!list_is_empty((list_head_t *) item))
    {
        prev->next = next;
        next->prev = prev;
    }
}

size_t list_size(list_head_t *head)
{
    size_t n = 0;
    struct list_entry *pos;
    list_for_each_entry(pos, head)
    {
        n += 1;
    }
    return n;
}
