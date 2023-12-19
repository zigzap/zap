#ifndef H_FIO_JSON_H
/* *****************************************************************************
 * Copyright: Boaz Segev, 2017-2019
 * License: MIT
 *
 * This header file is a single-file JSON naive parse.
 *
 * The code was extracted form the FIOBJ implementation in order to allow the
 * parser to be used independantly from the rest of the facil.io library.
 *
 * The parser ignores missing commas and other formatting errors when possible.
 *
 * The parser also extends the JSON format to allow for C and Bash style
 * comments as well as hex numerical formats.
 *****************************************************************************
 */
#define H_FIO_JSON_H

#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if DEBUG
#include <stdio.h>
#endif

#if !defined(__GNUC__) && !defined(__clang__) && !defined(FIO_GNUC_BYPASS)
#define __attribute__(...)
#define __has_include(...) 0
#define __has_builtin(...) 0
#define FIO_GNUC_BYPASS 1
#elif !defined(__clang__) && !defined(__has_builtin)
#define __has_builtin(...) 0
#define FIO_GNUC_BYPASS 1
#endif

/* *****************************************************************************
JSON API
***************************************************************************** */

/* maximum allowed depth values max out at 32, since a bitmap is used */
#if !defined(JSON_MAX_DEPTH) || JSON_MAX_DEPTH > 32
#undef JSON_MAX_DEPTH
#define JSON_MAX_DEPTH 32
#endif

/** The JSON parser type. Memory must be initialized to 0 before first uses. */
typedef struct {
  /** in dictionary flag. */
  uint32_t dict;
  /** level of nesting. */
  uint8_t depth;
  /** in dictionary waiting for key. */
  uint8_t key;
} json_parser_s;

/**
 * Stream parsing of JSON data using a persistent parser.
 *
 * Returns the number of bytes consumed (0 being a valid value).
 *
 * Unconsumed data should be resent to the parser once more data is available.
 *
 * For security (due to numeral parsing concerns), a NUL byte should be placed
 * at `buffer[length]`.
 */
static size_t __attribute__((unused))
fio_json_parse(json_parser_s *parser, const char *buffer, size_t length);

/**
 * This function allows JSON formatted strings to be converted to native
 * strings.
 */
static size_t __attribute__((unused))
fio_json_unescape_str(void *dest, const char *source, size_t length);

/* *****************************************************************************
JSON Callacks - these must be implemented in the C file that uses the parser
***************************************************************************** */

/** a NULL object was detected */
static void fio_json_on_null(json_parser_s *p);
/** a TRUE object was detected */
static void fio_json_on_true(json_parser_s *p);
/** a FALSE object was detected */
static void fio_json_on_false(json_parser_s *p);
/** a Numberl was detected (long long). */
static void fio_json_on_number(json_parser_s *p, long long i);
/** a Float was detected (double). */
static void fio_json_on_float(json_parser_s *p, double f);
/** a String was detected (int / float). update `pos` to point at ending */
static void fio_json_on_string(json_parser_s *p, void *start, size_t length);
/** a dictionary object was detected, should return 0 unless error occurred. */
static int fio_json_on_start_object(json_parser_s *p);
/** a dictionary object closure detected */
static void fio_json_on_end_object(json_parser_s *p);
/** an array object was detected, should return 0 unless error occurred. */
static int fio_json_on_start_array(json_parser_s *p);
/** an array closure was detected */
static void fio_json_on_end_array(json_parser_s *p);
/** the JSON parsing is complete */
static void fio_json_on_json(json_parser_s *p);
/** the JSON parsing is complete */
static void fio_json_on_error(json_parser_s *p);

/* *****************************************************************************
JSON maps (arrays used to map data to simplify `if` statements)
***************************************************************************** */

/*
Marks as object seperators any of the following:

* White Space: [0x09, 0x0A, 0x0D, 0x20]
* Comma ("," / 0x2C)
* NOT Colon (":" / 0x3A)
* == [0x09, 0x0A, 0x0D, 0x20, 0x2C]
The rest belong to objects,
*/
static const uint8_t JSON_SEPERATOR[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

/*
Marks a numeral valid char (it's a permisive list):
['0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'e', 'E', '+', '-', 'x', 'b',
'.']
*/
static const uint8_t JSON_NUMERAL[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 0,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

static const char hex_chars[] = {'0', '1', '2', '3', '4', '5', '6', '7',
                                 '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

static const uint8_t is_hex[] = {
    0,  0,  0,  0, 0, 0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0,  0,  0,
    0,  0,  0,  0, 0, 0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0,  0,  0,
    0,  0,  0,  0, 0, 0,  0,  0,  1,  2,  3,  4, 5, 6, 7, 8, 9, 10, 0,  0,
    0,  0,  0,  0, 0, 11, 12, 13, 14, 15, 16, 0, 0, 0, 0, 0, 0, 0,  0,  0,
    0,  0,  0,  0, 0, 0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 11, 12, 13,
    14, 15, 16, 0, 0, 0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0,  0,  0,
    0,  0,  0,  0, 0, 0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0,  0,  0,
    0,  0,  0,  0, 0, 0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0,  0,  0,
    0,  0,  0,  0, 0, 0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0,  0,  0,
    0,  0,  0,  0, 0, 0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0,  0,  0,
    0,  0,  0,  0, 0, 0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0,  0,  0,
    0,  0,  0,  0, 0, 0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0,  0,  0,
    0,  0,  0,  0, 0, 0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0};

/*
Stops seeking a String:
['\\', '"']
*/
static const uint8_t string_seek_stop[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

/* *****************************************************************************
JSON String Helper - Seeking to the end of a string
***************************************************************************** */

/**
 * finds the first occurance of either '"' or '\\'.
 */
static inline int seek2marker(uint8_t **buffer,
                              register const uint8_t *const limit) {
  if (string_seek_stop[**buffer])
    return 1;

#if !ALLOW_UNALIGNED_MEMORY_ACCESS || (!__x86_64__ && !__aarch64__)
  /* too short for this mess */
  if ((uintptr_t)limit <= 8 + ((uintptr_t)*buffer & (~(uintptr_t)7)))
    goto finish;
  /* align memory */
  if (1) {
    {
      const uint8_t *alignment =
          (uint8_t *)(((uintptr_t)(*buffer) & (~(uintptr_t)7)) + 8);
      if (limit >= alignment) {
        while (*buffer < alignment) {
          if (string_seek_stop[**buffer])
            return 1;
          *buffer += 1;
        }
      }
    }
    const uint8_t *limit64 = (uint8_t *)((uintptr_t)limit & (~(uintptr_t)7));
#else
  const uint8_t *limit64 = (uint8_t *)limit - 7;
#endif
    uint64_t wanted1 = 0x0101010101010101ULL * '"';
    uint64_t wanted2 = 0x0101010101010101ULL * '\\';
    for (; *buffer < limit64; *buffer += 8) {
      const uint64_t eq1 = ~((*((uint64_t *)*buffer)) ^ wanted1);
      const uint64_t t1 =
          ((eq1 & 0x7f7f7f7f7f7f7f7fULL) + 0x0101010101010101ULL) &
          (eq1 & 0x8080808080808080ULL);
      const uint64_t eq2 = ~((*((uint64_t *)*buffer)) ^ wanted2);
      const uint64_t t2 =
          ((eq2 & 0x7f7f7f7f7f7f7f7fULL) + 0x0101010101010101ULL) &
          (eq2 & 0x8080808080808080ULL);
      if ((t1 | t2)) {
        break;
      }
    }
  }
#if !ALLOW_UNALIGNED_MEMORY_ACCESS || (!__x86_64__ && !__aarch64__)
finish:
#endif
  if (*buffer + 4 <= limit) {
    if (string_seek_stop[(*buffer)[0]]) {
      // *buffer += 0;
      return 1;
    }
    if (string_seek_stop[(*buffer)[1]]) {
      *buffer += 1;
      return 1;
    }
    if (string_seek_stop[(*buffer)[2]]) {
      *buffer += 2;
      return 1;
    }
    if (string_seek_stop[(*buffer)[3]]) {
      *buffer += 3;
      return 1;
    }
    *buffer += 4;
  }
  while (*buffer < limit) {
    if (string_seek_stop[**buffer])
      return 1;
    (*buffer)++;
  }
  return 0;
}

static inline int seek2eos(uint8_t **buffer,
                           register const uint8_t *const limit) {
  while (*buffer < limit) {
    if (seek2marker(buffer, limit) && **buffer == '"')
      return 1;
    (*buffer) += 2; /* consume both the escape '\\' and the escape code. */
  }
  return 0;
}

/* *****************************************************************************
JSON String to Numeral Helpers - allowing for stand-alone mode
***************************************************************************** */

#ifndef H_FACIL_IO_H /* defined in fio.h */

/**
 * We include this in case the parser is used outside of facil.io.
 */
int64_t __attribute__((weak)) fio_atol(char **pstr) {
  return strtoll((char *)*pstr, (char **)pstr, 0);
}
#pragma weak fio_atol

/**
 * We include this in case the parser is used outside of facil.io.
 */
double __attribute__((weak)) fio_atof(char **pstr) {
  return strtod((char *)*pstr, (char **)pstr);
}
#pragma weak fio_atof

#endif

/* *****************************************************************************
JSON Consumption (astract parsing)
***************************************************************************** */

/**
 * Returns the number of bytes consumed. Stops as close as possible to the end
 * of the buffer or once an object parsing was completed.
 */
static size_t __attribute__((unused))
fio_json_parse(json_parser_s *parser, const char *buffer, size_t length) {
  if (!length || !buffer)
    return 0;
  uint8_t *pos = (uint8_t *)buffer;
  const uint8_t *limit = pos + length;
  do {
    while (pos < limit && JSON_SEPERATOR[*pos])
      ++pos;
    if (pos == limit)
      goto stop;
    switch (*pos) {
    case '"': {
      uint8_t *tmp = pos + 1;
      if (seek2eos(&tmp, limit) == 0)
        goto stop;
      if (parser->key) {
        uint8_t *key = tmp + 1;
        while (key < limit && JSON_SEPERATOR[*key])
          ++key;
        if (key >= limit)
          goto stop;
        if (*key != ':')
          goto error;
        ++pos;
        fio_json_on_string(parser, pos, (uintptr_t)(tmp - pos));
        pos = key + 1;
        parser->key = 0;
        continue; /* skip tests */
      } else {
        ++pos;
        fio_json_on_string(parser, pos, (uintptr_t)(tmp - pos));
        pos = tmp + 1;
      }
      break;
    }
    case '{':
      if (parser->key) {
#if DEBUG
        fprintf(stderr, "ERROR: JSON key can't be a Hash.\n");
#endif
        goto error;
      }
      ++parser->depth;
      if (parser->depth >= JSON_MAX_DEPTH)
        goto error;
      parser->dict = (parser->dict << 1) | 1;
      ++pos;
      if (fio_json_on_start_object(parser))
        goto error;
      break;
    case '}':
      if ((parser->dict & 1) == 0) {
#if DEBUG
        fprintf(stderr, "ERROR: JSON dictionary closure error.\n");
#endif
        goto error;
      }
      if (!parser->key) {
#if DEBUG
        fprintf(stderr, "ERROR: JSON dictionary closure missing key value.\n");
        goto error;
#endif
        fio_json_on_null(parser); /* append NULL and recuperate from error. */
      }
      --parser->depth;
      ++pos;
      parser->dict = (parser->dict >> 1);
      fio_json_on_end_object(parser);
      break;
    case '[':
      if (parser->key) {
#if DEBUG
        fprintf(stderr, "ERROR: JSON key can't be an array.\n");
#endif
        goto error;
      }
      ++parser->depth;
      if (parser->depth >= JSON_MAX_DEPTH)
        goto error;
      ++pos;
      parser->dict = (parser->dict << 1);
      if (fio_json_on_start_array(parser))
        goto error;
      break;
    case ']':
      if ((parser->dict & 1))
        goto error;
      --parser->depth;
      ++pos;
      parser->dict = (parser->dict >> 1);
      fio_json_on_end_array(parser);
      break;
    case 't':
      if (pos + 3 >= limit)
        goto stop;
      if (pos[1] == 'r' && pos[2] == 'u' && pos[3] == 'e')
        fio_json_on_true(parser);
      else
        goto error;
      pos += 4;
      break;
    case 'N': /* overflow */
    case 'n':
      if (pos + 2 <= limit && (pos[1] | 32) == 'a' && (pos[2] | 32) == 'n')
        goto numeral;
      if (pos + 3 >= limit)
        goto stop;
      if (pos[1] == 'u' && pos[2] == 'l' && pos[3] == 'l')
        fio_json_on_null(parser);
      else
        goto error;
      pos += 4;
      break;
    case 'f':
      if (pos + 4 >= limit)
        goto stop;
      if (pos + 4 < limit && pos[1] == 'a' && pos[2] == 'l' && pos[3] == 's' &&
          pos[4] == 'e')
        fio_json_on_false(parser);
      else
        goto error;
      pos += 5;
      break;
    case '-': /* overflow */
    case '0': /* overflow */
    case '1': /* overflow */
    case '2': /* overflow */
    case '3': /* overflow */
    case '4': /* overflow */
    case '5': /* overflow */
    case '6': /* overflow */
    case '7': /* overflow */
    case '8': /* overflow */
    case '9': /* overflow */
    case '.': /* overflow */
    case 'e': /* overflow */
    case 'E': /* overflow */
    case 'x': /* overflow */
    case 'i': /* overflow */
    case 'I': /* overflow */
    numeral : {
      uint8_t *tmp = pos;
      long long i = fio_atol((char **)&tmp);
      if (tmp > limit)
        goto stop;
      if (!tmp || JSON_NUMERAL[*tmp]) {
        tmp = pos;
        double f = fio_atof((char **)&tmp);
        if (tmp > limit)
          goto stop;
        if (!tmp || JSON_NUMERAL[*tmp])
          goto error;
        fio_json_on_float(parser, f);
        pos = tmp;
      } else {
        fio_json_on_number(parser, i);
        pos = tmp;
      }
      break;
    }
    case '#': /* Ruby style comment */
    {
      uint8_t *tmp = memchr(pos, '\n', (uintptr_t)(limit - pos));
      if (!tmp)
        goto stop;
      pos = tmp + 1;
      continue; /* skip tests */
      ;
    }
    case '/': /* C style / Javascript style comment */
      if (pos[1] == '*') {
        if (pos + 4 > limit)
          goto stop;
        uint8_t *tmp = pos + 3; /* avoid this: /*/
        do {
          tmp = memchr(tmp, '/', (uintptr_t)(limit - tmp));
        } while (tmp && tmp[-1] != '*');
        if (!tmp)
          goto stop;
        pos = tmp + 1;
      } else if (pos[1] == '/') {
        uint8_t *tmp = memchr(pos, '\n', (uintptr_t)(limit - pos));
        if (!tmp)
          goto stop;
        pos = tmp + 1;
      } else
        goto error;
      continue; /* skip tests */
      ;
    default:
      goto error;
    }
    if (parser->depth == 0) {
      fio_json_on_json(parser);
      goto stop;
    }
    parser->key = (parser->dict & 1);
  } while (pos < limit);
stop:
  return (size_t)((uintptr_t)pos - (uintptr_t)buffer);
error:
  fio_json_on_error(parser);
  return 0;
}

/* *****************************************************************************
JSON Unescape String
***************************************************************************** */

#ifdef __cplusplus
#define REGISTER
#else
#define REGISTER register
#endif

/* converts a uint32_t to UTF-8 and returns the number of bytes written */
static inline int utf8_from_u32(uint8_t *dest, uint32_t u) {
  if (u <= 127) {
    *dest = u;
    return 1;
  } else if (u <= 2047) {
    *(dest++) = 192 | (u >> 6);
    *(dest++) = 128 | (u & 63);
    return 2;
  } else if (u <= 65535) {
    *(dest++) = 224 | (u >> 12);
    *(dest++) = 128 | ((u >> 6) & 63);
    *(dest++) = 128 | (u & 63);
    return 3;
  }
  *(dest++) = 240 | ((u >> 18) & 7);
  *(dest++) = 128 | ((u >> 12) & 63);
  *(dest++) = 128 | ((u >> 6) & 63);
  *(dest++) = 128 | (u & 63);
  return 4;
}

static void __attribute__((unused))
fio_json_unescape_str_internal(uint8_t **dest, const uint8_t **src) {
  ++(*src);
  switch (**src) {
  case 'b':
    **dest = '\b';
    ++(*src);
    ++(*dest);
    return; /* from switch */
  case 'f':
    **dest = '\f';
    ++(*src);
    ++(*dest);
    return; /* from switch */
  case 'n':
    **dest = '\n';
    ++(*src);
    ++(*dest);
    return; /* from switch */
  case 'r':
    **dest = '\r';
    ++(*src);
    ++(*dest);
    return; /* from switch */
  case 't':
    **dest = '\t';
    ++(*src);
    ++(*dest);
    return;   /* from switch */
  case 'u': { /* test for octal notation */
    if (is_hex[(*src)[1]] && is_hex[(*src)[2]] && is_hex[(*src)[3]] &&
        is_hex[(*src)[4]]) {
      uint32_t t =
          ((((is_hex[(*src)[1]] - 1) << 4) | (is_hex[(*src)[2]] - 1)) << 8) |
          (((is_hex[(*src)[3]] - 1) << 4) | (is_hex[(*src)[4]] - 1));
      if ((*src)[5] == '\\' && (*src)[6] == 'u' && is_hex[(*src)[7]] &&
          is_hex[(*src)[8]] && is_hex[(*src)[9]] && is_hex[(*src)[10]]) {
        /* Serrogate Pair */
        t = (t & 0x03FF) << 10;
        t |= ((((((is_hex[(*src)[7]] - 1) << 4) | (is_hex[(*src)[8]] - 1))
                << 8) |
               (((is_hex[(*src)[9]] - 1) << 4) | (is_hex[(*src)[10]] - 1))) &
              0x03FF);
        t += 0x10000;
        (*src) += 6;
      }
      *dest += utf8_from_u32(*dest, t);
      *src += 5;
      return;
    } else
      goto invalid_escape;
  }
  case 'x': { /* test for hex notation */
    if (is_hex[(*src)[1]] && is_hex[(*src)[2]]) {
      **dest = ((is_hex[(*src)[1]] - 1) << 4) | (is_hex[(*src)[2]] - 1);
      ++(*dest);
      (*src) += 3;
      return;
    } else
      goto invalid_escape;
  }
  case '0':
  case '1':
  case '2':
  case '3':
  case '4':
  case '5':
  case '6':
  case '7': { /* test for octal notation */
    if ((*src)[1] >= '0' && (*src)[1] <= '7') {
      **dest = (((*src)[0] - '0') << 3) | ((*src)[1] - '0');
      ++(*dest);
      (*src) += 2;
      break; /* from switch */
    } else
      goto invalid_escape;
  }
  case '"':
  case '\\':
  case '/':
  /* fallthrough */
  default:
  invalid_escape:
    **dest = **src;
    ++(*src);
    ++(*dest);
  }
}

static size_t __attribute__((unused))
fio_json_unescape_str(void *dest, const char *source, size_t length) {
  const uint8_t *reader = (uint8_t *)source;
  const uint8_t *stop = reader + length;
  uint8_t *writer = (uint8_t *)dest;
  /* copy in chuncks unless we hit an escape marker */
  while (reader < stop) {
#if !__x86_64__ && !__aarch64__
    /* we can't leverage unaligned memory access, so we read the buffer twice */
    uint8_t *tmp = memchr(reader, '\\', (size_t)(stop - reader));
    if (!tmp) {
      memmove(writer, reader, (size_t)(stop - reader));
      writer += (size_t)(stop - reader);
      goto finish;
    }
    memmove(writer, reader, (size_t)(tmp - reader));
    writer += (size_t)(tmp - reader);
    reader = tmp;
#else
    const uint8_t *limit64 = (uint8_t *)stop - 7;
    uint64_t wanted1 = 0x0101010101010101ULL * '\\';
    while (reader < limit64) {
      const uint64_t eq1 = ~((*((uint64_t *)reader)) ^ wanted1);
      const uint64_t t0 = (eq1 & 0x7f7f7f7f7f7f7f7fllu) + 0x0101010101010101llu;
      const uint64_t t1 = (eq1 & 0x8080808080808080llu);
      if ((t0 & t1)) {
        break;
      }
      *((uint64_t *)writer) = *((uint64_t *)reader);
      reader += 8;
      writer += 8;
    }
    while (reader < stop) {
      if (*reader == '\\')
        break;
      *writer = *reader;
      ++reader;
      ++writer;
    }
    if (reader >= stop)
      goto finish;
#endif
    fio_json_unescape_str_internal(&writer, &reader);
  }
finish:
  return (size_t)((uintptr_t)writer - (uintptr_t)dest);
}

#undef REGISTER

#endif
