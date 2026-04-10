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
#include <pgagroal.h>
#include <art.h>
#include <json.h>
#include <logging.h>
#include <message.h>
#include <utils.h>

/* System */
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static bool type_allowed(enum value_type type);
static char* item_to_string(struct json* item, int32_t format, char* tag, int indent);
static char* array_to_string(struct json* array, int32_t format, char* tag, int indent);
static int parse_string(char* str, uint64_t* index, struct json** obj);
static int json_add(struct json* obj, char* key, uintptr_t val, enum value_type type);
static int fill_value(char* str, char* key, uint64_t* index, struct json* o);
static bool value_start(char ch);
static int handle_escape_char(char* str, uint64_t* index, uint64_t len, char* ch);

int
pgagroal_json_append(struct json* array, uintptr_t entry, enum value_type type)
{
   if (array != NULL && array->type == JSONUnknown)
   {
      array->type = JSONArray;
      pgagroal_deque_create(false, (struct deque**)&array->elements);
   }
   if (array == NULL || array->type != JSONArray || !type_allowed(type))
   {
      goto error;
   }
   return pgagroal_deque_add(array->elements, NULL, entry, type);
error:
   return 1;
}

int
pgagroal_json_put(struct json* item, char* key, uintptr_t val, enum value_type type)
{
   if (item != NULL && item->type == JSONUnknown)
   {
      item->type = JSONItem;
      pgagroal_art_create((struct art**)&item->elements);
   }
   if (item == NULL || item->type != JSONItem || !type_allowed(type) || key == NULL || strlen(key) == 0)
   {
      goto error;
   }
   return pgagroal_art_insert((struct art*)item->elements, key, val, type);
error:
   return 1;
}

int
pgagroal_json_remove(struct json* item, char* key)
{
   struct art* tree = NULL;
   if (item == NULL || key == NULL || strlen(key) == 0 || item->type != JSONItem)
   {
      if (item != NULL && item->type == JSONUnknown)
      {
         return 0;
      }
      goto error;
   }

   tree = (struct art*)item->elements;
   if (tree->size == 0)
   {
      // no need to delete from an empty map
      return 0;
   }
   if (pgagroal_art_delete(tree, key))
   {
      goto error;
   }

   if (tree->size == 0)
   {
      // reset json object status, so that it may become an array in the future
      item->type = JSONUnknown;
      pgagroal_art_destroy(tree);
      item->elements = NULL;
   }

   return 0;

error:
   return 1;
}

int
pgagroal_json_clear(struct json* obj)
{
   if (obj == NULL || obj->elements == NULL)
   {
      return 0;
   }
   switch (obj->type)
   {
      case JSONArray:
         pgagroal_deque_clear((struct deque*)obj->elements);
         break;
      case JSONItem:
         pgagroal_art_clear((struct art*)obj->elements);
         break;
      case JSONUnknown:
         return 1;
   }
   return 0;
}

int
pgagroal_json_create(struct json** object)
{
   struct json* o = malloc(sizeof(struct json));
   memset(o, 0, sizeof(struct json));
   o->type = JSONUnknown;
   *object = o;
   return 0;
}

int
pgagroal_json_destroy(struct json* object)
{
   if (object == NULL)
   {
      return 0;
   }
   if (object->type == JSONArray)
   {
      pgagroal_deque_destroy(object->elements);
   }
   else if (object->type == JSONItem)
   {
      pgagroal_art_destroy(object->elements);
   }
   free(object);
   return 0;
}

char*
pgagroal_json_to_string(struct json* object, int32_t format, char* tag, int indent)
{
   char* str = NULL;
   if (object == NULL || (object->type == JSONUnknown || object->elements == NULL))
   {
      str = pgagroal_indent(str, tag, indent);
      str = pgagroal_append(str, "{}");
      return str;
   }
   if (object->type != JSONArray)
   {
      return item_to_string(object, format, tag, indent);
   }
   else
   {
      return array_to_string(object, format, tag, indent);
   }
}

void
pgagroal_json_print(struct json* object, int32_t format)
{
   char* str = pgagroal_json_to_string(object, format, NULL, 0);
   printf("%s\n", str);
   free(str);
}

uint32_t
pgagroal_json_array_length(struct json* array)
{
   if (array == NULL || array->type != JSONArray)
   {
      goto error;
   }
   return pgagroal_deque_size(array->elements);
error:
   return 0;
}

uintptr_t
pgagroal_json_get(struct json* item, char* tag)
{
   if (item == NULL || item->type != JSONItem || tag == NULL || strlen(tag) == 0)
   {
      return 0;
   }
   return pgagroal_art_search(item->elements, tag);
}

uintptr_t
pgagroal_json_get_typed(struct json* item, char* tag, enum value_type* type)
{
   if (item == NULL || item->type != JSONItem || tag == NULL || strlen(tag) == 0)
   {
      *type = ValueNone;
      return 0;
   }
   return pgagroal_art_search_typed(item->elements, tag, type);
}

bool
pgagroal_json_contains_key(struct json* item, char* key)
{
   if (item == NULL || item->type != JSONItem || key == NULL || strlen(key) == 0)
   {
      return false;
   }
   return pgagroal_art_contains_key(item->elements, key);
}

int
pgagroal_json_iterator_create(struct json* object, struct json_iterator** iter)
{
   struct json_iterator* i = NULL;
   if (object == NULL || object->type == JSONUnknown)
   {
      return 1;
   }
   i = malloc(sizeof(struct json_iterator));
   memset(i, 0, sizeof(struct json_iterator));
   i->obj = object;
   if (object->type == JSONItem)
   {
      pgagroal_art_iterator_create(object->elements, (struct art_iterator**)(&i->iter));
   }
   else
   {
      pgagroal_deque_iterator_create(object->elements, (struct deque_iterator**)(&i->iter));
   }
   *iter = i;
   return 0;
}

void
pgagroal_json_iterator_destroy(struct json_iterator* iter)
{
   if (iter == NULL)
   {
      return;
   }
   if (iter->obj->type == JSONArray)
   {
      pgagroal_deque_iterator_destroy((struct deque_iterator*)iter->iter);
   }
   else
   {
      pgagroal_art_iterator_destroy((struct art_iterator*)iter->iter);
   }
   free(iter);
}

bool
pgagroal_json_iterator_next(struct json_iterator* iter)
{
   bool has_next = false;
   if (iter == NULL || iter->iter == NULL)
   {
      return false;
   }
   if (iter->obj->type == JSONArray)
   {
      has_next = pgagroal_deque_iterator_next((struct deque_iterator*)iter->iter);
      if (has_next)
      {
         iter->value = ((struct deque_iterator*)iter->iter)->value;
      }
   }
   else
   {
      has_next = pgagroal_art_iterator_next((struct art_iterator*)iter->iter);
      if (has_next)
      {
         iter->value = ((struct art_iterator*)iter->iter)->value;
         iter->key = ((struct art_iterator*)iter->iter)->key;
      }
   }
   return has_next;
}

bool
pgagroal_json_iterator_has_next(struct json_iterator* iter)
{
   bool has_next = false;
   if (iter == NULL || iter->iter == NULL)
   {
      return false;
   }
   if (iter->obj->type == JSONArray)
   {
      has_next = pgagroal_deque_iterator_has_next((struct deque_iterator*)iter->iter);
   }
   else
   {
      has_next = pgagroal_art_iterator_has_next((struct art_iterator*)iter->iter);
   }
   return has_next;
}

int
pgagroal_json_parse_string(char* str, struct json** obj)
{
   uint64_t idx = 0;
   if (str == NULL || strlen(str) < 2)
   {
      return 1;
   }

   return parse_string(str, &idx, obj);
}

int
pgagroal_json_clone(struct json* from, struct json** to)
{
   struct json* o = NULL;
   char* str = NULL;
   str = pgagroal_json_to_string(from, FORMAT_JSON, NULL, 0);
   if (pgagroal_json_parse_string(str, &o))
   {
      goto error;
   }
   *to = o;
   free(str);
   return 0;
error:
   free(str);
   return 1;
}

static int
parse_string(char* str, uint64_t* index, struct json** obj)
{
   enum json_type type;
   struct json* o = NULL;
   uint64_t idx = *index;
   char ch = str[idx];
   char* key = NULL;
   uint64_t len = strlen(str);

   if (ch == '{')
   {
      type = JSONItem;
   }
   else if (ch == '[')
   {
      type = JSONArray;
   }
   else
   {
      goto error;
   }
   idx++;
   pgagroal_json_create(&o);
   if (type == JSONItem)
   {
      while (idx < len)
      {
         // pre key
         while (idx < len && isspace(str[idx]))
         {
            idx++;
         }
         if (idx == len)
         {
            goto error;
         }
         if (str[idx] == ',')
         {
            idx++;
         }
         else if (str[idx] == '}')
         {
            idx++;
            break;
         }
         else if (!(str[idx] == '"' && o->type == JSONUnknown))
         {
            // if it's first key we won't see comma, otherwise we must see comma
            goto error;
         }
         while (idx < len && str[idx] != '"')
         {
            idx++;
         }
         if (idx == len)
         {
            goto error;
         }
         idx++;
         // The key
         while (idx < len && str[idx] != '"')
         {
            char ec_ch;
            // handle escape character
            if (str[idx] == '\\')
            {
               if (handle_escape_char(str, &idx, len, &ec_ch))
               {
                  goto error;
               }
               key = pgagroal_append_char(key, ec_ch);
               continue;
            }

            key = pgagroal_append_char(key, str[idx++]);
         }
         if (idx == len || key == NULL)
         {
            goto error;
         }
         // The lands between
         while (idx < len && (str[idx] == '"' || isspace(str[idx])))
         {
            idx++;
         }
         if (idx == len || str[idx] != ':')
         {
            goto error;
         }
         while (idx < len && (str[idx] == ':' || isspace(str[idx])))
         {
            idx++;
         }
         if (idx == len)
         {
            goto error;
         }
         // The value
         if (fill_value(str, key, &idx, o))
         {
            goto error;
         }
         free(key);
         key = NULL;
      }
   }
   else
   {
      while (idx < len)
      {
         while (idx < len && isspace(str[idx]))
         {
            idx++;
         }
         if (idx == len)
         {
            goto error;
         }
         if (str[idx] == ',')
         {
            idx++;
         }
         else if (str[idx] == ']')
         {
            idx++;
            break;
         }
         else if (!(value_start(str[idx]) && o->type == JSONUnknown))
         {
            // if it's first key we won't see comma, otherwise we must see comma
            goto error;
         }
         while (idx < len && !value_start(str[idx]))
         {
            idx++;
         }
         if (idx == len)
         {
            goto error;
         }

         if (fill_value(str, key, &idx, o))
         {
            goto error;
         }
      }
   }

   *index = idx;
   *obj = o;
   return 0;
error:
   pgagroal_json_destroy(o);
   free(key);
   return 1;
}

static int
json_add(struct json* obj, char* key, uintptr_t val, enum value_type type)
{
   if (obj == NULL)
   {
      return 1;
   }
   if (key == NULL)
   {
      return pgagroal_json_append(obj, val, type);
   }
   return pgagroal_json_put(obj, key, val, type);
}

static bool
value_start(char ch)
{
   return (isdigit(ch) || ch == '-' || ch == '+') || // number
          (ch == '[') ||                             // array
          (ch == '{') ||                             // item
          (ch == '"' || ch == 'n') ||                // string or null string
          (ch == 't' || ch == 'f');                  // potential boolean value
}

static int
fill_value(char* str, char* key, uint64_t* index, struct json* o)
{
   uint64_t idx = *index;
   uint64_t len = strlen(str);
   if (str[idx] == '"')
   {
      char* val = NULL;
      idx++;
      while (idx < len && str[idx] != '"')
      {
         char ec_ch;
         if (str[idx] == '\\')
         {
            if (handle_escape_char(str, &idx, len, &ec_ch))
            {
               goto error;
            }
            val = pgagroal_append_char(val, ec_ch);
            continue;
         }

         val = pgagroal_append_char(val, str[idx++]);
      }
      if (idx == len)
      {
         goto error;
      }
      if (val == NULL)
      {
         json_add(o, key, (uintptr_t)"", ValueString);
      }
      else
      {
         json_add(o, key, (uintptr_t)val, ValueString);
      }
      idx++;
      free(val);
   }
   else if (str[idx] == '-' || str[idx] == '+' || isdigit(str[idx]))
   {
      bool has_digit = false;
      char* val_str = NULL;
      while (idx < len && (isdigit(str[idx]) || str[idx] == '.' || str[idx] == '-' || str[idx] == '+'))
      {
         if (str[idx] == '.')
         {
            has_digit = true;
         }
         val_str = pgagroal_append_char(val_str, str[idx++]);
      }
      if (has_digit)
      {
         double val = 0.;
         if (sscanf(val_str, "%lf", &val) != 1)
         {
            free(val_str);
            goto error;
         }
         json_add(o, key, pgagroal_value_from_double(val), ValueDouble);
         free(val_str);
      }
      else
      {
         int64_t val = 0;
         if (sscanf(val_str, "%" PRId64, &val) != 1)
         {
            free(val_str);
            goto error;
         }
         json_add(o, key, (uintptr_t)val, ValueInt64);
         free(val_str);
      }
   }
   else if (str[idx] == '{')
   {
      struct json* val = NULL;
      if (parse_string(str, &idx, &val))
      {
         goto error;
      }
      json_add(o, key, (uintptr_t)val, ValueJSON);
   }
   else if (str[idx] == '[')
   {
      struct json* val = NULL;
      if (parse_string(str, &idx, &val))
      {
         goto error;
      }
      json_add(o, key, (uintptr_t)val, ValueJSON);
   }
   else if (str[idx] == 'n' || str[idx] == 't' || str[idx] == 'f')
   {
      char* val = NULL;
      while (idx < len && str[idx] >= 'a' && str[idx] <= 'z')
      {
         val = pgagroal_append_char(val, str[idx++]);
      }
      if (pgagroal_compare_string(val, "null"))
      {
         json_add(o, key, 0, ValueString);
      }
      else if (pgagroal_compare_string(val, "true"))
      {
         json_add(o, key, true, ValueBool);
      }
      else if (pgagroal_compare_string(val, "false"))
      {
         json_add(o, key, false, ValueBool);
      }
      else
      {
         free(val);
         goto error;
      }
      free(val);
   }
   else
   {
      goto error;
   }
   *index = idx;
   return 0;
error:
   return 1;
}

static int
handle_escape_char(char* str, uint64_t* index, uint64_t len, char* ch)
{
   uint64_t idx = *index;
   idx++;
   if (idx == len) // security check
   {
      return 1;
   }
   // Check the next character after checking '\' character
   switch (str[idx])
   {
      case '\"':
      case '\\':
         *ch = str[idx];
         break;
      case 'n':
         *ch = '\n';
         break;
      case 't':
         *ch = '\t';
         break;
      case 'r':
         *ch = '\r';
         break;
      default:
         return 1;
   }
   *index = idx + 1;
   return 0;
}

int
pgagroal_json_read_file(char* path, struct json** obj)
{
   FILE* file = NULL;
   char buf[DEFAULT_BUFFER_SIZE];
   char* str = NULL;
   struct json* j = NULL;

   *obj = NULL;

   if (path == NULL)
   {
      goto error;
   }

   file = fopen(path, "r");

   if (file == NULL)
   {
      pgagroal_log_error("Failed to open json file %s", path);
      goto error;
   }

   memset(buf, 0, sizeof(buf));

   // save the last one for ending so that we get to use append
   while (fread(buf, 1, DEFAULT_BUFFER_SIZE - 1, file) > 0)
   {
      str = pgagroal_append(str, buf);
      memset(buf, 0, sizeof(buf));
   }

   if (pgagroal_json_parse_string(str, &j))
   {
      pgagroal_log_error("Failed to parse json file %s", path);
      goto error;
   }

   *obj = j;

   fclose(file);
   free(str);
   return 0;

error:

   pgagroal_json_destroy(j);

   if (file != NULL)
   {
      fclose(file);
   }

   free(str);

   return 1;
}

int
pgagroal_json_write_file(char* path, struct json* obj)
{
   FILE* file = NULL;
   char* str = NULL;

   if (path == NULL || obj == NULL)
   {
      goto error;
   }

   file = fopen(path, "wb");
   if (file == NULL)
   {
      pgagroal_log_error("Failed to create json file %s", path);
      goto error;
   }

   str = pgagroal_json_to_string(obj, FORMAT_JSON, NULL, 0);
   if (str == NULL)
   {
      goto error;
   }

   if (fputs(str, file) == EOF)
   {
      pgagroal_log_error("Failed to write json file %s", path);
      goto error;
   }

   free(str);
   fclose(file);
   return 0;

error:
   free(str);
   if (file != NULL)
   {
      fclose(file);
   }
   return 1;
}

void
pgagroal_json_put_enum_value(struct json* res, char* key, int value, int (*to_str)(char*, int))
{
   struct json* obj = NULL;
   char str_buf[MISC_LENGTH];

   memset(str_buf, 0, MISC_LENGTH);

   if (pgagroal_json_create(&obj))
   {
      pgagroal_json_put(res, key, (uintptr_t)value, ValueInt64);
      return;
   }

   pgagroal_json_put(obj, "value", (uintptr_t)value, ValueInt64);

   if (!to_str(str_buf, value))
   {
      pgagroal_json_put(obj, "string_value", (uintptr_t)str_buf, ValueString);
   }

   pgagroal_json_put(res, key, (uintptr_t)obj, ValueJSON);
}

void
pgagroal_json_put_time_value(struct json* res, char* key, pgagroal_time_t t, enum pgagroal_time_format_t format)
{
   struct json* obj = NULL;
   char* str = NULL;
   int64_t converted;

   converted = pgagroal_time_convert(t, format);

   if (pgagroal_json_create(&obj))
   {
      pgagroal_json_put(res, key, (uintptr_t)converted, ValueInt64);
      return;
   }

   pgagroal_json_put(obj, "value", (uintptr_t)converted, ValueInt64);

   if (!pgagroal_time_format(t, format, &str))
   {
      pgagroal_json_put(obj, "string_value", (uintptr_t)str, ValueString);
      free(str);
   }

   pgagroal_json_put(res, key, (uintptr_t)obj, ValueJSON);
}

void
pgagroal_json_put_size_value(struct json* res, char* key, unsigned int bytes)
{
   struct json* obj = NULL;
   char str_buf[MISC_LENGTH];

   memset(str_buf, 0, MISC_LENGTH);

   if (pgagroal_json_create(&obj))
   {
      pgagroal_json_put(res, key, (uintptr_t)bytes, ValueInt64);
      return;
   }

   pgagroal_json_put(obj, "value", (uintptr_t)bytes, ValueInt64);
   snprintf(str_buf, MISC_LENGTH, "%uB", bytes);
   pgagroal_json_put(obj, "string_value", (uintptr_t)str_buf, ValueString);

   pgagroal_json_put(res, key, (uintptr_t)obj, ValueJSON);
}

static bool
type_allowed(enum value_type type)
{
   switch (type)
   {
      case ValueNone:
      case ValueInt8:
      case ValueUInt8:
      case ValueInt16:
      case ValueUInt16:
      case ValueInt32:
      case ValueUInt32:
      case ValueInt64:
      case ValueUInt64:
      case ValueBool:
      case ValueString:
      case ValueBASE64:
      case ValueFloat:
      case ValueDouble:
      case ValueJSON:
         return true;
      default:
         return false;
   }
}

static char*
item_to_string(struct json* item, int32_t format, char* tag, int indent)
{
   return pgagroal_art_to_string(item->elements, format, tag, indent);
}

static char*
array_to_string(struct json* array, int32_t format, char* tag, int indent)
{
   return pgagroal_deque_to_string(array->elements, format, tag, indent);
}
