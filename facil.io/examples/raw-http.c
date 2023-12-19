/**
In this example we will author an HTTP server with a smaller memory footprint
and a simplified design.

This simplified design gains performance at the price of ease of use and
flexibility.

This design can be effective for some applications, however it suffers form a
rigid HTTP header limit and a harder to use data structure.

Try compiling with wide-spread `poll` engine instead of `kqueue` or `epoll`:

    FIO_POLL=1 NAME=http make

Run with:

    ./tmp/http -t 1

Benchmark with keep-alive:

    ab -c 200 -t 4 -n 1000000 -k http://127.0.0.1:3000/
    wrk -c200 -d4 -t1 http://localhost:3000/

Benchmark with higher load:

    ab -c 4400 -t 4 -n 1000000 -k http://127.0.0.1:3000/
    wrk -c4400 -d4 -t1 http://localhost:3000/
*/

/* include the core library, without any extensions */
#include <fio.h>

/* use the fio_str_s helpers */
#define FIO_INCLUDE_STR
#include <fio.h>

/* use the CLI extension */
#include <fio_cli.h>

// #include "http.h" /* for date/time helper */
#include "http1_parser.h"

/* our header buffer size */
#define MAX_HTTP_HEADER_LENGTH 16384
#define MIN_HTTP_READFILE 4096
/* our header count */
#define MAX_HTTP_HEADER_COUNT 64
/* our HTTP POST limits */
#define MAX_HTTP_BODY_MAX 524288

/* *****************************************************************************
The Protocol Data Structure
***************************************************************************** */

typedef struct {
  fio_protocol_s protocol; /* all protocols must use this callback structure */
  intptr_t uuid; /* this will hold the connection's uuid for `sock` functions */
  http1_parser_s parser; /* the HTTP/1.1 parser */
  char *method;          /* the HTTP method, NUL terminated */
  char *path;            /* the URI path, NUL terminated */
  char *query; /* the URI query (after the '?'), if any, NUL terminated */
  char *http_version;    /* the HTTP version, NUL terminated */
  size_t content_length; /* the body's content length, if any */
  size_t header_count; /* the header count - everything after this is garbage */
  char *headers[MAX_HTTP_HEADER_COUNT];
  char *values[MAX_HTTP_HEADER_COUNT];
  fio_str_s body; /* the HTTP body, this is where a little complexity helps */
  size_t buf_reader; /* internal: marks the read position in the buffer */
  size_t buf_writer; /* internal: marks the write position in the buffer */
  uint8_t reset; /* used internally to mark when some buffer can be deleted */
} light_http_s;

/* turns a parser pointer into a `light_http_s` pointer using it's offset */
#define parser2pr(parser)                                                      \
  ((light_http_s *)((uintptr_t)(parser) -                                      \
                    (uintptr_t)(&((light_http_s *)(0))->parser)))

void light_http_send_response(intptr_t uuid, int status,
                              fio_str_info_s status_str, size_t header_count,
                              fio_str_info_s headers[][2], fio_str_info_s body);
/* *****************************************************************************
The HTTP/1.1 Request Handler - change this to whateve you feel like.
***************************************************************************** */

int on_http_request(light_http_s *http) {
  /* handle a request for `http->path` */
  if (1) {
    /* a simple, hardcoded HTTP/1.1 response */
    static char HTTP_RESPONSE[] = "HTTP/1.1 200 OK\r\n"
                                  "Content-Length: 13\r\n"
                                  "Connection: keep-alive\r\n"
                                  "Content-Type: text/plain\r\n"
                                  "\r\n"
                                  "Hello Wolrld!";
    fio_write2(http->uuid, .data.buffer = HTTP_RESPONSE,
               .length = sizeof(HTTP_RESPONSE) - 1,
               .after.dealloc = FIO_DEALLOC_NOOP);
  } else {
    /* an allocated, dynamic, HTTP/1.1 response */
    light_http_send_response(
        http->uuid, 200, (fio_str_info_s){.len = 2, .data = "OK"}, 1,
        (fio_str_info_s[][2]){{{.len = 12, .data = "Content-Type"},
                               {.len = 10, .data = "text/plain"}}},
        (fio_str_info_s){.len = 13, .data = "Hello Wolrld!"});
  }
  return 0;
}

/* *****************************************************************************
Listening for Connections (main)
***************************************************************************** */

/* we're referencing this function, but defining it later on. */
void light_http_on_open(intptr_t uuid, void *udata);

/* our main function / starting point */
int main(int argc, char const *argv[]) {
  /* A simple CLI interface. */
  fio_cli_start(argc, argv, 0, 0,
                "Custom HTTP example for the facil.io framework.",
                FIO_CLI_INT("-port -p Port to bind to. Default: 3000"),
                FIO_CLI_INT("-workers -w Number of workers (processes)."),
                FIO_CLI_INT("-threads -t Number of threads."));
  /* Default to port 3000. */
  fio_cli_set_default("-p", "3000");
  /* Default to single thread. */
  fio_cli_set_default("-t", "1");
  /* try to listen on port 3000. */
  if (fio_listen(.port = fio_cli_get("-p"), .address = NULL,
                 .on_open = light_http_on_open, .udata = NULL) == -1)
    perror("FATAL ERROR: Couldn't open listening socket"), exit(errno);
  /* run facil with 1 working thread - this blocks until we're done. */
  fio_start(.threads = fio_cli_get_i("-t"), .workers = fio_cli_get_i("-w"));
  /* clean up */
  fio_cli_end();
  return 0;
}

/* *****************************************************************************
The HTTP/1.1 Parsing Callbacks - we need to implememnt everything for the parser
***************************************************************************** */

/** called when a request was received. */
int light_http1_on_request(http1_parser_s *parser) {
  int ret = on_http_request(parser2pr(parser));
  fio_str_free(&parser2pr(parser)->body);
  parser2pr(parser)->reset = 1;
  return ret;
}

/** called when a response was received, this is for HTTP clients (error). */
int light_http1_on_response(http1_parser_s *parser) {
  return -1;
  (void)parser;
}

/** called when a request method is parsed. */
int light_http1_on_method(http1_parser_s *parser, char *method,
                          size_t method_len) {
  parser2pr(parser)->method = method;
  return 0;
  (void)method_len;
}

/** called when a response status is parsed. the status_str is the string
 * without the prefixed numerical status indicator.*/
int light_http1_on_status(http1_parser_s *parser, size_t status,
                          char *status_str, size_t len) {
  return -1;
  (void)parser;
  (void)status;
  (void)status_str;
  (void)len;
}
/** called when a request path (excluding query) is parsed. */
int light_http1_on_path(http1_parser_s *parser, char *path, size_t path_len) {
  parser2pr(parser)->path = path;
  return 0;
  (void)path_len;
}
/** called when a request path (excluding query) is parsed. */
int light_http1_on_query(http1_parser_s *parser, char *query,
                         size_t query_len) {
  parser2pr(parser)->query = query;
  return 0;
  (void)query_len;
}
/** called when a the HTTP/1.x version is parsed. */
int light_http1_on_http_version(http1_parser_s *parser, char *version,
                                size_t len) {
  parser2pr(parser)->http_version = version;
  return 0;
  (void)len;
}
/** called when a header is parsed. */
int light_http1_on_header(http1_parser_s *parser, char *name, size_t name_len,
                          char *data, size_t data_len) {
  if (parser2pr(parser)->header_count >= MAX_HTTP_HEADER_COUNT)
    return -1;
  parser2pr(parser)->headers[parser2pr(parser)->header_count] = name;
  parser2pr(parser)->values[parser2pr(parser)->header_count] = data;
  ++parser2pr(parser)->header_count;
  return 0;
  (void)name_len;
  (void)data_len;
}

/** called when a body chunk is parsed. */
int light_http1_on_body_chunk(http1_parser_s *parser, char *data,
                              size_t data_len) {
  if (parser->state.content_length >= MAX_HTTP_BODY_MAX)
    return -1;
  if (fio_str_write(&parser2pr(parser)->body, data, data_len).len >=
      MAX_HTTP_BODY_MAX)
    return -1;
  return 0;
}

/** called when a protocol error occurred. */
int light_http1_on_error(http1_parser_s *parser) {
  /* close the connection */
  fio_close(parser2pr(parser)->uuid);
  return 0;
}
/* *****************************************************************************
The Protocol Callbacks
***************************************************************************** */

/* facil.io callbacks we want to handle */
void light_http_on_open(intptr_t uuid, void *udata);
void light_http_on_data(intptr_t uuid, fio_protocol_s *pr);
void light_http_on_close(intptr_t uuid, fio_protocol_s *pr);

/* this will be called when a connection is opened. */
void light_http_on_open(intptr_t uuid, void *udata) {
  /*
   * we should allocate a protocol object for this connection.
   *
   * since protocol objects are stateful (the parsing, internal locks, etc'), we
   * need a different protocol object per connection.
   *
   * NOTE: the extra length in the memory will be the R/W buffer.
   */
  light_http_s *p =
      malloc(sizeof(*p) + MAX_HTTP_HEADER_LENGTH + MIN_HTTP_READFILE);
  *p = (light_http_s){
      .protocol.on_data = light_http_on_data,   /* setting the data callback */
      .protocol.on_close = light_http_on_close, /* setting the close callback */
      .uuid = uuid,
      .body = FIO_STR_INIT,
  };
  /* timeouts are important. timeouts are in seconds. */
  fio_timeout_set(uuid, 5);
  /*
   * this is a very IMPORTANT function call,
   * it attaches the protocol to the socket.
   */
  fio_attach(uuid, &p->protocol);
  /* the `udata` wasn't used, but it's good for dynamic settings and such */
  (void)udata;
}

/* this will be called when the connection has incoming data. */
void light_http_on_data(intptr_t uuid, fio_protocol_s *pr) {
  /* We will read some / all of the data */
  light_http_s *h = (light_http_s *)pr;
  ssize_t tmp =
      fio_read(uuid, (char *)(h + 1) + h->buf_writer,
               (MAX_HTTP_HEADER_LENGTH + MIN_HTTP_READFILE) - h->buf_writer);
  if (tmp <= 0) {
    /* reading failed, we're done. */
    return;
  }
  h->buf_writer += tmp;
  /* feed the parser until it's done consuminng data. */
  do {
    tmp = http1_fio_parser(.parser = &h->parser,
                           .buffer = (char *)(h + 1) + h->buf_reader,
                           .length = h->buf_writer - h->buf_reader,
                           .on_request = light_http1_on_request,
                           .on_response = light_http1_on_response,
                           .on_method = light_http1_on_method,
                           .on_status = light_http1_on_status,
                           .on_path = light_http1_on_path,
                           .on_query = light_http1_on_query,
                           .on_http_version = light_http1_on_http_version,
                           .on_header = light_http1_on_header,
                           .on_body_chunk = light_http1_on_body_chunk,
                           .on_error = light_http1_on_error);
    if (fio_str_len(&h->body)) {
      /* when reading to a body, the data is copied */
      /* keep the reading position at buf_reader. */
      h->buf_writer -= tmp;
      if (h->buf_writer != h->buf_reader) {
        /* some data wasn't processed, move it to the writer's position*/
        memmove((char *)(h + 1) + h->buf_reader,
                (char *)(h + 1) + h->buf_reader + tmp,
                h->buf_writer - h->buf_reader);
      }
    } else {
      /* since we didn't copy the data, we need to move the reader forward */
      h->buf_reader += tmp;
      if (h->reset) {
        h->header_count = 0;
        /* a request just finished, move the reader back to 0... */
        /* and test for HTTP pipelinig. */
        h->buf_writer -= h->buf_reader;
        if (h->buf_writer) {
          memmove((char *)(h + 1), (char *)(h + 1) + h->buf_reader,
                  h->buf_writer);
        }
        h->buf_reader = 0;
      }
    }
  } while ((size_t)tmp);
}

/* this will be called when the connection is closed. */
void light_http_on_close(intptr_t uuid, fio_protocol_s *pr) {
  /* in case we lost connection midway */
  fio_str_free(&((light_http_s *)pr)->body);
  /* free our protocol data and resources */
  free(pr);
  (void)uuid;
}

/* *****************************************************************************
Fast HTTP response handling
***************************************************************************** */

void light_http_send_response(intptr_t uuid, int status,
                              fio_str_info_s status_str, size_t header_count,
                              fio_str_info_s headers[][2],
                              fio_str_info_s body) {
  static size_t date_len = 0; /* TODO: implement a date header when missing */

  size_t total_len = 9 + 4 + 15 + 20 /* max content length */ + 2 +
                     status_str.len + 2 + date_len + 7 + 2 + body.len;
  for (size_t i = 0; i < header_count; ++i) {
    total_len += headers[i][0].len + 1 + headers[i][1].len + 2;
  }
  if (status < 100 || status > 999)
    status = 500;
  fio_str_s *response = fio_str_new2();
  fio_str_capa_assert(response, total_len);
  fio_str_write(response, "HTTP/1.1 ", 9);
  fio_str_write_i(response, status);
  fio_str_write(response, status_str.data, status_str.len);
  fio_str_write(response, "\r\nContent-Length:", 17);
  fio_str_write_i(response, body.len);
  fio_str_write(response, "\r\n", 2);

  // memcpy(pos, "Date:", 5);
  // pos += 5;
  // pos += http_time2str(pos, facil_last_tick().tv_sec);
  // *pos++ = '\r';
  // *pos++ = '\n';

  for (size_t i = 0; i < header_count; ++i) {
    fio_str_write(response, headers[i][0].data, headers[i][0].len);
    fio_str_write(response, ":", 1);
    fio_str_write(response, headers[i][1].data, headers[i][1].len);
    fio_str_write(response, "\r\n", 2);
  }
  fio_str_write(response, "\r\n", 2);
  if (body.len && body.data)
    fio_str_write(response, body.data, body.len);
  fio_str_send_free2(uuid, response);
}
