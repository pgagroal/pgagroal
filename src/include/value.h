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

#ifndef PGAGROAL_VALUE_H
#define PGAGROAL_VALUE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <inttypes.h>
#include <stdbool.h>

typedef void (*data_destroy_cb)(uintptr_t data);
typedef char* (*data_to_string_cb)(uintptr_t data, int32_t format, char* tag, int indent);

enum value_type {
   ValueNone,
   ValueInt8,
   ValueUInt8,
   ValueInt16,
   ValueUInt16,
   ValueInt32,
   ValueUInt32,
   ValueInt64,
   ValueUInt64,
   ValueChar,
   ValueBool,
   ValueString,
   ValueStringRef,
   ValueFloat,
   ValueDouble,
   ValueBASE64,
   ValueBASE64Ref,
   ValueJSON,
   ValueJSONRef,
   ValueDeque,
   ValueDequeRef,
   ValueART,
   ValueARTRef,
   ValueRef,
   ValueMem,
};

/**
 * @struct value
 * Defines a universal value
 */
struct value
{
   enum value_type type;         /**< The type of value data */
   uintptr_t data;               /**< The data, could be passed by value or by reference */
   data_destroy_cb destroy_data; /**< The callback to destroy data */
   data_to_string_cb to_string;  /**< The callback to convert data to string */
};

/**
 * @struct value_config
 * Defines configuration for managing a value
 */
struct value_config
{
   data_destroy_cb destroy_data; /**< The callback to destroy data */
   data_to_string_cb to_string;  /**< The callback to convert data to string */
};

/**
 * Create a value based on the data and value type
 * @param type The value type, use ValueRef if you are only storing pointers without the need to manage memory,
 * use ValueMem if you are storing pointers to a chunk of memory that needs to and can be simply freed
 * (meaning it can't have pointers to other malloced memories)
 * @param data The value data, type cast it to uintptr_t before passing into function
 * @param value [out] The value
 * @return 0 on success, 1 if otherwise
 */
int
pgagroal_value_create(enum value_type type, uintptr_t data, struct value** value);

/**
 * Create a value with a config for customized destroy or to_string callback,
 * the type will default to ValueRef
 * @param data The value data, type cast it to uintptr_t before passing into function
 * @param config The configuration
 * @param value [out] The value
 * @return 0 on success, 1 if otherwise
 */
int
pgagroal_value_create_with_config(uintptr_t data, struct value_config* config, struct value** value);

/**
 * Destroy a value along with the data within
 * @param value The value
 * @return 0 on success, 1 if otherwise
 */
int
pgagroal_value_destroy(struct value* value);

/**
 * Get the raw data from the value, which can be casted back to its original type
 * @param value The value
 * @return The value data within
 */
uintptr_t
pgagroal_value_data(struct value* value);

/**
 * Get the type of the value
 * @param value The value
 * @return The value type, or ValueNone if value is NULL
 */
enum value_type
pgagroal_value_type(struct value* value);

/**
 * Convert a value to string
 * @param value The value
 * @param format The format
 * @param tag The optional tag
 * @param indent The indent
 * @return The string
 */
char*
pgagroal_value_to_string(struct value* value, int32_t format, char* tag, int indent);

/**
 * Convert a double value to value data, since straight type cast discards the decimal part
 * @param val The value
 * @return The value data
 */
uintptr_t
pgagroal_value_from_double(double val);

/**
 * Convert a value data to double
 * @param val The value
 * @return
 */
double
pgagroal_value_to_double(uintptr_t val);

/**
 * Convert a float value to value data, since straight type cast discards the decimal part
 * @param val The value
 * @return The value data
 */
uintptr_t
pgagroal_value_from_float(float val);

/**
 * Convert a value data to float
 * @param val The value
 * @return
 */
float
pgagroal_value_to_float(uintptr_t val);

enum value_type
pgagroal_value_to_ref(enum value_type type);

/**
 * Translate the type to string for debugging purpose
 * @param type The type
 * @return The type
 */
char*
pgagroal_value_type_to_string(enum value_type type);

/**
 * Compare two values based on their type
 * @param a The first value
 * @param b The second value
 * @return -1 if a < b, 0 if a == b, 1 if a > b
 */
int
pgagroal_value_compare(struct value* a, struct value* b);

#ifdef __cplusplus
}
#endif

#endif
