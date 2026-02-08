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
 *
 */
#include <pgagroal.h>
#include <json.h>
#include <mctf.h>
#include <utils.h>

MCTF_TEST(test_json_create)
{
   struct json* obj = NULL;

   MCTF_ASSERT(!pgagroal_json_create(&obj), cleanup, "json creation should succeed");
   MCTF_ASSERT_PTR_NONNULL(obj, cleanup, "json object should not be NULL");
   MCTF_ASSERT_INT_EQ(obj->type, JSONUnknown, cleanup, "json type should be JSONUnknown");

cleanup:
   pgagroal_json_destroy(obj);
   MCTF_FINISH();
}
MCTF_TEST(test_json_put_basic)
{
   struct json* obj = NULL;

   MCTF_ASSERT(!pgagroal_json_create(&obj), cleanup, "json creation should succeed");
   MCTF_ASSERT_PTR_NONNULL(obj, cleanup, "json object should not be NULL");
   MCTF_ASSERT_INT_EQ(obj->type, JSONUnknown, cleanup, "json type should be JSONUnknown");

   MCTF_ASSERT(!pgagroal_json_put(obj, "key1", (uintptr_t)"value1", ValueString), cleanup, "put key1 should succeed");
   MCTF_ASSERT(pgagroal_json_contains_key(obj, "key1"), cleanup, "key1 should be contained");
   MCTF_ASSERT_STR_EQ((char*)pgagroal_json_get(obj, "key1"), "value1", cleanup, "key1 value should be value1");
   MCTF_ASSERT_INT_EQ(obj->type, JSONItem, cleanup, "json type should be JSONItem");

   // json only takes in certain types of value
   MCTF_ASSERT(pgagroal_json_put(obj, "key2", (uintptr_t)"value1", ValueMem), cleanup, "put with ValueMem should fail");
   MCTF_ASSERT(!pgagroal_json_contains_key(obj, "key2"), cleanup, "key2 should not be contained");

   // item should not take entry input
   MCTF_ASSERT(pgagroal_json_append(obj, (uintptr_t)"entry", ValueString), cleanup, "append to item should fail");

cleanup:
   pgagroal_json_destroy(obj);
   MCTF_FINISH();
}
MCTF_TEST(test_json_append_basic)
{
   struct json* obj = NULL;

   MCTF_ASSERT(!pgagroal_json_create(&obj), cleanup, "json creation should succeed");
   MCTF_ASSERT_PTR_NONNULL(obj, cleanup, "json object should not be NULL");
   MCTF_ASSERT_INT_EQ(obj->type, JSONUnknown, cleanup, "json type should be JSONUnknown");

   MCTF_ASSERT(!pgagroal_json_append(obj, (uintptr_t)"value1", ValueString), cleanup, "append value1 should succeed");
   MCTF_ASSERT_INT_EQ(obj->type, JSONArray, cleanup, "json type should be JSONArray");

   MCTF_ASSERT(pgagroal_json_append(obj, (uintptr_t)"value2", ValueMem), cleanup, "append with ValueMem should fail");
   MCTF_ASSERT(pgagroal_json_put(obj, "key", (uintptr_t)"value", ValueString), cleanup, "put to array should fail");

cleanup:
   pgagroal_json_destroy(obj);
   MCTF_FINISH();
}
MCTF_TEST(test_json_parse_to_string)
{
   struct json* obj = NULL;
   struct json* obj_parsed = NULL;
   char* str_obj = NULL;
   char* str_obj_parsed = NULL;

   struct json* int_array = NULL;
   struct json* str_array = NULL;
   struct json* json_item_shallow = NULL;

   struct json* json_array_nested_item1 = NULL;
   struct json* json_array_nested_item2 = NULL;
   struct json* json_array_item_nested = NULL;

   struct json* json_array_nested_array1 = NULL;
   struct json* json_array_nested_array2 = NULL;
   struct json* json_array_array_nested = NULL;

   struct json* json_item_nested_array1 = NULL;
   struct json* json_item_nested_array2 = NULL;
   struct json* json_item_array_nested = NULL;

   struct json* json_item_nested_item1 = NULL;
   struct json* json_item_nested_item2 = NULL;
   struct json* json_item_item_nested = NULL;

   pgagroal_json_create(&obj);
   pgagroal_json_create(&int_array);
   pgagroal_json_create(&str_array);
   pgagroal_json_create(&json_item_shallow);

   pgagroal_json_create(&json_array_nested_item1);
   pgagroal_json_create(&json_array_nested_item2);
   pgagroal_json_create(&json_array_item_nested);

   pgagroal_json_create(&json_array_nested_array1);
   pgagroal_json_create(&json_array_nested_array2);
   pgagroal_json_create(&json_array_array_nested);

   pgagroal_json_create(&json_item_nested_array1);
   pgagroal_json_create(&json_item_nested_array2);
   pgagroal_json_create(&json_item_array_nested);

   pgagroal_json_create(&json_item_nested_item1);
   pgagroal_json_create(&json_item_nested_item2);
   pgagroal_json_create(&json_item_item_nested);

   pgagroal_json_put(obj, "int_array", (uintptr_t)int_array, ValueJSON);
   pgagroal_json_put(obj, "str_array", (uintptr_t)str_array, ValueJSON);
   pgagroal_json_put(obj, "json_item_shallow", (uintptr_t)json_item_shallow, ValueJSON);
   pgagroal_json_put(obj, "json_array_item_nested", (uintptr_t)json_array_item_nested, ValueJSON);
   pgagroal_json_put(obj, "json_array_array_nested", (uintptr_t)json_array_array_nested, ValueJSON);
   pgagroal_json_put(obj, "json_item_array_nested", (uintptr_t)json_item_array_nested, ValueJSON);
   pgagroal_json_put(obj, "json_item_item_nested", (uintptr_t)json_item_item_nested, ValueJSON);
   pgagroal_json_put(obj, "empty_value", (uintptr_t)"", ValueString);
   pgagroal_json_put(obj, "null_value", (uintptr_t)NULL, ValueString);

   pgagroal_json_append(int_array, 1, ValueInt32);
   pgagroal_json_append(int_array, 2, ValueInt32);
   pgagroal_json_append(int_array, 3, ValueInt32);

   pgagroal_json_append(str_array, (uintptr_t)"str1", ValueString);
   pgagroal_json_append(str_array, (uintptr_t)"str2", ValueString);
   pgagroal_json_append(str_array, (uintptr_t)"str3", ValueString);

   pgagroal_json_put(json_item_shallow, "int", (uintptr_t)-1, ValueInt32);
   pgagroal_json_put(json_item_shallow, "float", pgagroal_value_from_float(-2.5), ValueFloat);
   pgagroal_json_put(json_item_shallow, "double", pgagroal_value_from_double(2.5), ValueDouble);
   pgagroal_json_put(json_item_shallow, "bool_true", true, ValueBool);
   pgagroal_json_put(json_item_shallow, "bool_false", false, ValueBool);
   pgagroal_json_put(json_item_shallow, "string", (uintptr_t)"str", ValueString);

   pgagroal_json_put(json_array_nested_item1, "1", 1, ValueInt32);
   pgagroal_json_put(json_array_nested_item1, "2", 2, ValueInt32);
   pgagroal_json_put(json_array_nested_item1, "3", 3, ValueInt32);
   pgagroal_json_put(json_array_nested_item2, "1", (uintptr_t)"1", ValueString);
   pgagroal_json_put(json_array_nested_item2, "2", (uintptr_t)"2", ValueString);
   pgagroal_json_put(json_array_nested_item2, "3", (uintptr_t)"3", ValueString);
   pgagroal_json_append(json_array_item_nested, (uintptr_t)json_array_nested_item1, ValueJSON);
   pgagroal_json_append(json_array_item_nested, (uintptr_t)json_array_nested_item2, ValueJSON);

   pgagroal_json_append(json_array_nested_array1, (uintptr_t)"1", ValueString);
   pgagroal_json_append(json_array_nested_array1, (uintptr_t)"2", ValueString);
   pgagroal_json_append(json_array_nested_array1, (uintptr_t)"3", ValueString);
   pgagroal_json_append(json_array_nested_array2, true, ValueBool);
   pgagroal_json_append(json_array_nested_array2, false, ValueBool);
   pgagroal_json_append(json_array_nested_array2, false, ValueBool);
   pgagroal_json_append(json_array_array_nested, (uintptr_t)json_array_nested_array1, ValueJSON);
   pgagroal_json_append(json_array_array_nested, (uintptr_t)json_array_nested_array2, ValueJSON);

   pgagroal_json_append(json_item_nested_array1, (uintptr_t)"1", ValueString);
   pgagroal_json_append(json_item_nested_array1, (uintptr_t)"2", ValueString);
   pgagroal_json_append(json_item_nested_array1, (uintptr_t)"3", ValueString);
   pgagroal_json_append(json_item_nested_array2, true, ValueBool);
   pgagroal_json_append(json_item_nested_array2, false, ValueBool);
   pgagroal_json_append(json_item_nested_array2, true, ValueBool);
   pgagroal_json_append(json_item_array_nested, (uintptr_t)json_item_nested_array1, ValueJSON);
   pgagroal_json_append(json_item_array_nested, (uintptr_t)json_item_nested_array2, ValueJSON);

   pgagroal_json_put(json_item_nested_item1, "1", 1, ValueInt32);
   pgagroal_json_put(json_item_nested_item1, "2", 2, ValueInt32);
   pgagroal_json_put(json_item_nested_item1, "3", 3, ValueInt32);
   pgagroal_json_put(json_item_nested_item2, "1", (uintptr_t)"1", ValueString);
   pgagroal_json_put(json_item_nested_item2, "2", (uintptr_t)"2", ValueString);
   pgagroal_json_put(json_item_nested_item2, "3", (uintptr_t)"3", ValueString);
   pgagroal_json_append(json_item_item_nested, (uintptr_t)json_item_nested_item1, ValueJSON);
   pgagroal_json_append(json_item_array_nested, (uintptr_t)json_item_nested_item2, ValueJSON);

   str_obj = pgagroal_json_to_string(obj, FORMAT_JSON, NULL, 0);
   MCTF_ASSERT(!pgagroal_json_parse_string(str_obj, &obj_parsed), cleanup, "parse string should succeed");
   MCTF_ASSERT_PTR_NONNULL(obj_parsed, cleanup, "parsed object should not be NULL");

   str_obj_parsed = pgagroal_json_to_string(obj_parsed, FORMAT_JSON, NULL, 0);
   MCTF_ASSERT_STR_EQ(str_obj, str_obj_parsed, cleanup, "parsed JSON string should match original");

   free(str_obj);
   str_obj = NULL;
   free(str_obj_parsed);
   str_obj_parsed = NULL;

   str_obj = pgagroal_json_to_string(obj, FORMAT_TEXT, NULL, 0);
   str_obj_parsed = pgagroal_json_to_string(obj_parsed, FORMAT_TEXT, NULL, 0);
   MCTF_ASSERT_STR_EQ(str_obj, str_obj_parsed, cleanup, "parsed TEXT string should match original");

cleanup:
   free(str_obj);
   free(str_obj_parsed);
   pgagroal_json_destroy(obj);
   pgagroal_json_destroy(obj_parsed);
   MCTF_FINISH();
}
MCTF_TEST(test_json_remove)
{
   struct json* obj = NULL;
   struct json* array = NULL;
   pgagroal_json_create(&obj);
   pgagroal_json_create(&array);

   pgagroal_json_put(obj, "key1", (uintptr_t)"1", ValueString);
   pgagroal_json_put(obj, "key2", 2, ValueInt32);
   pgagroal_json_append(array, (uintptr_t)"key1", ValueString);
   MCTF_ASSERT(pgagroal_json_remove(array, "key1"), cleanup, "remove from array should fail");
   MCTF_ASSERT(pgagroal_json_remove(obj, ""), cleanup, "remove with empty key should fail");
   MCTF_ASSERT(pgagroal_json_remove(obj, NULL), cleanup, "remove with NULL key should fail");
   MCTF_ASSERT(pgagroal_json_remove(NULL, "key1"), cleanup, "remove from NULL object should fail");

   MCTF_ASSERT(pgagroal_json_contains_key(obj, "key1"), cleanup, "key1 should be contained");
   MCTF_ASSERT(!pgagroal_json_remove(obj, "key3"), cleanup, "remove non-existent key should succeed");
   MCTF_ASSERT(!pgagroal_json_remove(obj, "key1"), cleanup, "remove key1 should succeed");
   MCTF_ASSERT(!pgagroal_json_contains_key(obj, "key1"), cleanup, "key1 should not be contained");
   MCTF_ASSERT_INT_EQ(obj->type, JSONItem, cleanup, "json type should be JSONItem");

   // double delete
   MCTF_ASSERT(!pgagroal_json_remove(obj, "key1"), cleanup, "double remove should succeed");

   MCTF_ASSERT(pgagroal_json_contains_key(obj, "key2"), cleanup, "key2 should be contained");
   MCTF_ASSERT(!pgagroal_json_remove(obj, "key2"), cleanup, "remove key2 should succeed");
   MCTF_ASSERT(!pgagroal_json_contains_key(obj, "key2"), cleanup, "key2 should not be contained");
   MCTF_ASSERT_INT_EQ(obj->type, JSONUnknown, cleanup, "json type should be JSONUnknown");

   // double delete
   MCTF_ASSERT(!pgagroal_json_remove(obj, "key2"), cleanup, "double remove should succeed");

cleanup:
   pgagroal_json_destroy(obj);
   pgagroal_json_destroy(array);
   MCTF_FINISH();
}
MCTF_TEST(test_json_iterator)
{
   struct json* item = NULL;
   struct json* array = NULL;
   struct json_iterator* iiter = NULL;
   struct json_iterator* aiter = NULL;
   char key[2] = {0};
   int cnt = 0;

   pgagroal_json_create(&item);
   pgagroal_json_create(&array);

   MCTF_ASSERT(pgagroal_json_iterator_create(NULL, &iiter), cleanup, "iterator create with NULL should fail");
   MCTF_ASSERT(pgagroal_json_iterator_create(item, &aiter), cleanup, "iterator creation should fail if json type is unknown");

   pgagroal_json_put(item, "1", 1, ValueInt32);
   pgagroal_json_put(item, "2", 2, ValueInt32);
   pgagroal_json_put(item, "3", 3, ValueInt32);

   pgagroal_json_append(array, 1, ValueInt32);
   pgagroal_json_append(array, 2, ValueInt32);
   pgagroal_json_append(array, 3, ValueInt32);

   MCTF_ASSERT(!pgagroal_json_iterator_create(item, &iiter), cleanup, "iterator create for item should succeed");
   MCTF_ASSERT(!pgagroal_json_iterator_create(array, &aiter), cleanup, "iterator create for array should succeed");
   MCTF_ASSERT(pgagroal_json_iterator_has_next(iiter), cleanup, "item iterator should have next");
   MCTF_ASSERT(pgagroal_json_iterator_has_next(aiter), cleanup, "array iterator should have next");

   while (pgagroal_json_iterator_next(iiter))
   {
      cnt++;
      key[0] = '0' + cnt;
      MCTF_ASSERT_STR_EQ(iiter->key, key, cleanup, "iterator key should match count");
      MCTF_ASSERT_INT_EQ(iiter->value->data, cnt, cleanup, "iterator value should match count");
   }

   cnt = 0;

   while (pgagroal_json_iterator_next(aiter))
   {
      cnt++;
      MCTF_ASSERT_INT_EQ(aiter->value->data, cnt, cleanup, "array iterator value should match count");
   }

   MCTF_ASSERT(!pgagroal_json_iterator_has_next(iiter), cleanup, "item iterator should not have next");
   MCTF_ASSERT(!pgagroal_json_iterator_has_next(aiter), cleanup, "array iterator should not have next");

cleanup:
   pgagroal_json_iterator_destroy(iiter);
   pgagroal_json_iterator_destroy(aiter);
   pgagroal_json_destroy(item);
   pgagroal_json_destroy(array);
   MCTF_FINISH();
}
