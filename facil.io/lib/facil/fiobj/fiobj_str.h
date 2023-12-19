#ifndef H_FIOBJ_STR_H
/*
Copyright: Boaz Segev, 2017-2019
License: MIT
*/
#define H_FIOBJ_STR_H

#include <fiobject.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FIOBJ_IS_STRING(obj) FIOBJ_TYPE_IS((obj), FIOBJ_T_STRING)

/* *****************************************************************************
API: Creating a String Object
***************************************************************************** */

/** Creates a String object. Remember to use `fiobj_free`. */
FIOBJ fiobj_str_new(const char *str, size_t len);

/**
 * Creates a String object with pre-allocation for Strings up to `capa` long.
 *
 * If `capa` is zero, a whole memory page will be allocated.
 *
 * Remember to use `fiobj_free`.
 */
FIOBJ fiobj_str_buf(size_t capa);

/** Creates a copy from an existing String. Remember to use `fiobj_free`. */
static inline __attribute__((unused)) FIOBJ fiobj_str_copy(FIOBJ src) {
  fio_str_info_s s = fiobj_obj2cstr(src);
  return fiobj_str_new(s.data, s.len);
}

/**
 * Creates a String object. Remember to use `fiobj_free`.
 *
 * It's possible to wrap a previosly allocated memory block in a FIOBJ String
 * object, as long as it was allocated using `fio_malloc`.
 *
 * The ownership of the memory indicated by `str` will "move" to the object and
 * will be freed (using `fio_free`) once the object's reference count drops to
 * zero.
 *
 * Note: The original memory MUST be allocated using `fio_malloc` (NOT the
 *       system's `malloc`) and it will be freed using `fio_free`.
 */
FIOBJ fiobj_str_move(char *str, size_t len, size_t capacity);

/**
 * Returns a thread-static temporary string. Avoid calling `fiobj_dup` or
 * `fiobj_free`.
 */
FIOBJ fiobj_str_tmp(void);

/* *****************************************************************************
API: Editing a String
***************************************************************************** */

/**
 * Prevents the String object from being changed.
 *
 * When a String is used as a key for a Hash, it is automatically frozen to
 * prevent the Hash from becoming broken.
 */
void fiobj_str_freeze(FIOBJ str);

/**
 * Confirms the String allows for the requested capacity (counting used space as
 * well as free space).
 *
 * Returns updated capacity.
 */
size_t fiobj_str_capa_assert(FIOBJ str, size_t size);

/** Returns a String's capacity, if any. This should include the NUL byte. */
size_t fiobj_str_capa(FIOBJ str);

/** Resizes a String object, allocating more memory if required. */
void fiobj_str_resize(FIOBJ str, size_t size);

/**
 * Performs a best attempt at minimizing memory consumption.
 *
 * Actual effects depend on the underlying memory allocator and it's
 * implementation. Not all allocators will free any memory.
 */
void fiobj_str_compact(FIOBJ str);

/** Alias for `fiobj_str_compact`. */
#define fiobj_str_minimize(str) fiobj_str_compact((str))

/** Empties a String's data. */
void fiobj_str_clear(FIOBJ str);

/**
 * Writes data at the end of the string, resizing the string as required.
 * Returns the new length of the String
 */
size_t fiobj_str_write(FIOBJ dest, const char *data, size_t len);

/**
 * Writes a number at the end of the String using normal base 10 notation.
 *
 * Returns the new length of the String
 */
size_t fiobj_str_write_i(FIOBJ dest, int64_t num);

/**
 * Writes data at the end of the string using a printf like interface, resizing
 * the string as required. Returns the new length of the String
 */
__attribute__((format(printf, 2, 3))) size_t
fiobj_str_printf(FIOBJ dest, const char *format, ...);

/**
 * Writes data at the end of the string using a vprintf like interface, resizing
 * the string as required.
 *
 * Returns the new length of the String
 */
__attribute__((format(printf, 2, 0))) size_t
fiobj_str_vprintf(FIOBJ dest, const char *format, va_list argv);

/**
 * Writes data at the end of the string, resizing the string as required.
 *
 * Remember to call `fiobj_free` to free the source (when done with it).
 *
 * Returns the new length of the String.
 */
size_t fiobj_str_concat(FIOBJ dest, FIOBJ source);
#define fiobj_str_join(dest, src) fiobj_str_concat((dest), (src))

/**
 * Dumps the `filename` file's contents at the end of the String.
 *
 * If `limit == 0`, than the data will be read until EOF.
 *
 * If the file can't be located, opened or read, or if `start_at` is out of
 * bounds (i.e., beyond the EOF position), FIOBJ_INVALID is returned.
 *
 * If `start_at` is negative, it will be computed from the end of the file.
 *
 * Remember to use `fiobj_free`.
 *
 * NOTE: Requires a UNIX system, otherwise always returns FIOBJ_INVALID.
 */
size_t fiobj_str_readfile(FIOBJ dest, const char *filename, intptr_t start_at,
                          intptr_t limit);

/* *****************************************************************************
API: String Values
***************************************************************************** */

/**
 * Calculates a String's SipHash value for possible use as a HashMap key.
 */
uint64_t fiobj_str_hash(FIOBJ o);

#if DEBUG
void fiobj_test_string(void);
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
