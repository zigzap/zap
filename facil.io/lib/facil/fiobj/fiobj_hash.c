/*
Copyright: Boaz Segev, 2017-2019
License: MIT
*/

#include <fiobject.h>

#include <assert.h>
#include <fiobj_hash.h>

#define FIO_SET_CALLOC(size, count) fio_calloc((size), (count))
#define FIO_SET_REALLOC(ptr, original_size, size, valid_data_length)           \
  fio_realloc2((ptr), (size), (valid_data_length))
#define FIO_SET_FREE(ptr, size) fio_free((ptr))

#define FIO_SET_NAME fio_hash__
#define FIO_SET_KEY_TYPE FIOBJ
#define FIO_SET_KEY_COMPARE(o1, o2)                                            \
  ((o2) == ((FIOBJ)-1) || (o1) == ((FIOBJ)-1) || fiobj_iseq((o1), (o2)))
#define FIO_SET_KEY_COPY(dest, obj) ((dest) = fiobj_dup((obj)))
#define FIO_SET_KEY_DESTROY(obj)                                               \
  do {                                                                         \
    fiobj_free((obj));                                                         \
    (obj) = FIOBJ_INVALID;                                                     \
  } while (0)
#define FIO_SET_OBJ_TYPE FIOBJ
#define FIO_SET_OBJ_COMPARE(o1, o2) fiobj_iseq((o1), (o2))
#define FIO_SET_OBJ_COPY(dest, obj) ((dest) = fiobj_dup(obj))
#define FIO_SET_OBJ_DESTROY(obj)                                               \
  do {                                                                         \
    fiobj_free((obj));                                                         \
    (obj) = FIOBJ_INVALID;                                                     \
  } while (0)

#include <fio.h>

#include <errno.h>

/* *****************************************************************************
Hash types
***************************************************************************** */
typedef struct {
  fiobj_object_header_s head;
  fio_hash___s hash;
} fiobj_hash_s;

#define obj2hash(o) ((fiobj_hash_s *)(FIOBJ2PTR(o)))

void fiobj_hash_rehash(FIOBJ h) {
  assert(h && FIOBJ_TYPE_IS(h, FIOBJ_T_HASH));
  fio_hash___rehash(&obj2hash(h)->hash);
}

/* *****************************************************************************
Hash alloc + VTable
***************************************************************************** */

static void fiobj_hash_dealloc(FIOBJ o, void (*task)(FIOBJ, void *),
                               void *arg) {
  FIO_SET_FOR_LOOP(&obj2hash(o)->hash, i) {
    if (i->obj.key)
      task((FIOBJ)i->obj.obj, arg);
    fiobj_free((FIOBJ)i->obj.key);
    i->obj.key = FIOBJ_INVALID;
    i->obj.obj = FIOBJ_INVALID;
  }
  obj2hash(o)->hash.count = 0;
  fio_hash___free(&obj2hash(o)->hash);
  fio_free(FIOBJ2PTR(o));
}

static __thread FIOBJ each_at_key = FIOBJ_INVALID;

static size_t fiobj_hash_each1(FIOBJ o, size_t start_at,
                               int (*task)(FIOBJ obj, void *arg), void *arg) {
  assert(o && FIOBJ_TYPE_IS(o, FIOBJ_T_HASH));
  FIOBJ old_each_at_key = each_at_key;
  fio_hash___s *hash = &obj2hash(o)->hash;
  size_t count = 0;
  if (hash->count == hash->pos) {
    /* no holes in the hash, we can work as we please. */
    for (count = start_at; count < hash->count; ++count) {
      each_at_key = hash->ordered[count].obj.key;
      if (task((FIOBJ)hash->ordered[count].obj.obj, arg) == -1) {
        ++count;
        goto end;
      }
    }
  } else {
    size_t pos = 0;
    for (; pos < start_at && pos < hash->pos; ++pos) {
      /* counting */
      if (hash->ordered[pos].obj.key == FIOBJ_INVALID)
        ++start_at;
      else
        ++count;
    }
    for (; pos < hash->pos; ++pos) {
      /* performing */
      if (hash->ordered[pos].obj.key == FIOBJ_INVALID)
        continue;
      ++count;
      each_at_key = hash->ordered[pos].obj.key;
      if (task((FIOBJ)hash->ordered[pos].obj.obj, arg) == -1)
        break;
    }
  }
end:
  each_at_key = old_each_at_key;
  return count;
}

FIOBJ fiobj_hash_key_in_loop(void) { return each_at_key; }

static size_t fiobj_hash_is_eq(const FIOBJ self, const FIOBJ other) {
  if (fio_hash___count(&obj2hash(self)->hash) !=
      fio_hash___count(&obj2hash(other)->hash))
    return 0;
  return 1;
}

/** Returns the number of elements in the Array. */
size_t fiobj_hash_count(const FIOBJ o) {
  assert(o && FIOBJ_TYPE_IS(o, FIOBJ_T_HASH));
  return fio_hash___count(&obj2hash(o)->hash);
}

intptr_t fiobj_hash2num(const FIOBJ o) { return (intptr_t)fiobj_hash_count(o); }

static size_t fiobj_hash_is_true(const FIOBJ o) {
  return fiobj_hash_count(o) != 0;
}

fio_str_info_s fiobject___noop_to_str(const FIOBJ o);
intptr_t fiobject___noop_to_i(const FIOBJ o);
double fiobject___noop_to_f(const FIOBJ o);

const fiobj_object_vtable_s FIOBJECT_VTABLE_HASH = {
    .class_name = "Hash",
    .dealloc = fiobj_hash_dealloc,
    .is_eq = fiobj_hash_is_eq,
    .count = fiobj_hash_count,
    .each = fiobj_hash_each1,
    .is_true = fiobj_hash_is_true,
    .to_str = fiobject___noop_to_str,
    .to_i = fiobj_hash2num,
    .to_f = fiobject___noop_to_f,
};

/* *****************************************************************************
Hash API
***************************************************************************** */

/**
 * Creates a mutable empty Hash object. Use `fiobj_free` when done.
 *
 * Notice that these Hash objects are designed for smaller collections and
 * retain order of object insertion.
 */
FIOBJ fiobj_hash_new(void) {
  fiobj_hash_s *h = fio_malloc(sizeof(*h));
  FIO_ASSERT_ALLOC(h);
  *h = (fiobj_hash_s){.head = {.ref = 1, .type = FIOBJ_T_HASH},
                      .hash = FIO_SET_INIT};
  return (FIOBJ)h | FIOBJECT_HASH_FLAG;
}

/**
 * Creates a mutable empty Hash object with an initial capacity of `capa`. Use
 * `fiobj_free` when done.
 *
 * Notice that these Hash objects are designed for smaller collections and
 * retain order of object insertion.
 */
FIOBJ fiobj_hash_new2(size_t capa) {
  fiobj_hash_s *h = fio_malloc(sizeof(*h));
  FIO_ASSERT_ALLOC(h);
  *h = (fiobj_hash_s){.head = {.ref = 1, .type = FIOBJ_T_HASH},
                      .hash = FIO_SET_INIT};
  fio_hash___capa_require(&h->hash, capa);
  return (FIOBJ)h | FIOBJECT_HASH_FLAG;
}

/**
 * Returns a temporary theoretical Hash map capacity.
 * This could be used for testing performance and memory consumption.
 */
size_t fiobj_hash_capa(const FIOBJ hash) {
  assert(hash && FIOBJ_TYPE_IS(hash, FIOBJ_T_HASH));
  return fio_hash___capa(&obj2hash(hash)->hash);
}

/**
 * Sets a key-value pair in the Hash, duplicating the Symbol and **moving**
 * the ownership of the object to the Hash.
 *
 * Returns -1 on error.
 */
int fiobj_hash_set(FIOBJ hash, FIOBJ key, FIOBJ obj) {
  assert(hash && FIOBJ_TYPE_IS(hash, FIOBJ_T_HASH));
  if (FIOBJ_TYPE_IS(key, FIOBJ_T_STRING))
    fiobj_str_freeze(key);
  fio_hash___insert(&obj2hash(hash)->hash, fiobj_obj2hash(key), key, obj, NULL);
  fiobj_free(obj); /* take ownership - free the user's reference. */
  return 0;
}

/**
 * Allows the Hash to be used as a stack.
 *
 * If a pointer `key` is provided, it will receive ownership of the key
 * (remember to free).
 *
 * Returns FIOBJ_INVALID on error.
 *
 * Returns and object if successful (remember to free).
 */
FIOBJ fiobj_hash_pop(FIOBJ hash, FIOBJ *key) {
  assert(hash && FIOBJ_TYPE_IS(hash, FIOBJ_T_HASH));
  FIOBJ old;
  if (fio_hash___count(&obj2hash(hash)->hash))
    return FIOBJ_INVALID;
  old = fiobj_dup(fio_hash___last(&obj2hash(hash)->hash).obj);
  if (key)
    *key = fiobj_dup(fio_hash___last(&obj2hash(hash)->hash).key);
  fio_hash___pop(&obj2hash(hash)->hash);
  return old;
}

/**
 * Replaces the value in a key-value pair, returning the old value (and it's
 * ownership) to the caller.
 *
 * A return value of NULL indicates that no previous object existed (but a new
 * key-value pair was created.
 *
 * Errors are silently ignored.
 */
FIOBJ fiobj_hash_replace(FIOBJ hash, FIOBJ key, FIOBJ obj) {
  assert(hash && FIOBJ_TYPE_IS(hash, FIOBJ_T_HASH));
  FIOBJ old = FIOBJ_INVALID;
  fio_hash___insert(&obj2hash(hash)->hash, fiobj_obj2hash(key), key, obj, &old);
  fiobj_free(obj); /* take ownership - free the user's reference. */
  return old;
}

/**
 * Removes a key-value pair from the Hash, if it exists.
 */
FIOBJ fiobj_hash_remove(FIOBJ hash, FIOBJ key) {
  assert(hash && FIOBJ_TYPE_IS(hash, FIOBJ_T_HASH));
  FIOBJ old = FIOBJ_INVALID;
  fio_hash___remove(&obj2hash(hash)->hash, fiobj_obj2hash(key), key, &old);
  return old;
}

/**
 * Removes a key-value pair from the Hash, if it exists, returning the old
 * object (instead of freeing it).
 */
FIOBJ fiobj_hash_remove2(FIOBJ hash, uint64_t hash_value) {
  assert(hash && FIOBJ_TYPE_IS(hash, FIOBJ_T_HASH));
  FIOBJ old = FIOBJ_INVALID;
  fio_hash___remove(&obj2hash(hash)->hash, hash_value, -1, &old);
  return old;
}

/**
 * Deletes a key-value pair from the Hash, if it exists, freeing the
 * associated object.
 *
 * Returns -1 on type error or if the object never existed.
 */
int fiobj_hash_delete(FIOBJ hash, FIOBJ key) {
  return fio_hash___remove(&obj2hash(hash)->hash, fiobj_obj2hash(key), key,
                           NULL);
}

/**
 * Deletes a key-value pair from the Hash, if it exists, freeing the
 * associated object.
 *
 * This function takes a `uintptr_t` Hash value (see `fio_siphash`) to
 * perform a lookup in the HashMap, which is slightly faster than the other
 * variations.
 *
 * Returns -1 on type error or if the object never existed.
 */
int fiobj_hash_delete2(FIOBJ hash, uint64_t key_hash) {
  return fio_hash___remove(&obj2hash(hash)->hash, key_hash, -1, NULL);
}

/**
 * Returns a temporary handle to the object associated with the Symbol, NULL
 * if none.
 */
FIOBJ fiobj_hash_get(const FIOBJ hash, FIOBJ key) {
  assert(hash && FIOBJ_TYPE_IS(hash, FIOBJ_T_HASH));
  return fio_hash___find(&obj2hash(hash)->hash, fiobj_obj2hash(key), key);
  ;
}

/**
 * Returns a temporary handle to the object associated hashed key value.
 *
 * This function takes a `uintptr_t` Hash value (see `fio_siphash`) to
 * perform a lookup in the HashMap.
 *
 * Returns NULL if no object is associated with this hashed key value.
 */
FIOBJ fiobj_hash_get2(const FIOBJ hash, uint64_t key_hash) {
  assert(hash && FIOBJ_TYPE_IS(hash, FIOBJ_T_HASH));
  return fio_hash___find(&obj2hash(hash)->hash, key_hash, -1);
  ;
}

/**
 * Returns 1 if the key (Symbol) exists in the Hash, even if value is NULL.
 */
int fiobj_hash_haskey(const FIOBJ hash, FIOBJ key) {
  assert(hash && FIOBJ_TYPE_IS(hash, FIOBJ_T_HASH));
  return fio_hash___find(&obj2hash(hash)->hash, fiobj_obj2hash(key), key) !=
         FIOBJ_INVALID;
}

/**
 * Empties the Hash.
 */
void fiobj_hash_clear(const FIOBJ hash) {
  assert(hash && FIOBJ_TYPE_IS(hash, FIOBJ_T_HASH));
  fio_hash___free(&obj2hash(hash)->hash);
}

/* *****************************************************************************
Simple Tests
***************************************************************************** */

#if DEBUG
void fiobj_test_hash(void) {
  fprintf(stderr, "=== Testing Hash\n");
#define TEST_ASSERT(cond, ...)                                                 \
  if (!(cond)) {                                                               \
    fprintf(stderr, "* " __VA_ARGS__);                                         \
    fprintf(stderr, "Testing failed.\n");                                      \
    exit(-1);                                                                  \
  }
  FIOBJ o = fiobj_hash_new();
  FIOBJ str_key = fiobj_str_new("Hello World!", 12);
  TEST_ASSERT(FIOBJ_TYPE_IS(o, FIOBJ_T_HASH), "Type identification error!\n");
  TEST_ASSERT(fiobj_hash_count(o) == 0, "Hash should be empty!\n");
  fiobj_hash_set(o, str_key, fiobj_true());
  TEST_ASSERT(fiobj_str_write(str_key, "should fail...", 13) == 0,
              "wrote to frozen string?");
  TEST_ASSERT(fiobj_obj2cstr(str_key).len == 12,
              "String was mutated (not frozen)!\n");
  TEST_ASSERT(fiobj_hash_get(o, str_key) == fiobj_true(),
              "full compare didn't get value back");
  TEST_ASSERT(fiobj_hash_get2(o, fiobj_obj2hash(str_key)) == fiobj_true(),
              "hash compare didn't get value back");

  FIOBJ o2 = fiobj_hash_new2(3);
  TEST_ASSERT(obj2hash(o2)->hash.capa >= 3,
              "Hash capacity should be larger than 3! %zu != 4\n",
              (size_t)obj2hash(o2)->hash.capa);
  fiobj_hash_set(o2, str_key, fiobj_true());
  TEST_ASSERT(fiobj_hash_is_eq(o, o2), "Hashes not equal at core! %zu != %zu\n",
              fiobj_hash_count(o), fiobj_hash_count(o2));
  TEST_ASSERT(fiobj_iseq(o, o2), "Hashes not equal!\n");
  TEST_ASSERT(obj2hash(o2)->hash.capa > 3,
              "Hash capacity should be larger than 3! %zu != 4\n",
              (size_t)obj2hash(o2)->hash.capa);

  fiobj_hash_delete(o, str_key);

  TEST_ASSERT(fiobj_hash_get2(o, fiobj_obj2hash(str_key)) == 0,
              "item wasn't deleted!");
  fiobj_free(
      str_key); /* note that a copy will remain in the Hash until rehashing. */
  fiobj_free(o);
  fiobj_free(o2);
  fprintf(stderr, "* passed.\n");
}
#endif
