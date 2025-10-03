/**
 * Copyright 2023 Morse Micro
 * SPDX-License-Identifier: GPL-2.0-or-later OR LicenseRef-MorseMicroCommercial
 */

#pragma once

#include <stdbool.h>

#include "utils.h"

/**
 * Doubly linked list APIs.
 *
 * Nomenclature:
 *
 * List entry - list pointer struct which must be included in a struct you wish to make a list
 *          struct list_entry {
 *              struct list_entry *next, *prev;
 *          };
 *
 * list item - struct containing a list entry and user data, which forms an 'item' of the list
 *         struct some_list_data_type {
 *              list_entry_t list;
 *              ... (arbitrary user data)
 *         }
 *
 * list head - list entry which forms the head of the list. does not contain data.
 */

/**
 * @brief List entry. Include this type in a structure to make it a list item.
 */
typedef struct list_entry
{
    struct list_entry *next;
    struct list_entry *prev;
} list_entry_t;

/**
 * @brief List head. Root of a list.
 * Same structure as a list entry, but explicitly a different type so the compiler will warn us
 * if we use it as a list item.
 *
 * @note List head MUST be initialised with a call to @ref list_reset() before being used.
 */
typedef struct list_head {
    struct list_entry *next;
    struct list_entry *prev;
} list_head_t;

/**
 * @brief Initialise and empty a list
 *
 * @param head List head to reset
 */
void list_reset(list_head_t *head);

/**
 * @brief Add a list item to the end of a list
 *
 * @param head List head to add to
 * @param item Item to add
 */
void list_add_tail(list_head_t *head, list_entry_t *item);

/**
 * @brief Add a list item to the start of a list
 *
 * @param head List head to add to
 * @param item Item to add
 */
void list_add_head(list_head_t *head, list_entry_t *item);

/**
 * @brief Add a list item in the middle of a list
 *
 * @param item Item to add
 * @param prev previous item in the list
 * @param next next item in the list
 */
void list_add(list_entry_t *item, list_entry_t *prev, list_entry_t *next);

/**
 * @brief Remove an item from its list. MUST be a part of a list before calling remove. Calling this
 * function on an item not within a list is undefined behaviour.
 *
 * @param item Item to remove
 */
void list_remove(list_entry_t *item);

/**
 * @brief Check if this item is the last item in a list
 *
 * @param head List head item is a member of
 * @param item Item to check
 * @return true if last item in list, else false
 */
static inline bool list_is_end(list_head_t *head, list_entry_t *item)
{
    return (item->next == (list_entry_t *)head);
}

/**
 * @brief Check if a list is empty
 *
 * @param head List head to check
 * @return true if empty, else false
 */
static inline bool list_is_empty(list_head_t *head)
{
    return (head ? head->next == (list_entry_t *)head : true);
}

/**
 * @brief Check if a list entry is currently part of a list.
 *
 * @param entry Pointer to the list entry
 * @return true if the entry is part of a list, false otherwise
 */
static inline bool list_entry_is_linked(const list_entry_t *entry)
{
    return (entry->next != NULL && entry->prev != NULL &&
            entry->next != entry && entry->prev != entry);
}

/**
 * @brief Check if an entry is a list head
 *
 * @param head Head of the list
 * @param entry entry to check
 * @return true if entry is list head, else false
 */
static inline bool list_is_head(list_head_t *head, list_entry_t *entry)
{
    return entry == (list_entry_t *)head;
}

/**
 * @brief Return the size of the list. Note that this function walks the list so avoid using it on
 * large lists / repeatedly called codepaths
 *
 * @param head List head to query
 * @return Number of elements in list
 */
size_t list_size(list_head_t *head);

/**
 * @brief Initialize a list entry to point to itself (empty state)
 *
 * @param entry List entry to initialize
 */
#define INIT_LIST_ENTRY(entry) \
    do { \
        (entry)->next = (entry); \
        (entry)->prev = (entry); \
    } while (0)

/**
 * @brief Get a list item given a list entry
 *
 * eg.
 *  struct user_data_type {
 *      list_entry_t list;
 *      uint32_t flags;
 *      uint8_t *buffer;
 *  };
 *
 *  void some_function(list_entry_t *entry)
 *  {
 *       struct user_data_type *data;
 *       data = list_get_item(data, entry, list);
 *
 *
 *       ... (do something with data)
 *  }
 *
 * @param _item_ptr - Pointer of list item to retrieve
 * @param _list_entry - List entry within the item
 * @param _member - Name of the list entry member within the item
 * @return Pointer to list item
 */
#define list_get_item(_item_ptr, _list_entry, _member) \
        (container_of((_list_entry), typeof(*_item_ptr), _member))

/**
 * @brief Get the first list item given a list head. If the list is empty, will return NULL
 *
 * @param _item_ptr - Pointer of list item to retreive
 * @param _list_head_ptr - List head to retrieve from
 * @param _list_member - Name of list entry member within the item
 * @return Pointer to list item
 */
#define list_get_first_item(_item_ptr, _list_head_ptr, _list_member) \
        (list_is_empty(_list_head_ptr) ? NULL : \
            list_get_item(_item_ptr, (_list_head_ptr)->next, _list_member))

/**
 * @brief Get the next item given an item in a list
 * Will return the list head if there is no more items
 *
 * @param _item_ptr Pointer to item to iterate on
 * @param _list_member Name of list entry within item
 * @return Pointer to next list item
 */
#define list_get_next_item(_item_ptr, _list_member) \
        list_get_item(_item_ptr, (_item_ptr)->_list_member.next, _list_member)

/**
 * @brief Get the next item given an item in a list
 * Will return NULL if there is no more items
 *
 * @param _item_ptr Pointer to item to iterate on
 * @param _list_head_ptr Pointer to list head
 * @param _list_member Name of list entry within item
 * @return Pointer to next list item, or NULL if reached the end
 */
#define list_get_next_item_or_null(_item_ptr, _list_head_ptr, _list_member) \
        (list_is_end((_list_head_ptr), &(_item_ptr)->_list_member) ? \
            NULL : \
            list_get_next_item(_item_ptr, _list_member))

/**
 * @brief Get the next item in the list, given a previous item.
 * Will wrap to the start if it reaches the end
 *
 * @param _item_ptr Pointer to item to iterate on
 * @param _list_head_ptr Pointer to list head
 * @param _list_member Name of list entry within item
 * @return Pointer to next list item
 */
#define list_get_next_item_circular(_item_ptr, _list_head_ptr, _list_member) \
        (list_is_end((_list_head_ptr), &(_item_ptr)->_list_member) ? \
            list_get_first_item(_item_ptr, _list_head_ptr, _list_member) : \
            list_get_next_item(_item_ptr, _list_member))

/**
 * @brief Iterate a list
 *
 * @param _list_entry - pointer to list entry which will be set to the entry
 * @param _list_head - pointer to list head to iterate
 */
#define list_for_each_entry(_list_entry, _list_head) \
        for (_list_entry = (_list_head)->next; _list_entry != (list_entry_t *)(_list_head); \
            _list_entry = _list_entry->next)

/**
 * @brief Iterate a list, safe against removal of list entry
 *
 * @param _list_entry - pointer to list entry which will be set to the entry
 * @param _temp - another list entry to use as temporary storage
 * @param _head The head of the list
 */
#define list_for_each_entry_safe(_list_entry, _temp, _head) \
    for (_list_entry = (_head)->next, _temp = _list_entry->next; \
        !list_is_head((_head), _list_entry); \
        _list_entry = _temp, _temp = _list_entry->next)
