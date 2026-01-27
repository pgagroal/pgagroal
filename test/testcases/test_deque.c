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
#include <deque.h>
#include <mctf.h>
#include <utils.h>
#include <value.h>

struct deque_test_obj
{
   char* str;
   int idx;
};

static void test_obj_create(int idx, struct deque_test_obj** obj);
static void test_obj_destroy(struct deque_test_obj* obj);
static void test_obj_destroy_cb(uintptr_t obj);

MCTF_TEST(test_deque_create)
{
   struct deque* dq = NULL;

   MCTF_ASSERT(!pgagroal_deque_create(false, &dq), cleanup, "deque creation should succeed");
   MCTF_ASSERT_PTR_NONNULL(dq, cleanup, "deque should not be NULL");
   MCTF_ASSERT_INT_EQ(dq->size, 0, cleanup, "deque size should be 0");

cleanup:
   pgagroal_deque_destroy(dq);
   MCTF_FINISH();
}
MCTF_TEST(test_deque_add_poll)
{
   struct deque* dq = NULL;
   char* value1 = NULL;

   pgagroal_deque_create(false, &dq);
   MCTF_ASSERT(!pgagroal_deque_add(dq, NULL, (uintptr_t)-1, ValueInt32), cleanup, "add int should succeed");
   MCTF_ASSERT(!pgagroal_deque_add(dq, NULL, (uintptr_t)true, ValueBool), cleanup, "add bool should succeed");
   MCTF_ASSERT(!pgagroal_deque_add(dq, NULL, (uintptr_t)"value1", ValueString), cleanup, "add string should succeed");
   MCTF_ASSERT_INT_EQ(dq->size, 3, cleanup, "deque size should be 3");

   MCTF_ASSERT_INT_EQ((int)pgagroal_deque_peek(dq, NULL), -1, cleanup, "peek should return -1");

   MCTF_ASSERT_INT_EQ((int)pgagroal_deque_poll(dq, NULL), -1, cleanup, "poll should return -1");
   MCTF_ASSERT_INT_EQ(dq->size, 2, cleanup, "deque size should be 2");

   MCTF_ASSERT((bool)pgagroal_deque_poll(dq, NULL), cleanup, "poll should return true");
   MCTF_ASSERT_INT_EQ(dq->size, 1, cleanup, "deque size should be 1");

   value1 = (char*)pgagroal_deque_poll(dq, NULL);
   MCTF_ASSERT_STR_EQ(value1, "value1", cleanup, "polled value should be value1");
   MCTF_ASSERT_INT_EQ(dq->size, 0, cleanup, "deque size should be 0");

   MCTF_ASSERT_INT_EQ(pgagroal_deque_poll(dq, NULL), 0, cleanup, "poll on empty deque should return 0");
   MCTF_ASSERT_INT_EQ(dq->size, 0, cleanup, "deque size should remain 0");

cleanup:
   free(value1);
   pgagroal_deque_destroy(dq);
   MCTF_FINISH();
}
MCTF_TEST(test_deque_add_poll_last)
{
   struct deque* dq = NULL;
   char* value1 = NULL;

   pgagroal_deque_create(false, &dq);
   pgagroal_deque_add(dq, NULL, 0, ValueNone);
   MCTF_ASSERT(!pgagroal_deque_add(dq, NULL, (uintptr_t)"value1", ValueString), cleanup, "add string should succeed");
   MCTF_ASSERT(!pgagroal_deque_add(dq, NULL, (uintptr_t)true, ValueBool), cleanup, "add bool should succeed");
   MCTF_ASSERT(!pgagroal_deque_add(dq, NULL, (uintptr_t)-1, ValueInt32), cleanup, "add int should succeed");
   MCTF_ASSERT_INT_EQ(dq->size, 3, cleanup, "deque size should be 3");

   MCTF_ASSERT_INT_EQ((int)pgagroal_deque_peek_last(dq, NULL), -1, cleanup, "peek_last should return -1");

   MCTF_ASSERT_INT_EQ((int)pgagroal_deque_poll_last(dq, NULL), -1, cleanup, "poll_last should return -1");
   MCTF_ASSERT_INT_EQ(dq->size, 2, cleanup, "deque size should be 2");

   MCTF_ASSERT((bool)pgagroal_deque_poll_last(dq, NULL), cleanup, "poll_last should return true");
   MCTF_ASSERT_INT_EQ(dq->size, 1, cleanup, "deque size should be 1");

   value1 = (char*)pgagroal_deque_poll_last(dq, NULL);
   MCTF_ASSERT_STR_EQ(value1, "value1", cleanup, "polled value should be value1");
   MCTF_ASSERT_INT_EQ(dq->size, 0, cleanup, "deque size should be 0");

   MCTF_ASSERT_INT_EQ(pgagroal_deque_poll_last(dq, NULL), 0, cleanup, "poll_last on empty deque should return 0");
   MCTF_ASSERT_INT_EQ(dq->size, 0, cleanup, "deque size should remain 0");

cleanup:
   free(value1);
   pgagroal_deque_destroy(dq);
   MCTF_FINISH();
}
MCTF_TEST(test_deque_clear)
{
   struct deque* dq = NULL;

   pgagroal_deque_create(false, &dq);
   MCTF_ASSERT(!pgagroal_deque_add(dq, NULL, (uintptr_t)"value1", ValueString), cleanup, "add string should succeed");
   MCTF_ASSERT(!pgagroal_deque_add(dq, NULL, (uintptr_t)true, ValueBool), cleanup, "add bool should succeed");
   MCTF_ASSERT(!pgagroal_deque_add(dq, NULL, (uintptr_t)-1, ValueInt32), cleanup, "add int should succeed");
   MCTF_ASSERT_INT_EQ(dq->size, 3, cleanup, "deque size should be 3");

   pgagroal_deque_clear(dq);
   MCTF_ASSERT_INT_EQ(dq->size, 0, cleanup, "deque size should be 0 after clear");
   MCTF_ASSERT_INT_EQ(pgagroal_deque_poll(dq, NULL), 0, cleanup, "poll on empty deque should return 0");

cleanup:
   pgagroal_deque_destroy(dq);
   MCTF_FINISH();
}
MCTF_TEST(test_deque_remove)
{
   struct deque* dq = NULL;
   char* value1 = NULL;
   char* tag = NULL;

   pgagroal_deque_create(false, &dq);
   MCTF_ASSERT(!pgagroal_deque_add(dq, "tag1", (uintptr_t)"value1", ValueString), cleanup, "add with tag1 should succeed");
   MCTF_ASSERT(!pgagroal_deque_add(dq, "tag2", (uintptr_t)true, ValueBool), cleanup, "add with tag2 should succeed");
   MCTF_ASSERT(!pgagroal_deque_add(dq, "tag2", (uintptr_t)-1, ValueInt32), cleanup, "add with tag2 should succeed");
   MCTF_ASSERT_INT_EQ(dq->size, 3, cleanup, "deque size should be 3");

   MCTF_ASSERT_INT_EQ(pgagroal_deque_remove(dq, NULL), 0, cleanup, "remove with NULL tag should return 0");
   MCTF_ASSERT_INT_EQ(pgagroal_deque_remove(NULL, "tag2"), 0, cleanup, "remove with NULL deque should return 0");
   MCTF_ASSERT_INT_EQ(pgagroal_deque_remove(dq, "tag3"), 0, cleanup, "remove non-existent tag should return 0");

   MCTF_ASSERT_INT_EQ(pgagroal_deque_remove(dq, "tag2"), 2, cleanup, "remove tag2 should return 2");
   MCTF_ASSERT_INT_EQ(dq->size, 1, cleanup, "deque size should be 1");

   value1 = (char*)pgagroal_deque_peek(dq, &tag);
   MCTF_ASSERT_STR_EQ(value1, "value1", cleanup, "peeked value should be value1");
   MCTF_ASSERT_STR_EQ(tag, "tag1", cleanup, "peeked tag should be tag1");

cleanup:
   pgagroal_deque_destroy(dq);
   MCTF_FINISH();
}
MCTF_TEST(test_deque_add_with_config_and_get)
{
   struct deque* dq = NULL;
   struct value_config test_obj_config = {.destroy_data = test_obj_destroy_cb, .to_string = NULL};
   struct deque_test_obj* obj1 = NULL;
   struct deque_test_obj* obj2 = NULL;
   struct deque_test_obj* obj3 = NULL;

   test_obj_create(1, &obj1);
   test_obj_create(2, &obj2);
   test_obj_create(3, &obj3);

   pgagroal_deque_create(false, &dq);
   MCTF_ASSERT(!pgagroal_deque_add_with_config(dq, "tag1", (uintptr_t)obj1, &test_obj_config), cleanup, "add obj1 should succeed");
   MCTF_ASSERT(!pgagroal_deque_add_with_config(dq, "tag2", (uintptr_t)obj2, &test_obj_config), cleanup, "add obj2 should succeed");
   MCTF_ASSERT(!pgagroal_deque_add_with_config(dq, "tag3", (uintptr_t)obj3, &test_obj_config), cleanup, "add obj3 should succeed");
   MCTF_ASSERT_INT_EQ(dq->size, 3, cleanup, "deque size should be 3");

   MCTF_ASSERT_INT_EQ(((struct deque_test_obj*)pgagroal_deque_get(dq, "tag1"))->idx, 1, cleanup, "obj1 idx should be 1");
   MCTF_ASSERT_STR_EQ(((struct deque_test_obj*)pgagroal_deque_get(dq, "tag1"))->str, "obj1", cleanup, "obj1 str should be obj1");

   MCTF_ASSERT_INT_EQ(((struct deque_test_obj*)pgagroal_deque_get(dq, "tag2"))->idx, 2, cleanup, "obj2 idx should be 2");
   MCTF_ASSERT_STR_EQ(((struct deque_test_obj*)pgagroal_deque_get(dq, "tag2"))->str, "obj2", cleanup, "obj2 str should be obj2");

   MCTF_ASSERT_INT_EQ(((struct deque_test_obj*)pgagroal_deque_get(dq, "tag3"))->idx, 3, cleanup, "obj3 idx should be 3");
   MCTF_ASSERT_STR_EQ(((struct deque_test_obj*)pgagroal_deque_get(dq, "tag3"))->str, "obj3", cleanup, "obj3 str should be obj3");

cleanup:
   pgagroal_deque_destroy(dq);
   MCTF_FINISH();
}
MCTF_TEST(test_deque_iterator_read)
{
   struct deque* dq = NULL;
   struct deque_iterator* iter = NULL;
   int cnt = 0;
   char tag[2] = {0};

   pgagroal_deque_create(false, &dq);
   MCTF_ASSERT(!pgagroal_deque_add(dq, "1", 1, ValueInt32), cleanup, "add 1 should succeed");
   MCTF_ASSERT(!pgagroal_deque_add(dq, "2", 2, ValueInt32), cleanup, "add 2 should succeed");
   MCTF_ASSERT(!pgagroal_deque_add(dq, "3", 3, ValueInt32), cleanup, "add 3 should succeed");
   MCTF_ASSERT_INT_EQ(dq->size, 3, cleanup, "deque size should be 3");

   MCTF_ASSERT(pgagroal_deque_iterator_create(NULL, &iter), cleanup, "iterator create with NULL deque should fail");
   MCTF_ASSERT(!pgagroal_deque_iterator_create(dq, &iter), cleanup, "iterator create should succeed");
   MCTF_ASSERT_PTR_NONNULL(iter, cleanup, "iterator should not be NULL");
   MCTF_ASSERT(pgagroal_deque_iterator_has_next(iter), cleanup, "iterator should have next");

   while (pgagroal_deque_iterator_next(iter))
   {
      cnt++;
      MCTF_ASSERT_INT_EQ(pgagroal_value_data(iter->value), cnt, cleanup, "iterator value should match count");
      tag[0] = '0' + cnt;
      MCTF_ASSERT_STR_EQ(iter->tag, tag, cleanup, "iterator tag should match count");
   }
   MCTF_ASSERT_INT_EQ(cnt, 3, cleanup, "count should be 3");
   MCTF_ASSERT(!pgagroal_deque_iterator_has_next(iter), cleanup, "iterator should not have next");

cleanup:
   pgagroal_deque_iterator_destroy(iter);
   pgagroal_deque_destroy(dq);
   MCTF_FINISH();
}
MCTF_TEST(test_deque_iterator_remove)
{
   struct deque* dq = NULL;
   struct deque_iterator* iter = NULL;
   int cnt = 0;
   char tag[2] = {0};

   pgagroal_deque_create(false, &dq);
   MCTF_ASSERT(!pgagroal_deque_add(dq, "1", 1, ValueInt32), cleanup, "add 1 should succeed");
   MCTF_ASSERT(!pgagroal_deque_add(dq, "2", 2, ValueInt32), cleanup, "add 2 should succeed");
   MCTF_ASSERT(!pgagroal_deque_add(dq, "3", 3, ValueInt32), cleanup, "add 3 should succeed");
   MCTF_ASSERT_INT_EQ(dq->size, 3, cleanup, "deque size should be 3");

   MCTF_ASSERT(pgagroal_deque_iterator_create(NULL, &iter), cleanup, "iterator create with NULL deque should fail");
   MCTF_ASSERT(!pgagroal_deque_iterator_create(dq, &iter), cleanup, "iterator create should succeed");
   MCTF_ASSERT_PTR_NONNULL(iter, cleanup, "iterator should not be NULL");
   MCTF_ASSERT(pgagroal_deque_iterator_has_next(iter), cleanup, "iterator should have next");

   while (pgagroal_deque_iterator_next(iter))
   {
      cnt++;
      MCTF_ASSERT_INT_EQ(pgagroal_value_data(iter->value), cnt, cleanup, "iterator value should match count");
      tag[0] = '0' + cnt;
      MCTF_ASSERT_STR_EQ(iter->tag, tag, cleanup, "iterator tag should match count");

      if (cnt == 2 || cnt == 3)
      {
         pgagroal_deque_iterator_remove(iter);
      }
   }

   // should be no-op
   pgagroal_deque_iterator_remove(iter);

   MCTF_ASSERT_INT_EQ(dq->size, 1, cleanup, "deque size should be 1");
   MCTF_ASSERT(!pgagroal_deque_iterator_has_next(iter), cleanup, "iterator should not have next");

   MCTF_ASSERT_INT_EQ(pgagroal_deque_peek(dq, NULL), 1, cleanup, "peek should return 1");

cleanup:
   pgagroal_deque_iterator_destroy(iter);
   pgagroal_deque_destroy(dq);
   MCTF_FINISH();
}
MCTF_TEST(test_deque_sort)
{
   struct deque* dq = NULL;
   struct deque_iterator* iter = NULL;
   int cnt = 0;
   char tag[2] = {0};
   int index[6] = {2, 1, 3, 5, 4, 0};

   pgagroal_deque_create(false, &dq);
   for (int i = 0; i < 6; i++)
   {
      tag[0] = '0' + index[i];
      MCTF_ASSERT(!pgagroal_deque_add(dq, tag, index[i], ValueInt32), cleanup, "add should succeed");
   }

   pgagroal_deque_sort(dq);

   MCTF_ASSERT(!pgagroal_deque_iterator_create(dq, &iter), cleanup, "iterator create should succeed");

   while (pgagroal_deque_iterator_next(iter))
   {
      MCTF_ASSERT_INT_EQ(pgagroal_value_data(iter->value), cnt, cleanup, "iterator value should match count");
      tag[0] = '0' + cnt;
      MCTF_ASSERT_STR_EQ(iter->tag, tag, cleanup, "iterator tag should match count");
      cnt++;
   }

cleanup:
   pgagroal_deque_iterator_destroy(iter);
   pgagroal_deque_destroy(dq);
   MCTF_FINISH();
}

static void
test_obj_create(int idx, struct deque_test_obj** obj)
{
   struct deque_test_obj* o = NULL;

   o = malloc(sizeof(struct deque_test_obj));
   memset(o, 0, sizeof(struct deque_test_obj));
   o->idx = idx;
   o->str = pgagroal_append(o->str, "obj");
   o->str = pgagroal_append_int(o->str, idx);

   *obj = o;
}
static void
test_obj_destroy(struct deque_test_obj* obj)
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
   test_obj_destroy((struct deque_test_obj*)obj);
}