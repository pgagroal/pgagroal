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

#ifndef PGAGROAL_DEQUE_H
#define PGAGROAL_DEQUE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <value.h>

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

/** @struct deque_node
 * Defines a deque node
 */
struct deque_node
{
   struct value* data;      /**< The value */
   char* tag;               /**< The tag */
   struct deque_node* next; /**< The next pointer */
   struct deque_node* prev; /**< The previous pointer */
};

/** @struct deque
 * Defines a deque
 */
struct deque
{
   uint32_t size;            /**< The size of the deque */
   bool thread_safe;         /**< If the deque is thread safe */
   pthread_rwlock_t mutex;   /**< The mutex of the deque */
   struct deque_node* start; /**< The start node */
   struct deque_node* end;   /**< The end node */
};

/**
 * Compare two values in a deque
 * @param a The first value
 * @param b The second value
 * @return Less than 0 if a should be before b, 0 if equal, otherwise greater than 0
 */
typedef int (*compare_cb)(struct value* a, struct value* b);

/** @struct deque_iterator
 * Defines a deque iterator
 */
struct deque_iterator
{
   struct deque* deque;    /**< The deque */
   struct deque_node* cur; /**< The current deque node */
   char* tag;              /**< The current tag */
   struct value* value;    /**< The current value */
};

/**
 * Create a deque
 * @param thread_safe If the deque needs to be thread safe
 * @param deque The deque
 * @return 0 if success, otherwise 1
 */
int
pgagroal_deque_create(bool thread_safe, struct deque** deque);

/**
 * Add a node to deque's tail, the tag will be copied
 * This function is thread safe
 * @param deque The deque
 * @param tag The tag,optional
 * @param data The data
 * @param type The data type
 * @return 0 if success, otherwise 1
 */
int
pgagroal_deque_add(struct deque* deque, char* tag, uintptr_t data, enum value_type type);

/**
 * Remove all the nodes with the given tag
 * @param deque The deque
 * @param tag The tag
 * @return Number of nodes removed
 */
int
pgagroal_deque_remove(struct deque* deque, char* tag);

/**
 * Remove all the nodes inside a deque
 * @param deque The deque
 * @return 0 on success, 1 if otherwise
 */
int
pgagroal_deque_clear(struct deque* deque);

/**
 * Add a node to deque's tail with custom to_string and data destroy callback,
 * the type will be set to ValueRef
 * This function is thread safe
 * @param deque The deque
 * @param tag The tag,optional
 * @param data The data
 * @return 0 if success, otherwise 1
 */
int
pgagroal_deque_add_with_config(struct deque* deque, char* tag, uintptr_t data, struct value_config* config);

/**
 * Retrieve value and remove the node from deque's head.
 * Note that if the value was copied into node,
 * this function will return the original value and tag
 * rather than making a copy of it.
 * This function is thread safe, but the returned value is not protected
 * @param deque The deque
 * @param tag [out] Optional, tag will be returned through if not NULL
 * @return The value data if deque's not empty, otherwise 0
 */
uintptr_t
pgagroal_deque_poll(struct deque* deque, char** tag);

/**
 * Retrieve value and remove the node from deque's tail.
 * Note that if the value was copied into node,
 * this function will return the original value and tag
 * rather than making a copy of it.
 * This function is thread safe, but the returned value is not protected
 * @param deque The deque
 * @param tag [out] Optional, tag will be returned through if not NULL
 * @return The value data if deque's not empty, otherwise 0
 */
uintptr_t
pgagroal_deque_poll_last(struct deque* deque, char** tag);

/**
 * Retrieve value without removing the node from deque's head.
 * Note that if the value was copied into node,
 * this function will return the original value and tag
 * rather than making a copy of it.
 * This function is thread safe, but the returned value is not protected
 * @param deque The deque
 * @param tag [out] Optional, tag will be returned through if not NULL
 * @return The value data if deque's not empty, otherwise 0
 */
uintptr_t
pgagroal_deque_peek(struct deque* deque, char** tag);

/**
 * Retrieve value without removing the node from deque's tail.
 * Note that if the value was copied into node,
 * this function will return the original value and tag
 * rather than making a copy of it.
 * This function is thread safe, but the returned value is not protected
 * @param deque The deque
 * @param tag [out] Optional, tag will be returned through if not NULL
 * @return The value data if deque's not empty, otherwise 0
 */
uintptr_t
pgagroal_deque_peek_last(struct deque* deque, char** tag);

/**
 * Get the data for the specified tag
 * @param deque The deque
 * @param tag The tag
 * @return The data, or 0
 */
uintptr_t
pgagroal_deque_get(struct deque* deque, char* tag);

/**
 * Does the tag exists
 * @param deque The deque
 * @param tag The tag
 * @return True if exists, otherwise false
 */
bool
pgagroal_deque_exists(struct deque* deque, char* tag);

/**
 * Create a deque iterator
 * @param deque The deque
 * @param iter [out] The iterator
 * @return 0 on success, 1 if otherwise
 */
int
pgagroal_deque_iterator_create(struct deque* deque, struct deque_iterator** iter);

/**
 * Get the next deque value
 * @param iter The iterator
 * @return true if has next, false if otherwise
 */
bool
pgagroal_deque_iterator_next(struct deque_iterator* iter);

/**
 * Check if iterator has next value without advancing it
 * @param iter The iterator
 * @return true if has next, false if otherwise
 */
bool
pgagroal_deque_iterator_has_next(struct deque_iterator* iter);

/**
 * Remove the current node iterator points to and place the iterator to the previous node
 * @param iter The iterator
 */
void
pgagroal_deque_iterator_remove(struct deque_iterator* iter);

/**
 * Destroy a deque iterator
 * @param iter The iterator
 */
void
pgagroal_deque_iterator_destroy(struct deque_iterator* iter);

/**
 * Get the size of the deque
 * @param deque The deque
 * @return The size
 */
uint32_t
pgagroal_deque_size(struct deque* deque);

/**
 * Check if the deque is empty
 * @param deque The deque
 * @return true if deque size is 0, otherwise false
 */
bool
pgagroal_deque_empty(struct deque* deque);

/**
 * List the nodes in the deque
 * @param deque The deque
 */
void
pgagroal_deque_list(struct deque* deque);

/**
 * Sort the deque
 * @param deque The deque
 * @param compare [Optional] The compare function pointer, if NULL then tag comparison is used
 */
void
pgagroal_deque_sort(struct deque* deque, compare_cb compare);

/**
 * Convert what's inside deque to string
 * @param deque The deque
 * @param format The format
 * @param tag [Optional] The tag, which will be applied before the content if not null
 * @param indent The current indentation
 * @return The string
 */
char*
pgagroal_deque_to_string(struct deque* deque, int32_t format, char* tag, int indent);

/**
 * Destroy the deque and free its and its nodes' memory
 * @param deque The deque
 */
void
pgagroal_deque_destroy(struct deque* deque);

#ifdef __cplusplus
}
#endif

#endif
