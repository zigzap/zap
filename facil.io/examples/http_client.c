#include "fio_cli.h"
#include "http.h"

static void on_response(http_s *h);

int main(int argc, char const *argv[]) {
  fio_cli_start(
      argc, argv, 1, 1,
      "This is an HTTP client example, use:\n"
      "\n\tfioapp http://example.com/foo\n",
      FIO_CLI_STRING("-unix -u Unix Socket address (has no place in url)."));
  http_connect(fio_cli_unnamed(0), fio_cli_get("-u"),
               .on_response = on_response);
  fio_start(.threads = 1);
  return 0;
}

static void on_response(http_s *h) {
  if (h->status_str == FIOBJ_INVALID) {
    /* first response is always empty, nothing was sent yet */
    http_finish(h);
    return;
  }
  /* Second response is actual response */
  FIOBJ r = http_req2str(h);
  fprintf(stderr, "%s\n", fiobj_obj2cstr(r).data);
  fio_stop();
}
