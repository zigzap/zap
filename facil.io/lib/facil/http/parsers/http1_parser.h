#ifndef H_HTTP1_PARSER_H
/*
Copyright: Boaz Segev, 2017-2020
License: MIT

Feel free to copy, use and enjoy according to the license provided.
*/

/**
This is a callback based parser. It parses the skeleton of the HTTP/1.x protocol
and leaves most of the work (validation, error checks, etc') to the callbacks.
*/
#define H_HTTP1_PARSER_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

/* *****************************************************************************
Parser Settings
***************************************************************************** */

#ifndef HTTP_HEADERS_LOWERCASE
/**
 * When defined, HTTP headers will be converted to lowercase and header
 * searches will be case sensitive.
 *
 * This is highly recommended, required by facil.io and helps with HTTP/2
 * compatibility.
 */
#define HTTP_HEADERS_LOWERCASE 1
#endif

#ifndef HTTP_ADD_CONTENT_LENGTH_HEADER_IF_MISSING
#define HTTP_ADD_CONTENT_LENGTH_HEADER_IF_MISSING 1
#endif

#ifndef FIO_MEMCHAR
/** Prefer a custom memchr implementation. Usualy memchr is better. */
#define FIO_MEMCHAR 0
#endif

#ifndef ALLOW_UNALIGNED_MEMORY_ACCESS
/** Peforms some optimizations assuming unaligned memory access is okay. */
#define ALLOW_UNALIGNED_MEMORY_ACCESS 0
#endif

#ifndef HTTP1_PARSER_CONVERT_EOL2NUL
#define HTTP1_PARSER_CONVERT_EOL2NUL 0
#endif

/* *****************************************************************************
Parser API
***************************************************************************** */

/** this struct contains the state of the parser. */
typedef struct http1_parser_s {
  struct http1_parser_protected_read_only_state_s {
    long long content_length; /* negative values indicate chuncked data state */
    ssize_t read;     /* total number of bytes read so far (body only) */
    uint8_t *next;    /* the known position for the end of request/response */
    uint8_t reserved; /* for internal use */
  } state;
} http1_parser_s;

#define HTTP1_PARSER_INIT                                                      \
  {                                                                            \
    { 0 }                                                                      \
  }

/**
 * Returns the amount of data actually consumed by the parser.
 *
 * The value 0 indicates there wasn't enough data to be parsed and the same
 * buffer (with more data) should be resubmitted.
 *
 * A value smaller than the buffer size indicates that EITHER a request /
 * response was detected OR that the leftover could not be consumed because more
 * data was required.
 *
 * Simply resubmit the reminder of the data to continue parsing.
 *
 * A request / response callback automatically stops the parsing process,
 * allowing the user to adjust or refresh the state of the data.
 */
static size_t http1_parse(http1_parser_s *parser, void *buffer, size_t length);

/* *****************************************************************************
Required Callbacks (MUST be implemented by including file)
***************************************************************************** */

/** called when a request was received. */
static int http1_on_request(http1_parser_s *parser);
/** called when a response was received. */
static int http1_on_response(http1_parser_s *parser);
/** called when a request method is parsed. */
static int http1_on_method(http1_parser_s *parser, char *method,
                           size_t method_len);
/** called when a response status is parsed. the status_str is the string
 * without the prefixed numerical status indicator.*/
static int http1_on_status(http1_parser_s *parser, size_t status,
                           char *status_str, size_t len);
/** called when a request path (excluding query) is parsed. */
static int http1_on_path(http1_parser_s *parser, char *path, size_t path_len);
/** called when a request path (excluding query) is parsed. */
static int http1_on_query(http1_parser_s *parser, char *query,
                          size_t query_len);
/** called when a the HTTP/1.x version is parsed. */
static int http1_on_version(http1_parser_s *parser, char *version, size_t len);
/** called when a header is parsed. */
static int http1_on_header(http1_parser_s *parser, char *name, size_t name_len,
                           char *data, size_t data_len);
/** called when a body chunk is parsed. */
static int http1_on_body_chunk(http1_parser_s *parser, char *data,
                               size_t data_len);
/** called when a protocol error occurred. */
static int http1_on_error(http1_parser_s *parser);

/* *****************************************************************************

















                        Implementation Details

















***************************************************************************** */

#if HTTP_HEADERS_LOWERCASE
#define HEADER_NAME_IS_EQ(var_name, const_name, len)                           \
  (!memcmp((var_name), (const_name), (len)))
#else
#define HEADER_NAME_IS_EQ(var_name, const_name, len)                           \
  (!strncasecmp((var_name), (const_name), (len)))
#endif

#define HTTP1_P_FLAG_STATUS_LINE 1
#define HTTP1_P_FLAG_HEADER_COMPLETE 2
#define HTTP1_P_FLAG_COMPLETE 4
#define HTTP1_P_FLAG_CLENGTH 8
#define HTTP1_PARSER_BIT_16 16
#define HTTP1_PARSER_BIT_32 32
#define HTTP1_P_FLAG_CHUNKED 64
#define HTTP1_P_FLAG_RESPONSE 128

/* *****************************************************************************
Seeking for characters in a string
***************************************************************************** */

#if FIO_MEMCHAR

/**
 * This seems to be faster on some systems, especially for smaller distances.
 *
 * On newer systems, `memchr` should be faster.
 */
static int seek2ch(uint8_t **buffer, register uint8_t *const limit,
                   const uint8_t c) {
  if (*buffer >= limit)
    return 0;
  if (**buffer == c) {
    return 1;
  }

#if !HTTP1_UNALIGNED_MEMORY_ACCESS_ENABLED
  /* too short for this mess */
  if ((uintptr_t)limit <= 16 + ((uintptr_t)*buffer & (~(uintptr_t)7)))
    goto finish;

  /* align memory */
  {
    const uint8_t *alignment =
        (uint8_t *)(((uintptr_t)(*buffer) & (~(uintptr_t)7)) + 8);
    if (*buffer < alignment)
      *buffer += 1; /* we already tested this char */
    if (limit >= alignment) {
      while (*buffer < alignment) {
        if (**buffer == c) {
          return 1;
        }
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
#if !HTTP1_UNALIGNED_MEMORY_ACCESS_ENABLED
finish:
#endif
  while (*buffer < limit) {
    if (**buffer == c) {
      return 1;
    }
    (*buffer)++;
  }
  return 0;
}

#else

/* a helper that seeks any char, converts it to NUL and returns 1 if found. */
inline static uint8_t seek2ch(uint8_t **pos, uint8_t *const limit, uint8_t ch) {
  /* This is library based alternative that is sometimes slower  */
  if (*pos >= limit)
    return 0;
  if (**pos == ch) {
    return 1;
  }
  uint8_t *tmp = memchr(*pos, ch, limit - (*pos));
  if (tmp) {
    *pos = tmp;
    return 1;
  }
  *pos = limit;
  return 0;
}

#endif

/* a helper that seeks the EOL, converts it to NUL and returns it's length */
inline static uint8_t seek2eol(uint8_t **pos, uint8_t *const limit) {
  /* single char lookup using memchr might be better when target is far... */
  if (!seek2ch(pos, limit, '\n'))
    return 0;
  if ((*pos)[-1] == '\r') {
#if HTTP1_PARSER_CONVERT_EOL2NUL
    (*pos)[-1] = (*pos)[0] = 0;
#endif
    return 2;
  }
#if HTTP1_PARSER_CONVERT_EOL2NUL
  (*pos)[0] = 0;
#endif
  return 1;
}

/* *****************************************************************************
Change a letter to lower case (latin only)
***************************************************************************** */

static uint8_t http_tolower(uint8_t c) {
  if (c >= 'A' && c <= 'Z')
    c |= 32;
  return c;
}

/* *****************************************************************************
String to Number
***************************************************************************** */

/** Converts a String to a number using base 10 */
static long long http1_atol(const uint8_t *buf, const uint8_t **end) {
  register unsigned long long i = 0;
  uint8_t inv = 0;
  while (*buf == ' ' || *buf == '\t' || *buf == '\f')
    ++buf;
  while (*buf == '-' || *buf == '+')
    inv ^= (*(buf++) == '-');
  while (i <= ((((~0ULL) >> 1) / 10)) && *buf >= '0' && *buf <= '9') {
    i = i * 10;
    i += *buf - '0';
    ++buf;
  }
  /* test for overflow */
  if (i >= (~((~0ULL) >> 1)) || (*buf >= '0' && *buf <= '9'))
    i = (~0ULL >> 1);
  if (inv)
    i = 0ULL - i;
  if (end)
    *end = buf;
  return i;
}

/** Converts a String to a number using base 16, overflow limited to 113bytes */
static long long http1_atol16(const uint8_t *buf, const uint8_t **end) {
  register unsigned long long i = 0;
  uint8_t inv = 0;
  for (int limit_ = 0;
       (*buf == ' ' || *buf == '\t' || *buf == '\f') && limit_ < 32; ++limit_)
    ++buf;
  for (int limit_ = 0; (*buf == '-' || *buf == '+') && limit_ < 32; ++limit_)
    inv ^= (*(buf++) == '-');
  if (*buf == '0')
    ++buf;
  if ((*buf | 32) == 'x')
    ++buf;
  for (int limit_ = 0; (*buf == '0') && limit_ < 32; ++limit_)
    ++buf;
  while (!(i & (~((~(0ULL)) >> 4)))) {
    if (*buf >= '0' && *buf <= '9') {
      i <<= 4;
      i |= *buf - '0';
    } else if ((*buf | 32) >= 'a' && (*buf | 32) <= 'f') {
      i <<= 4;
      i |= (*buf | 32) - ('a' - 10);
    } else
      break;
    ++buf;
  }
  if (inv)
    i = 0ULL - i;
  if (end)
    *end = buf;
  return i;
}

/* *****************************************************************************
HTTP/1.1 parsre stages
***************************************************************************** */

inline static int http1_consume_response_line(http1_parser_s *parser,
                                              uint8_t *start, uint8_t *end) {
  parser->state.reserved |= HTTP1_P_FLAG_RESPONSE;
  uint8_t *tmp = start;
  if (!seek2ch(&tmp, end, ' '))
    return -1;
  if (http1_on_version(parser, (char *)start, tmp - start))
    return -1;
  tmp = start = tmp + 1;
  if (!seek2ch(&tmp, end, ' '))
    return -1;
  if (http1_on_status(parser, http1_atol(start, NULL), (char *)(tmp + 1),
                      end - tmp))
    return -1;
  return 0;
}

inline static int http1_consume_request_line(http1_parser_s *parser,
                                             uint8_t *start, uint8_t *end) {
  uint8_t *tmp = start;
  uint8_t *host_start = NULL;
  uint8_t *host_end = NULL;
  if (!seek2ch(&tmp, end, ' '))
    return -1;
  if (http1_on_method(parser, (char *)start, tmp - start))
    return -1;
  tmp = start = tmp + 1;
  if (start[0] == 'h' && start[1] == 't' && start[2] == 't' &&
      start[3] == 'p') {
    if (start[4] == ':' && start[5] == '/' && start[6] == '/') {
      /* Request URI is in long form... emulate Host header instead. */
      tmp = host_end = host_start = (start += 7);
    } else if (start[4] == 's' && start[5] == ':' && start[6] == '/' &&
               start[7] == '/') {
      /* Secure request is in long form... emulate Host header instead. */
      tmp = host_end = host_start = (start += 8);
    } else
      goto review_path;
    if (!seek2ch(&tmp, end, ' '))
      return -1;
    *tmp = ' ';
    if (!seek2ch(&host_end, tmp, '/')) {
      if (http1_on_path(parser, (char *)"/", 1))
        return -1;
      goto start_version;
    }
    host_end[0] = '/';
    start = host_end;
  }
review_path:
  tmp = start;
  if (seek2ch(&tmp, end, '?')) {
    if (http1_on_path(parser, (char *)start, tmp - start))
      return -1;
    tmp = start = tmp + 1;
    if (!seek2ch(&tmp, end, ' '))
      return -1;
    if (tmp - start > 0 && http1_on_query(parser, (char *)start, tmp - start))
      return -1;
  } else {
    tmp = start;
    if (!seek2ch(&tmp, end, ' '))
      return -1;
    if (http1_on_path(parser, (char *)start, tmp - start))
      return -1;
  }
start_version:
  start = tmp + 1;
  if (start + 5 >= end) /* require "HTTP/" */
    return -1;
  if (http1_on_version(parser, (char *)start, end - start))
    return -1;
  /* */
  if (host_start && http1_on_header(parser, (char *)"host", 4,
                                    (char *)host_start, host_end - host_start))
    return -1;
  return 0;
}

#ifndef HTTP1_ALLOW_CHUNKED_IN_MIDDLE_OF_HEADER
inline /* inline the function of it's short enough */
#endif
    static int
    http1_consume_header_transfer_encoding(http1_parser_s *parser,
                                           uint8_t *start, uint8_t *end_name,
                                           uint8_t *start_value, uint8_t *end) {
  /* this removes the `chunked` marker and prepares to "unchunk" the data */
  while (start_value < end && (end[-1] == ',' || end[-1] == ' '))
    --end;
  if ((end - start_value) == 7 &&
#if HTTP1_UNALIGNED_MEMORY_ACCESS_ENABLED
      (((uint32_t *)(start_value))[0] | 0x20202020) ==
          ((uint32_t *)"chun")[0] &&
      (((uint32_t *)(start_value + 3))[0] | 0x20202020) ==
          ((uint32_t *)"nked")[0]
#else
      ((start_value[0] | 32) == 'c' && (start_value[1] | 32) == 'h' &&
       (start_value[2] | 32) == 'u' && (start_value[3] | 32) == 'n' &&
       (start_value[4] | 32) == 'k' && (start_value[5] | 32) == 'e' &&
       (start_value[6] | 32) == 'd')
#endif
  ) {
    /* simple case,only `chunked` as a value */
    parser->state.reserved |= HTTP1_P_FLAG_CHUNKED;
    parser->state.content_length = 0;
    start_value += 7;
    while (start_value < end && (*start_value == ',' || *start_value == ' '))
      ++start_value;
    if (!(end - start_value))
      return 0;
  } else if ((end - start_value) > 7 &&
             ((end[(-7 + 0)] | 32) == 'c' && (end[(-7 + 1)] | 32) == 'h' &&
              (end[(-7 + 2)] | 32) == 'u' && (end[(-7 + 3)] | 32) == 'n' &&
              (end[(-7 + 4)] | 32) == 'k' && (end[(-7 + 5)] | 32) == 'e' &&
              (end[(-7 + 6)] | 32) == 'd')) {
    /* simple case,`chunked` at the end of list (RFC required) */
    parser->state.reserved |= HTTP1_P_FLAG_CHUNKED;
    parser->state.content_length = 0;
    end -= 7;
    while (start_value < end && (end[-1] == ',' || end[-1] == ' '))
      --end;
    if (!(end - start_value))
      return 0;
  }
#ifdef HTTP1_ALLOW_CHUNKED_IN_MIDDLE_OF_HEADER /* RFC diisallows this */
  else if ((end - start_value) > 7 && (end - start_value) < 256) {
    /* complex case, `the, chunked, marker, is in the middle of list */
    uint8_t val[256];
    size_t val_len = 0;
    while (start_value < end && val_len < 256) {
      if ((end - start_value) >= 7) {
        if (
#if HTTP1_UNALIGNED_MEMORY_ACCESS_ENABLED
            (((uint32_t *)(start_value))[0] | 0x20202020) ==
                ((uint32_t *)"chun")[0] &&
            (((uint32_t *)(start_value + 3))[0] | 0x20202020) ==
                ((uint32_t *)"nked")[0]
#else
            ((start_value[0] | 32) == 'c' && (start_value[1] | 32) == 'h' &&
             (start_value[2] | 32) == 'u' && (start_value[3] | 32) == 'n' &&
             (start_value[4] | 32) == 'k' && (start_value[5] | 32) == 'e' &&
             (start_value[6] | 32) == 'd')
#endif

        ) {
          parser->state.reserved |= HTTP1_P_FLAG_CHUNKED;
          parser->state.content_length = 0;
          start_value += 7;
          /* skip comma / white space */
          while (start_value < end &&
                 (*start_value == ',' || *start_value == ' '))
            ++start_value;
          continue;
        }
      }
      /* copy value */
      while (start_value < end && val_len < 256 && start_value[0] != ',') {
        val[val_len++] = *start_value;
        ++start_value;
      }
      /* copy comma */
      if (start_value[0] == ',' && val_len < 256) {
        val[val_len++] = *start_value;
        ++start_value;
      }
      /* skip spaces */
      while (start_value < end && start_value[0] == ' ') {
        ++start_value;
      }
    }
    if (val_len < 256) {
      while (start_value < end && val_len < 256) {
        val[val_len++] = *start_value;
        ++start_value;
      }
      val[val_len] = 0;
    }
    /* perform callback with `val` or indicate error */
    if (val_len == 256 ||
        (val_len && http1_on_header(parser, (char *)start, (end_name - start),
                                    (char *)val, val_len)))
      return -1;
    return 0;
  }
#endif /* HTTP1_ALLOW_CHUNKED_IN_MIDDLE_OF_HEADER */
  /* perform callback */
  if (http1_on_header(parser, (char *)start, (end_name - start),
                      (char *)start_value, end - start_value))
    return -1;
  return 0;
}
inline static int http1_consume_header_top(http1_parser_s *parser,
                                           uint8_t *start, uint8_t *end_name,
                                           uint8_t *start_value, uint8_t *end) {
  if ((end_name - start) == 14 &&
#if HTTP1_UNALIGNED_MEMORY_ACCESS_ENABLED && HTTP_HEADERS_LOWERCASE
      *((uint64_t *)start) == *((uint64_t *)"content-") &&
      *((uint64_t *)(start + 6)) == *((uint64_t *)"t-length")
#else
      HEADER_NAME_IS_EQ((char *)start, "content-length", 14)
#endif
  ) {
    /* handle the special `content-length` header */
    if ((parser->state.reserved & HTTP1_P_FLAG_CHUNKED))
      return 0; /* ignore if `chunked` */
    long long old_clen = parser->state.content_length;
    parser->state.content_length = http1_atol(start_value, NULL);
    if ((parser->state.reserved & HTTP1_P_FLAG_CLENGTH) &&
        old_clen != parser->state.content_length) {
      /* content-length header repeated with conflict */
      return -1;
    }
    parser->state.reserved |= HTTP1_P_FLAG_CLENGTH;
  } else if ((end_name - start) == 17 && (end - start_value) >= 7 &&
             !parser->state.content_length &&
#if HTTP1_UNALIGNED_MEMORY_ACCESS_ENABLED && HTTP_HEADERS_LOWERCASE
             *((uint64_t *)start) == *((uint64_t *)"transfer") &&
             *((uint64_t *)(start + 8)) == *((uint64_t *)"-encodin")
#else
             HEADER_NAME_IS_EQ((char *)start, "transfer-encoding", 17)
#endif
  ) {
    /* handle the special `transfer-encoding: chunked` header */
    return http1_consume_header_transfer_encoding(parser, start, end_name,
                                                  start_value, end);
  }
  /* perform callback */
  if (http1_on_header(parser, (char *)start, (end_name - start),
                      (char *)start_value, end - start_value))
    return -1;
  return 0;
}

inline static int http1_consume_header_trailer(http1_parser_s *parser,
                                               uint8_t *start,
                                               uint8_t *end_name,
                                               uint8_t *start_value,
                                               uint8_t *end) {
  if ((end_name - start) > 1 && start[0] == 'x') {
    /* X- headers are allowed */
    goto white_listed;
  }

  /* white listed trailer names */
  const struct {
    char *name;
    long len;
  } http1_trailer_white_list[] = {
      {"server-timing", 13}, /* specific for client data... */
      {NULL, 0},             /* end of list marker */
  };
  for (size_t i = 0; http1_trailer_white_list[i].name; ++i) {
    if ((long)(end_name - start) == http1_trailer_white_list[i].len &&
        HEADER_NAME_IS_EQ((char *)start, http1_trailer_white_list[i].name,
                          http1_trailer_white_list[i].len)) {
      /* header disallowed here */
      goto white_listed;
    }
  }
  return 0;
white_listed:
  /* perform callback */
  if (http1_on_header(parser, (char *)start, (end_name - start),
                      (char *)start_value, end - start_value))
    return -1;
  return 0;
}

inline static int http1_consume_header(http1_parser_s *parser, uint8_t *start,
                                       uint8_t *end) {
  uint8_t *end_name = start;
  /* divide header name from data */
  if (!seek2ch(&end_name, end, ':'))
    return -1;
  if (end_name[-1] == ' ' || end_name[-1] == '\t')
    return -1;
#if HTTP_HEADERS_LOWERCASE
  for (uint8_t *t = start; t < end_name; t++) {
    *t = http_tolower(*t);
  }
#endif
  uint8_t *start_value = end_name + 1;
  // clear away leading white space from value.
  while (start_value < end &&
         (start_value[0] == ' ' || start_value[0] == '\t')) {
    start_value++;
  };
  return (parser->state.read ? http1_consume_header_trailer
                             : http1_consume_header_top)(
      parser, start, end_name, start_value, end);
}

/* *****************************************************************************
HTTP/1.1 Body handling
***************************************************************************** */

inline static int http1_consume_body_streamed(http1_parser_s *parser,
                                              void *buffer, size_t length,
                                              uint8_t **start) {
  uint8_t *end = *start + parser->state.content_length - parser->state.read;
  uint8_t *const stop = ((uint8_t *)buffer) + length;
  if (end > stop)
    end = stop;
  if (end > *start &&
      http1_on_body_chunk(parser, (char *)(*start), end - *start))
    return -1;
  parser->state.read += (end - *start);
  *start = end;
  if (parser->state.content_length <= parser->state.read)
    parser->state.reserved |= HTTP1_P_FLAG_COMPLETE;
  return 0;
}

inline static int http1_consume_body_chunked(http1_parser_s *parser,
                                             void *buffer, size_t length,
                                             uint8_t **start) {
  uint8_t *const stop = ((uint8_t *)buffer) + length;
  uint8_t *end = *start;
  while (*start < stop) {
    if (parser->state.content_length == 0) {
      if (end + 2 >= stop)
        return 0;
      if ((end[0] == '\r' && end[1] == '\n')) {
        /* remove tailing EOL that wasn't processed and retest */
        end += 2;
        *start = end;
        if (end + 2 >= stop)
          return 0;
      }
      long long chunk_len = http1_atol16(end, (const uint8_t **)&end);
      if (end + 2 > stop) /* overflowed? */
        return 0;
      if ((end[0] != '\r' || end[1] != '\n'))
        return -1; /* required EOL after content length */
      end += 2;

      parser->state.content_length = 0 - chunk_len;
      *start = end;
      if (parser->state.content_length == 0) {
        /* all chunked data was parsed */
        /* update content-length */
        parser->state.content_length = parser->state.read;
#ifdef HTTP_ADD_CONTENT_LENGTH_HEADER_IF_MISSING
        { /* add virtual header ... ? */
          char buf[512];
          size_t buf_len = 512;
          size_t tmp_len = parser->state.read;
          buf[--buf_len] = 0;
          while (tmp_len) {
            size_t mod = tmp_len / 10;
            buf[--buf_len] = '0' + (tmp_len - (mod * 10));
            tmp_len = mod;
          }
          if (!(parser->state.reserved & HTTP1_P_FLAG_CLENGTH) &&
              http1_on_header(parser, "content-length", 14,
                              (char *)buf + buf_len, 511 - buf_len)) {
            return -1;
          }
        }
#endif
        /* FIXME: consume trailing EOL */
        if (*start + 2 <= stop && (start[0][0] == '\r' || start[0][0] == '\n'))
          *start += 1 + (start[0][1] == '\r' || start[0][1] == '\n');
        else {
          /* remove the "headers complete" and "trailer" flags */
          parser->state.reserved =
              HTTP1_P_FLAG_STATUS_LINE | HTTP1_P_FLAG_CLENGTH;
          return -2;
        }
        /* the parsing complete flag */
        parser->state.reserved |= HTTP1_P_FLAG_COMPLETE;
        return 0;
      }
    }
    end = *start + (0 - parser->state.content_length);
    if (end > stop)
      end = stop;
    if (end > *start &&
        http1_on_body_chunk(parser, (char *)(*start), end - *start)) {
      return -1;
    }
    parser->state.read += (end - *start);
    parser->state.content_length += (end - *start);
    *start = end;
  }
  return 0;
}

inline static int http1_consume_body(http1_parser_s *parser, void *buffer,
                                     size_t length, uint8_t **start) {
  if (parser->state.content_length > 0 &&
      parser->state.content_length > parser->state.read) {
    /* normal, streamed data */
    return http1_consume_body_streamed(parser, buffer, length, start);
  } else if (parser->state.content_length <= 0 &&
             (parser->state.reserved & HTTP1_P_FLAG_CHUNKED)) {
    /* chuncked encoding */
    return http1_consume_body_chunked(parser, buffer, length, start);
  } else {
    /* nothing to do - parsing complete */
    parser->state.reserved |= HTTP1_P_FLAG_COMPLETE;
  }
  return 0;
}

/* *****************************************************************************
HTTP/1.1 parsre function
***************************************************************************** */
#if DEBUG
#include <assert.h>
#define HTTP1_ASSERT assert
#else
#define HTTP1_ASSERT(...)
#endif

/**
 * Returns the amount of data actually consumed by the parser.
 *
 * The value 0 indicates there wasn't enough data to be parsed and the same
 * buffer (with more data) should be resubmitted.
 *
 * A value smaller than the buffer size indicates that EITHER a request /
 * response was detected OR that the leftover could not be consumed because more
 * data was required.
 *
 * Simply resubmit the reminder of the data to continue parsing.
 *
 * A request / response callback automatically stops the parsing process,
 * allowing the user to adjust or refresh the state of the data.
 */
static size_t http1_parse(http1_parser_s *parser, void *buffer, size_t length) {
  if (!length)
    return 0;
  HTTP1_ASSERT(parser && buffer);
  parser->state.next = NULL;
  uint8_t *start = (uint8_t *)buffer;
  uint8_t *end = start;
  uint8_t *const stop = start + length;
  uint8_t eol_len = 0;
#define HTTP1_CONSUMED ((size_t)((uintptr_t)start - (uintptr_t)buffer))

re_eval:
  switch ((parser->state.reserved & 7)) {

  case 0: /* request / response line */
    /* clear out any leading white space */
    while ((start < stop) &&
           (*start == '\r' || *start == '\n' || *start == ' ' || *start == 0)) {
      ++start;
    }
    end = start;
    /* make sure the whole line is available*/
    if (!(eol_len = seek2eol(&end, stop)))
      return HTTP1_CONSUMED;

    if (start[0] == 'H' && start[1] == 'T' && start[2] == 'T' &&
        start[3] == 'P') {
      /* HTTP response */
      if (http1_consume_response_line(parser, start, end - eol_len + 1))
        goto error;
    } else if (http_tolower(start[0]) >= 'a' && http_tolower(start[0]) <= 'z') {
      /* HTTP request */
      if (http1_consume_request_line(parser, start, end - eol_len + 1))
        goto error;
    } else
      goto error;
    end = start = end + 1;
    parser->state.reserved |= HTTP1_P_FLAG_STATUS_LINE;

  /* fallthrough */
  case 1: /* headers */
    do {
      if (start >= stop)
        return HTTP1_CONSUMED; /* buffer ended on header line */
      if (*start == '\r' || *start == '\n') {
        goto finished_headers; /* empty line, end of headers */
      }
      end = start;
      if (!(eol_len = seek2eol(&end, stop)))
        return HTTP1_CONSUMED;
      if (http1_consume_header(parser, start, end - eol_len + 1))
        goto error;
      end = start = end + 1;
    } while ((parser->state.reserved & HTTP1_P_FLAG_HEADER_COMPLETE) == 0);
  finished_headers:
    ++start;
    if (*start == '\n')
      ++start;
    end = start;
    parser->state.reserved |= HTTP1_P_FLAG_HEADER_COMPLETE;
  /* fallthrough */
  case (HTTP1_P_FLAG_HEADER_COMPLETE | HTTP1_P_FLAG_STATUS_LINE):
    /* request body */
    {
      int t3 = http1_consume_body(parser, buffer, length, &start);
      switch (t3) {
      case -1:
        goto error;
      case -2:
        goto re_eval;
      }
      break;
    }
  }
  /* are we done ? */
  if (parser->state.reserved & HTTP1_P_FLAG_COMPLETE) {
    parser->state.next = start;
    if (((parser->state.reserved & HTTP1_P_FLAG_RESPONSE)
             ? http1_on_response
             : http1_on_request)(parser))
      goto error;
    parser->state = (struct http1_parser_protected_read_only_state_s){0};
  }
  return HTTP1_CONSUMED;
error:
  http1_on_error(parser);
  parser->state = (struct http1_parser_protected_read_only_state_s){0};
  return length;
#undef HTTP1_CONSUMED
}

#endif
