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

#ifndef PGAGROAL_JSON_H
#define PGAGROAL_JSON_H

#ifdef __cplusplus
extern "C" {
#endif

/* pgagroal */
#include <pgagroal.h>
#include <deque.h>
#include <utils.h>
#include <value.h>

/* System */
#include <stdarg.h>

enum json_type {
   JSONUnknown,
   JSONItem,
   JSONArray
};

/** @struct json
 * Defines a JSON structure
 */
struct json
{
   // a json object can only be item or array
   enum json_type type; /**< The json object type */
   // if the object is an array, it can have at most one json element
   void* elements; /**< The json elements, could be an array or some kv pairs */
};

/** @struct json_iterator
 * Defines a JSON iterator
 */
struct json_iterator
{
   void* iter;          /**< The internal iterator */
   struct json* obj;    /**< The json object */
   char* key;           /**< The current key, if it's json item */
   struct value* value; /**< The current value or entry */
};

/**
 * Create a json object
 * @param item [out] The json item
 * @return 0 if success, 1 if otherwise
 */
int
pgagroal_json_create(struct json** object);

/**
 * Put a key value pair into the json item,
 * if the key exists, value will be overwritten,
 *
 * If the kv pair is put into an empty json object, it will be treated as json item,
 * otherwise if the json object is an array, it will reject the kv pair
 * @param item The json item
 * @param key The json key
 * @param val The value data
 * @param type The value type
 * @return 0 on success, otherwise 1
 */

/**
 * Append an entry into the json array
 * If the entry is put into an empty json object, it will be treated as json array,
 * otherwise if the json object is an item, it will reject the entry
 * @param array The json array
 * @param entry The entry data
 * @param type The entry value type
 * @return 0 is successful,
 * otherwise when the json object is an array, value is null, or value type conflicts with old value, 1 will be returned
 */
int
pgagroal_json_append(struct json* array, uintptr_t entry, enum value_type type);

int
pgagroal_json_put(struct json* item, char* key, uintptr_t val, enum value_type type);

/**
 * Remove a key and destroy the associated value within the json item.
 * If the key does not exist or the json object is an array, the function will be noop.
 * @param item The json item
 * @param key The key
 * @return 0 on success or key not found, otherwise 1
 */
int
pgagroal_json_remove(struct json* item, char* key);

/**
 * Clear all the entries or key value pairs inside a json object
 * @param obj The json object
 * @return 0 on success, otherwise 1
 */
int
pgagroal_json_clear(struct json* obj);

/**
 * Get the value data from json item
 * @param item The item
 * @param tag The tag
 * @return The value data, 0 if not found
 */
uintptr_t
pgagroal_json_get(struct json* item, char* tag);

/**
 * Get the value data and its type from json item
 * @param item The item
 * @param tag The tag
 * @param type [out] The value type, ValueNone if tag not found
 * @return The value data, 0 if not found
 */
uintptr_t
pgagroal_json_get_typed(struct json* item, char* tag, enum value_type* type);

/**
 * Check if the json item contains the given key
 * @param item The json item
 * @param key The key
 * @return True if the key exists, otherwise false
 */
bool
pgagroal_json_contains_key(struct json* item, char* key);

/**
 * Get json array length
 * @param array The json array
 * @return The length
 */
uint32_t
pgagroal_json_array_length(struct json* array);

/**
 * Create a json iterator
 * @param object The JSON object
 * @param iter [out] The iterator
 * @return 0 on success, 1 if otherwise
 */
int
pgagroal_json_iterator_create(struct json* object, struct json_iterator** iter);
/**
 * Get the next kv pair/entry from JSON object
 * @param iter The iterator
 * @return true if has next, false if otherwise
 */
bool
pgagroal_json_iterator_next(struct json_iterator* iter);

/**
 * Check if the JSON object has next kv pair/entry
 * @param iter The iterator
 * @return true if has next, false if otherwise
 */
bool
pgagroal_json_iterator_has_next(struct json_iterator* iter);

/**
 * Destroy a iterator
 * @param iter The iterator
 */
void
pgagroal_json_iterator_destroy(struct json_iterator* iter);

/**
 * Parse a string into json item
 * @param str The string
 * @param obj [out] The json object
 * @return 0 if success, 1 if otherwise
 */
int
pgagroal_json_parse_string(char* str, struct json** obj);

/**
 * Clone a json object
 * @param from The from object
 * @param to [out] The to object
 * @return 0 if success, 1 if otherwise
 */
int
pgagroal_json_clone(struct json* from, struct json** to);

/**
 * Convert a json to string
 * @param object The json object
 * @param format The format
 * @param tag The optional tag
 * @param indent The indent
 * @return The json formatted string
 */
char*
pgagroal_json_to_string(struct json* object, int32_t format, char* tag, int indent);

/**
 * Print a json object
 * @param object The object
 * @param format The format
 * @param indent_per_level The indent per level
 */
void
pgagroal_json_print(struct json* object, int32_t format);

/**
 * Destroy the json object
 * @param item The json object
 * @return 0 if success, 1 if otherwise
 */
int
pgagroal_json_destroy(struct json* object);

/**
 * Read a json from disk
 * @param path The path
 * @param obj [out] The json object
 * @return 0 if success, 1 if otherwise
 */
int
pgagroal_json_read_file(char* path, struct json** obj);

/**
 * Write a json file to disk
 * @param path The path
 * @param obj The json object
 * @return 0 if success, 1 if otherwise
 */
int
pgagroal_json_write_file(char* path, struct json* obj);

/**
 * Put an enum value as a nested JSON object { "value": N, "string_value": "label" }
 * @param res The JSON object
 * @param key The key
 * @param value The numeric enum value
 * @param to_str The converter function
 */
void
pgagroal_json_put_enum_value(struct json* res, char* key, int value, int (*to_str)(char*, int));

/**
 * Put a time value as a nested JSON object { "value": N, "string_value": "Ns" }
 * @param res The JSON object
 * @param key The key
 * @param t The time value
 * @param format The time format (e.g., FORMAT_TIME_S, FORMAT_TIME_MS)
 */
void
pgagroal_json_put_time_value(struct json* res, char* key, pgagroal_time_t t, enum pgagroal_time_format_t format);

/**
 * Put a size value as a nested JSON object { "value": N, "string_value": "NB" }
 * @param res The JSON object
 * @param key The key
 * @param bytes The size in bytes
 */
void
pgagroal_json_put_size_value(struct json* res, char* key, unsigned int bytes);

#ifdef __cplusplus
}
#endif

#endif
