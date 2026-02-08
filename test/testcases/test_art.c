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
#include <art.h>
#include <tsclient.h>
#include <mctf.h>
#include <utils.h>
#include <value.h>

#include <stdio.h>
#include <string.h>

struct art_test_obj
{
   char* str;
   int idx;
};

static void test_obj_create(int idx, struct art_test_obj** obj);
static void test_obj_destroy(struct art_test_obj* obj);
static void test_obj_destroy_cb(uintptr_t obj);

MCTF_TEST(test_art_create)
{
   struct art* t = NULL;
   pgagroal_art_create(&t);

   MCTF_ASSERT_PTR_NONNULL(t, cleanup, "art tree should be created");
   MCTF_ASSERT_INT_EQ(t->size, 0, cleanup, "art tree size should be 0");
cleanup:
   pgagroal_art_destroy(t);
   MCTF_FINISH();
}
MCTF_TEST(test_art_insert)
{
   struct art* t = NULL;
   void* mem = NULL;
   struct art_test_obj* obj = NULL;

   pgagroal_art_create(&t);
   mem = malloc(10);
   struct value_config test_obj_config = {.destroy_data = test_obj_destroy_cb, .to_string = NULL};

   MCTF_ASSERT_PTR_NONNULL(t, cleanup, "art tree should be created");

   MCTF_ASSERT(pgagroal_art_insert(t, "key_none", 0, ValueNone), cleanup, "insert with ValueNone should fail");
   MCTF_ASSERT(pgagroal_art_insert(t, NULL, 0, ValueInt8), cleanup, "insert with NULL key should fail");
   MCTF_ASSERT(pgagroal_art_insert(NULL, "key_none", 0, ValueInt8), cleanup, "insert with NULL tree should fail");

   MCTF_ASSERT(!pgagroal_art_insert(t, "key_str", (uintptr_t)"value1", ValueString), cleanup, "insert key_str should succeed");
   MCTF_ASSERT(!pgagroal_art_insert(t, "key_int", 1, ValueInt32), cleanup, "insert key_int should succeed");
   MCTF_ASSERT(!pgagroal_art_insert(t, "key_bool", true, ValueBool), cleanup, "insert key_bool should succeed");
   MCTF_ASSERT(!pgagroal_art_insert(t, "key_float", pgagroal_value_from_float(2.5), ValueFloat), cleanup, "insert key_float should succeed");
   MCTF_ASSERT(!pgagroal_art_insert(t, "key_double", pgagroal_value_from_double(2.5), ValueDouble), cleanup, "insert key_double should succeed");
   MCTF_ASSERT(!pgagroal_art_insert(t, "key_mem", (uintptr_t)mem, ValueMem), cleanup, "insert key_mem should succeed");

   test_obj_create(0, &obj);
   MCTF_ASSERT(!pgagroal_art_insert_with_config(t, "key_obj", (uintptr_t)obj, &test_obj_config), cleanup, "insert key_obj should succeed");
   MCTF_ASSERT_INT_EQ(t->size, 7, cleanup, "art tree size should be 7");
cleanup:
   pgagroal_art_destroy(t);
   MCTF_FINISH();
}
MCTF_TEST(test_art_search)
{
   struct art* t = NULL;
   struct art_test_obj* obj1 = NULL;
   struct art_test_obj* obj2 = NULL;
   enum value_type type = ValueNone;
   char* value2 = NULL;
   char* key_str = NULL;

   pgagroal_art_create(&t);
   struct value_config test_obj_config = {.destroy_data = test_obj_destroy_cb, .to_string = NULL};

   MCTF_ASSERT_PTR_NONNULL(t, cleanup, "art tree should be created");

   MCTF_ASSERT(pgagroal_art_insert(t, "key_none", 0, ValueNone), cleanup, "insert with ValueNone should fail");
   MCTF_ASSERT(!pgagroal_art_contains_key(t, "key_none"), cleanup, "key_none should not be contained");
   MCTF_ASSERT_INT_EQ(pgagroal_art_search(t, "key_none"), 0, cleanup, "search for key_none should return 0");
   MCTF_ASSERT_INT_EQ(pgagroal_art_search_typed(t, "key_none", &type), 0, cleanup, "search_typed for key_none should return 0");
   MCTF_ASSERT_INT_EQ(type, ValueNone, cleanup, "type should be ValueNone");

   MCTF_ASSERT(!pgagroal_art_insert(t, "key_str", (uintptr_t)"value1", ValueString), cleanup, "insert key_str should succeed");
   MCTF_ASSERT(pgagroal_art_contains_key(t, "key_str"), cleanup, "key_str should be contained");
   MCTF_ASSERT_STR_EQ((char*)pgagroal_art_search(t, "key_str"), "value1", cleanup, "search for key_str should return value1");

   // inserting string makes a copy
   key_str = pgagroal_append(key_str, "key_str");
   value2 = pgagroal_append(value2, "value2");
   MCTF_ASSERT(!pgagroal_art_insert(t, key_str, (uintptr_t)value2, ValueString), cleanup, "insert key_str with value2 should succeed");
   MCTF_ASSERT_STR_EQ((char*)pgagroal_art_search(t, "key_str"), "value2", cleanup, "search for key_str should return value2");
   free(value2);
   value2 = NULL;
   free(key_str);
   key_str = NULL;

   MCTF_ASSERT(!pgagroal_art_insert(t, "key_int", -1, ValueInt32), cleanup, "insert key_int should succeed");
   MCTF_ASSERT(pgagroal_art_contains_key(t, "key_int"), cleanup, "key_int should be contained");
   MCTF_ASSERT_INT_EQ((int)pgagroal_art_search(t, "key_int"), -1, cleanup, "search for key_int should return -1");

   MCTF_ASSERT(!pgagroal_art_insert(t, "key_bool", true, ValueBool), cleanup, "insert key_bool should succeed");
   MCTF_ASSERT((bool)pgagroal_art_search(t, "key_bool"), cleanup, "search for key_bool should return true");

   MCTF_ASSERT(!pgagroal_art_insert(t, "key_float", pgagroal_value_from_float(2.5), ValueFloat), cleanup, "insert key_float should succeed");
   MCTF_ASSERT(!pgagroal_art_insert(t, "key_double", pgagroal_value_from_double(2.5), ValueDouble), cleanup, "insert key_double should succeed");
   MCTF_ASSERT_FLOAT_EQ(pgagroal_value_to_float(pgagroal_art_search(t, "key_float")), 2.5, cleanup, "search for key_float should return 2.5");
   MCTF_ASSERT_DOUBLE_EQ(pgagroal_value_to_double(pgagroal_art_search(t, "key_double")), 2.5, cleanup, "search for key_double should return 2.5");

   test_obj_create(1, &obj1);
   MCTF_ASSERT(!pgagroal_art_insert_with_config(t, "key_obj", (uintptr_t)obj1, &test_obj_config), cleanup, "insert key_obj should succeed");
   MCTF_ASSERT_INT_EQ(((struct art_test_obj*)pgagroal_art_search(t, "key_obj"))->idx, 1, cleanup, "obj1 idx should be 1");
   MCTF_ASSERT_STR_EQ(((struct art_test_obj*)pgagroal_art_search(t, "key_obj"))->str, "obj1", cleanup, "obj1 str should be obj1");
   pgagroal_art_search_typed(t, "key_obj", &type);
   MCTF_ASSERT_INT_EQ(type, ValueRef, cleanup, "type should be ValueRef");

   // test obj overwrite with memory free up
   test_obj_create(2, &obj2);
   MCTF_ASSERT(!pgagroal_art_insert_with_config(t, "key_obj", (uintptr_t)obj2, &test_obj_config), cleanup, "insert key_obj with obj2 should succeed");
   MCTF_ASSERT_INT_EQ(((struct art_test_obj*)pgagroal_art_search(t, "key_obj"))->idx, 2, cleanup, "obj2 idx should be 2");
   MCTF_ASSERT_STR_EQ(((struct art_test_obj*)pgagroal_art_search(t, "key_obj"))->str, "obj2", cleanup, "obj2 str should be obj2");
cleanup:
   pgagroal_art_destroy(t);
   free(value2);
   free(key_str);
   MCTF_FINISH();
}
MCTF_TEST(test_art_basic_delete)
{
   struct art* t = NULL;
   void* mem = NULL;
   struct art_test_obj* obj = NULL;

   pgagroal_art_create(&t);
   mem = malloc(10);
   struct value_config test_obj_config = {.destroy_data = test_obj_destroy_cb, .to_string = NULL};

   MCTF_ASSERT_PTR_NONNULL(t, cleanup, "art tree should be created");
   test_obj_create(0, &obj);

   MCTF_ASSERT(!pgagroal_art_insert(t, "key_str", (uintptr_t)"value1", ValueString), cleanup, "insert key_str should succeed");
   MCTF_ASSERT(!pgagroal_art_insert(t, "key_int", 1, ValueInt32), cleanup, "insert key_int should succeed");
   MCTF_ASSERT(!pgagroal_art_insert(t, "key_bool", true, ValueBool), cleanup, "insert key_bool should succeed");
   MCTF_ASSERT(!pgagroal_art_insert(t, "key_float", pgagroal_value_from_float(2.5), ValueFloat), cleanup, "insert key_float should succeed");
   MCTF_ASSERT(!pgagroal_art_insert(t, "key_double", pgagroal_value_from_double(2.5), ValueDouble), cleanup, "insert key_double should succeed");
   MCTF_ASSERT(!pgagroal_art_insert(t, "key_mem", (uintptr_t)mem, ValueMem), cleanup, "insert key_mem should succeed");
   MCTF_ASSERT(!pgagroal_art_insert_with_config(t, "key_obj", (uintptr_t)obj, &test_obj_config), cleanup, "insert key_obj should succeed");

   MCTF_ASSERT(pgagroal_art_contains_key(t, "key_str"), cleanup, "key_str should be contained");
   MCTF_ASSERT(pgagroal_art_contains_key(t, "key_int"), cleanup, "key_int should be contained");
   MCTF_ASSERT(pgagroal_art_contains_key(t, "key_bool"), cleanup, "key_bool should be contained");
   MCTF_ASSERT(pgagroal_art_contains_key(t, "key_mem"), cleanup, "key_mem should be contained");
   MCTF_ASSERT(pgagroal_art_contains_key(t, "key_float"), cleanup, "key_float should be contained");
   MCTF_ASSERT(pgagroal_art_contains_key(t, "key_double"), cleanup, "key_double should be contained");
   MCTF_ASSERT(pgagroal_art_contains_key(t, "key_obj"), cleanup, "key_obj should be contained");
   MCTF_ASSERT_INT_EQ(t->size, 7, cleanup, "art tree size should be 7");

   MCTF_ASSERT(pgagroal_art_delete(t, NULL), cleanup, "delete with NULL key should fail");
   MCTF_ASSERT(pgagroal_art_delete(NULL, "key_str"), cleanup, "delete with NULL tree should fail");

   MCTF_ASSERT(!pgagroal_art_delete(t, "key_str"), cleanup, "delete key_str should succeed");
   MCTF_ASSERT(!pgagroal_art_contains_key(t, "key_str"), cleanup, "key_str should not be contained");
   MCTF_ASSERT_INT_EQ(t->size, 6, cleanup, "art tree size should be 6");

   MCTF_ASSERT(!pgagroal_art_delete(t, "key_int"), cleanup, "delete key_int should succeed");
   MCTF_ASSERT(!pgagroal_art_contains_key(t, "key_int"), cleanup, "key_int should not be contained");
   MCTF_ASSERT_INT_EQ(t->size, 5, cleanup, "art tree size should be 5");

   MCTF_ASSERT(!pgagroal_art_delete(t, "key_bool"), cleanup, "delete key_bool should succeed");
   MCTF_ASSERT(!pgagroal_art_contains_key(t, "key_bool"), cleanup, "key_bool should not be contained");
   MCTF_ASSERT_INT_EQ(t->size, 4, cleanup, "art tree size should be 4");

   MCTF_ASSERT(!pgagroal_art_delete(t, "key_mem"), cleanup, "delete key_mem should succeed");
   MCTF_ASSERT(!pgagroal_art_contains_key(t, "key_mem"), cleanup, "key_mem should not be contained");
   MCTF_ASSERT_INT_EQ(t->size, 3, cleanup, "art tree size should be 3");

   MCTF_ASSERT(!pgagroal_art_delete(t, "key_float"), cleanup, "delete key_float should succeed");
   MCTF_ASSERT(!pgagroal_art_contains_key(t, "key_float"), cleanup, "key_float should not be contained");
   MCTF_ASSERT_INT_EQ(t->size, 2, cleanup, "art tree size should be 2");

   MCTF_ASSERT(!pgagroal_art_delete(t, "key_double"), cleanup, "delete key_double should succeed");
   MCTF_ASSERT(!pgagroal_art_contains_key(t, "key_double"), cleanup, "key_double should not be contained");
   MCTF_ASSERT_INT_EQ(t->size, 1, cleanup, "art tree size should be 1");

   MCTF_ASSERT(!pgagroal_art_delete(t, "key_obj"), cleanup, "delete key_obj should succeed");
   MCTF_ASSERT(!pgagroal_art_contains_key(t, "key_obj"), cleanup, "key_obj should not be contained");
   MCTF_ASSERT_INT_EQ(t->size, 0, cleanup, "art tree size should be 0");
cleanup:
   pgagroal_art_destroy(t);
   MCTF_FINISH();
}
MCTF_TEST(test_art_double_delete)
{
   struct art* t = NULL;
   pgagroal_art_create(&t);

   MCTF_ASSERT_PTR_NONNULL(t, cleanup, "art tree should be created");

   MCTF_ASSERT(!pgagroal_art_insert(t, "key_str", (uintptr_t)"value1", ValueString), cleanup, "insert key_str should succeed");
   MCTF_ASSERT(!pgagroal_art_insert(t, "key_int", 1, ValueInt32), cleanup, "insert key_int should succeed");

   MCTF_ASSERT(pgagroal_art_contains_key(t, "key_str"), cleanup, "key_str should be contained");
   MCTF_ASSERT_INT_EQ(t->size, 2, cleanup, "art tree size should be 2");

   MCTF_ASSERT(!pgagroal_art_delete(t, "key_str"), cleanup, "delete key_str should succeed");
   MCTF_ASSERT(!pgagroal_art_contains_key(t, "key_str"), cleanup, "key_str should not be contained");
   MCTF_ASSERT_INT_EQ(t->size, 1, cleanup, "art tree size should be 1");

   MCTF_ASSERT(!pgagroal_art_delete(t, "key_str"), cleanup, "delete key_str again should succeed");
   MCTF_ASSERT(!pgagroal_art_contains_key(t, "key_str"), cleanup, "key_str should not be contained");
   MCTF_ASSERT_INT_EQ(t->size, 1, cleanup, "art tree size should still be 1");
cleanup:
   pgagroal_art_destroy(t);
   MCTF_FINISH();
}
MCTF_TEST(test_art_clear)
{
   struct art* t = NULL;
   void* mem = NULL;
   struct art_test_obj* obj = NULL;

   pgagroal_art_create(&t);
   mem = malloc(10);
   struct value_config test_obj_config = {.destroy_data = test_obj_destroy_cb, .to_string = NULL};

   MCTF_ASSERT_PTR_NONNULL(t, cleanup, "art tree should be created");
   test_obj_create(0, &obj);

   MCTF_ASSERT(!pgagroal_art_insert(t, "key_str", (uintptr_t)"value1", ValueString), cleanup, "insert key_str should succeed");
   MCTF_ASSERT(!pgagroal_art_insert(t, "key_int", 1, ValueInt32), cleanup, "insert key_int should succeed");
   MCTF_ASSERT(!pgagroal_art_insert(t, "key_bool", true, ValueBool), cleanup, "insert key_bool should succeed");
   MCTF_ASSERT(!pgagroal_art_insert(t, "key_float", pgagroal_value_from_float(2.5), ValueFloat), cleanup, "insert key_float should succeed");
   MCTF_ASSERT(!pgagroal_art_insert(t, "key_double", pgagroal_value_from_double(2.5), ValueDouble), cleanup, "insert key_double should succeed");
   MCTF_ASSERT(!pgagroal_art_insert(t, "key_mem", (uintptr_t)mem, ValueMem), cleanup, "insert key_mem should succeed");
   MCTF_ASSERT(!pgagroal_art_insert_with_config(t, "key_obj", (uintptr_t)obj, &test_obj_config), cleanup, "insert key_obj should succeed");

   MCTF_ASSERT(pgagroal_art_contains_key(t, "key_str"), cleanup, "key_str should be contained");
   MCTF_ASSERT(pgagroal_art_contains_key(t, "key_int"), cleanup, "key_int should be contained");
   MCTF_ASSERT(pgagroal_art_contains_key(t, "key_bool"), cleanup, "key_bool should be contained");
   MCTF_ASSERT(pgagroal_art_contains_key(t, "key_mem"), cleanup, "key_mem should be contained");
   MCTF_ASSERT(pgagroal_art_contains_key(t, "key_float"), cleanup, "key_float should be contained");
   MCTF_ASSERT(pgagroal_art_contains_key(t, "key_double"), cleanup, "key_double should be contained");
   MCTF_ASSERT(pgagroal_art_contains_key(t, "key_obj"), cleanup, "key_obj should be contained");
   MCTF_ASSERT_INT_EQ(t->size, 7, cleanup, "art tree size should be 7");

   MCTF_ASSERT(!pgagroal_art_clear(t), cleanup, "clear should succeed");
   MCTF_ASSERT_INT_EQ(t->size, 0, cleanup, "art tree size should be 0");
   MCTF_ASSERT_PTR_NULL(t->root, cleanup, "art tree root should be NULL");
cleanup:
   pgagroal_art_destroy(t);
   MCTF_FINISH();
}
MCTF_TEST(test_art_iterator_read)
{
   struct art* t = NULL;
   struct art_iterator* iter = NULL;
   void* mem = NULL;
   struct art_test_obj* obj = NULL;

   pgagroal_art_create(&t);
   mem = malloc(10);
   struct value_config test_obj_config = {.destroy_data = test_obj_destroy_cb, .to_string = NULL};

   MCTF_ASSERT_PTR_NONNULL(t, cleanup, "art tree should be created");
   test_obj_create(1, &obj);

   MCTF_ASSERT(!pgagroal_art_insert(t, "key_str", (uintptr_t)"value1", ValueString), cleanup, "insert key_str should succeed");
   MCTF_ASSERT(!pgagroal_art_insert(t, "key_int", 1, ValueInt32), cleanup, "insert key_int should succeed");
   MCTF_ASSERT(!pgagroal_art_insert(t, "key_bool", true, ValueBool), cleanup, "insert key_bool should succeed");
   MCTF_ASSERT(!pgagroal_art_insert(t, "key_float", pgagroal_value_from_float(2.5), ValueFloat), cleanup, "insert key_float should succeed");
   MCTF_ASSERT(!pgagroal_art_insert(t, "key_double", pgagroal_value_from_double(2.5), ValueDouble), cleanup, "insert key_double should succeed");
   MCTF_ASSERT(!pgagroal_art_insert(t, "key_mem", (uintptr_t)mem, ValueMem), cleanup, "insert key_mem should succeed");
   MCTF_ASSERT(!pgagroal_art_insert_with_config(t, "key_obj", (uintptr_t)obj, &test_obj_config), cleanup, "insert key_obj should succeed");

   MCTF_ASSERT(pgagroal_art_iterator_create(NULL, &iter), cleanup, "iterator_create with NULL tree should fail");
   MCTF_ASSERT_PTR_NULL(iter, cleanup, "iterator should be NULL");
   MCTF_ASSERT(!pgagroal_art_iterator_create(t, &iter), cleanup, "iterator_create should succeed");
   MCTF_ASSERT_PTR_NONNULL(iter, cleanup, "iterator should be created");
   MCTF_ASSERT(pgagroal_art_iterator_has_next(iter), cleanup, "iterator should have next");

   int cnt = 0;
   while (pgagroal_art_iterator_next(iter))
   {
      if (pgagroal_compare_string(iter->key, "key_str"))
      {
         MCTF_ASSERT_STR_EQ((char*)pgagroal_value_data(iter->value), "value1", cleanup, "key_str value should be value1");
      }
      else if (pgagroal_compare_string(iter->key, "key_int"))
      {
         MCTF_ASSERT_INT_EQ((int)pgagroal_value_data(iter->value), 1, cleanup, "key_int value should be 1");
      }
      else if (pgagroal_compare_string(iter->key, "key_bool"))
      {
         MCTF_ASSERT((bool)pgagroal_value_data(iter->value), cleanup, "key_bool value should be true");
      }
      else if (pgagroal_compare_string(iter->key, "key_float"))
      {
         MCTF_ASSERT_FLOAT_EQ(pgagroal_value_to_float(pgagroal_value_data(iter->value)), 2.5, cleanup, "key_float value should be 2.5");
      }
      else if (pgagroal_compare_string(iter->key, "key_double"))
      {
         MCTF_ASSERT_DOUBLE_EQ(pgagroal_value_to_double(pgagroal_value_data(iter->value)), 2.5, cleanup, "key_double value should be 2.5");
      }
      else if (pgagroal_compare_string(iter->key, "key_mem"))
      {
         // as long as it exists...
         MCTF_ASSERT(true, cleanup, "key_mem should exist");
      }
      else if (pgagroal_compare_string(iter->key, "key_obj"))
      {
         MCTF_ASSERT_INT_EQ(((struct art_test_obj*)pgagroal_value_data(iter->value))->idx, 1, cleanup, "key_obj idx should be 1");
         MCTF_ASSERT_STR_EQ(((struct art_test_obj*)pgagroal_value_data(iter->value))->str, "obj1", cleanup, "key_obj str should be obj1");
      }
      else
      {
         MCTF_ASSERT(false, cleanup, "found key not inserted: %s", iter->key);
      }

      cnt++;
   }
   MCTF_ASSERT_INT_EQ(cnt, t->size, cleanup, "iterator count should match tree size");
   MCTF_ASSERT(!pgagroal_art_iterator_has_next(iter), cleanup, "iterator should not have next");
cleanup:
   pgagroal_art_iterator_destroy(iter);
   pgagroal_art_destroy(t);
   MCTF_FINISH();
}
MCTF_TEST(test_art_iterator_remove)
{
   struct art* t = NULL;
   struct art_iterator* iter = NULL;
   void* mem = NULL;
   struct art_test_obj* obj = NULL;

   pgagroal_art_create(&t);
   mem = malloc(10);
   struct value_config test_obj_config = {.destroy_data = test_obj_destroy_cb, .to_string = NULL};

   MCTF_ASSERT_PTR_NONNULL(t, cleanup, "art tree should be created");
   test_obj_create(1, &obj);

   MCTF_ASSERT(!pgagroal_art_insert(t, "key_str", (uintptr_t)"value1", ValueString), cleanup, "insert key_str should succeed");
   MCTF_ASSERT(!pgagroal_art_insert(t, "key_int", 1, ValueInt32), cleanup, "insert key_int should succeed");
   MCTF_ASSERT(!pgagroal_art_insert(t, "key_bool", true, ValueBool), cleanup, "insert key_bool should succeed");
   MCTF_ASSERT(!pgagroal_art_insert(t, "key_float", pgagroal_value_from_float(2.5), ValueFloat), cleanup, "insert key_float should succeed");
   MCTF_ASSERT(!pgagroal_art_insert(t, "key_double", pgagroal_value_from_double(2.5), ValueDouble), cleanup, "insert key_double should succeed");
   MCTF_ASSERT(!pgagroal_art_insert(t, "key_mem", (uintptr_t)mem, ValueMem), cleanup, "insert key_mem should succeed");
   MCTF_ASSERT(!pgagroal_art_insert_with_config(t, "key_obj", (uintptr_t)obj, &test_obj_config), cleanup, "insert key_obj should succeed");

   MCTF_ASSERT_INT_EQ(t->size, 7, cleanup, "art tree size should be 7");

   MCTF_ASSERT(!pgagroal_art_iterator_create(t, &iter), cleanup, "iterator_create should succeed");
   MCTF_ASSERT_PTR_NONNULL(iter, cleanup, "iterator should be created");
   MCTF_ASSERT(pgagroal_art_iterator_has_next(iter), cleanup, "iterator should have next");

   int cnt = 0;
   while (pgagroal_art_iterator_next(iter))
   {
      cnt++;
      if (pgagroal_compare_string(iter->key, "key_str"))
      {
         MCTF_ASSERT_STR_EQ((char*)pgagroal_value_data(iter->value), "value1", cleanup, "key_str value should be value1");
         pgagroal_art_iterator_remove(iter);
         MCTF_ASSERT(!pgagroal_art_contains_key(t, "key_str"), cleanup, "key_str should not be contained");
      }
      else if (pgagroal_compare_string(iter->key, "key_int"))
      {
         MCTF_ASSERT_INT_EQ((int)pgagroal_value_data(iter->value), 1, cleanup, "key_int value should be 1");
         pgagroal_art_iterator_remove(iter);
         MCTF_ASSERT(!pgagroal_art_contains_key(t, "key_int"), cleanup, "key_int should not be contained");
      }
      else if (pgagroal_compare_string(iter->key, "key_bool"))
      {
         MCTF_ASSERT((bool)pgagroal_value_data(iter->value), cleanup, "key_bool value should be true");
         pgagroal_art_iterator_remove(iter);
         MCTF_ASSERT(!pgagroal_art_contains_key(t, "key_bool"), cleanup, "key_bool should not be contained");
      }
      else if (pgagroal_compare_string(iter->key, "key_float"))
      {
         MCTF_ASSERT_FLOAT_EQ(pgagroal_value_to_float(pgagroal_value_data(iter->value)), 2.5, cleanup, "key_float value should be 2.5");
         pgagroal_art_iterator_remove(iter);
         MCTF_ASSERT(!pgagroal_art_contains_key(t, "key_float"), cleanup, "key_float should not be contained");
      }
      else if (pgagroal_compare_string(iter->key, "key_double"))
      {
         MCTF_ASSERT_DOUBLE_EQ(pgagroal_value_to_double(pgagroal_value_data(iter->value)), 2.5, cleanup, "key_double value should be 2.5");
         pgagroal_art_iterator_remove(iter);
         MCTF_ASSERT(!pgagroal_art_contains_key(t, "key_double"), cleanup, "key_double should not be contained");
      }
      else if (pgagroal_compare_string(iter->key, "key_mem"))
      {
         pgagroal_art_iterator_remove(iter);
         MCTF_ASSERT(!pgagroal_art_contains_key(t, "key_mem"), cleanup, "key_mem should not be contained");
      }
      else if (pgagroal_compare_string(iter->key, "key_obj"))
      {
         MCTF_ASSERT_INT_EQ(((struct art_test_obj*)pgagroal_value_data(iter->value))->idx, 1, cleanup, "key_obj idx should be 1");
         MCTF_ASSERT_STR_EQ(((struct art_test_obj*)pgagroal_value_data(iter->value))->str, "obj1", cleanup, "key_obj str should be obj1");
         pgagroal_art_iterator_remove(iter);
         MCTF_ASSERT(!pgagroal_art_contains_key(t, "key_obj"), cleanup, "key_obj should not be contained");
      }
      else
      {
         MCTF_ASSERT(false, cleanup, "found key not inserted: %s", iter->key);
      }

      MCTF_ASSERT_INT_EQ(t->size, 7 - cnt, cleanup, "art tree size should decrease");
      MCTF_ASSERT_PTR_NULL(iter->key, cleanup, "iterator key should be NULL after remove");
      MCTF_ASSERT_PTR_NULL(iter->value, cleanup, "iterator value should be NULL after remove");
   }
   MCTF_ASSERT_INT_EQ(cnt, 7, cleanup, "iterator count should be 7");
   MCTF_ASSERT_INT_EQ(t->size, 0, cleanup, "art tree size should be 0");
   MCTF_ASSERT(!pgagroal_art_iterator_has_next(iter), cleanup, "iterator should not have next");
cleanup:
   pgagroal_art_iterator_destroy(iter);
   pgagroal_art_destroy(t);
   MCTF_FINISH();
}
MCTF_TEST(test_art_insert_search_extensive)
{
   struct art* t = NULL;
   char buf[512];
   FILE* f = NULL;
   uintptr_t line = 1;
   int len = 0;
   char* path = NULL;
   path = pgagroal_append(path, project_directory);
   path = pgagroal_append(path, "/pgagroal-testsuite/resource/art_advanced_test/words.txt");

   f = fopen(path, "r");
   MCTF_ASSERT_PTR_NONNULL(f, cleanup, "file should open");

   pgagroal_art_create(&t);
   while (fgets(buf, sizeof(buf), f))
   {
      len = strlen(buf);
      buf[len - 1] = '\0';
      MCTF_ASSERT(!pgagroal_art_insert(t, buf, line, ValueInt32), cleanup, "insert should succeed");
      line++;
   }

   // Seek back to the start
   fseek(f, 0, SEEK_SET);
   line = 1;
   while (fgets(buf, sizeof(buf), f))
   {
      len = strlen(buf);
      buf[len - 1] = '\0';
      int val = (int)pgagroal_art_search(t, buf);
      MCTF_ASSERT_INT_EQ(val, (int)line, cleanup, "test_art_insert_search_advanced Line: %d Val: %d Str: %s", (int)line, val, buf);
      line++;
   }
cleanup:
   if (f != NULL)
   {
      fclose(f);
   }
   free(path);
   pgagroal_art_destroy(t);
   MCTF_FINISH();
}
MCTF_TEST(test_art_insert_very_long)
{
   struct art* t = NULL;
   pgagroal_art_create(&t);

   unsigned char key1[300] = {16, 1, 1, 1, 7, 11, 1, 1, 1, 2, 17, 11, 1, 1, 1, 121, 11, 1, 1, 1, 121, 11, 1,
                              1, 1, 216, 11, 1, 1, 1, 202, 11, 1, 1, 1, 194, 11, 1, 1, 1, 224, 11, 1, 1, 1,
                              231, 11, 1, 1, 1, 211, 11, 1, 1, 1, 206, 11, 1, 1, 1, 208, 11, 1, 1, 1, 232,
                              11, 1, 1, 1, 124, 11, 1, 1, 1, 124, 2, 16, 1, 1, 1, 2, 12, 185, 89, 44, 213,
                              251, 173, 202, 210, 95, 185, 89, 111, 118, 250, 173, 202, 199, 101, 1,
                              8, 18, 182, 92, 236, 147, 171, 101, 151, 195, 112, 185, 218, 108, 246,
                              139, 164, 234, 195, 58, 177, 1, 8, 16, 1, 1, 1, 2, 12, 185, 89, 44, 213,
                              251, 173, 202, 211, 95, 185, 89, 111, 118, 250, 173, 202, 199, 101, 1,
                              8, 18, 181, 93, 46, 150, 9, 212, 191, 95, 102, 178, 217, 44, 178, 235,
                              29, 191, 218, 8, 16, 1, 1, 1, 2, 12, 185, 89, 44, 213, 251, 173, 202,
                              211, 95, 185, 89, 111, 118, 251, 173, 202, 199, 101, 1, 8, 18, 181, 93,
                              46, 151, 9, 212, 191, 95, 102, 183, 219, 229, 214, 59, 125, 182, 71,
                              108, 181, 220, 238, 150, 91, 117, 151, 201, 84, 183, 128, 8, 16, 1, 1,
                              1, 2, 12, 185, 89, 44, 213, 251, 173, 202, 211, 95, 185, 89, 111, 118,
                              251, 173, 202, 199, 100, 1, 8, 18, 181, 93, 46, 151, 9, 212, 191, 95,
                              108, 176, 217, 47, 51, 219, 61, 134, 207, 97, 151, 88, 237, 246, 208,
                              8, 18, 255, 255, 255, 219, 191, 198, 134, 5, 223, 212, 72, 44, 208,
                              251, 181, 14, 1, 1, 1, 8, '\0'};
   unsigned char key2[303] = {16, 1, 1, 1, 7, 10, 1, 1, 1, 2, 17, 11, 1, 1, 1, 121, 11, 1, 1, 1, 121, 11, 1,
                              1, 1, 216, 11, 1, 1, 1, 202, 11, 1, 1, 1, 194, 11, 1, 1, 1, 224, 11, 1, 1, 1,
                              231, 11, 1, 1, 1, 211, 11, 1, 1, 1, 206, 11, 1, 1, 1, 208, 11, 1, 1, 1, 232,
                              11, 1, 1, 1, 124, 10, 1, 1, 1, 124, 2, 16, 1, 1, 1, 2, 12, 185, 89, 44, 213,
                              251, 173, 202, 211, 95, 185, 89, 111, 118, 251, 173, 202, 199, 101, 1,
                              8, 18, 182, 92, 236, 147, 171, 101, 150, 195, 112, 185, 218, 108, 246,
                              139, 164, 234, 195, 58, 177, 1, 8, 16, 1, 1, 1, 2, 12, 185, 89, 44, 213,
                              251, 173, 202, 211, 95, 185, 89, 111, 118, 251, 173, 202, 199, 101, 1,
                              8, 18, 181, 93, 46, 151, 9, 212, 191, 95, 102, 178, 217, 44, 178, 235,
                              29, 191, 218, 8, 16, 1, 1, 1, 2, 12, 185, 89, 44, 213, 251, 173, 202,
                              211, 95, 185, 89, 111, 118, 251, 173, 202, 199, 101, 1, 8, 18, 181, 93,
                              46, 151, 9, 212, 191, 95, 102, 183, 219, 229, 214, 59, 125, 182, 71,
                              108, 181, 221, 238, 151, 91, 117, 151, 201, 84, 183, 128, 8, 16, 1, 1,
                              1, 3, 12, 185, 89, 44, 213, 250, 133, 178, 195, 105, 183, 87, 237, 151,
                              155, 165, 151, 229, 97, 182, 1, 8, 18, 161, 91, 239, 51, 11, 61, 151,
                              223, 114, 179, 217, 64, 8, 12, 186, 219, 172, 151, 91, 53, 166, 221,
                              101, 178, 1, 8, 18, 255, 255, 255, 219, 191, 198, 134, 5, 208, 212, 72,
                              44, 208, 251, 180, 14, 1, 1, 1, 8, '\0'};

   MCTF_ASSERT(!pgagroal_art_insert(t, (char*)key1, (uintptr_t)key1, ValueRef), cleanup, "insert key1 should succeed");
   MCTF_ASSERT(!pgagroal_art_insert(t, (char*)key2, (uintptr_t)key2, ValueRef), cleanup, "insert key2 should succeed");
   MCTF_ASSERT(!pgagroal_art_insert(t, (char*)key2, (uintptr_t)key2, ValueRef), cleanup, "insert key2 again should succeed");
   MCTF_ASSERT_INT_EQ(t->size, 2, cleanup, "art tree size should be 2");
cleanup:
   pgagroal_art_destroy(t);
   MCTF_FINISH();
}
MCTF_TEST(test_art_random_delete)
{
   struct art* t = NULL;
   char buf[512];
   FILE* f = NULL;
   uintptr_t line = 1;
   int len = 0;
   char* path = NULL;
   path = pgagroal_append(path, project_directory);
   path = pgagroal_append(path, "/pgagroal-testsuite/resource/art_advanced_test/words.txt");

   f = fopen(path, "r");
   MCTF_ASSERT_PTR_NONNULL(f, cleanup, "file should open");

   pgagroal_art_create(&t);
   while (fgets(buf, sizeof(buf), f))
   {
      len = strlen(buf);
      buf[len - 1] = '\0';
      MCTF_ASSERT(!pgagroal_art_insert(t, buf, line, ValueInt32), cleanup, "insert should succeed");
      line++;
   }

   // Seek back to the start
   fseek(f, 0, SEEK_SET);
   line = 1;
   while (fgets(buf, sizeof(buf), f))
   {
      len = strlen(buf);
      buf[len - 1] = '\0';
      int val = (int)pgagroal_art_search(t, buf);
      MCTF_ASSERT_INT_EQ(val, (int)line, cleanup, "test_art_insert_search_advanced Line: %d Val: %d Str: %s", (int)line, val, buf);
      line++;
   }

   MCTF_ASSERT(!pgagroal_art_delete(t, "A"), cleanup, "delete A should succeed");
   MCTF_ASSERT(!pgagroal_art_contains_key(t, "A"), cleanup, "A should not be contained");

   MCTF_ASSERT(!pgagroal_art_delete(t, "yard"), cleanup, "delete yard should succeed");
   MCTF_ASSERT(!pgagroal_art_contains_key(t, "yard"), cleanup, "yard should not be contained");

   MCTF_ASSERT(!pgagroal_art_delete(t, "Xenarchi"), cleanup, "delete Xenarchi should succeed");
   MCTF_ASSERT(!pgagroal_art_contains_key(t, "Xenarchi"), cleanup, "Xenarchi should not be contained");

   MCTF_ASSERT(!pgagroal_art_delete(t, "F"), cleanup, "delete F should succeed");
   MCTF_ASSERT(!pgagroal_art_contains_key(t, "F"), cleanup, "F should not be contained");

   MCTF_ASSERT(!pgagroal_art_delete(t, "wirespun"), cleanup, "delete wirespun should succeed");
   MCTF_ASSERT(!pgagroal_art_contains_key(t, "wirespun"), cleanup, "wirespun should not be contained");
cleanup:
   if (f != NULL)
   {
      fclose(f);
   }
   free(path);
   pgagroal_art_destroy(t);
   MCTF_FINISH();
}
MCTF_TEST(test_art_insert_index_out_of_range)
{
   struct art* t = NULL;
   pgagroal_art_create(&t);
   char* s1 = "abcdefghijklmnxyz";
   char* s2 = "abcdefghijklmnopqrstuvw";
   char* s3 = "abcdefghijk";
   MCTF_ASSERT(!pgagroal_art_insert(t, s1, 1, ValueUInt8), cleanup, "insert s1 should succeed");
   MCTF_ASSERT(!pgagroal_art_insert(t, s2, 1, ValueUInt8), cleanup, "insert s2 should succeed");
   MCTF_ASSERT_INT_EQ(pgagroal_art_search(t, s3), 0, cleanup, "search s3 should return 0");
cleanup:
   pgagroal_art_destroy(t);
   MCTF_FINISH();
}

static void
test_obj_create(int idx, struct art_test_obj** obj)
{
   struct art_test_obj* o = NULL;

   o = malloc(sizeof(struct art_test_obj));
   memset(o, 0, sizeof(struct art_test_obj));
   o->idx = idx;
   o->str = pgagroal_append(o->str, "obj");
   o->str = pgagroal_append_int(o->str, idx);

   *obj = o;
}
static void
test_obj_destroy(struct art_test_obj* obj)
{
   if (obj == NULL)
   {
      return;
   }
   free(obj->str);
   free(obj);
}

static void
test_obj_destroy_cb(uintptr_t obj)
{
   test_obj_destroy((struct art_test_obj*)obj);
}
