
#include "tests/mustache.c.h"

#include <fio.h>
#include <fiobj.h>
#include <http.h>

#include "resp_parser.h"

void resp_test(void);

int main(void) {
  fio_test();
  mustache_test();
  fiobj_test();
  http_tests();
  resp_test();
}

void resp_test(void) {
  const char OK[] = "+OK\r\n";
  const char array_x3_i[] = "*3\r\n$3\r\nfoo\r\n$-1\r\n$3\r\nbar\r\n:-42\r\n";
  resp_parser_s parser;
  memset(&parser, 0, sizeof(parser));
  FIO_ASSERT(!resp_parse(&parser, OK, sizeof(OK) - 1),
             "RESP parser didn't parse the whole of the OK response data.");
  FIO_ASSERT(!resp_parse(&parser, array_x3_i, sizeof(array_x3_i) - 1),
             "RESP parser didn't parse the whole of the Array response data "
             "(or parsed more).");
}

static int resp_on_number(resp_parser_s *parser, int64_t num) {
  fprintf(stderr, "%ld\n", (long)num);
  (void)parser;
  return 0;
}

static int resp_on_okay(resp_parser_s *parser) {
  fprintf(stderr, "OK\n");
  (void)parser;
  return 0;
}
static int resp_on_null(resp_parser_s *parser) {
  fprintf(stderr, "NULL\n");
  (void)parser;
  return 0;
}

static int resp_on_start_string(resp_parser_s *parser, size_t str_len) {
  fprintf(stderr, "starting string %ld long\n", (long)str_len);
  (void)parser;
  return 0;
}
static int resp_on_string_chunk(resp_parser_s *parser, void *data, size_t len) {
  fprintf(stderr, "%.*s", (int)len, (char *)data);
  (void)parser;
  return 0;
}
static int resp_on_end_string(resp_parser_s *parser) {
  fprintf(stderr, "\n");
  (void)parser;
  return 0;
}

static int resp_on_err_msg(resp_parser_s *parser, void *data, size_t len) {
  fprintf(stderr, "Error message: %.*s\n", (int)len, (char *)data);
  (void)parser;
  return 0;
}

static int resp_on_start_array(resp_parser_s *parser, size_t array_len) {
  fprintf(stderr, "starting array with %ld items\n", (long)array_len);
  (void)parser;
  return 0;
}

static int resp_on_message(resp_parser_s *parser) {
  fprintf(stderr, "--- complete message ---\n");
  (void)parser;
  return 0;
}

static int resp_on_parser_error(resp_parser_s *parser) {
  fprintf(stderr, "--- PARSER ERROR ---\n");
  (void)parser;
  return 0;
}
