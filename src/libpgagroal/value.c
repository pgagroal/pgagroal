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
/* pgagroal */
#include <art.h>
#include <deque.h>
#include <json.h>
#include <utils.h>
#include <value.h>

/* System */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void noop_destroy_cb(uintptr_t data);
static void free_destroy_cb(uintptr_t data);
static void art_destroy_cb(uintptr_t data);
static void deque_destroy_cb(uintptr_t data);
static void json_destroy_cb(uintptr_t data);
static char* noop_to_string_cb(uintptr_t data, int32_t format, char* tag, int indent);
static char* int8_to_string_cb(uintptr_t data, int32_t format, char* tag, int indent);
static char* uint8_to_string_cb(uintptr_t data, int32_t format, char* tag, int indent);
static char* int16_to_string_cb(uintptr_t data, int32_t format, char* tag, int indent);
static char* uint16_to_string_cb(uintptr_t data, int32_t format, char* tag, int indent);
static char* int32_to_string_cb(uintptr_t data, int32_t format, char* tag, int indent);
static char* uint32_to_string_cb(uintptr_t data, int32_t format, char* tag, int indent);
static char* int64_to_string_cb(uintptr_t data, int32_t format, char* tag, int indent);
static char* uint64_to_string_cb(uintptr_t data, int32_t format, char* tag, int indent);
static char* float_to_string_cb(uintptr_t data, int32_t format, char* tag, int indent);
static char* double_to_string_cb(uintptr_t data, int32_t format, char* tag, int indent);
static char* string_to_string_cb(uintptr_t data, int32_t format, char* tag, int indent);
static char* char_to_string_cb(uintptr_t data, int32_t format, char* tag, int indent);
static char* bool_to_string_cb(uintptr_t data, int32_t format, char* tag, int indent);
static char* deque_to_string_cb(uintptr_t data, int32_t format, char* tag, int indent);
static char* art_to_string_cb(uintptr_t data, int32_t format, char* tag, int indent);
static char* json_to_string_cb(uintptr_t data, int32_t format, char* tag, int indent);
static char* mem_to_string_cb(uintptr_t data, int32_t format, char* tag, int indent);

int
pgagroal_value_create(enum value_type type, uintptr_t data, struct value** value)
{
   struct value* val = NULL;
   val = (struct value*)malloc(sizeof(struct value));
   if (val == NULL)
   {
      goto error;
   }
   val->data = 0;
   val->type = type;
   switch (type)
   {
      case ValueNone:
         val->to_string = noop_to_string_cb;
         break;
      case ValueInt8:
         val->to_string = int8_to_string_cb;
         break;
      case ValueUInt8:
         val->to_string = uint8_to_string_cb;
         break;
      case ValueInt16:
         val->to_string = int16_to_string_cb;
         break;
      case ValueUInt16:
         val->to_string = uint16_to_string_cb;
         break;
      case ValueInt32:
         val->to_string = int32_to_string_cb;
         break;
      case ValueUInt32:
         val->to_string = uint32_to_string_cb;
         break;
      case ValueInt64:
         val->to_string = int64_to_string_cb;
         break;
      case ValueUInt64:
         val->to_string = uint64_to_string_cb;
         break;
      case ValueFloat:
         val->to_string = float_to_string_cb;
         break;
      case ValueDouble:
         val->to_string = double_to_string_cb;
         break;
      case ValueBool:
         val->to_string = bool_to_string_cb;
         break;
      case ValueChar:
         val->to_string = char_to_string_cb;
         break;
      case ValueString:
      case ValueBASE64:
      case ValueStringRef:
      case ValueBASE64Ref:
         val->to_string = string_to_string_cb;
         break;
      case ValueJSON:
      case ValueJSONRef:
         val->to_string = json_to_string_cb;
         break;
      case ValueDeque:
      case ValueDequeRef:
         val->to_string = deque_to_string_cb;
         break;
      case ValueART:
      case ValueARTRef:
         val->to_string = art_to_string_cb;
         break;
      case ValueMem:
      case ValueRef:
         val->to_string = mem_to_string_cb;
         break;
      default:
         val->to_string = noop_to_string_cb;
         break;
   }
   switch (type)
   {
      case ValueString:
      {
         val->data = (uintptr_t)pgagroal_append(NULL, (char*)data);
         val->destroy_data = free_destroy_cb;
         break;
      }
      case ValueBASE64:
      {
         val->data = (uintptr_t)pgagroal_append(NULL, (char*)data);
         val->destroy_data = free_destroy_cb;
         break;
      }
      case ValueMem:
         val->data = data;
         val->destroy_data = free_destroy_cb;
         break;
      case ValueJSON:
         val->data = data;
         val->destroy_data = json_destroy_cb;
         break;
      case ValueDeque:
         val->data = data;
         val->destroy_data = deque_destroy_cb;
         break;
      case ValueART:
         val->data = data;
         val->destroy_data = art_destroy_cb;
         break;
      default:
         val->data = data;
         val->destroy_data = noop_destroy_cb;
         break;
   }
   *value = val;
   return 0;

error:
   return 1;
}

int
pgagroal_value_create_with_config(uintptr_t data, struct value_config* config, struct value** value)
{
   if (pgagroal_value_create(ValueRef, data, value))
   {
      return 1;
   }
   if (config != NULL)
   {
      if (config->destroy_data != NULL)
      {
         (*value)->destroy_data = config->destroy_data;
      }
      if (config->to_string != NULL)
      {
         (*value)->to_string = config->to_string;
      }
   }
   return 0;
}

int
pgagroal_value_destroy(struct value* value)
{
   if (value == NULL)
   {
      return 0;
   }
   value->destroy_data(value->data);
   free(value);
   return 0;
}

uintptr_t
pgagroal_value_data(struct value* value)
{
   if (value == NULL)
   {
      return 0;
   }
   return value->data;
}

enum value_type
pgagroal_value_type(struct value* value)
{
   if (value == NULL)
   {
      return ValueNone;
   }
   return value->type;
}

char*
pgagroal_value_to_string(struct value* value, int32_t format, char* tag, int indent)
{
   return value->to_string(value->data, format, tag, indent);
}

uintptr_t
pgagroal_value_from_double(double val)
{
   union duni
   {
      double val;
      uintptr_t data;
   };
   union duni uni;
   uni.val = val;
   return uni.data;
}

double
pgagroal_value_to_double(uintptr_t val)
{
   union duni
   {
      double val;
      uintptr_t data;
   };
   union duni uni;
   uni.data = val;
   return uni.val;
}

uintptr_t
pgagroal_value_from_float(float val)
{
   union funi
   {
      float val;
      uintptr_t data;
   };
   union funi uni;
   uni.val = val;
   return uni.data;
}

float
pgagroal_value_to_float(uintptr_t val)
{
   union funi
   {
      float val;
      uintptr_t data;
   };
   union funi uni;
   uni.data = val;
   return uni.val;
}

enum value_type
pgagroal_value_to_ref(enum value_type type)
{
   switch (type)
   {
      case ValueString:
         return ValueStringRef;
      case ValueBASE64:
         return ValueBASE64Ref;
      case ValueJSON:
         return ValueJSONRef;
      case ValueDeque:
         return ValueDequeRef;
      case ValueART:
         return ValueARTRef;
      case ValueMem:
         return ValueRef;
      default:
         return type;
   }
}

char*
pgagroal_value_type_to_string(enum value_type type)
{
   switch (type)
   {
      case ValueNone:
         return "none";
      case ValueInt8:
         return "int8";
      case ValueUInt8:
         return "uint8";
      case ValueInt16:
         return "int16";
      case ValueUInt16:
         return "uint16";
      case ValueInt32:
         return "int32";
      case ValueUInt32:
         return "uint32";
      case ValueInt64:
         return "int64";
      case ValueUInt64:
         return "uint64";
      case ValueChar:
         return "char";
      case ValueBool:
         return "bool";
      case ValueString:
         return "string";
      case ValueStringRef:
         return "string_ref";
      case ValueFloat:
         return "float";
      case ValueDouble:
         return "double";
      case ValueBASE64:
         return "base64";
      case ValueBASE64Ref:
         return "base64_ref";
      case ValueJSON:
         return "json";
      case ValueJSONRef:
         return "json_ref";
      case ValueDeque:
         return "deque";
      case ValueDequeRef:
         return "deque_ref";
      case ValueART:
         return "art";
      case ValueARTRef:
         return "art_ref";
      case ValueRef:
         return "ref";
      case ValueMem:
         return "mem";
      default:
         return "unknown type";
   }
}

static void
noop_destroy_cb(uintptr_t data)
{
   (void)data;
}

static void
free_destroy_cb(uintptr_t data)
{
   free((void*)data);
}

static void
art_destroy_cb(uintptr_t data)
{
   pgagroal_art_destroy((struct art*)data);
}

static void
deque_destroy_cb(uintptr_t data)
{
   pgagroal_deque_destroy((struct deque*)data);
}

static void
json_destroy_cb(uintptr_t data)
{
   pgagroal_json_destroy((struct json*)data);
}

static char*
noop_to_string_cb(uintptr_t data, int32_t format, char* tag, int indent)
{
   char* ret = NULL;
   ret = pgagroal_indent(ret, tag, indent);
   (void)data;
   (void)format;
   return ret;
}

static char*
int8_to_string_cb(uintptr_t data, int32_t format __attribute__((unused)), char* tag, int indent)
{
   char* ret = NULL;
   char buf[MISC_LENGTH];

   ret = pgagroal_indent(ret, tag, indent);
   memset(buf, 0, MISC_LENGTH);
   snprintf(buf, MISC_LENGTH, "%" PRId8, (int8_t)data);
   ret = pgagroal_append(ret, buf);
   return ret;
}

static char*
uint8_to_string_cb(uintptr_t data, int32_t format __attribute__((unused)), char* tag, int indent)
{
   char* ret = NULL;
   char buf[MISC_LENGTH];
   ret = pgagroal_indent(ret, tag, indent);
   memset(buf, 0, MISC_LENGTH);
   snprintf(buf, MISC_LENGTH, "%" PRIu8, (uint8_t)data);
   ret = pgagroal_append(ret, buf);
   return ret;
}

static char*
int16_to_string_cb(uintptr_t data, int32_t format __attribute__((unused)), char* tag, int indent)
{
   char* ret = NULL;
   char buf[MISC_LENGTH];

   ret = pgagroal_indent(ret, tag, indent);
   memset(buf, 0, MISC_LENGTH);
   snprintf(buf, MISC_LENGTH, "%" PRId16, (int16_t)data);
   ret = pgagroal_append(ret, buf);
   return ret;
}

static char*
uint16_to_string_cb(uintptr_t data, int32_t format __attribute__((unused)), char* tag, int indent)
{
   char* ret = NULL;
   char buf[MISC_LENGTH];

   ret = pgagroal_indent(ret, tag, indent);
   memset(buf, 0, MISC_LENGTH);
   snprintf(buf, MISC_LENGTH, "%" PRIu16, (uint16_t)data);
   ret = pgagroal_append(ret, buf);
   return ret;
}

static char*
int32_to_string_cb(uintptr_t data, int32_t format __attribute__((unused)), char* tag, int indent)
{
   char* ret = NULL;
   char buf[MISC_LENGTH];

   ret = pgagroal_indent(ret, tag, indent);
   memset(buf, 0, MISC_LENGTH);
   snprintf(buf, MISC_LENGTH, "%" PRId32, (int32_t)data);
   ret = pgagroal_append(ret, buf);
   return ret;
}

static char*
uint32_to_string_cb(uintptr_t data, int32_t format __attribute__((unused)), char* tag, int indent)
{
   char* ret = NULL;
   char buf[MISC_LENGTH];

   ret = pgagroal_indent(ret, tag, indent);
   memset(buf, 0, MISC_LENGTH);
   snprintf(buf, MISC_LENGTH, "%" PRIu32, (uint32_t)data);
   ret = pgagroal_append(ret, buf);
   return ret;
}

static char*
int64_to_string_cb(uintptr_t data, int32_t format __attribute__((unused)), char* tag, int indent)
{
   char* ret = NULL;
   char buf[MISC_LENGTH];

   ret = pgagroal_indent(ret, tag, indent);
   memset(buf, 0, MISC_LENGTH);
   snprintf(buf, MISC_LENGTH, "%" PRId64, (int64_t)data);
   ret = pgagroal_append(ret, buf);
   return ret;
}

static char*
uint64_to_string_cb(uintptr_t data, int32_t format __attribute__((unused)), char* tag, int indent)
{
   char* ret = NULL;
   char buf[MISC_LENGTH];

   ret = pgagroal_indent(ret, tag, indent);
   memset(buf, 0, MISC_LENGTH);
   snprintf(buf, MISC_LENGTH, "%" PRIu64, (uint64_t)data);
   ret = pgagroal_append(ret, buf);
   return ret;
}

static char*
float_to_string_cb(uintptr_t data, int32_t format __attribute__((unused)), char* tag, int indent)
{
   char* ret = NULL;
   char buf[MISC_LENGTH];

   ret = pgagroal_indent(ret, tag, indent);
   memset(buf, 0, MISC_LENGTH);
   snprintf(buf, MISC_LENGTH, "%f", pgagroal_value_to_float(data));
   ret = pgagroal_append(ret, buf);
   return ret;
}

static char*
double_to_string_cb(uintptr_t data, int32_t format __attribute__((unused)), char* tag, int indent)
{
   char* ret = NULL;
   char buf[MISC_LENGTH];

   ret = pgagroal_indent(ret, tag, indent);
   memset(buf, 0, MISC_LENGTH);
   snprintf(buf, MISC_LENGTH, "%f", pgagroal_value_to_double(data));
   ret = pgagroal_append(ret, buf);

   return ret;
}

static char*
string_to_string_cb(uintptr_t data, int32_t format __attribute__((unused)), char* tag, int indent)
{
   char* ret = NULL;
   char* str = (char*)data;
   char* translated_string = NULL;

   ret = pgagroal_indent(ret, tag, indent);
   if (str == NULL)
   {
      if (format == FORMAT_JSON || format == FORMAT_JSON_COMPACT)
      {
         ret = pgagroal_append(ret, "null");
      }
   }
   else if (strlen(str) == 0)
   {
      if (format == FORMAT_JSON || format == FORMAT_JSON_COMPACT)
      {
         ret = pgagroal_append(ret, "\"\"");
      }
      else if (format == FORMAT_TEXT)
      {
         ret = pgagroal_append(ret, "''");
      }
   }
   else
   {
      if (format == FORMAT_JSON || format == FORMAT_JSON_COMPACT)
      {
         translated_string = pgagroal_escape_string(str);
         ret = pgagroal_append(ret, "\"");
         ret = pgagroal_append(ret, translated_string);
         ret = pgagroal_append(ret, "\"");
         free(translated_string);
      }
      else if (format == FORMAT_TEXT)
      {
         ret = pgagroal_append(ret, str);
      }
   }
   return ret;
}

static char*
bool_to_string_cb(uintptr_t data, int32_t format __attribute__((unused)), char* tag, int indent)
{
   char* ret = NULL;
   bool val = (bool)data;
   ret = pgagroal_indent(ret, tag, indent);
   ret = pgagroal_append(ret, val ? "true" : "false");
   return ret;
}

static char*
char_to_string_cb(uintptr_t data, int32_t format __attribute__((unused)), char* tag, int indent)
{
   char* ret = NULL;
   char buf[MISC_LENGTH];

   ret = pgagroal_indent(ret, tag, indent);
   memset(buf, 0, MISC_LENGTH);
   snprintf(buf, MISC_LENGTH, "'%c'", (char)data);
   ret = pgagroal_append(ret, buf);

   return ret;
}

static char*
deque_to_string_cb(uintptr_t data, int32_t format __attribute__((unused)), char* tag, int indent)
{
   return pgagroal_deque_to_string((struct deque*)data, format, tag, indent);
}

static char*
art_to_string_cb(uintptr_t data, int32_t format __attribute__((unused)), char* tag, int indent)
{
   return pgagroal_art_to_string((struct art*)data, format, tag, indent);
}

static char*
json_to_string_cb(uintptr_t data, int32_t format, char* tag, int indent)
{
   return pgagroal_json_to_string((struct json*)data, format, tag, indent);
}

static char*
mem_to_string_cb(uintptr_t data, int32_t format __attribute__((unused)), char* tag, int indent)
{
   char* ret = NULL;
   char buf[MISC_LENGTH];

   ret = pgagroal_indent(ret, tag, indent);
   memset(buf, 0, MISC_LENGTH);
   snprintf(buf, MISC_LENGTH, "%p", (void*)data);
   ret = pgagroal_append(ret, buf);

   return ret;
}
