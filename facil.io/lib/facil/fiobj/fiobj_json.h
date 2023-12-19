#ifndef H_FIOBJ_JSON_H
#define H_FIOBJ_JSON_H

/*
Copyright: Boaz Segev, 2017-2019
License: MIT
*/

#include <fiobj_ary.h>
#include <fiobj_hash.h>
#include <fiobj_numbers.h>
#include <fiobj_str.h>
#include <fiobject.h>

#ifdef __cplusplus
extern "C" {
#endif

/* *****************************************************************************
JSON API
***************************************************************************** */

/** Limit JSON nesting, 32 is the limit to accomodate a 32 bit type. */
#if !defined(JSON_MAX_DEPTH) || JSON_MAX_DEPTH > 32
#undef JSON_MAX_DEPTH
#define JSON_MAX_DEPTH 32
#endif

/**
 * Parses JSON, setting `pobj` to point to the new Object.
 *
 * Returns the number of bytes consumed. On Error, 0 is returned and no data is
 * consumed.
 */
size_t fiobj_json2obj(FIOBJ *pobj, const void *data, size_t len);
/**
 * Stringify an object into a JSON string. Remember to `fiobj_free`.
 *
 * Note that only the foloowing basic fiobj types are supported: Primitives
 * (True / False / NULL), Numbers (Number / Float), Strings, Hashes and Arrays.
 *
 * Some objects (such as the POSIX specific IO type) are unsupported and may be
 * formatted incorrectly.
 */
FIOBJ fiobj_obj2json(FIOBJ, uint8_t pretty);

/**
 * Formats an object into a JSON string, appending the JSON string to an
 * existing String. Remember to `fiobj_free` as usual.
 *
 * Note that only the following basic fiobj types are supported: Primitives
 * (True / False / NULL), Numbers (Number / Float), Strings, Hashes and
 * Arrays.
 *
 * Some objects (such as the POSIX specific IO type) are unsupported and may be
 * formatted incorrectly.
 */
FIOBJ fiobj_obj2json2(FIOBJ dest, FIOBJ object, uint8_t pretty);

#if DEBUG
void fiobj_test_json(void);
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
