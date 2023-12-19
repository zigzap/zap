/*
Copyright: Boaz Segev, 2018-2019
License: MIT
*/
#ifndef H_FIOBJ_MUSTACHE_H
#define H_FIOBJ_MUSTACHE_H

#include <fiobject.h>

#include <mustache_parser.h>

/**
 * Loads a mustache template, converting it into an opaque instruction array.
 *
 * Returns a pointer to the instruction array or NULL (on error).
 *
 * The `filename` argument should contain the template's file name.
 */
mustache_s *fiobj_mustache_load(fio_str_info_s filename);

/**
 * Loads a mustache template, either from memory of a file, converting it into
 * an opaque instruction array.
 *
 * Returns a pointer to the instruction array or NULL (on error).
 *
 * Accepts any of the following named arguments:
 *   * `char const *filename`  - The root template's file name.
 *   * `size_t filename_len`  - The file name's length.
 *   * `char const *data`  - If set, will be used as the file's contents.
 *   * `size_t data_len`  - If set, `data` will be used as the file's contents.
 *   * `mustache_error_en *err`  - A container for any template load errors (see
 * mustache_parser.h).
 */
mustache_s *fiobj_mustache_new(mustache_load_args_s args);
#define fiobj_mustache_new(...)                                                \
  fiobj_mustache_new((mustache_load_args_s){__VA_ARGS__})

/** Free the mustache template */
void fiobj_mustache_free(mustache_s *mustache);

/**
 * Creates a FIOBJ String containing the rendered template using the information
 * in the `data` object.
 *
 * Returns FIOBJ_INVALID if an error occurred and a FIOBJ String on success.
 */
FIOBJ fiobj_mustache_build(mustache_s *mustache, FIOBJ data);

/**
 * Renders a template into an existing FIOBJ String (`dest`'s end), using the
 * information in the `data` object.
 *
 * Returns FIOBJ_INVALID if an error occurred and a FIOBJ String on success.
 */
FIOBJ fiobj_mustache_build2(FIOBJ dest, mustache_s *mustache, FIOBJ data);

#if DEBUG
void fiobj_mustache_test(void);
#endif

#endif /* H_FIOBJ_MUSTACHE_H */
