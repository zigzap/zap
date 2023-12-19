/*
Copyright: Boaz Segev, 2017-2019
License: MIT
*/
#ifndef H_FIOBJ_HASH_H
/**
 * The facil.io Hash object is an ordered Hash Table implementation.
 *
 * By compromising some of the HashMap's collision resistance (comparing only
 * the Hash values rather than comparing key data), memory comparison can be
 * avoided and performance increased.
 *
 * By being ordered it's possible to iterate over key-value pairs in the order
 * in which they were added to the Hash table, making it possible to output JSON
 * in a controlled manner.
 */
#define H_FIOBJ_HASH_H

#include <fiobject.h>

#include <fio_siphash.h>
#include <fiobj_str.h>

#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

/* MUST be a power of 2 */
#define HASH_INITIAL_CAPACITY 16

/** attempts to rehash the hashmap. */
void fiobj_hash_rehash(FIOBJ h);

/* *****************************************************************************
Hash Creation
***************************************************************************** */

/**
 * Creates a mutable empty Hash object. Use `fiobj_free` when done.
 *
 * Notice that these Hash objects are optimized for smaller collections and
 * retain order of object insertion.
 */
FIOBJ fiobj_hash_new(void);

/**
 * Creates a mutable empty Hash object with an initial capacity of `capa`. Use
 * `fiobj_free` when done.
 *
 * This allows optimizations for larger (or smaller) collections.
 */
FIOBJ fiobj_hash_new2(size_t capa);

/* *****************************************************************************
Hash properties and state
***************************************************************************** */

/**
 * Returns a temporary theoretical Hash map capacity.
 * This could be used for testing performance and memory consumption.
 */
size_t fiobj_hash_capa(const FIOBJ hash);

/** Returns the number of elements in the Hash. */
size_t fiobj_hash_count(const FIOBJ hash);

/** Returns the key for the object in the current `fiobj_each` loop (if any). */
FIOBJ fiobj_hash_key_in_loop(void);

/* *****************************************************************************
Populating the Hash
***************************************************************************** */

/**
 * Sets a key-value pair in the Hash, duplicating the Symbol and **moving**
 * the ownership of the object to the Hash.
 *
 * Returns -1 on error.
 */
int fiobj_hash_set(FIOBJ hash, FIOBJ key, FIOBJ obj);

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
FIOBJ fiobj_hash_pop(FIOBJ hash, FIOBJ *key);

/**
 * Replaces the value in a key-value pair, returning the old value (and it's
 * ownership) to the caller.
 *
 * A return value of FIOBJ_INVALID indicates that no previous object existed
 * (but a new key-value pair was created.
 *
 * Errors are silently ignored.
 *
 * Remember to free the returned object.
 */
FIOBJ fiobj_hash_replace(FIOBJ hash, FIOBJ key, FIOBJ obj);

/**
 * Removes a key-value pair from the Hash, if it exists, returning the old
 * object (instead of freeing it).
 */
FIOBJ fiobj_hash_remove(FIOBJ hash, FIOBJ key);

/**
 * Removes a key-value pair from the Hash, if it exists, returning the old
 * object (instead of freeing it).
 */
FIOBJ fiobj_hash_remove2(FIOBJ hash, uint64_t key_hash);

/**
 * Deletes a key-value pair from the Hash, if it exists, freeing the
 * associated object.
 *
 * Returns -1 on type error or if the object never existed.
 */
int fiobj_hash_delete(FIOBJ hash, FIOBJ key);

/**
 * Deletes a key-value pair from the Hash, if it exists, freeing the
 * associated object.
 *
 * This function takes a `uint64_t` Hash value (see `fio_siphash`) to
 * perform a lookup in the HashMap, which is slightly faster than the other
 * variations.
 *
 * Returns -1 on type error or if the object never existed.
 */
int fiobj_hash_delete2(FIOBJ hash, uint64_t key_hash);

/**
 * Returns a temporary handle to the object associated with the Symbol,
 * FIOBJ_INVALID if none.
 */
FIOBJ fiobj_hash_get(const FIOBJ hash, FIOBJ key);

/**
 * Returns a temporary handle to the object associated hashed key value.
 *
 * This function takes a `uint64_t` Hash value (see `fio_siphash`) to
 * perform a lookup in the HashMap, which is slightly faster than the other
 * variations.
 *
 * Returns FIOBJ_INVALID if no object is associated with this hashed key value.
 */
FIOBJ fiobj_hash_get2(const FIOBJ hash, uint64_t key_hash);

/**
 * Returns 1 if the key (Symbol) exists in the Hash, even if it's value is NULL.
 */
int fiobj_hash_haskey(const FIOBJ hash, FIOBJ key);

/**
 * Empties the Hash.
 */
void fiobj_hash_clear(const FIOBJ hash);

#if DEBUG
void fiobj_test_hash(void);
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
