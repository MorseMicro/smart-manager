/**
 * Copyright 2025 Morse Micro
 * SPDX-License-Identifier: GPL-2.0-or-later OR LicenseRef-MorseMicroCommercial
 *
 * hashmap.h - Generic hashmap utility
 * Supports key size in bytes, built-in hash and compare functions.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "utils.h"

#define DEFAULT_HASH_VAL    0x12345
#define DEFAULT_HASH_MUL    8

typedef struct hashmap
{
    list_head_t *buckets;
    size_t size;           /* Number of buckets */
    size_t key_size;       /* Key size in bytes */
} hashmap_t;

/* Default hash function */
static inline uint32_t hashmap_calc_hash(const void *key, size_t key_size)
{
    uint32_t hash = DEFAULT_HASH_VAL;
    const uint8_t *bytes = (const uint8_t *)key;

    for (size_t i = 0; i < key_size; i++)
    {
        hash ^= bytes[i];
        hash *= DEFAULT_HASH_MUL;
    }
    return hash;
}

/* Default comparison: memory match */
static inline bool hashmap_key_equal(const void *key, const void *entry_key, size_t key_size)
{
    return memcmp(key, entry_key, key_size) == 0;
}

/**
 * @brief Initialize the hashmap.
 *
 * @param map Pointer to hashmap struct
 * @param size Number of buckets
 * @param key_size Size of key in bytes
 */
static inline void hashmap_init(hashmap_t *map, size_t size, size_t key_size)
{
    map->size = size;
    map->key_size = key_size;
    map->buckets = (list_head_t *)calloc(size, sizeof(list_head_t));
    for (size_t i = 0; i < size; ++i)
    {
        list_reset(&map->buckets[i]);
    }
}

/**
 * @brief Insert an entry into the hashmap.
 *
 * @param map Pointer to hashmap
 * @param key Pointer to key data
 * @param entry Pointer to list_entry embedded in the item
 */
static inline void hashmap_insert(hashmap_t *map, const void *key, list_entry_t *entry)
{
    uint32_t index = hashmap_calc_hash(key, map->key_size) % map->size;

    list_add_tail(&map->buckets[index], entry);
}

/**
 * @brief Find an entry in the hashmap by key.
 *
 * @param map Pointer to hashmap
 * @param key Key to search
 * @param get_key_fn Function to extract key from entry
 * @return Pointer to matching list_entry or NULL
 */
static inline list_entry_t *hashmap_find(hashmap_t *map, const void *key,
                        const void *(*get_key_fn)(list_entry_t *entry))
{
    uint32_t index = hashmap_calc_hash(key, map->key_size) % map->size;
    list_entry_t *entry;
    list_for_each_entry(entry, &map->buckets[index])
    {
        const void *entry_key = get_key_fn(entry);
        if (hashmap_key_equal(key, entry_key, map->key_size))
        {
            return entry;
        }
    }
    return NULL;
}

/**
 * @brief Remove an entry from hashmap (caller must call after finding).
 *
 * @param entry Entry to remove
 */
static inline void hashmap_remove(list_entry_t *entry)
{
    list_remove(entry);
}

/**
 * @brief Cleanup the hashmap and optionally free each item.
 *
 * @param map Pointer to hashmap
 * @param free_fn Optional function to free each item (can be NULL)
 */
static inline void hashmap_cleanup(hashmap_t *map, void (*free_fn)(list_entry_t *entry))
{
    for (size_t i = 0; i < map->size; ++i)
    {
        list_entry_t *entry, *tmp;
        list_for_each_entry_safe(entry, tmp, &map->buckets[i])
        {
            list_remove(entry);
            if (free_fn)
            {
                free_fn(entry);
            }
        }
        list_reset(&map->buckets[i]);
    }
    free(map->buckets);
    map->buckets = NULL;
    map->size = 0;
    map->key_size = 0;
}

/**
 * @brief Iterate over all entries in the hashmap and apply a user function.
 *
 * @param map Pointer to hashmap
 * @param fn Callback function to apply for each entry
 * @param arg Optional user argument passed to fn
 *
 * The callback function should have the signature:
 *     void fn(list_entry_t *entry, void *arg);
 */
static inline void hashmap_iterate(hashmap_t *map,
            void (*fn)(list_entry_t *entry, void *arg),
            void *arg)
{
    for (size_t i = 0; i < map->size; ++i)
    {
        list_entry_t *entry, *tmp;
        list_for_each_entry_safe(entry, tmp, &map->buckets[i])
        {
            fn(entry, arg);
        }
    }
}
