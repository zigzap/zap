#include "main.h"

int main(int argc, char const *argv[]) {
  /* accept command line arguments and setup default values, see "cli.c" */
  initialize_cli(argc, argv);

  /* initialize HTTP service, see "http_service.h" */
  initialize_http_service();

  /* start facil */
  fio_start(.threads = fio_cli_get_i("-t"), .workers = fio_cli_get_i("-w"));

  /* cleanup CLI, see "cli.c" */
  free_cli();
  return 0;
}
