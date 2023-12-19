/*
Copyright: Boaz Segev, 2018-2019
License: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#ifndef H_HTTP_MIME_PARSER_H
#define H_HTTP_MIME_PARSER_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* *****************************************************************************
Known Limitations:

- Doesn't support nested multipart form structures (i.e., multi-file selection).
  See: https://www.w3.org/TR/html401/interact/forms.html#h-17.13.4.2

To circumvent limitation, initialize a new parser to parse nested multiparts.
***************************************************************************** */

/* *****************************************************************************
The HTTP MIME Multipart Form Parser Type
***************************************************************************** */

/** all data id read-only / for internal use */
typedef struct {
  char *boundary;
  size_t boundary_len;
  uint8_t in_obj;
  uint8_t done;
  uint8_t error;
} http_mime_parser_s;

/* *****************************************************************************
Callbacks to be implemented.
***************************************************************************** */

/** Called when all the data is available at once. */
static void http_mime_parser_on_data(http_mime_parser_s *parser, void *name,
                                     size_t name_len, void *filename,
                                     size_t filename_len, void *mimetype,
                                     size_t mimetype_len, void *value,
                                     size_t value_len);

/** Called when the data didn't fit in the buffer. Data will be streamed. */
static void http_mime_parser_on_partial_start(
    http_mime_parser_s *parser, void *name, size_t name_len, void *filename,
    size_t filename_len, void *mimetype, size_t mimetype_len);

/** Called when partial data is available. */
static void http_mime_parser_on_partial_data(http_mime_parser_s *parser,
                                             void *value, size_t value_len);

/** Called when the partial data is complete. */
static void http_mime_parser_on_partial_end(http_mime_parser_s *parser);

/**
 * Called when URL decoding is required.
 *
 * Should support inplace decoding (`dest == encoded`).
 *
 * Should return the length of the decoded string.
 */
static size_t http_mime_decode_url(char *dest, const char *encoded,
                                   size_t length);

/* *****************************************************************************
API
***************************************************************************** */

/**
 * Takes the HTTP Content-Type header and initializes the parser data.
 *
 * Note: the Content-Type header should persist in memory while the parser is in
 * use.
 */
static int http_mime_parser_init(http_mime_parser_s *parser, char *content_type,
                                 size_t len);

/**
 * Consumes data from a streaming buffer.
 *
 * The data might be partially consumed, in which case the unconsumed data
 * should be resent to the parser as more data becomes available.
 *
 * Note: test the `parser->done` and `parser->error` flags between iterations.
 */
static size_t http_mime_parse(http_mime_parser_s *parser, void *buffer,
                              size_t length);

/* *****************************************************************************
Implementations
***************************************************************************** */

/** takes the HTTP Content-Type header and initializes the parser data. */
static int http_mime_parser_init(http_mime_parser_s *parser, char *content_type,
                                 size_t len) {
  *parser = (http_mime_parser_s){.done = 0};
  if (len < 14 || strncasecmp("multipart/form", content_type, 14))
    return -1;
  char *cut = memchr(content_type, ';', len);
  while (cut) {
    ++cut;
    len -= (size_t)(cut - content_type);
    while (len && cut[0] == ' ') {
      --len;
      ++cut;
    }
    if (len <= 9)
      return -1;
    if (strncasecmp("boundary=", cut, 9)) {
      content_type = cut;
      cut = memchr(cut, ';', len);
      continue;
    }
    cut += 9;
    len -= 9;
    content_type = cut;
    parser->boundary = content_type;
    if ((cut = memchr(content_type, ';', len)))
      parser->boundary_len = (size_t)(cut - content_type);
    else
      parser->boundary_len = len;
    return 0;
  }
  return -1;
}

/**
 * Consumes data from a streaming buffer.
 *
 * The data might be partially consumed, in which case the unconsumed data
 * should be resent to the parser as more data becomes available.
 *
 * Note: test the `parser->done` and `parser->error` flags between iterations.
 */
static size_t http_mime_parse(http_mime_parser_s *parser, void *buffer,
                              size_t length) {
  int first_run = 1;
  char *pos = buffer;
  const char *stop = pos + length;
  if (!length)
    goto end_of_data;
consume_partial:
  if (parser->in_obj) {
    /* we're in an object longer than the buffer */
    char *start = pos;
    char *end = start;
    do {
      end = memchr(end, '\n', (size_t)(stop - end));
    } while (end && ++end &&
             (size_t)(stop - end) >= (4 + parser->boundary_len) &&
             (end[0] != '-' || end[1] != '-' ||
              memcmp(end + 2, parser->boundary, parser->boundary_len)));
    if (!end) {
      end = (char *)stop;
      pos = end;
      if (end - start)
        http_mime_parser_on_partial_data(parser, start, (size_t)(end - start));
      goto end_of_data;
    } else if (end + 4 + parser->boundary_len >= stop) {
      end -= 2;
      if (end[0] == '\r')
        --end;
      pos = end;
      if (end - start)
        http_mime_parser_on_partial_data(parser, start, (size_t)(end - start));
      goto end_of_data;
    }
    size_t len = (end - start) - 1;
    if (start[len - 1] == '\r')
      --len;
    if (len)
      http_mime_parser_on_partial_data(parser, start, len);
    http_mime_parser_on_partial_end(parser);
    pos = end;
    parser->in_obj = 0;
    first_run = 0;
  } else if (length < (4 + parser->boundary_len) || pos[0] != '-' ||
             pos[1] != '-' ||
             memcmp(pos + 2, parser->boundary, parser->boundary_len))
    goto error;
  /* We're at a boundary */
  while (pos < stop) {
    char *start;
    char *end;
    char *name = NULL;
    uint32_t name_len = 0;
    char *value = NULL;
    uint32_t value_len = 0;
    char *filename = NULL;
    uint32_t filename_len = 0;
    char *mime = NULL;
    uint32_t mime_len = 0;
    uint8_t header_count = 0;
    /* test for ending */
    if (pos[2 + parser->boundary_len] == '-' &&
        pos[3 + parser->boundary_len] == '-') {
      pos += 5 + parser->boundary_len;
      if (pos > stop)
        pos = (char *)stop;
      else if (pos < stop && pos[0] == '\n')
        ++pos;
      goto done;
    }
    start = pos + 3 + parser->boundary_len;
    if (start[0] == '\n') {
      /* should be true, unless new line marker was just '\n' */
      ++start;
    }
    /* consume headers */
    while (start + 4 < stop && start[0] != '\n' && start[1] != '\n') {
      end = memchr(start, '\n', (size_t)(stop - start));
      if (!end) {
        if (first_run)
          goto error;
        goto end_of_data;
      }
      if (end - start > 29 && !strncasecmp(start, "content-disposition:", 20)) {
        /* content-disposition header */
        start = memchr(start + 20, ';', end - (start + 20));
        // if (!start)
        //   start = end + 1;
        while (start) {
          ++start;
          if (start[0] == ' ')
            ++start;
          if (start + 6 < end && !strncasecmp(start, "name=", 5)) {
            name = start + 5;
            if (name[0] == '"')
              ++name;
            start = memchr(name, ';', (size_t)(end - start));
            if (!start) {
              name_len = (size_t)(end - name);
              if (name[name_len - 1] == '\r')
                --name_len;
            } else {
              name_len = (size_t)(start - name);
            }
            if (name[name_len - 1] == '"')
              --name_len;
          } else if (start + 9 < end && !strncasecmp(start, "filename", 8)) {
            uint8_t encoded = 0;
            start += 8;
            if (start[0] == '*') {
              encoded = 1;
              ++start;
            }
            if (start[0] != '=')
              goto error;
            ++start;
            if (start[0] == ' ')
              ++start;
            if (start[0] == '"')
              ++start;
            if (filename && !encoded) {
              /* prefer URL encoded version */
              start = memchr(filename, ';', (size_t)(end - start));
              continue;
            }
            filename = start;
            start = memchr(filename, ';', (size_t)(end - start));
            if (!start) {
              filename_len = (size_t)((end - filename));
              if (filename[filename_len - 1] == '\r') {
                --filename_len;
              }
            } else {
              filename_len = (size_t)(start - filename);
            }
            if (filename[filename_len - 1] == '"')
              --filename_len;
            if (encoded) {
              ssize_t new_len =
                  http_mime_decode_url(filename, filename, filename_len);
              if (new_len > 0)
                filename_len = new_len;
            }
          } else {
            start = memchr(start, ';', (size_t)(end - start));
          }
        }
      } else if (end - start > 14 && !strncasecmp(start, "content-type:", 13)) {
        /* content-type header */
        start += 13;
        if (start[0] == ' ')
          ++start;
        mime = start;
        start = memchr(start, ';', (size_t)(end - start));
        if (!start) {
          mime_len = (size_t)(end - mime);
          if (mime[mime_len - 1] == '\r')
            --mime_len;
        } else {
          mime_len = (size_t)(start - mime);
        }
      }
      start = end + 1;
      if (header_count++ > 4)
        goto error;
    }
    if (!name) {
      if (start + 4 >= stop)
        goto end_of_data;
      goto error;
    }

    /* advance to end of boundry */
    ++start;
    if (start[0] == '\n')
      ++start;
    value = start;
    end = start;
    do {
      end = memchr(end, '\n', (size_t)(stop - end));
    } while (end && ++end &&
             (size_t)(stop - end) >= (4 + parser->boundary_len) &&
             (end[0] != '-' || end[1] != '-' ||
              memcmp(end + 2, parser->boundary, parser->boundary_len)));
    if (!end || end + 4 + parser->boundary_len >= stop) {
      if (first_run) {
        http_mime_parser_on_partial_start(parser, name, name_len, filename,
                                          filename_len, mime, mime_len);
        parser->in_obj = 1;
        pos = value;
        goto consume_partial;
      }
      goto end_of_data;
    }
    value_len = (size_t)((end - value) - 1);
    if (value[value_len - 1] == '\r')
      --value_len;
    pos = end;
    http_mime_parser_on_data(parser, name, name_len, filename, filename_len,
                             mime, mime_len, value, value_len);
    first_run = 0;
  }
end_of_data:
  return (size_t)((uintptr_t)pos - (uintptr_t)buffer);
done:
  parser->done = 1;
  parser->error = 0;
  return (size_t)((uintptr_t)pos - (uintptr_t)buffer);
error:
  parser->done = 0;
  parser->error = 1;
  return (size_t)((uintptr_t)pos - (uintptr_t)buffer);
}
#endif
