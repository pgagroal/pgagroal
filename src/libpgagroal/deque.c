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

#include <pgagroal.h>
#include <deque.h>
#include <logging.h>
#include <utils.h>
#include <value.h>

#include <stdlib.h>
#include <string.h>

// tag is copied if not NULL
static void
deque_offer(struct deque* deque, char* tag, uintptr_t data, enum value_type type, struct value_config* config);

// tag is copied if not NULL
static void
deque_node_create(uintptr_t data, enum value_type type, char* tag, struct value_config* config, struct deque_node** node);

// tag will always be freed
static void
deque_node_destroy(struct deque_node* node);

static void
deque_read_lock(struct deque* deque);

static void
deque_write_lock(struct deque* deque);

static void
deque_unlock(struct deque* deque);

static struct deque_node*
deque_next(struct deque* deque, struct deque_node* node);

static struct deque_node*
deque_find(struct deque* deque, char* tag);

static char*
to_json_string(struct deque* deque, char* tag, int indent);

static char*
to_compact_json_string(struct deque* deque, char* tag, int indent);

static char*
to_text_string(struct deque* deque, char* tag, int indent);

static struct deque_node*
deque_remove(struct deque* deque, struct deque_node* node);

static struct deque_node*
get_middle(struct deque_node* node);

static struct deque_node*
deque_sort(struct deque_node* node, compare_cb cmp);

static struct deque_node*
deque_merge(struct deque_node* node1, struct deque_node* node2, compare_cb cmp);

static int
tag_compare(char* tag1, char* tag2);

int
pgagroal_deque_create(bool thread_safe, struct deque** deque)
{
   struct deque* q = NULL;
   q = malloc(sizeof(struct deque));
   q->size = 0;
   q->thread_safe = thread_safe;
   if (thread_safe)
   {
      pthread_rwlock_init(&q->mutex, NULL);
   }
   deque_node_create(0, ValueInt32, NULL, NULL, &q->start);
   deque_node_create(0, ValueInt32, NULL, NULL, &q->end);
   q->start->next = q->end;
   q->end->prev = q->start;
   *deque = q;
   return 0;
}

int
pgagroal_deque_add(struct deque* deque, char* tag, uintptr_t data, enum value_type type)
{
   deque_offer(deque, tag, data, type, NULL);
   return 0;
}

int
pgagroal_deque_remove(struct deque* deque, char* tag)
{
   int cnt = 0;
   struct deque_iterator* iter = NULL;
   if (deque == NULL || tag == NULL)
   {
      return 0;
   }
   pgagroal_deque_iterator_create(deque, &iter);
   while (pgagroal_deque_iterator_next(iter))
   {
      if (pgagroal_compare_string(iter->tag, tag))
      {
         pgagroal_deque_iterator_remove(iter);
         cnt++;
      }
   }
   pgagroal_deque_iterator_destroy(iter);
   return cnt;
}

int
pgagroal_deque_clear(struct deque* deque)
{
   struct deque_iterator* iter = NULL;
   if (deque == NULL)
   {
      return 0;
   }
   pgagroal_deque_iterator_create(deque, &iter);
   while (pgagroal_deque_iterator_next(iter))
   {
      pgagroal_deque_iterator_remove(iter);
   }
   pgagroal_deque_iterator_destroy(iter);
   return 0;
}

int
pgagroal_deque_add_with_config(struct deque* deque, char* tag, uintptr_t data, struct value_config* config)
{
   deque_offer(deque, tag, data, ValueRef, config);
   return 0;
}

uintptr_t
pgagroal_deque_poll(struct deque* deque, char** tag)
{
   struct deque_node* head = NULL;
   struct value* val = NULL;
   uintptr_t data = 0;
   if (deque == NULL || pgagroal_deque_size(deque) == 0)
   {
      return 0;
   }
   deque_write_lock(deque);
   head = deque->start->next;
   // this should not happen when size is not 0, but just in case
   if (head == deque->end)
   {
      deque_unlock(deque);
      return 0;
   }
   // remove node
   deque->start->next = head->next;
   head->next->prev = deque->start;
   deque->size--;
   val = head->data;
   if (tag != NULL)
   {
      *tag = head->tag;
   }
   free(head);

   data = pgagroal_value_data(val);
   free(val);

   deque_unlock(deque);
   return data;
}

uintptr_t
pgagroal_deque_poll_last(struct deque* deque, char** tag)
{
   struct deque_node* tail = NULL;
   struct value* val = NULL;
   uintptr_t data = 0;
   if (deque == NULL || pgagroal_deque_size(deque) == 0)
   {
      return 0;
   }
   deque_write_lock(deque);
   tail = deque->end->prev;
   if (tail == deque->start)
   {
      deque_unlock(deque);
      return 0;
   }
   // remove node
   deque->end->prev = tail->prev;
   tail->prev->next = deque->end;
   deque->size--;

   val = tail->data;
   if (tag != NULL)
   {
      *tag = tail->tag;
   }
   free(tail);

   data = pgagroal_value_data(val);
   free(val);

   deque_unlock(deque);
   return data;
}

uintptr_t
pgagroal_deque_peek(struct deque* deque, char** tag)
{
   struct deque_node* head = NULL;
   struct value* val = NULL;
   if (deque == NULL || pgagroal_deque_size(deque) == 0)
   {
      return 0;
   }
   deque_read_lock(deque);
   head = deque->start->next;
   // this should not happen when size is not 0, but just in case
   if (head == deque->end)
   {
      deque_unlock(deque);
      return 0;
   }
   val = head->data;
   if (tag != NULL)
   {
      *tag = head->tag;
   }
   deque_unlock(deque);
   return pgagroal_value_data(val);
}

uintptr_t
pgagroal_deque_peek_last(struct deque* deque, char** tag)
{
   struct deque_node* tail = NULL;
   struct value* val = NULL;
   if (deque == NULL || pgagroal_deque_size(deque) == 0)
   {
      return 0;
   }
   deque_read_lock(deque);
   tail = deque->end->prev;
   // this should not happen when size is not 0, but just in case
   if (tail == deque->start)
   {
      deque_unlock(deque);
      return 0;
   }
   val = tail->data;
   if (tag != NULL)
   {
      *tag = tail->tag;
   }
   deque_unlock(deque);
   return pgagroal_value_data(val);
}

uintptr_t
pgagroal_deque_get(struct deque* deque, char* tag)
{
   struct deque_node* n = NULL;
   uintptr_t ret = 0;

#ifdef DEBUG
   pgagroal_log_trace("pgagroal_deque_get: %s", tag);
#endif

   deque_read_lock(deque);
   n = deque_find(deque, tag);
   if (n == NULL)
   {
      goto error;
   }
   ret = pgagroal_value_data(n->data);
   deque_unlock(deque);
   return ret;
error:
   deque_unlock(deque);
   return 0;
}

bool
pgagroal_deque_exists(struct deque* deque, char* tag)
{
   bool ret = false;
   struct deque_node* n = NULL;

   deque_read_lock(deque);

   n = deque_find(deque, tag);
   if (n != NULL)
   {
      ret = true;
   }

   deque_unlock(deque);

   return ret;
}

bool
pgagroal_deque_empty(struct deque* deque)
{
   return pgagroal_deque_size(deque) == 0;
}

void
pgagroal_deque_list(struct deque* deque)
{
   char* str = NULL;
   if (pgagroal_log_is_enabled(PGAGROAL_LOGGING_LEVEL_DEBUG5))
   {
      str = pgagroal_deque_to_string(deque, FORMAT_JSON, NULL, 0);
      pgagroal_log_trace("Deque: %s", str);
      free(str);
   }
}

void
pgagroal_deque_sort(struct deque* deque, compare_cb compare)
{
   deque_write_lock(deque);
   if (deque == NULL || deque->start == NULL || deque->end == NULL || deque->size <= 1)
   {
      deque_unlock(deque);
      return;
   }
   // break the connection to start and end node since we are going to move nodes around
   struct deque_node* first = deque->start->next;
   struct deque_node* last = deque->end->prev;
   struct deque_node* node = NULL;
   first->prev = NULL;
   last->next = NULL;
   deque->start->next = NULL;
   deque->end->prev = NULL;
   node = deque_sort(first, compare);
   deque->start->next = node;
   node->prev = deque->start;
   while (node->next != NULL)
   {
      node = node->next;
   }
   deque->end->prev = node;
   node->next = deque->end;
   deque_unlock(deque);
}

void
pgagroal_deque_destroy(struct deque* deque)
{
   struct deque_node* n = NULL;
   struct deque_node* next = NULL;
   if (deque == NULL)
   {
      return;
   }
   n = deque->start;
   while (n != NULL)
   {
      next = n->next;
      deque_node_destroy(n);
      n = next;
   }
   if (deque->thread_safe)
   {
      pthread_rwlock_destroy(&deque->mutex);
   }
   free(deque);
}

char*
pgagroal_deque_to_string(struct deque* deque, int32_t format, char* tag, int indent)
{
   if (format == FORMAT_JSON)
   {
      return to_json_string(deque, tag, indent);
   }
   else if (format == FORMAT_TEXT)
   {
      return to_text_string(deque, tag, indent);
   }
   else if (format == FORMAT_JSON_COMPACT)
   {
      return to_compact_json_string(deque, tag, indent);
   }
   return NULL;
}

uint32_t
pgagroal_deque_size(struct deque* deque)
{
   uint32_t size = 0;
   if (deque == NULL)
   {
      return 0;
   }
   deque_read_lock(deque);
   size = deque->size;
   deque_unlock(deque);
   return size;
}

int
pgagroal_deque_iterator_create(struct deque* deque, struct deque_iterator** iter)
{
   struct deque_iterator* i = NULL;
   if (deque == NULL)
   {
      return 1;
   }
   i = malloc(sizeof(struct deque_iterator));
   i->deque = deque;
   i->cur = deque->start;
   i->tag = NULL;
   i->value = NULL;
   *iter = i;
   return 0;
}

void
pgagroal_deque_iterator_remove(struct deque_iterator* iter)
{
   if (iter == NULL || iter->cur == NULL || iter->deque == NULL ||
       iter->cur == iter->deque->start || iter->cur == iter->deque->end)
   {
      return;
   }
   iter->cur = deque_remove(iter->deque, iter->cur);
   if (iter->cur == iter->deque->start)
   {
      iter->value = NULL;
      iter->tag = NULL;
      return;
   }
   iter->value = iter->cur->data;
   iter->tag = iter->cur->tag;
   return;
}

void
pgagroal_deque_iterator_destroy(struct deque_iterator* iter)
{
   if (iter == NULL)
   {
      return;
   }
   free(iter);
}

bool
pgagroal_deque_iterator_next(struct deque_iterator* iter)
{
   if (iter == NULL)
   {
      return false;
   }
   iter->cur = deque_next(iter->deque, iter->cur);
   if (iter->cur == NULL)
   {
      return false;
   }
   iter->value = iter->cur->data;
   iter->tag = iter->cur->tag;
   return true;
}

bool
pgagroal_deque_iterator_has_next(struct deque_iterator* iter)
{
   if (iter == NULL)
   {
      return false;
   }
   return deque_next(iter->deque, iter->cur) != NULL;
}

static void
deque_offer(struct deque* deque, char* tag, uintptr_t data, enum value_type type, struct value_config* config)
{
   struct deque_node* n = NULL;
   struct deque_node* last = NULL;

   if (type == ValueNone)
   {
      return;
   }

   deque_node_create(data, type, tag, config, &n);
   deque_write_lock(deque);
   deque->size++;
   last = deque->end->prev;
   last->next = n;
   n->prev = last;
   n->next = deque->end;
   deque->end->prev = n;
   deque_unlock(deque);
}

static void
deque_node_create(uintptr_t data, enum value_type type, char* tag, struct value_config* config, struct deque_node** node)
{
   struct deque_node* n = NULL;
   n = malloc(sizeof(struct deque_node));
   memset(n, 0, sizeof(struct deque_node));
   if (config != NULL)
   {
      pgagroal_value_create_with_config(data, config, &n->data);
   }
   else
   {
      pgagroal_value_create(type, data, &n->data);
   }
   if (tag != NULL)
   {
      n->tag = pgagroal_append(NULL, tag);
   }
   else
   {
      n->tag = NULL;
   }
   *node = n;
}

static void
deque_node_destroy(struct deque_node* node)
{
   if (node == NULL)
   {
      return;
   }
   pgagroal_value_destroy(node->data);
   free(node->tag);
   free(node);
}

static void
deque_read_lock(struct deque* deque)
{
   if (deque == NULL || !deque->thread_safe)
   {
      return;
   }
   pthread_rwlock_rdlock(&deque->mutex);
}

static void
deque_write_lock(struct deque* deque)
{
   if (deque == NULL || !deque->thread_safe)
   {
      return;
   }
   pthread_rwlock_wrlock(&deque->mutex);
}

static void
deque_unlock(struct deque* deque)
{
   if (deque == NULL || !deque->thread_safe)
   {
      return;
   }
   pthread_rwlock_unlock(&deque->mutex);
}

static struct deque_node*
deque_next(struct deque* deque, struct deque_node* node)
{
   struct deque_node* next = NULL;
   if (deque == NULL || deque->size == 0 || node == NULL)
   {
      return NULL;
   }
   if (node->next == deque->end)
   {
      return NULL;
   }
   next = node->next;
   return next;
}

static struct deque_node*
deque_find(struct deque* deque, char* tag)
{
   struct deque_node* n = NULL;
   if (tag == NULL || strlen(tag) == 0 || deque == NULL || deque->size == 0)
   {
      return NULL;
   }
   n = deque_next(deque, deque->start);

   while (n != NULL)
   {
      if (pgagroal_compare_string(tag, n->tag))
      {
         return n;
      }

      n = deque_next(deque, n);
   }
   return NULL;
}

static char*
to_json_string(struct deque* deque, char* tag, int indent)
{
   char* ret = NULL;
   ret = pgagroal_indent(ret, tag, indent);
   struct deque_node* cur = NULL;
   if (deque == NULL || pgagroal_deque_empty(deque))
   {
      ret = pgagroal_append(ret, "[]");
      return ret;
   }
   deque_read_lock(deque);
   ret = pgagroal_append(ret, "[\n");
   cur = deque_next(deque, deque->start);
   while (cur != NULL)
   {
      bool has_next = cur->next != deque->end;
      char* str = NULL;
      char* t = NULL;
      if (cur->tag != NULL)
      {
         t = pgagroal_append(t, cur->tag);
         t = pgagroal_append(t, ": ");
      }
      str = pgagroal_value_to_string(cur->data, FORMAT_JSON, t, indent + INDENT_PER_LEVEL);
      free(t);
      ret = pgagroal_append(ret, str);
      ret = pgagroal_append(ret, has_next ? ",\n" : "\n");
      free(str);
      cur = deque_next(deque, cur);
   }
   ret = pgagroal_indent(ret, NULL, indent);
   ret = pgagroal_append(ret, "]");
   deque_unlock(deque);
   return ret;
}

static char*
to_compact_json_string(struct deque* deque, char* tag, int indent)
{
   char* ret = NULL;
   ret = pgagroal_indent(ret, tag, indent);
   struct deque_node* cur = NULL;
   if (deque == NULL || pgagroal_deque_empty(deque))
   {
      ret = pgagroal_append(ret, "[]");
      return ret;
   }
   deque_read_lock(deque);
   ret = pgagroal_append(ret, "[");
   cur = deque_next(deque, deque->start);
   while (cur != NULL)
   {
      bool has_next = cur->next != deque->end;
      char* str = NULL;
      char* t = NULL;
      if (cur->tag != NULL)
      {
         t = pgagroal_append(t, cur->tag);
         t = pgagroal_append(t, ":");
      }
      str = pgagroal_value_to_string(cur->data, FORMAT_JSON_COMPACT, t, indent);
      free(t);
      ret = pgagroal_append(ret, str);
      ret = pgagroal_append(ret, has_next ? "," : "");
      free(str);
      cur = deque_next(deque, cur);
   }
   ret = pgagroal_append(ret, "]");
   deque_unlock(deque);
   return ret;
}

static char*
to_text_string(struct deque* deque, char* tag, int indent)
{
   char* ret = NULL;
   int cnt = 0;
   int next_indent = pgagroal_compare_string(tag, BULLET_POINT) ? 0 : indent;
   // we have a tag and it's not the bullet point, so that means another line
   if (tag != NULL && !pgagroal_compare_string(tag, BULLET_POINT))
   {
      ret = pgagroal_indent(ret, tag, indent);
      next_indent += INDENT_PER_LEVEL;
   }
   struct deque_node* cur = NULL;
   if (deque == NULL || pgagroal_deque_empty(deque))
   {
      ret = pgagroal_append(ret, "[]");
      return ret;
   }
   deque_read_lock(deque);
   cur = deque_next(deque, deque->start);
   while (cur != NULL)
   {
      bool has_next = cur->next != deque->end;
      char* str = NULL;
      str = pgagroal_value_to_string(cur->data, FORMAT_TEXT, BULLET_POINT, next_indent);
      if (cnt == 0)
      {
         cnt++;
         if (pgagroal_compare_string(tag, BULLET_POINT))
         {
            next_indent = indent + INDENT_PER_LEVEL;
         }
      }
      if (cur->data->type == ValueJSON)
      {
         ret = pgagroal_indent(ret, BULLET_POINT, next_indent);
      }
      ret = pgagroal_append(ret, str);
      ret = pgagroal_append(ret, has_next ? "\n" : "");
      free(str);
      cur = deque_next(deque, cur);
   }
   deque_unlock(deque);
   return ret;
}

static struct deque_node*
deque_remove(struct deque* deque, struct deque_node* node)
{
   if (deque == NULL || node == NULL || node == deque->start || node == deque->end)
   {
      return NULL;
   }
   struct deque_node* prev = node->prev;
   struct deque_node* next = node->next;
   prev->next = next;
   next->prev = prev;
   deque_node_destroy(node);
   deque->size--;
   return prev;
}

static struct deque_node*
get_middle(struct deque_node* node)
{
   struct deque_node* slow = node;
   struct deque_node* fast = node;
   while (fast != NULL && fast->next != NULL)
   {
      slow = slow->next;
      fast = fast->next->next;
   }
   return slow;
}

static struct deque_node*
deque_sort(struct deque_node* node, compare_cb cmp)
{
   struct deque_node* mid = NULL;
   struct deque_node* prevmid = NULL;
   struct deque_node* node1 = NULL;
   struct deque_node* node2 = NULL;
   if (node == NULL || node->next == NULL)
   {
      return node;
   }
   mid = get_middle(node);
   prevmid = mid->prev;
   mid->prev = NULL;
   prevmid->next = NULL;
   node1 = deque_sort(node, cmp);
   node2 = deque_sort(mid, cmp);
   return deque_merge(node1, node2, cmp);
}

static struct deque_node*
deque_merge(struct deque_node* node1, struct deque_node* node2, compare_cb cmp)
{
   struct deque_node* node = NULL;
   struct deque_node* left = node1;
   struct deque_node* right = node2;
   struct deque_node* next = NULL;
   struct deque_node* start = NULL;
   if (node1 == NULL)
   {
      return node2;
   }
   if (node2 == NULL)
   {
      return node1;
   }
   while (left != NULL && right != NULL)
   {
      int cmp_result = 0;

      if (cmp != NULL)
      {
         cmp_result = cmp(left->data, right->data);
      }
      else
      {
         cmp_result = tag_compare(left->tag, right->tag);
      }

      if (cmp_result <= 0)
      {
         next = left->next;
         if (node == NULL)
         {
            start = left;
            node = left;
            node->prev = NULL;
            node->next = NULL;
         }
         else
         {
            node->next = left;
            left->prev = node;
            left->next = NULL;
            node = node->next;
         }
         left = next;
      }
      else
      {
         next = right->next;
         if (node == NULL)
         {
            start = right;
            node = right;
            node->prev = NULL;
            node->next = NULL;
         }
         else
         {
            node->next = right;
            right->prev = node;
            right->next = NULL;
            node = node->next;
         }
         right = next;
      }
   }
   while (left != NULL)
   {
      next = left->next;
      node->next = left;
      left->prev = node;
      left = next;
      node = node->next;
   }
   while (right != NULL)
   {
      next = right->next;
      node->next = right;
      right->prev = node;
      right = next;
      node = node->next;
   }
   return start;
}
static int
tag_compare(char* tag1, char* tag2)
{
   if (tag1 == NULL)
   {
      return tag2 == NULL ? 0 : 1;
   }
   if (tag2 == NULL)
   {
      return tag1 == NULL ? 0 : -1;
   }
   return strcmp(tag1, tag2);
}
