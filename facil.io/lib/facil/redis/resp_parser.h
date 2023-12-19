/*
Copyright: Boaz segev, 2016-2019
License: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#ifndef H_RESP_PARSER_H
/**
 * This single file library is a RESP parser for Redis connections.
 *
 * To use this file, the `.c` file in which this file is included MUST define a
 * number of callbacks, as later inticated.
 *
 * When feeding the parser, the parser will inform of any trailing bytes (bytes
 * at the end of the buffer that could not be parsed). These bytes should be
 * resent to the parser along with more data. Zero is a valid return value.
 *
 * Note: mostly, callback return vaslues are ignored.
 */
#define H_RESP_PARSER_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* *****************************************************************************
The Parser
***************************************************************************** */

typedef struct resp_parser_s {
  /* for internal use - (array / object countdown) */
  intptr_t obj_countdown;
  /* for internal use - (string byte consumption) */
  intptr_t expecting;
} resp_parser_s;

/**
 * Returns the number of bytes to be resent. i.e., for a return value 5, the
 * last 5 bytes in the buffer need to be resent to the parser.
 */
static size_t resp_parse(resp_parser_s *parser, const void *buffer,
                         size_t length);

/* *****************************************************************************
Required Parser Callbacks (to be defined by the including file)
***************************************************************************** */

/** a local static callback, called when the RESP message is complete. */
static int resp_on_message(resp_parser_s *parser);

/** a local static callback, called when a Number object is parsed. */
static int resp_on_number(resp_parser_s *parser, int64_t num);
/** a local static callback, called when a OK message is received. */
static int resp_on_okay(resp_parser_s *parser);
/** a local static callback, called when NULL is received. */
static int resp_on_null(resp_parser_s *parser);

/**
 * a local static callback, called when a String should be allocated.
 *
 * `str_len` is the expected number of bytes that will fill the final string
 * object, without any NUL byte marker (the string might be binary).
 *
 * If this function returns any value besides 0, parsing is stopped.
 */
static int resp_on_start_string(resp_parser_s *parser, size_t str_len);
/** a local static callback, called as String objects are streamed. */
static int resp_on_string_chunk(resp_parser_s *parser, void *data, size_t len);
/** a local static callback, called when a String object had finished streaming.
 */
static int resp_on_end_string(resp_parser_s *parser);

/** a local static callback, called an error message is received. */
static int resp_on_err_msg(resp_parser_s *parser, void *data, size_t len);

/**
 * a local static callback, called when an Array should be allocated.
 *
 * `array_len` is the expected number of objects that will fill the Array
 * object.
 *
 * There's no `resp_on_end_array` callback since the RESP protocol assumes the
 * message is finished along with the Array (`resp_on_message` is called).
 * However, just in case a non-conforming client/server sends nested Arrays, the
 * callback should test against possible overflow or nested Array endings.
 *
 * If this function returns any value besides 0, parsing is stopped.
 */
static int resp_on_start_array(resp_parser_s *parser, size_t array_len);

/** a local static callback, called when a parser / protocol error occurs. */
static int resp_on_parser_error(resp_parser_s *parser);

/* *****************************************************************************
Seeking the new line...
***************************************************************************** */

#if FIO_MEMCHAR

/**
 * This seems to be faster on some systems, especially for smaller distances.
 *
 * On newer systems, `memchr` should be faster.
 */
static inline int seek2ch(uint8_t **buffer, register const uint8_t *limit,
                          const uint8_t c) {
  if (**buffer == c)
    return 1;

#if !ALLOW_UNALIGNED_MEMORY_ACCESS || !defined(__x86_64__)
  /* too short for this mess */
  if ((uintptr_t)limit <= 16 + ((uintptr_t)*buffer & (~(uintptr_t)7)))
    goto finish;

  /* align memory */
  {
    const uint8_t *alignment =
        (uint8_t *)(((uintptr_t)(*buffer) & (~(uintptr_t)7)) + 8);
    if (limit >= alignment) {
      while (*buffer < alignment) {
        if (**buffer == c)
          return 1;
        *buffer += 1;
      }
    }
  }
  const uint8_t *limit64 = (uint8_t *)((uintptr_t)limit & (~(uintptr_t)7));
#else
  const uint8_t *limit64 = (uint8_t *)limit - 7;
#endif
  uint64_t wanted1 = 0x0101010101010101ULL * c;
  for (; *buffer < limit64; *buffer += 8) {
    const uint64_t eq1 = ~((*((uint64_t *)*buffer)) ^ wanted1);
    const uint64_t t0 = (eq1 & 0x7f7f7f7f7f7f7f7fllu) + 0x0101010101010101llu;
    const uint64_t t1 = (eq1 & 0x8080808080808080llu);
    if ((t0 & t1)) {
      break;
    }
  }
#if !ALLOW_UNALIGNED_MEMORY_ACCESS || !defined(__x86_64__)
finish:
#endif
  while (*buffer < limit) {
    if (**buffer == c)
      return 1;
    (*buffer)++;
  }
  return 0;
}

#else

/* a helper that seeks any char, converts it to NUL and returns 1 if found. */
inline static uint8_t seek2ch(uint8_t **pos, const uint8_t *limit, uint8_t ch) {
  /* This is library based alternative that is sometimes slower  */
  if (*pos >= limit || **pos == ch) {
    return 0;
  }
  uint8_t *tmp = (uint8_t *)memchr(*pos, ch, limit - (*pos));
  if (tmp) {
    *pos = tmp;
    return 1;
  }
  *pos = (uint8_t *)limit;
  return 0;
}

#endif

/* *****************************************************************************
Parsing RESP requests
***************************************************************************** */

/**
 * Returns the number of bytes to be resent. i.e., for a return value 5, the
 * last 5 bytes in the buffer need to be resent to the parser.
 */
static size_t resp_parse(resp_parser_s *parser, const void *buffer,
                         size_t length) {
  if (!parser->obj_countdown)
    parser->obj_countdown = 1; /* always expect something... */
  uint8_t *pos = (uint8_t *)buffer;
  const uint8_t *stop = pos + length;
  while (pos < stop) {
    uint8_t *eol;
    if (parser->expecting) {
      if (pos + parser->expecting + 2 > stop) {
        /* read, but make sure the buffer includes the new line markers */
        size_t tmp = (size_t)((uintptr_t)stop - (uintptr_t)pos);
        if ((intptr_t)tmp >= parser->expecting)
          tmp = parser->expecting - 1;
        resp_on_string_chunk(parser, (void *)pos, tmp);
        parser->expecting -= tmp;
        return (size_t)((uintptr_t)stop - ((uintptr_t)pos + tmp)); /* 0 or 1 */
      } else {
        resp_on_string_chunk(parser, (void *)pos, parser->expecting);
        resp_on_end_string(parser);
        pos += parser->expecting;
        if (pos[0] == '\r')
          ++pos;
        if (pos[0] == '\n')
          ++pos;
        parser->expecting = 0;
        --parser->obj_countdown;
        if (parser->obj_countdown <= 0) {
          parser->obj_countdown = 1;
          if (resp_on_message(parser))
            goto finish;
        }
        continue;
      }
    }
    eol = pos;
    if (seek2ch(&eol, stop, '\n') == 0)
      break;
    switch (*pos) {
    case '+':
      if (pos[1] == 'O' && pos[2] == 'K' && pos[3] == '\r' && pos[4] == '\n') {
        resp_on_okay(parser);
        --parser->obj_countdown;
        break;
      }
      if (resp_on_start_string(parser,
                               (size_t)((uintptr_t)eol - (uintptr_t)pos - 2))) {
        pos = eol + 1;
        goto finish;
      }
      resp_on_string_chunk(parser, (void *)(pos + 1),
                           (size_t)((uintptr_t)eol - (uintptr_t)pos - 1));
      resp_on_end_string(parser);
      --parser->obj_countdown;
      break;
    case '-':
      resp_on_err_msg(parser, pos,
                      (size_t)((uintptr_t)eol - (uintptr_t)pos - 1));
      --parser->obj_countdown;
      break;
    case '*': /* fallthrough */
    case '$': /* fallthrough */
    case ':': {
      uint8_t id = *pos;
      uint8_t inv = 0;
      int64_t i = 0;
      ++pos;
      if (pos[0] == '-') {
        inv = 1;
        ++pos;
      }
      while ((size_t)(pos[0] - (uint8_t)'0') <= 9) {
        i = (i * 10) + (pos[0] - ((uint8_t)'0'));
        ++pos;
      }
      if (inv)
        i = i * -1;

      switch (id) {
      case ':':
        resp_on_number(parser, i);
        --parser->obj_countdown;
        break;
      case '$':
        if (i < 0) {
          resp_on_null(parser);
          --parser->obj_countdown;
        } else if (i == 0) {
          resp_on_start_string(parser, 0);
          resp_on_end_string(parser);
          --parser->obj_countdown;
          eol += 2; /* consume the extra "\r\n" */
        } else {
          if (resp_on_start_string(parser, i)) {
            pos = eol + 1;
            goto finish;
          }
          parser->expecting = i;
        }
        break;
      case '*':
        if (i < 0) {
          resp_on_null(parser);
        } else {
          if (resp_on_start_array(parser, i)) {
            pos = eol + 1;
            goto finish;
          }
          parser->obj_countdown += i;
        }
        --parser->obj_countdown;
        break;
      }
    } break;
    default:
      if (!parser->obj_countdown && !parser->expecting) {
        /* possible (probable) inline command... for server authoring. */
        /* Not Supported, PRs are welcome. */
        resp_on_parser_error(parser);
        return (size_t)((uintptr_t)stop - (uintptr_t)pos);
      } else {
        resp_on_parser_error(parser);
        return (size_t)((uintptr_t)stop - (uintptr_t)pos);
      }
    }
    pos = eol + 1;
    if (parser->obj_countdown <= 0 && !parser->expecting) {
      parser->obj_countdown = 1;
      resp_on_message(parser);
    }
  }
finish:
  return (size_t)((uintptr_t)stop - (uintptr_t)pos);
}

#endif /* H_RESP_PARSER_H */
