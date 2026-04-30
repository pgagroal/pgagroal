\newpage

## Core APIs

[**pgagroal**][pgagroal] offers data structures and APIs to help you write safer code and 
enable you to develop more advanced functionalities. Currently, we offer adaptive radix tree (ART),
deque and JSON, which are all based on a universal value type system that help you manage the memory easily.

The document will mostly focus on design decisions, functionalities and things to be cautious about. It may
offer some examples as to how to use the APIs.

### Value
The `value` struct and its APIs are defined and implemented in [value.h][value_h] and [value.c][value_c]. 

The `value` struct wraps the underlying data and manages its memory according to the type users specified.
In some cases the data is stored inline, other times it stores a pointer to the actual memory.
Most of the time the `value` struct is transparent to users. The most common use case would be that user put the data into some
data structure such as a deque. The deque will internally wrap the data into a value object. When user reads the data,
the deque will unwrap the value and return the internal data. An exception here is when you work with iterators,
the iterator will return the value wrapper directly, which tells you the type of the value data. 
This allows you to store different types of value data into one data structure without worrying about losing the type info 
of the data when iterating over the structure.

When you free the deque, deque will automatically free up all the data stored within. In other words, you won't ever need to iterate 
over the deque and free all the stored data manually and explicitly. 

The `value` struct can also print out the wrapped data according to its type. This is convenient for debugging and building
output -- since `deque`, `ART` and `JSON` are also value types, and their internal data are all wrapped in `value`, 
their content can be easily printed out.

**Types**
We support the following value types:

| type            | type enum        | free behavior<br/> (no-op if left blank)                                 |
|:----------------|:-----------------|:------------------------------------------------------------------------|
| `none`          | `ValueNone`      |                                                                         |
| `int8_t`        | `ValueInt8`      |                                                                         |
| `uint8_t`       | `ValueUInt8`     |                                                                         |
| `int16_t`       | `ValueInt16`     |                                                                         |
| `uint16_t`      | `ValueUInt16`    |                                                                         |
| `int32_t`       | `ValueInt32`     |                                                                         |
| `uint32_t`      | `ValueUInt32`    |                                                                         |
| `int64_t`       | `ValueInt64`     |                                                                         |
| `uint64_t`      | `ValueUInt64`    |                                                                         |
| `char`          | `ValueChar`      |                                                                         |
| `bool`          | `ValueBool`      |                                                                         |
| `char*`         | `ValueString`    | `free()`                                                                |
| `char*`         | `ValueStringRef` |                                                                         |
| `float`         | `ValueFloat`     |                                                                         |
| `double`        | `ValueDouble`    |                                                                         |
| `char*`         | `ValueBASE64`    | `free()`                                                                |
| `char*`         | `ValueBASE64Ref` |                                                                         |
| `struct json*`  | `ValueJSON`      | `pgagroal_json_destroy()`, this will recursively destroy internal data  |
| `struct json*`  | `ValueJSONRef`   |                                                                         |
| `struct deque*` | `ValueDeque`     | `pgagroal_deque_destroy()`, this will recursively destroy internal data |
| `struct deque*` | `ValueDequeRef`  |                                                                         |
| `struct art*`   | `ValueART`       | `pgagroal_art_destroy()`, this will recursively destroy internal data   |
| `struct art*`   | `ValueARTRef`    |                                                                         |
| `void*`         | `ValueRef`       |                                                                         |
| `void*`         | `ValueMem`       | `free()`                                                                |

You may have noticed that some types have corresponding `Ref` types. This is especially handy when you try to share
data among multiple data structures -- only one of them should be in charge of freeing up the value. The rest should only
take the corresponding reference type to avoid double free.

There are cases where you try to put a pointer into the core data structure, but it's not any of the predefined types.
In such cases, we offer a few options:

* If you want to free the pointed memory yourself, or it doesn't need to be freed, use `ValueRef`.
* If you just need to invoke a simple `free()`, use `ValueMem`.
* If you need to customize how to destroy the value, we offer you APIs to configure the behavior yourself, which will be illustrated below.

Note that the system does not enforce any kind of borrow checks or lifetime validation. It is still the programmers'
responsibility to use the system correctly and ensure memory safe. But hopefully the system will make the burden a little lighter.

**APIs**

**pgagroal_value_create**

Create a value to wrap your data. Internally the value use a `uintptr_t` to hold your data in place or use it to represent
a pointer, so simply cast your data into `uintptr_t` before passing it into the function (one exception is when you try to
 put in float or double, which requires extra work, see pgagroal_value_from_float/pgagroal_value_from_double for details). 
For `ValueString` or `ValueBASE64`, the value **makes a copy** of your string data. So if your string is malloced on heap, 
you still need to free it since what the value holds is a copy.

```
pgagroal_value_create(ValueString, (uintptr_t)str, &val);
// free the string if it's stored on heap
free(str);
```

**pgagroal_value_create_with_config**
Create a value wrapper with `ValueRef` type and customized destroy and to-string callback. If you want to leave a callback as
default, set the field to NULL.

You normally don't have to create a value yourself, but you will indirectly invoke it when you try to put data into a deque or ART
with a customized configuration.

The callback definition is
```
typedef void (*data_destroy_cb)(uintptr_t data);
typedef char* (*data_to_string_cb)(uintptr_t data, int32_t format, char* tag, int indent);
```

**pgagroal_value_to_string**

This invokes the internal to-string callback and prints out the wrapped data content. You don't usually need to call this function
yourself, as the nested core data structures will invoke this for you on each of its stored value.

For core data structure types, such as `deque`, `ART` or `JSON`, there are multiple supported types of format:
* `FORMAT_JSON`: This prints the wrapped data in JSON format
* `FORMAT_TEXT`: This prints the wrapped data in YAML-like format
* `FORMAT_JSON_COMPACT`: This prints the wrapped data in JSON format, with all whitespaces omitted

Note that the format may also affect primitive types. For example, a string will be enclosed by `"` in JSON format, while in 
TEXT format, it will be printed as-is.

For `ValueMem` and `ValueRef`, the pointer to the memory will be printed. 

**pgagroal_value_data**

Reader function to unwrap the data from the value wrapper. This is especially handy when you fetched the value with wrapper from the iterator.

**pgagroal_value_type**

Reader function to get the type from the value wrapper. The function returns `ValueNone` if the input is NULL.

**pgagroal_value_destroy**

Destroy a value, this invokes the destroy callback to destroy the wrapped data.

**pgagroal_value_to_float/pgagroal_value_to_double**

Use the corresponding function to cast the raw data into the float or double you had wrapped inside the value.

Float and double types are stored in place inside the `uintptr_t` data field. But since C cannot automatically 
cast a `uintptr_t` to float or double correctly, -- it doesn't interpret the bit representation as-is -- we have to 
resort to some union magic to enforce the casting.

**pgagroal_value_from_float/pgagroal_value_from_double**

For the same reason mentioned above, use the corresponding function to cast the float or double you try to 
put inside the value wrapper to raw data.
```
pgagroal_value_create(ValueFloat, pgagroal_value_from_float(float_val), &val);
```

**pgagroal_value_to_ref**

Return the corresponding reference type. Input `ValueJSON` will give you `ValueJSONRef`. For in-place types such as
`ValueInt8`, or if the type is already the reference type, the same type will be returned.

### Deque

The deque is defined and implemented in [deque.h][deque_h] and [deque.c][deque_c].
The deque is built upon the value system, so it can automatically destroy the internal items when it gets destroyed.

You can specify an optional tag for each deque node, so that you can sort of use it as a key-value map. However, since the
introduction of ART and json, this isn't the recommended usage anymore.

**APIs**

**pgagroal_deque_create**

Create a deque. If thread safe is set, a global read/write lock will be acquired before you try to write to deque or read it.
The deque should still be used with cautious even with thread safe enabled -- it does not guard against the value you have read out.
So if you had stored a pointer, deque will not protect the pointed memory from being modified by another thread.

**pgagroal_deque_add**

Add a value to the deque's tail. You need to cast the value to `uintptr_t` since it creates a value wrapper underneath.
Again, for float and double you need to use the corresponding type casting function (`pgagroal_value_from_float` / `pgagroal_value_from_double`).
The function acquires write lock if thread safe is enabled.

The time complexity for adding a node is O(1).

**pgagroal_deque_add_with_config**

Add data with type `ValueRef` and customized to-string/destroy callback into the deque. The function acquires write lock
if thread safe is enabled.
```
static void
rfile_destroy_cb(uintptr_t data)
{
   rfile_destroy((struct rfile*) data);
}

static void
add_rfile()
{
struct deque* sources;
struct rfile* latest_source;
struct value_config rfile_config = {.destroy_data = rfile_destroy_cb, .to_string = NULL};
...

pgagroal_deque_add_with_config(sources, NULL, (uintptr_t)latest_source, &rfile_config);
}
```

**pgagroal_deque_poll**

Retrieve value and remove the node from the deque's head. If the node has tag, you can optionally read it out. The function
transfers value ownership, so you will be responsible to free the value if it was copied into the node when you put it in. 
The function acquires read lock if thread safe is enabled.

The time complexity for polling a node is O(1).
```
pgagroal_deque_add(deque, "Hello", (uintptr_t)"world", ValueString);
char* tag = NULL;
char* value = (char*)pgagroal_deque_poll(deque, &tag);

printf("%s, %s!\n", tag, value) // "Hello, world!"

// remember to free them!
free(tag);
free(value);
```

```
// if you don't care about tag
pgagroal_deque_add(deque, "Hello", (uintptr_t)"world", ValueString);
char* value = (char*)pgagroal_deque_poll(deque, NULL);

printf("%s!\n", value) // "world!"

// remember to free it!
free(value);
```

**pgagroal_deque_poll_last**

Retrieve value and remove the node from the deque's tail. If the node has tag, you can optionally read it out. The function
transfers value ownership, so you will be responsible to free the value if it was copied into the node when you put it in. 
The function acquires read lock if thread safe is enabled.

The time complexity for polling a node is O(1).
```
pgagroal_deque_add(deque, "Hello", (uintptr_t)"world", ValueString);
char* tag = NULL;
char* value = (char*)pgagroal_deque_poll_last(deque, &tag);

printf("%s, %s!\n", tag, value) // "Hello, world!"

// remember to free them!
free(tag);
free(value);
```

```
// if you don't care about tag
pgagroal_deque_add(deque, "Hello", (uintptr_t)"world", ValueString);
char* value = (char*)pgagroal_deque_poll_last(deque, NULL);

printf("%s!\n", value) // "world!"

// remember to free it!
free(value);
```

**pgagroal_deque_peek**

Retrieve value without removing the node from deque's head. The function acquires read lock 
if thread safe is enabled.

The time complexity for peeking a node is O(1).

**pgagroal_deque_peek_last**

Retrieve value without removing the node from deque's tail. The function acquires read lock 
if thread safe is enabled.

The time complexity for peeking a node is O(1).

**pgagroal_deque_iterator_create**

Create a deque iterator, note that iterator is **NOT** thread safe

**pgagroal_deque_iterator_destroy**

Destroy a deque iterator

**pgagroal_deque_iterator_next**

Advance the iterator to the next value. You will need to call it before reading the first item.
The function is a no-op if it reaches the end and will return false.

**pgagroal_deque_iterator_has_next**

Check if iterator has next value without advancing it.

**pgagroal_deque_iterator_remove**

Remove the current node the iterator is pointing to. Then the iterator will fall back to the previous node.

For example, for a deque `a -> b -> c`, after removing node `b`, iterator will point to `a`, 
then calling `pgagroal_deque_iterator_next` will advance the iterator to `c`. If node `a` is removed instead,
iterator will point to the internal dummy head node.

```
// remove nodes without a tag
pgagroal_deque_iterator_create(deque, &iter);
while (pgagroal_deque_iterator_next(iter)) {
    if (iter->tag == NULL) {
        pgagroal_deque_iterator_remove(iter);
    }
    else {
        printf("%s: %s\n", iter->tag, (char*)pgagroal_value_data(iter->value));
    }
}
pgagroal_deque_iterator_destroy(iter);
```

**pgagroal_deque_size**
Get the current deque size, the function acquires the read lock

**pgagroal_deque_empty**

Check if the deque is empty

**pgagroal_deque_to_string**

Convert the deque to string of the specified format.

**pgagroal_deque_list**

Log the deque content in logs. This only works in TRACE log level.

**pgagroal_deque_sort**

Merge sort the deque. The time complexity is O(log(n)).

**pgagroal_deque_get**

Get the data with a specific tag from the deque.

The time complexity for getting a node is O(n).

**pgagroal_deque_exists**

Check if a tag exists in deque.

**pgagroal_deque_remove**

Remove all the nodes in the deque that have the given tag.

**pgagroal_deque_clear**

Remove all the nodes in the deque.

**pgagroal_deque_set_thread_safe**

Set the deque to be thread safe.


### Adaptive Radix Tree (ART)

ART shares similar ideas as trie. But it is very space efficient by adopting techniques such as
adaptive node size, path compression and lazy expansion. The time complexity of inserting, deleting or searching a key
in an ART is always O(k) where the k is the length of the key. And since most of the time our key type is string, ART can
be used as **an ideal key-value map** with much less space overhead than hashmap.

ART is defined and implemented in [art.h][art_h] and [art.c][art_c].

**APIs**

**pgagroal_art_create**

Create an adaptive radix tree

**pgagroal_art_insert**

Insert a key value pair into the ART. Likewise, the ART tree wraps the data in value internally. So you need to cast the value to
`uintptr_t`. If the key already exists, the previous value will be destroyed and replaced by the new value.

**pgagroal_art_insert_with_config**

Insert a key value pair with a customized configuration. The idea and usage is identical to `pgagroal_deque_add_with_config`.

**pgagroal_art_contains_key**

Check if a key exists in ART.

**pgagroal_art_search**

Search a value inside the ART by its key. The ART unwraps the value and return the raw data. If key is not found, it returns 0.
So if you need to tell whether it returns a zero value or the key does not exist, use `pgagroal_art_contains_key`.

**pgagroal_art_search_typed**

Search a value inside the ART by its key. The ART unwraps the value and return the raw data. 
It also returns the value type through the output `type` parameter.
If key is not found, it returns 0, and the type is set to `ValueNone`. So you can also use it to tell if a value exists.

**pgagroal_art_delete**

Delete a key from ART. Note that the function returns success(i.e. 0) even if the key does not exist.

**pgagroal_art_clear**

Removes all the key value pairs in the ART tree.

**pgagroal_art_to_string**

Convert an ART to string. The function uses an internal iterator function which iterates the tree using DFS.
So unlike the iterator, this traverses and prints out keys by lexicographical order.

**pgagroal_art_destroy**

Destroy an ART.

**pgagroal_art_iterator_create**

Create an ART iterator, the iterator iterates the tree using BFS, which means it won't traverse the keys by lexicographical order.

**pgagroal_art_iterator_destroy**

Destroy an ART iterator. This will recursively destroy all of its key value entries.

**pgagroal_art_iterator_remove**

Remove the key value pair the iterator points to. Note that currently the function just invokes `pgagroal_art_delete()` 
with the current key. Since there's no rebalance mechanism in ART, it shouldn't affect the subsequent iteration. But still
use with caution, as this is not thoroughly tested.

**pgagroal_art_iterator_next**

Advance an ART iterator. You need to call this function before inspecting the first entry.
If there are no more entries, the function is a no-op and will return false.

**pgagroal_art_iterator_has_next**

Check if the iterator has next value without advancing it.

```
pgagroal_art_iterator_create(t, &iter);
while (pgagroal_art_iterator_next(iter)) {
    printf("%s: %s\n", iter->key, (char*)pgagroal_value_data(iter->value));
}
pgagroal_art_iterator_destroy(iter);
```

### JSON

JSON is essentially built upon deque and ART. Find its definition and implementation in 
[json.h][json_h] and [json.c][json_c].

**APIs**

**pgagroal_json_create**

Create a JSON object. Note that the json could be an array (`JSONArray`) or key value pairs (`JSONItem`). We don't specify the JSON type
on creation. The json object will decide by itself based on the subsequent API invocation. 

**pgagroal_json_destroy**

Destroy a JSON object

**pgagroal_json_put**

Put a key value pair into the json object. This function invokes `pgagroal_art_insert` underneath so it will override
the old value if key already exists. Also when invoked for the first time, the function sets the JSON object to `JSONItem`, 
which will reject `pgagroal_json_append` from then on. Note that unlike ART, JSON only takes certain types of value. See [JSON introduction](https://www.json.org/json-en.html)
for details.

**pgagroal_json_append**

Append a value entry to the json object. When invoked for the first time, the function sets the JSON object to `JSONArray`, which will
reject `pgagroal_json_put` from then on.

**pgagroal_json_remove**

Remove a key and destroy the associated value within the json item. If the key does not exist or the json object 
is an array, the function will be no-op. If the JSON item becomes empty after removal, it will fall back to undefined status,
and you can turn it into an array by appending entries to it.

**pgagroal_json_clear**

For `JSONArray`, the function removes all entries. For `JSONItem`, the function removes all key value pairs.
The JSON object will fall back to undefined status.

**pgagroal_json_get**

Get and unwrap the value data from a JSON item. If the JSON object is an array, the function returns 0.

**pgagroal_json_get_typed**

Get and unwrap the value data from a JSON item, also returns the value type through output `type` parameter.
If the JSON object is an array, the function returns 0. If the key is not found, the function sets `type` to `ValueNone`.
So you can also use it to check if a key exists.

**pgagroal_json_contains_key**

Check if the JSON item contains a specific key. It always returns false if the object is an array.

**pgagroal_json_array_length**

Get the length of a JSON array

**pgagroal_json_iterator_create**

Create a JSON iterator. For JSON array, it creates an internal deque iterator. For JSON item,
it creates an internal ART iterator. You can read the value or the array entry from `value` field. And the `key`
field is ignored when the object is an array.

**pgagroal_json_iterator_next**

Advance to the next entry or key value pairs. You need to call this before accessing the first entry or kv pair.

**pgagroal_json_iterator_has_next**

Check if the object has the next entry or key value pair.

**pgagroal_json_iterator_destroy**

Destroy the JSON iterator.

**pgagroal_json_parse_string**

Parse a JSON string into a JSON object.

**pgagroal_json_clone**

Clone a JSON object. This works by converting the object to string and parse it
back to another object. So the value type could be a little different. For example,
an `int8` value will be parsed into an `int64` value.

**pgagroal_json_to_string**

Convert the JSON object to string.

**pgagroal_json_print**

A convenient wrapper to quickly print out the JSON object.

**pgagroal_json_read_file**

Read the JSON file and parse it into the JSON object.

**pgagroal_json_write_file**

Convert the JSON to string and write it to a JSON file.
