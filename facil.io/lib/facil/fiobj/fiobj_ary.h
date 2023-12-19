#ifndef FIOBJ_ARRAY_H
/*
Copyright: Boaz Segev, 2017-2019
License: MIT
*/

/**
A dynamic Array type for the fiobj_s dynamic type system.
*/
#define FIOBJ_ARRAY_H

#include <fiobject.h>

#ifdef __cplusplus
extern "C" {
#endif

/* *****************************************************************************
Array creation API
***************************************************************************** */

/** Creates a mutable empty Array object. Use `fiobj_free` when done. */
FIOBJ fiobj_ary_new(void);

/** Creates a mutable empty Array object with the requested capacity. */
FIOBJ fiobj_ary_new2(size_t capa);

/* *****************************************************************************
Array direct entry access API
***************************************************************************** */

/** Returns the number of elements in the Array. */
size_t fiobj_ary_count(FIOBJ ary);

/** Returns the current, temporary, array capacity (it's dynamic). */
size_t fiobj_ary_capa(FIOBJ ary);

/**
 * Returns a TEMPORARY pointer to the beginning of the array.
 *
 * This pointer can be used for sorting and other direct access operations as
 * long as no other actions (insertion/deletion) are performed on the array.
 */
FIOBJ *fiobj_ary2ptr(FIOBJ ary);

/**
 * Returns a temporary object owned by the Array.
 *
 * Wrap this function call within `fiobj_dup` to get a persistent handle. i.e.:
 *
 *     fiobj_dup(fiobj_ary_index(array, 0));
 *
 * Negative values are retrieved from the end of the array. i.e., `-1`
 * is the last item.
 */
FIOBJ fiobj_ary_index(FIOBJ ary, int64_t pos);
/** alias for `fiobj_ary_index` */
#define fiobj_ary_entry(a, p) fiobj_ary_index((a), (p))

/**
 * Sets an object at the requested position.
 */
void fiobj_ary_set(FIOBJ ary, FIOBJ obj, int64_t pos);

/* *****************************************************************************
Array push / shift API
***************************************************************************** */

/**
 * Pushes an object to the end of the Array.
 */
void fiobj_ary_push(FIOBJ ary, FIOBJ obj);

/** Pops an object from the end of the Array. */
FIOBJ fiobj_ary_pop(FIOBJ ary);

/**
 * Unshifts an object to the beginning of the Array. This could be
 * expensive.
 */
void fiobj_ary_unshift(FIOBJ ary, FIOBJ obj);

/** Shifts an object from the beginning of the Array. */
FIOBJ fiobj_ary_shift(FIOBJ ary);

/* *****************************************************************************
Array Find / Remove / Replace
***************************************************************************** */

/**
 * Replaces the object at a specific position, returning the old object -
 * remember to `fiobj_free` the old object.
 */
FIOBJ fiobj_ary_replace(FIOBJ ary, FIOBJ obj, int64_t pos);

/**
 * Finds the index of a specifide object (if any). Returns -1 if the object
 * isn't found.
 */
int64_t fiobj_ary_find(FIOBJ ary, FIOBJ data);

/**
 * Removes the object at the index (if valid), changing the index of any
 * following objects.
 *
 * Returns 0 on success or -1 (if no object or out of bounds).
 */
int fiobj_ary_remove(FIOBJ ary, int64_t pos);

/**
 * Removes the first instance of an object from the Array (if any), changing the
 * index of any following objects.
 *
 * Returns 0 on success or -1 (if the object wasn't found).
 */
int fiobj_ary_remove2(FIOBJ ary, FIOBJ data);

/* *****************************************************************************
Array compacting (untested)
***************************************************************************** */

/**
 * Removes any NULL *pointers* from an Array, keeping all Objects (including
 * explicit NULL objects) in the array.
 *
 * This action is O(n) where n in the length of the array.
 * It could get expensive.
 */
void fiobj_ary_compact(FIOBJ ary);

#if DEBUG
void fiobj_test_array(void);
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
