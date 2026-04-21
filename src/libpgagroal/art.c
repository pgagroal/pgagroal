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

#include <art.h>
#include <json.h>
#include <utils.h>

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define IS_LEAF(x)  (((uintptr_t)(x) & 1))
#define SET_LEAF(x) ((void*)((uintptr_t)(x) | 1))
#define GET_LEAF(x) ((struct art_leaf*)((void*)((uintptr_t)(x) & ~1)))

enum art_node_type {
   Node4,
   Node16,
   Node48,
   Node256
};

/**
 * This struct is included as part
 * of all the various node sizes.
 * All node types should be aligned
 * because we need the last bit to be 0 as a flag bit for leaf.
 * So that leaf can be treated as a node as well and stored in the children field,
 * and only converted back when necessary
 */
struct art_node
{
   uint32_t prefix_len;                  /**< The actual length of the prefix segment */
   enum art_node_type type;              /**< The node type */
   uint8_t num_children;                 /**< The number of children */
   unsigned char prefix[MAX_PREFIX_LEN]; /**< The (potentially partial) prefix, only record up to MAX_PREFIX_LEN characters */
} __attribute__((aligned(64)));

/**
 * The ART leaf with key buffer of arbitrary size
 */
struct art_leaf
{
   struct value* value;
   uint32_t key_len;
   unsigned char key[];
} __attribute__((aligned(64)));

/**
 * The ART node with only 4 children,
 * the key character and the children pointer are stored
 * in the same position of the corresponding array.
 * The keys are stored in sorted order sequentially.
 */
struct art_node4
{
   struct art_node node;
   unsigned char keys[4];
   struct art_node* children[4];
} __attribute__((aligned(64)));

/**
 * Similar structure as node4, but with 16 children.
 * The keys are stored in sorted order sequentially.
 */
struct art_node16
{
   struct art_node node;
   unsigned char keys[16];
   struct art_node* children[16];
} __attribute__((aligned(64)));

/**
 * A full key array that can be indexed by the key character directly,
 * the array stores the index of the corresponding child in the array.
 * Note that in practice 0 is used for invalid index,
 * so the actual index is the index in key array - 1
 */
struct art_node48
{
   struct art_node node;
   unsigned char keys[256];
   struct art_node* children[48];
} __attribute__((aligned(64)));

/**
 * A direct array of children using key character as index
 */
struct art_node256
{
   struct art_node node;
   struct art_node* children[256];
} __attribute__((aligned(64)));

struct to_string_param
{
   char* str;
   int indent;
   uint64_t cnt;
   char* tag;
   struct art* t;
};

static struct art_node**
node_get_child(struct art_node* node, unsigned char ch);

// Get the left most leaf of a child
static struct art_leaf*
node_get_minimum(struct art_node* node);

static void
create_art_leaf(struct art_leaf** leaf, unsigned char* key, uint32_t key_len, uintptr_t value, enum value_type type, struct value_config* config);

static void
create_art_node(struct art_node** node, enum art_node_type type);

static void
create_art_node4(struct art_node4** node);

static void
create_art_node16(struct art_node16** node);

static void
create_art_node48(struct art_node48** node);

static void
create_art_node256(struct art_node256** node);

// Destroy ART nodes/leaves recursively
static void
destroy_art_node(struct art_node* node);

static int
art_iterate(struct art* t, art_callback cb, void* data);

/**
 * Get where the keys diverge starting from depth.
 * This function only compares within the partial prefix range
 * @param node The node
 * @param key The key
 * @param depth The starting depth
 * @param key_len The length of the key
 * @return The length of the part of the prefix that matches
 */
static uint32_t
check_prefix_partial(struct art_node* node, unsigned char* key, uint32_t depth, uint32_t key_len);

/**
 * Get where the keys diverge starting from depth.
 * This function compares with the complete key to determine if diverging point goes beyond current prefix or partial prefix
 * @param node The node
 * @param key The key
 * @param depth The starting depth
 * @param key_len The length of the key
 * @return The length of the part of the prefix that matches
 */
static uint32_t
check_prefix(struct art_node* node, unsigned char* key, uint32_t depth, uint32_t key_len);

/**
 * Compare the key stored in leaf and the original key
 * @param leaf
 * @param key
 * @param key_len
 * @return true if the key matches
 */
static bool
leaf_match(struct art_leaf* leaf, unsigned char* key, uint32_t key_len);

/**
 * Find the index of the corresponding key character using binary search.
 * If not found, the index of the largest element smaller than ch,
 * or -1 if ch is the smallest, will be returned,
 * @param ch
 * @param keys
 * @param length
 * @return The index
 */
static int
find_index(unsigned char ch, const unsigned char* keys, int length);

/**
 * Insert a value into a node recursively, adopting lazy expansion and path compression --
 * Expand the leaf, or split inner node should keys diverge within node's prefix range
 * @param node The node
 * @param node_ref The reference to node pointer
 * @param depth The depth into the node, which is the same as the total prefix length
 * @param key The key
 * @param key_len The length of the key
 * @param value The value data
 * @param type The value type
 * @param config The config
 * @param new If the key value is newly inserted (not replaced)
 * @return Old value if the key exists, otherwise NULL
 */
static struct value*
art_node_insert(struct art_node* node, struct art_node** node_ref, uint32_t depth, unsigned char* key, uint32_t key_len, uintptr_t value, enum value_type type, struct value_config* config, bool* new);

/**
 * Delete a value from a node recursively.
 * @param node The node
 * @param node_ref The reference to node pointer
 * @param depth The depth into the node
 * @param key The key
 * @param key_len The length of the key
 * @return Deleted value if the key exists, otherwise NULL
 */
static struct art_leaf*
art_node_delete(struct art_node* node, struct art_node** node_ref, uint32_t depth, unsigned char* key, uint32_t key_len);

static int
art_node_iterate(struct art_node* node, art_callback cb, void* data);

static void
node_add_child(struct art_node* node, struct art_node** node_ref, unsigned char ch, void* child);

/**
 * Add a child to the node. The function assumes node is not NULL,
 * nor the key character already exists.
 * If node is full, a new node of type node16 will be created. The old
 * node will be replaced by new node through node_ref.
 * @param node The node
 * @param node_ref The reference of the node pointer
 * @param ch The key character
 * @param child The child
 */
static void
node4_add_child(struct art_node4* node, struct art_node** node_ref, unsigned char ch, void* child);

static void
node16_add_child(struct art_node16* node, struct art_node** node_ref, unsigned char ch, void* child);

static void
node48_add_child(struct art_node48* node, struct art_node** node_ref, unsigned char ch, void* child);

static void
node256_add_child(struct art_node256* node, unsigned char ch, void* child);

// All removal functions assume the child to remove is leaf, meaning they don't try removing anything recursively.
// They also do not free the leaf node for bookkeeping purpose. The key insight is that due to path compression,
// no node will have only one child, if node has only one child after deletion, it merges with this child
static void
node_remove_child(struct art_node* node, struct art_node** node_ref, unsigned char ch);

static void
node4_remove_child(struct art_node4* node, struct art_node** node_ref, unsigned char ch);

static void
node16_remove_child(struct art_node16* node, struct art_node** node_ref, unsigned char ch);

static void
node48_remove_child(struct art_node48* node, struct art_node** node_ref, unsigned char ch);

static void
node256_remove_child(struct art_node256* node, struct art_node** node_ref, unsigned char ch);

static void
copy_header(struct art_node* dest, struct art_node* src);

static uint32_t
min(uint32_t a, uint32_t b);

static struct value*
art_search(struct art* t, unsigned char* key, uint32_t key_len);

static int
art_to_json_string_cb(void* param, const char* key, struct value* value);

static int
art_to_text_string_cb(void* param, const char* key, struct value* value);

static int
art_to_compact_json_string_cb(void* param, const char* key, struct value* value);

static char*
to_json_string(struct art* t, char* tag, int indent);

static char*
to_compact_json_string(struct art* t, char* tag, int indent);

static char*
to_text_string(struct art* t, char* tag, int indent);

int
pgagroal_art_create(struct art** tree)
{
   struct art* t = NULL;
   t = malloc(sizeof(struct art));
   t->size = 0;
   t->root = NULL;
   *tree = t;
   return 0;
}

int
pgagroal_art_destroy(struct art* tree)
{
   if (tree == NULL)
   {
      return 0;
   }
   destroy_art_node(tree->root);
   free(tree);
   return 0;
}

uintptr_t
pgagroal_art_search(struct art* t, char* key)
{
   if (t == NULL || key == NULL)
   {
      return false;
   }
   struct value* val = art_search(t, (unsigned char*)key, strlen(key) + 1);
   return pgagroal_value_data(val);
}

uintptr_t
pgagroal_art_search_typed(struct art* t, char* key, enum value_type* type)
{
   struct value* val = NULL;

   if (t == NULL || key == NULL)
   {
      return false;
   }

   val = art_search(t, (unsigned char*)key, strlen(key) + 1);

   *type = pgagroal_value_type(val);

   return pgagroal_value_data(val);
}

bool
pgagroal_art_contains_key(struct art* t, char* key)
{
   if (t == NULL || key == NULL)
   {
      return false;
   }
   struct value* val = art_search(t, (unsigned char*)key, strlen(key) + 1);
   return val != NULL;
}

struct prefix_search_state
{
   char*** matches;
   int max_matches;
   int current_count;
   uint32_t key_len;
   char* prefix;
};

static int
prefix_search_cb(void* param, const char* key, struct value* value)
{
   struct prefix_search_state* state = (struct prefix_search_state*)param;
   (void)value;

   if (state->current_count >= state->max_matches)
   {
      return 1;
   }

   (*state->matches)[state->current_count] = pgagroal_append(NULL, (char*)key);
   state->current_count++;

   return 0;
}

int
pgagroal_art_prefix_search(struct art* t, char* prefix, char*** matches, int max_matches)
{
   struct art_node* node = NULL;
   struct art_node** child = NULL;
   uint32_t depth = 0;
   uint32_t key_len = 0;
   unsigned char* key = (unsigned char*)prefix;
   struct prefix_search_state state;
   struct art_leaf* min_leaf = NULL;

   if (t == NULL || t->root == NULL || matches == NULL || max_matches <= 0)
   {
      return -1;
   }

   *matches = malloc((max_matches + 1) * sizeof(char*));
   if (*matches == NULL)
   {
      return -1;
   }
   memset(*matches, 0, (max_matches + 1) * sizeof(char*));

   state.matches = matches;
   state.max_matches = max_matches;
   state.current_count = 0;
   state.prefix = prefix;

   if (prefix == NULL || strlen(prefix) == 0)
   {
      art_node_iterate(t->root, prefix_search_cb, &state);
      return state.current_count;
   }

   key_len = strlen(prefix);
   state.key_len = key_len;
   node = t->root;

   while (node != NULL)
   {
      if (IS_LEAF(node))
      {
         struct art_leaf* leaf = GET_LEAF(node);
         if (leaf->key_len >= key_len && strncmp((char*)leaf->key, prefix, key_len) == 0)
         {
            prefix_search_cb(&state, (char*)leaf->key, leaf->value);
         }
         return state.current_count;
      }

      uint32_t cpl = check_prefix_partial(node, key, depth, key_len);
      if (cpl < min(node->prefix_len, MAX_PREFIX_LEN) && depth + cpl < key_len)
      {
         return state.current_count;
      }

      depth += node->prefix_len;
      if (depth >= key_len)
      {
         min_leaf = node_get_minimum(node);
         if (min_leaf != NULL && min_leaf->key_len >= key_len && strncmp((char*)min_leaf->key, prefix, key_len) == 0)
         {
            art_node_iterate(node, prefix_search_cb, &state);
         }
         return state.current_count;
      }

      child = node_get_child(node, key[depth]);
      node = child != NULL ? *child : NULL;
      depth++;
   }

   return state.current_count;
}

int
pgagroal_art_insert(struct art* t, char* key, uintptr_t value, enum value_type type)
{
   struct value* old_val = NULL;
   bool new = false;
   if (t == NULL || key == NULL || type == ValueNone)
   {
      // c'mon, at least create a tree first...
      goto error;
   }
   old_val = art_node_insert(t->root, &t->root, 0, (unsigned char*)key, strlen(key) + 1, value, type, NULL, &new);
   pgagroal_value_destroy(old_val);
   if (new)
   {
      t->size++;
   }
   return 0;
error:
   return 1;
}

int
pgagroal_art_insert_with_config(struct art* t, char* key, uintptr_t value, struct value_config* config)
{
   struct value* old_val = NULL;
   bool new = false;
   if (t == NULL || key == NULL)
   {
      goto error;
   }
   old_val = art_node_insert(t->root, &t->root, 0, (unsigned char*)key, strlen(key) + 1, value, ValueRef, config, &new);
   pgagroal_value_destroy(old_val);
   if (new)
   {
      t->size++;
   }
   return 0;
error:
   return 1;
}

int
pgagroal_art_delete(struct art* t, char* key)
{
   struct art_leaf* l = NULL;
   if (t == NULL || key == NULL)
   {
      return 1;
   }
   l = art_node_delete(t->root, &t->root, 0, (unsigned char*)key, strlen(key) + 1);
   if (l != NULL)
   {
      t->size--;
      pgagroal_value_destroy(l->value);
   }

   free(l);
   return 0;
}

int
pgagroal_art_clear(struct art* t)
{
   if (t == NULL)
   {
      return 0;
   }
   destroy_art_node(t->root);
   t->root = NULL;
   t->size = 0;
   return 0;
}

char*
pgagroal_art_to_string(struct art* t, int32_t format, char* tag, int indent)
{
   if (format == FORMAT_JSON)
   {
      return to_json_string(t, tag, indent);
   }
   else if (format == FORMAT_TEXT)
   {
      return to_text_string(t, tag, indent);
   }
   else if (format == FORMAT_JSON_COMPACT)
   {
      return to_compact_json_string(t, tag, indent);
   }
   return NULL;
}

static uint32_t
min(uint32_t a, uint32_t b)
{
   if (a >= b)
   {
      return b;
   }
   return a;
}

static void
create_art_leaf(struct art_leaf** leaf, unsigned char* key, uint32_t key_len, uintptr_t value, enum value_type type, struct value_config* config)
{
   struct art_leaf* l = NULL;
   l = malloc(sizeof(struct art_leaf) + key_len);
   memset(l, 0, sizeof(struct art_leaf) + key_len);
   if (config != NULL)
   {
      pgagroal_value_create_with_config(value, config, &l->value);
   }
   else
   {
      pgagroal_value_create(type, value, &l->value);
   }

   l->key_len = key_len;
   memcpy(l->key, key, key_len);
   *leaf = l;
}

static void
create_art_node(struct art_node** node, enum art_node_type type)
{
   struct art_node* n = NULL;
   switch (type)
   {
      case Node4:
      {
         struct art_node4* n4 = malloc(sizeof(struct art_node4));
         memset(n4, 0, sizeof(struct art_node4));
         n4->node.type = Node4;
         n = (struct art_node*)n4;
         break;
      }
      case Node16:
      {
         struct art_node16* n16 = malloc(sizeof(struct art_node16));
         memset(n16, 0, sizeof(struct art_node16));
         n16->node.type = Node16;
         n = (struct art_node*)n16;
         break;
      }
      case Node48:
      {
         struct art_node48* n48 = malloc(sizeof(struct art_node48));
         memset(n48, 0, sizeof(struct art_node48));
         n48->node.type = Node48;
         n = (struct art_node*)n48;
         break;
      }
      case Node256:
      {
         struct art_node256* n256 = malloc(sizeof(struct art_node256));
         memset(n256, 0, sizeof(struct art_node256));
         n256->node.type = Node256;
         n = (struct art_node*)n256;
         break;
      }
   }
   *node = n;
}

static void
create_art_node4(struct art_node4** node)
{
   struct art_node* n = NULL;
   create_art_node(&n, Node4);
   *node = (struct art_node4*)n;
}

static void
create_art_node16(struct art_node16** node)
{
   struct art_node* n = NULL;
   create_art_node(&n, Node16);
   *node = (struct art_node16*)n;
}

static void
create_art_node48(struct art_node48** node)
{
   struct art_node* n = NULL;
   create_art_node(&n, Node48);
   *node = (struct art_node48*)n;
}

static void
create_art_node256(struct art_node256** node)
{
   struct art_node* n = NULL;
   create_art_node(&n, Node256);
   *node = (struct art_node256*)n;
}

static void
destroy_art_node(struct art_node* node)
{
   if (node == NULL)
   {
      return;
   }
   if (IS_LEAF(node))
   {
      pgagroal_value_destroy(GET_LEAF(node)->value);
      free(GET_LEAF(node));
      return;
   }
   switch (node->type)
   {
      case Node4:
      {
         struct art_node4* n = (struct art_node4*)node;
         for (int i = 0; i < node->num_children; i++)
         {
            destroy_art_node(n->children[i]);
         }
         break;
      }
      case Node16:
      {
         struct art_node16* n = (struct art_node16*)node;
         for (int i = 0; i < node->num_children; i++)
         {
            destroy_art_node(n->children[i]);
         }
         break;
      }
      case Node48:
      {
         struct art_node48* n = (struct art_node48*)node;
         for (int i = 0; i < 256; i++)
         {
            int idx = n->keys[i];
            if (idx == 0)
            {
               continue;
            }
            destroy_art_node(n->children[idx - 1]);
         }
         break;
      }

      case Node256:
      {
         struct art_node256* n = (struct art_node256*)node;
         for (int i = 0; i < 256; i++)
         {
            if (n->children[i] == NULL)
            {
               continue;
            }
            destroy_art_node(n->children[i]);
         }
         break;
      }
   }
   free(node);
}

static struct art_node**
node_get_child(struct art_node* node, unsigned char ch)
{
   switch (node->type)
   {
      case Node4:
      {
         struct art_node4* n = (struct art_node4*)node;
         int idx = find_index(ch, n->keys, n->node.num_children);
         if (idx == -1 || n->keys[idx] != ch)
         {
            goto error;
         }
         return &n->children[idx];
      }
      case Node16:
      {
         struct art_node16* n = (struct art_node16*)node;
         int idx = find_index(ch, n->keys, n->node.num_children);
         if (idx == -1 || n->keys[idx] != ch)
         {
            goto error;
         }
         return &n->children[idx];
      }
      case Node48:
      {
         struct art_node48* n = (struct art_node48*)node;
         if (n->keys[ch] == 0)
         {
            goto error;
         }
         return &n->children[n->keys[ch] - 1];
      }
      case Node256:
      {
         struct art_node256* n = (struct art_node256*)node;
         return &n->children[ch];
      }
   }
error:
   return NULL;
}

static struct value*
art_node_insert(struct art_node* node, struct art_node** node_ref, uint32_t depth, unsigned char* key, uint32_t key_len, uintptr_t value, enum value_type type, struct value_config* config, bool* new)
{
   struct art_leaf* leaf = NULL;
   struct art_leaf* min_leaf = NULL;
   uint32_t idx = 0;
   uint32_t diff_len = 0; // where the keys diverge
   struct art_node* new_node = NULL;
   struct art_node** next = NULL;
   unsigned char* leaf_key = NULL;
   void* old_val = NULL;
   if (node == NULL)
   {
      // Lazy expansion, skip creating an inner node since it currently will have only this one leaf.
      // We will compare keys when reach leaf anyway, the path doesn't need to 100% match the key along the way
      create_art_leaf(&leaf, key, key_len, value, type, config);
      *node_ref = SET_LEAF(leaf);
      *new = true;
      return NULL;
   }
   // base case, reaching leaf, either replace or expand
   if (IS_LEAF(node))
   {
      // Lazy expansion, expand the leaf node to an inner node with 2 leaves
      // If the key already exists, replace with new value and return old value
      if (leaf_match(GET_LEAF(node), key, key_len))
      {
         old_val = GET_LEAF(node)->value;
         if (config != NULL)
         {
            pgagroal_value_create_with_config(value, config, &(GET_LEAF(node)->value));
         }
         else
         {
            pgagroal_value_create(type, value, &(GET_LEAF(node)->value));
         }
         return old_val;
      }
      // If the key does not match with existing key, old key and new key diverged some point after depth
      // Even if we merely store a partial prefix for each node, it couldn't have diverged before depth.
      // The reason is that when we find it diverged outside the partial prefix range,
      // we compare with the existing key in the left most leaf and find an exact diverging point to split the node (see details below).
      // This way we inductively guarantee that all children to a parent share the same prefix even if it's only partially stored
      leaf_key = GET_LEAF(node)->key;
      create_art_node(&new_node, Node4);
      create_art_leaf(&leaf, key, key_len, value, type, config);
      // Get the diverging index after point of depth
      for (idx = depth; idx < min(key_len, GET_LEAF(node)->key_len); idx++)
      {
         if (key[idx] != leaf_key[idx])
         {
            break;
         }
         if (idx - depth < MAX_PREFIX_LEN)
         {
            new_node->prefix[idx - depth] = key[idx];
         }
      }
      new_node->prefix_len = idx - depth;
      depth += new_node->prefix_len;
      node_add_child(new_node, &new_node, key[depth], SET_LEAF(leaf));
      node_add_child(new_node, &new_node, leaf_key[depth], (void*)node);
      // replace with new node
      *node_ref = new_node;
      *new = true;
      return NULL;
   }

   // There are several cases,
   // 1. The key diverges outside the current prefix (diff_len >= prefix_len)
   // 2. The key diverges within the current prefix (diff_len < prefix_len)
   //   2.1. The key diverges within the partial prefix range (diff_len < MAX_PREFIX_LEN)
   //   2.2. The key diverges outside the partial prefix range (MAX_PREFIX_LEN <= diff_len < prefix_len)
   // For case 1, go to the next child to add node recursively, or add leaf to current node in place
   // For case 2, split the current node and add child to new node.
   // Note that it's tricky to check case 2.2, or in that case know the exact diverging point,
   // since we merely store the first 10 (MAX_PREFIX_LEN) bytes of the prefix.
   // In this case we use the key in the left most leaf of the node to determine the diverging point.
   // Theoretically we inductively guarantee that all children to the same parent share the same prefixes.
   // So we can use the key inside any leaf under this node to see if the diverging point goes beyond the current prefix,
   // but it's most convenient and efficient to reach the left most key.

   diff_len = check_prefix(node, key, depth, key_len);
   if (diff_len < node->prefix_len)
   {
      // case 2, split the node
      create_art_node(&new_node, Node4);
      create_art_leaf(&leaf, key, key_len, value, type, config);
      new_node->prefix_len = diff_len;
      memcpy(new_node->prefix, node->prefix, min(MAX_PREFIX_LEN, diff_len));
      // We need to know if new bytes that were once outside the partial prefix range will now come into the range
      // If original key didn't fill up the partial prefix buffer in the first place,
      // no new bytes will come into buffer when prefix shifts left
      if (node->prefix_len <= MAX_PREFIX_LEN)
      {
         node->prefix_len = node->prefix_len - (diff_len + 1);
         node_add_child(new_node, &new_node, key[depth + diff_len], SET_LEAF(leaf));
         node_add_child(new_node, &new_node, node->prefix[diff_len], node);
         // Update node's prefix info since we move it downwards
         // The first diverging character serves as the key byte in keys array,
         // so we don't duplicate store it in the prefix.
         // In other words, if prefix is the starting point,
         // prefix + prefix_len - 1 is the last byte of the prefix,
         // prefix + prefix_len is the indexing byte
         // prefix + prefix_len + 1 is the starting point of the next prefix
         memmove(node->prefix, node->prefix + diff_len + 1, node->prefix_len);
      }
      else
      {
         node->prefix_len = node->prefix_len - (diff_len + 1);
         min_leaf = node_get_minimum(node);
         node_add_child(new_node, &new_node, key[depth + diff_len], SET_LEAF(leaf));
         node_add_child(new_node, &new_node, min_leaf->key[depth + diff_len], node);
         // node is moved downwards
         memmove(node->prefix, min_leaf->key + depth + diff_len + 1, min(MAX_PREFIX_LEN, node->prefix_len));
      }
      // replace
      *node_ref = new_node;
      *new = true;
      return NULL;
   }
   else
   {
      // case 1
      depth += node->prefix_len;
      next = node_get_child(node, key[depth]);
      if (next != NULL)
      {
         // recursively add node
         if (*next == NULL)
         {
            node->num_children++;
         }
         return art_node_insert(*next, next, depth + 1, key, key_len, value, type, config, new);
      }
      else
      {
         // add a child to current node since the spot is available
         create_art_leaf(&leaf, key, key_len, value, type, config);
         node_add_child(node, node_ref, key[depth], SET_LEAF(leaf));
         *new = true;
         return NULL;
      }
   }
}

static struct art_leaf*
art_node_delete(struct art_node* node, struct art_node** node_ref, uint32_t depth, unsigned char* key, uint32_t key_len)
{
   struct art_leaf* l = NULL;
   struct art_node** child = NULL;
   uint32_t diff_len = 0;
   if (node == NULL)
   {
      return NULL;
   }
   // Only one way we encounter this case, the tree only has one leaf
   if (IS_LEAF(node))
   {
      if (leaf_match(GET_LEAF(node), key, key_len))
      {
         l = GET_LEAF(node);
         *node_ref = NULL;
         return l;
      }
      return NULL;
   }
   diff_len = check_prefix_partial(node, key, depth, key_len);
   if (diff_len != min(MAX_PREFIX_LEN, node->prefix_len))
   {
      return NULL;
   }
   else
   {
      depth += node->prefix_len;
      if (depth >= key_len)
      {
         return NULL;
      }
      child = node_get_child(node, key[depth]);
      if (child == NULL)
      {
         // dead end
         return NULL;
      }
      if (IS_LEAF(*child))
      {
         if (leaf_match(GET_LEAF(*child), key, key_len))
         {
            l = GET_LEAF(*child);
            node_remove_child(node, node_ref, key[depth]);
            return l;
         }
         else
         {
            return NULL;
         }
      }
      else
      {
         return art_node_delete(*child, child, depth + 1, key, key_len);
      }
   }
}

static int
art_node_iterate(struct art_node* node, art_callback cb, void* data)
{
   struct art_leaf* l = NULL;
   struct art_node* child = NULL;
   int idx = 0;
   int res = 0;
   if (node == NULL)
   {
      return 0;
   }
   if (IS_LEAF(node))
   {
      l = GET_LEAF(node);
      return cb(data, (char*)l->key, l->value);
   }
   switch (node->type)
   {
      case Node4:
      {
         struct art_node4* n = (struct art_node4*)node;
         for (int i = 0; i < node->num_children; i++)
         {
            child = n->children[i];
            res = art_node_iterate(child, cb, data);
            if (res)
            {
               return res;
            }
         }
         break;
      }
      case Node16:
      {
         struct art_node16* n = (struct art_node16*)node;
         for (int i = 0; i < node->num_children; i++)
         {
            child = n->children[i];
            res = art_node_iterate(child, cb, data);
            if (res)
            {
               return res;
            }
         }
         break;
      }
      case Node48:
      {
         struct art_node48* n = (struct art_node48*)node;
         for (int i = 0; i < 256; i++)
         {
            idx = n->keys[i];
            if (idx == 0)
            {
               continue;
            }
            child = n->children[idx - 1];
            res = art_node_iterate(child, cb, data);
            if (res)
            {
               return res;
            }
         }
         break;
      }
      case Node256:
      {
         struct art_node256* n = (struct art_node256*)node;
         for (int i = 0; i < 256; i++)
         {
            if (n->children[i] == NULL)
            {
               continue;
            }
            child = n->children[i];
            res = art_node_iterate(child, cb, data);
            if (res)
            {
               return res;
            }
         }
         break;
      }
   }
   return 0;
}

static void
node_add_child(struct art_node* node, struct art_node** node_ref, unsigned char ch, void* child)
{
   switch (node->type)
   {
      case Node4:
         node4_add_child((struct art_node4*)node, node_ref, ch, child);
         break;
      case Node16:
         node16_add_child((struct art_node16*)node, node_ref, ch, child);
         break;
      case Node48:
         node48_add_child((struct art_node48*)node, node_ref, ch, child);
         break;
      case Node256:
         node256_add_child((struct art_node256*)node, ch, child);
         break;
   }
}

static void
node4_add_child(struct art_node4* node, struct art_node** node_ref, unsigned char ch, void* child)
{
   if (node->node.num_children < 4)
   {
      int idx = find_index(ch, node->keys, node->node.num_children);
      // right shift the right part to make space for the key, so that we keep the keys in order
      memmove(node->keys + (idx + 1) + 1, node->keys + (idx + 1), node->node.num_children - (idx + 1));
      memmove(node->children + (idx + 1) + 1, node->children + (idx + 1), (node->node.num_children - (idx + 1)) * sizeof(void*));

      node->keys[idx + 1] = ch;
      node->children[idx + 1] = (struct art_node*)child;
      node->node.num_children++;
   }
   else
   {
      // expand
      struct art_node16* new_node = NULL;
      create_art_node16(&new_node);
      copy_header((struct art_node*)new_node, (struct art_node*)node);
      memcpy(new_node->children, node->children, node->node.num_children * sizeof(void*));
      memcpy(new_node->keys, node->keys, node->node.num_children);
      // replace the node through node reference
      *node_ref = (struct art_node*)new_node;
      free(node);

      node16_add_child(new_node, node_ref, ch, child);
   }
}

static void
node16_add_child(struct art_node16* node, struct art_node** node_ref, unsigned char ch, void* child)
{
   if (node->node.num_children < 16)
   {
      int idx = find_index(ch, node->keys, node->node.num_children);
      // right shift the right part to make space for the key, so that we keep the keys in order
      memmove(node->keys + (idx + 1) + 1, node->keys + (idx + 1), node->node.num_children - (idx + 1));
      memmove(node->children + (idx + 1) + 1, node->children + (idx + 1), (node->node.num_children - (idx + 1)) * sizeof(void*));

      node->keys[idx + 1] = ch;
      node->children[idx + 1] = (struct art_node*)child;
      node->node.num_children++;
   }
   else
   {
      // expand
      struct art_node48* new_node = NULL;
      create_art_node48(&new_node);
      copy_header((struct art_node*)new_node, (struct art_node*)node);
      memcpy(new_node->children, node->children, node->node.num_children * sizeof(void*));
      for (int i = 0; i < node->node.num_children; i++)
      {
         new_node->keys[node->keys[i]] = i + 1;
      }
      // replace the node through node reference
      *node_ref = (struct art_node*)new_node;
      free(node);
      node48_add_child(new_node, node_ref, ch, child);
   }
}

static void
node48_add_child(struct art_node48* node, struct art_node** node_ref, unsigned char ch, void* child)
{
   if (node->node.num_children < 48)
   {
      // we cannot simply append to last because delete could have caused fragmentation
      int pos = 0;
      while (node->children[pos] != NULL)
      {
         pos++;
      }
      node->children[pos] = (struct art_node*)child;
      node->keys[ch] = pos + 1;
      node->node.num_children++;
   }
   else
   {
      // expand
      struct art_node256* new_node = NULL;
      create_art_node256(&new_node);
      copy_header((struct art_node*)new_node, (struct art_node*)node);
      for (int i = 0; i < 256; i++)
      {
         if (node->keys[i] == 0)
         {
            continue;
         }
         new_node->children[i] = node->children[node->keys[i] - 1];
      }
      // replace the node through node reference
      *node_ref = (struct art_node*)new_node;
      free(node);
      node256_add_child(new_node, ch, child);
   }
}

static void
node256_add_child(struct art_node256* node, unsigned char ch, void* child)
{
   node->node.num_children++;
   node->children[ch] = (struct art_node*)child;
}

static int
find_index(unsigned char ch, const unsigned char* keys, int length)
{
   int left = 0;
   int right = length - 1;
   int mid = 0;
   if (length == 0)
   {
      return -1;
   }
   while (left + 1 < right)
   {
      mid = (left + right) / 2;
      if (keys[mid] == ch)
      {
         return mid;
      }
      if (keys[mid] < ch)
      {
         left = mid;
      }
      else
      {
         right = mid;
      }
   }
   if (keys[right] <= ch)
   {
      return right;
   }
   else if (keys[left] <= ch)
   {
      return left;
   }
   return -1;
}

static void
copy_header(struct art_node* dest, struct art_node* src)
{
   dest->num_children = src->num_children;
   dest->prefix_len = src->prefix_len;
   memcpy(dest->prefix, src->prefix, min(MAX_PREFIX_LEN, src->prefix_len));
}

static uint32_t
check_prefix_partial(struct art_node* node, unsigned char* key, uint32_t depth, uint32_t key_len)
{
   uint32_t len = 0;
   uint32_t max_cmp = min(min(node->prefix_len, MAX_PREFIX_LEN), key_len - depth);
   while (len < max_cmp && key[depth + len] == node->prefix[len])
   {
      len++;
   }
   return len;
}

static uint32_t
check_prefix(struct art_node* node, unsigned char* key, uint32_t depth, uint32_t key_len)
{
   uint32_t len = 0;
   struct art_leaf* leaf = NULL;
   uint32_t max_cmp = min(min(node->prefix_len, MAX_PREFIX_LEN), key_len - depth);
   while (len < max_cmp && key[depth + len] == node->prefix[len])
   {
      len++;
   }
   // diverge within partial prefix range
   if (len < MAX_PREFIX_LEN)
   {
      return len;
   }

   leaf = node_get_minimum(node);
   max_cmp = min(leaf->key_len, key_len) - depth;
   // continue comparing the real keys
   while (len < max_cmp && leaf->key[depth + len] == key[depth + len])
   {
      len++;
   }
   return len;
}

static bool
leaf_match(struct art_leaf* leaf, unsigned char* key, uint32_t key_len)
{
   if (leaf->key_len != key_len)
   {
      return false;
   }
   return memcmp(leaf->key, key, key_len) == 0;
}

static struct art_leaf*
node_get_minimum(struct art_node* node)
{
   if (node == NULL)
   {
      return NULL;
   }
   while (node != NULL && !IS_LEAF(node))
   {
      switch (node->type)
      {
         case Node4:
         {
            struct art_node4* n = (struct art_node4*)node;
            node = n->children[0];
            break;
         }
         case Node16:
         {
            struct art_node16* n = (struct art_node16*)node;
            node = n->children[0];
            break;
         }
         case Node48:
         {
            struct art_node48* n = (struct art_node48*)node;
            int idx = 0;
            while (n->keys[idx] == 0)
            {
               idx++;
            }
            node = n->children[n->keys[idx] - 1];
            break;
         }
         case Node256:
         {
            struct art_node256* n = (struct art_node256*)node;
            int idx = 0;
            while (n->children[idx] == NULL)
            {
               idx++;
            }
            node = n->children[idx];
            break;
         }
      }
   }
   if (node == NULL)
   {
      return NULL;
   }
   return GET_LEAF(node);
}

static void
node_remove_child(struct art_node* node, struct art_node** node_ref, unsigned char ch)
{
   switch (node->type)
   {
      case Node4:
         node4_remove_child((struct art_node4*)node, node_ref, ch);
         break;
      case Node16:
         node16_remove_child((struct art_node16*)node, node_ref, ch);
         break;
      case Node48:
         node48_remove_child((struct art_node48*)node, node_ref, ch);
         break;
      case Node256:
         node256_remove_child((struct art_node256*)node, node_ref, ch);
         break;
   }
}

static void
node4_remove_child(struct art_node4* node, struct art_node** node_ref, unsigned char ch)
{
   int idx = 0;
   uint32_t len = 0;
   struct art_node* child = NULL;
   idx = find_index(ch, node->keys, node->node.num_children);
   memmove(node->keys + idx, node->keys + idx + 1, node->node.num_children - (idx + 1));
   memmove(node->children + idx, node->children + idx + 1, sizeof(void*) * (node->node.num_children - (idx + 1)));
   node->node.num_children--;
   // path compression, merge the node with its child
   if (node->node.num_children == 1)
   {
      child = node->children[0];
      if (IS_LEAF(child))
      {
         // replace directly
         free(node);
         *node_ref = child;
         return;
      }
      // parent prefix bytes + byte index to child + child prefix bytes
      len = node->node.prefix_len;
      if (len < MAX_PREFIX_LEN)
      {
         node->node.prefix[len] = node->keys[0];
         len++;
      }
      // keep filling as much as we can
      for (uint32_t i = 0; len + i < MAX_PREFIX_LEN && i < child->prefix_len; i++)
      {
         node->node.prefix[len + i] = child->prefix[i];
      }
      child->prefix_len = node->node.prefix_len + 1 + child->prefix_len;
      memcpy(child->prefix, node->node.prefix, min(child->prefix_len, MAX_PREFIX_LEN));
      free(node);
      // replace
      *node_ref = child;
   }
}

static void
node16_remove_child(struct art_node16* node, struct art_node** node_ref, unsigned char ch)
{
   int idx = 0;
   struct art_node4* new_node = NULL;
   idx = find_index(ch, node->keys, node->node.num_children);
   memmove(node->keys + idx, node->keys + idx + 1, node->node.num_children - (idx + 1));
   memmove(node->children + idx, node->children + idx + 1, sizeof(void*) * (node->node.num_children - (idx + 1)));
   node->node.num_children--;
   // downgrade node
   // Trick from libart, do not downgrade immediately to avoid jumping on 4/5 boundary
   if (node->node.num_children <= 3)
   {
      create_art_node4(&new_node);
      copy_header((struct art_node*)new_node, (struct art_node*)node);
      memcpy(new_node->keys, node->keys, node->node.num_children);
      memcpy(new_node->children, node->children, node->node.num_children * sizeof(void*));
      free(node);
      *node_ref = (struct art_node*)new_node;
   }
}

static void
node48_remove_child(struct art_node48* node, struct art_node** node_ref, unsigned char ch)
{
   int idx = node->keys[ch];
   int cnt = 0;
   struct art_node16* new_node = NULL;
   node->children[idx - 1] = NULL;
   node->keys[ch] = 0;
   node->node.num_children--;

   if (node->node.num_children <= 12)
   {
      create_art_node16(&new_node);
      copy_header((struct art_node*)new_node, (struct art_node*)node);
      for (int i = 0; i < 256; i++)
      {
         if (node->keys[i] != 0)
         {
            new_node->children[cnt] = node->children[node->keys[i] - 1];
            new_node->keys[cnt] = i;
            cnt++;
         }
      }
      free(node);
      *node_ref = (struct art_node*)new_node;
   }
}

static void
node256_remove_child(struct art_node256* node, struct art_node** node_ref, unsigned char ch)
{
   int num = 0;
   for (int i = 0; i < 48; i++)
   {
      if (node->children[i] != NULL)
      {
         num++;
      }
   }
   if (num != node->node.num_children)
   {
      num++;
   }
   struct art_node48* new_node = NULL;
   int cnt = 0;
   node->children[ch] = NULL;
   node->node.num_children--;

   if (node->node.num_children <= 37)
   {
      create_art_node48(&new_node);
      copy_header((struct art_node*)new_node, (struct art_node*)node);
      for (int i = 0; i < 256; i++)
      {
         if (node->children[i] != NULL)
         {
            new_node->keys[i] = cnt + 1;
            new_node->children[cnt] = node->children[i];
            cnt++;
         }
      }
      free(node);
      *node_ref = (struct art_node*)new_node;
   }
}

int
pgagroal_art_iterator_create(struct art* t, struct art_iterator** iter)
{
   struct art_iterator* i = NULL;
   if (t == NULL)
   {
      return 1;
   }
   i = malloc(sizeof(struct art_iterator));
   i->count = 0;
   i->tree = t;
   i->key = NULL;
   i->value = NULL;
   pgagroal_deque_create(false, &i->que);
   *iter = i;
   return 0;
}

bool
pgagroal_art_iterator_next(struct art_iterator* iter)
{
   struct deque* que = NULL;
   struct art* tree = NULL;
   struct art_node* node = NULL;
   struct art_node* child = NULL;
   int idx = 0;
   if (iter == NULL || iter->que == NULL || iter->tree == NULL || iter->count == iter->tree->size)
   {
      return false;
   }
   que = iter->que;
   tree = iter->tree;
   if (iter->count == 0)
   {
      pgagroal_deque_add(que, NULL, (uintptr_t)tree->root, ValueRef);
   }
   while (!pgagroal_deque_empty(que))
   {
      node = (struct art_node*)pgagroal_deque_poll(que, NULL);
      if (IS_LEAF(node))
      {
         iter->count++;
         iter->key = (char*)GET_LEAF(node)->key;
         iter->value = GET_LEAF(node)->value;
         return true;
      }
      switch (node->type)
      {
         case Node4:
         {
            struct art_node4* n = (struct art_node4*)node;
            for (int i = 0; i < node->num_children; i++)
            {
               child = n->children[i];
               pgagroal_deque_add(que, NULL, (uintptr_t)child, ValueRef);
            }
            break;
         }
         case Node16:
         {
            struct art_node16* n = (struct art_node16*)node;
            for (int i = 0; i < node->num_children; i++)
            {
               child = n->children[i];
               pgagroal_deque_add(que, NULL, (uintptr_t)child, ValueRef);
            }
            break;
         }
         case Node48:
         {
            struct art_node48* n = (struct art_node48*)node;
            for (int i = 0; i < 256; i++)
            {
               idx = n->keys[i];
               if (idx == 0)
               {
                  continue;
               }
               child = n->children[idx - 1];
               pgagroal_deque_add(que, NULL, (uintptr_t)child, ValueRef);
            }
            break;
         }
         case Node256:
         {
            struct art_node256* n = (struct art_node256*)node;
            for (int i = 0; i < 256; i++)
            {
               if (n->children[i] == NULL)
               {
                  continue;
               }
               child = n->children[i];
               pgagroal_deque_add(que, NULL, (uintptr_t)child, ValueRef);
            }
            break;
         }
      }
   }
   return false;
}

bool
pgagroal_art_iterator_has_next(struct art_iterator* iter)
{
   if (iter == NULL || iter->tree == NULL)
   {
      return false;
   }
   return iter->count < iter->tree->size;
}

void
pgagroal_art_iterator_remove(struct art_iterator* iter)
{
   if (iter == NULL || iter->tree == NULL || iter->key == NULL)
   {
      return;
   }

   pgagroal_art_delete(iter->tree, iter->key);
   iter->key = NULL;
   iter->value = NULL;
   iter->count--;
}

void
pgagroal_art_iterator_destroy(struct art_iterator* iter)
{
   if (iter == NULL)
   {
      return;
   }
   pgagroal_deque_destroy(iter->que);
   free(iter);
}

void
pgagroal_art_destroy_value_noop(void* val)
{
   (void)val;
}

void
pgagroal_art_destroy_value_default(void* val)
{
   free(val);
}

static struct value*
art_search(struct art* t, unsigned char* key, uint32_t key_len)
{
   struct art_node* node = NULL;
   struct art_node** child = NULL;
   uint32_t depth = 0;
   if (t == NULL || t->root == NULL)
   {
      return NULL;
   }
   node = t->root;
   while (node != NULL)
   {
      if (IS_LEAF(node))
      {
         if (!leaf_match(GET_LEAF(node), key, key_len))
         {
            return NULL;
         }
         return GET_LEAF(node)->value;
      }
      // optimistically check the prefix,
      // we move forward as long as up to MAX_PREFIX_LEN characters match
      if (check_prefix_partial(node, key, depth, key_len) != min(node->prefix_len, MAX_PREFIX_LEN))
      {
         return NULL;
      }
      depth += node->prefix_len;
      if (depth >= key_len)
      {
         return NULL;
      }
      // you can't dereference what the function returns directly since it could be null
      child = node_get_child(node, key[depth]);
      node = child != NULL ? *child : NULL;
      // child is indexed by key[depth], so the next round we should skip this byte and start checking at the next
      depth++;
   }
   return NULL;
}

static int
art_to_json_string_cb(void* param, const char* key, struct value* value)
{
   struct to_string_param* p = (struct to_string_param*)param;
   char* str = NULL;
   char* tag = NULL;
   char* translated_key = NULL;
   p->cnt++;
   bool has_next = p->cnt < p->t->size;
   tag = pgagroal_append_char(tag, '"');
   translated_key = pgagroal_escape_string((char*)key);
   tag = pgagroal_append(tag, translated_key);
   free(translated_key);
   tag = pgagroal_append_char(tag, '"');
   tag = pgagroal_append(tag, ": ");
   str = pgagroal_value_to_string(value, FORMAT_JSON, tag, p->indent);
   free(tag);
   p->str = pgagroal_append(p->str, str);
   p->str = pgagroal_append(p->str, has_next ? ",\n" : "\n");

   free(str);
   return 0;
}

static int
art_to_compact_json_string_cb(void* param, const char* key, struct value* value)
{
   struct to_string_param* p = (struct to_string_param*)param;
   char* str = NULL;
   char* tag = NULL;
   char* translated_key = NULL;
   p->cnt++;
   bool has_next = p->cnt < p->t->size;
   tag = pgagroal_append_char(tag, '"');
   translated_key = pgagroal_escape_string((char*)key);
   tag = pgagroal_append(tag, (char*)translated_key);
   free(translated_key);
   tag = pgagroal_append_char(tag, '"');
   tag = pgagroal_append(tag, ":");
   str = pgagroal_value_to_string(value, FORMAT_JSON_COMPACT, tag, p->indent);
   free(tag);
   p->str = pgagroal_append(p->str, str);
   p->str = pgagroal_append(p->str, has_next ? "," : "");

   free(str);
   return 0;
}

static int
art_to_text_string_cb(void* param, const char* key, struct value* value)
{
   struct to_string_param* p = (struct to_string_param*)param;
   char* str = NULL;
   char* tag = NULL;
   p->cnt++;
   bool has_next = p->cnt < p->t->size;
   tag = pgagroal_append(tag, (char*)key);
   tag = pgagroal_append(tag, ":");
   if (value->type == ValueJSON && ((struct json*)value->data)->type != JSONUnknown)
   {
      tag = pgagroal_append(tag, "\n");
   }
   else
   {
      tag = pgagroal_append(tag, " ");
   }
   if (pgagroal_compare_string(p->tag, BULLET_POINT))
   {
      if (p->cnt == 1)
      {
         if (value->type != ValueJSON || ((struct json*)value->data)->type == JSONUnknown)
         {
            str = pgagroal_value_to_string(value, FORMAT_TEXT, tag, 0);
         }
         else
         {
            p->str = pgagroal_indent(p->str, tag, 0);
            str = pgagroal_value_to_string(value, FORMAT_TEXT, NULL, p->indent + INDENT_PER_LEVEL);
         }
      }
      else
      {
         str = pgagroal_value_to_string(value, FORMAT_TEXT, tag, p->indent + INDENT_PER_LEVEL);
      }
   }
   else
   {
      str = pgagroal_value_to_string(value, FORMAT_TEXT, tag, p->indent);
   }
   free(tag);
   p->str = pgagroal_append(p->str, str);
   p->str = pgagroal_append(p->str, has_next ? "\n" : "");

   free(str);
   return 0;
}

static char*
to_json_string(struct art* t, char* tag, int indent)
{
   char* ret = NULL;
   ret = pgagroal_indent(ret, tag, indent);
   if (t == NULL || t->size == 0)
   {
      ret = pgagroal_append(ret, "{}");
      return ret;
   }
   ret = pgagroal_append(ret, "{\n");
   struct to_string_param param = {
      .indent = indent + INDENT_PER_LEVEL,
      .str = ret,
      .t = t,
      .cnt = 0,
   };
   art_iterate(t, art_to_json_string_cb, &param);
   ret = param.str;
   ret = pgagroal_indent(ret, NULL, indent);
   ret = pgagroal_append(ret, "}");
   return ret;
}

static char*
to_compact_json_string(struct art* t, char* tag, int indent)
{
   char* ret = NULL;
   ret = pgagroal_indent(ret, tag, indent);
   if (t == NULL || t->size == 0)
   {
      ret = pgagroal_append(ret, "{}");
      return ret;
   }
   ret = pgagroal_append(ret, "{");
   struct to_string_param param = {
      .indent = indent,
      .str = ret,
      .t = t,
      .cnt = 0,
   };
   art_iterate(t, art_to_compact_json_string_cb, &param);
   ret = param.str;
   ret = pgagroal_append(ret, "}");
   return ret;
}

static char*
to_text_string(struct art* t, char* tag, int indent)
{
   char* ret = NULL;
   int next_indent = indent;
   if (tag != NULL && !pgagroal_compare_string(tag, BULLET_POINT))
   {
      ret = pgagroal_indent(ret, tag, indent);
      next_indent += INDENT_PER_LEVEL;
   }
   if (t == NULL || t->size == 0)
   {
      ret = pgagroal_append(ret, "{}");
      return ret;
   }
   struct to_string_param param = {
      .indent = next_indent,
      .str = ret,
      .t = t,
      .cnt = 0,
      .tag = tag};
   art_iterate(t, art_to_text_string_cb, &param);
   ret = param.str;
   return ret;
}

static int
art_iterate(struct art* t, art_callback cb, void* data)
{
   return art_node_iterate(t->root, cb, data);
}
