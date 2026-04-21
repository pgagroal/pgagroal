/*
 * Copyright (C) 2026 The pgagroal community
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list
 * of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may
 * be used to endorse or promote products derived from this software without specific
 * prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef PGAGROAL_ART_H
#define PGAGROAL_ART_H

#ifdef __cplusplus
extern "C" {
#endif

#include <deque.h>
#include <value.h>

#include <stdint.h>

#define MAX_PREFIX_LEN 10

typedef int (*art_callback)(void* data, const char* key, struct value* value);

typedef void (*value_destroy_callback)(void* value);

/** @struct art
 * The ART tree
 */
struct art
{
   struct art_node* root; /**< The root node of ART */
   uint64_t size;         /**< The size of the ART */
};

/** @struct art_iterator
 * Defines an art_iterator
 */
struct art_iterator
{
   struct deque* que;   /**< The deque */
   struct art* tree;    /**< The ART */
   uint32_t count;      /**< The count of the iterator */
   char* key;           /**< The key */
   struct value* value; /**< The value */
};

/**
 * Initializes an adaptive radix tree
 * @param tree [out] The tree
 * @return 0 on success, 1 if otherwise
 */
int
pgagroal_art_create(struct art** tree);

/**
 * inserts a new value into the art tree,note that the key is copied while the value is sometimes not(depending on value type)
 * @param t The tree
 * @param key The key
 * @param value The value data
 * @param type The value type
 * @return 0 if the item was successfully inserted, otherwise 1
 */
int
pgagroal_art_insert(struct art* t, char* key, uintptr_t value, enum value_type type);

/**
 * inserts a new ValueRef value into the art tree with a custom to_string and destroy data callback config
 * @param t The tree
 * @param key The key
 * @param value The value data
 * @param config The config
 * @return 0 if the item was successfully inserted, otherwise 1
 */
int
pgagroal_art_insert_with_config(struct art* t, char* key, uintptr_t value, struct value_config* config);

/**
 * Check if a key exists in the ART tree
 * @param t The tree
 * @param key The key
 * @return true if the key exists, false if otherwise
 */
bool
pgagroal_art_contains_key(struct art* t, char* key);

/**
 * Searches for a value in the ART tree
 * @param t The tree
 * @param key The key
 * @return NULL if the item was not found, otherwise the value pointer is returned
 */
uintptr_t
pgagroal_art_search(struct art* t, char* key);

/**
 * Perform a prefix search in the ART tree, storing all matching string keys in the internal nodes.
 * Matches will be dynamically allocated string copies, which must be freed by the caller.
 * @param t The tree
 * @param prefix The prefix string to search for
 * @param matches [out] Pointer to array of strings (allocated internally)
 * @param max_matches Maximum number of matches to return (sets a limit on array allocation)
 * @return Number of matching elements found, or -1 on error (invalid arguments)
 */
int
pgagroal_art_prefix_search(struct art* t, char* prefix, char*** matches, int max_matches);

/**
 * Searches for a value in the ART tree, and also returns its type
 * @param t The tree
 * @param key The key
 * @param [out] type The type of the value, ValueNone if the key doesn't exist
 * @return 0 if the item was not found, otherwise the value data is returned
 */
uintptr_t
pgagroal_art_search_typed(struct art* t, char* key, enum value_type* type);

/**
 * Deletes a value from the ART tree
 * @param t The tree
 * @param key The key
 * @return 0 if success or value not found, 1 if otherwise
 */
int
pgagroal_art_delete(struct art* t, char* key);

/**
 * Remove all the key value pairs in the ART tree
 * @param t The tree
 * @return 0 on success, 1 if otherwise
 */
int
pgagroal_art_clear(struct art* t);

/**
 * Get the next key value pair into iterator
 * @param iter The iterator
 * @return true if iterator has next, otherwise false
 */
bool
pgagroal_art_iterator_next(struct art_iterator* iter);

/**
 * Check if the iterator has next value without advancing it
 * @param iter The iterator
 * @return true if iterator has next, otherwise false
 */
bool
pgagroal_art_iterator_has_next(struct art_iterator* iter);

/**
 * Remove the current key value pair the iterator points to.
 * The key and value will be set to NULL afterward.
 * @param iter The iterator
 */
void
pgagroal_art_iterator_remove(struct art_iterator* iter);

/**
 * Create an art iterator
 * @param t The tree
 * @param iter [out] The iterator
 * @return 0 if success, otherwise 1
 */
int
pgagroal_art_iterator_create(struct art* t, struct art_iterator** iter);

/**
 * Destroy the iterator
 * @param iter The iterator
 */
void
pgagroal_art_iterator_destroy(struct art_iterator* iter);

/**
 * Convert the ART tree to string
 * @param t The ART tree
 * @param format The format
 * @param tag The optional tag
 * @param indent The indent
 * @return The string
 */
char*
pgagroal_art_to_string(struct art* t, int32_t format, char* tag, int indent);

/**
 * Destroys an ART tree
 * @return 0 on success, 1 if otherwise
 */
int
pgagroal_art_destroy(struct art* tree);

#ifdef __cplusplus
}
#endif

#endif
