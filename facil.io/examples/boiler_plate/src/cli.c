#include <stdlib.h>
#include <string.h>

#include "cli.h"

#include "fio.h"
#include "fio_cli.h"
#include "http.h"
#include "redis_engine.h"

static void redis_cleanup(void *e_) {
  redis_engine_destroy(e_);
  FIO_LOG_DEBUG("Cleaned up redis engine object.");
  FIO_PUBSUB_DEFAULT = FIO_PUBSUB_CLUSTER;
}

void initialize_cli(int argc, char const *argv[]) {
  /*     ****  Command line arguments ****     */
  fio_cli_start(
      argc, argv, 0, 0, NULL, FIO_CLI_PRINT_HEADER("Address binding:"),
      FIO_CLI_INT("-port -p port number to listen to. defaults port 3000"),
      FIO_CLI_STRING("-bind -b address to listen to. defaults any available."),
      FIO_CLI_PRINT_HEADER("Concurrency:"),
      FIO_CLI_INT("-workers -w number of processes to use."),
      FIO_CLI_INT("-threads -t number of threads per process."),
      FIO_CLI_PRINT_HEADER("HTTP Server:"),
      FIO_CLI_STRING("-public -www public folder, for static file service."),
      FIO_CLI_INT(
          "-keep-alive -k HTTP keep-alive timeout (0..255). default: ~5s"),
      FIO_CLI_INT("-max-body -maxbd HTTP upload limit. default: ~50Mb"),
      FIO_CLI_BOOL("-log -v request verbosity (logging)."),
      FIO_CLI_PRINT_HEADER("WebSocket Server:"),
      FIO_CLI_INT("-ping websocket ping interval (0..255). default: ~40s"),
      FIO_CLI_INT("-max-msg -maxms incoming websocket message size limit. "
                  "default: ~250Kb"),
      FIO_CLI_PRINT_HEADER("Redis support:"),
      FIO_CLI_STRING("-redis -r an optional Redis URL server address."),
      FIO_CLI_PRINT("\t\ti.e.: redis://user:password@localhost:6379/"));

  /* Test and set any default options */
  if (!fio_cli_get("-b")) {
    char *tmp = getenv("ADDRESS");
    if (tmp) {
      fio_cli_set("-b", tmp);
      fio_cli_set("-bind", tmp);
    }
  }
  if (!fio_cli_get("-p")) {
    /* Test environment as well and make sure address is missing */
    char *tmp = getenv("PORT");
    if (!tmp && !fio_cli_get("-b"))
      tmp = "3000";
    /* Set default (unlike cmd line arguments, aliases are manually set) */
    fio_cli_set("-p", tmp);
    fio_cli_set("-port", tmp);
  }
  if (!fio_cli_get("-public")) {
    char *tmp = getenv("HTTP_PUBLIC_FOLDER");
    if (tmp) {
      fio_cli_set("-public", tmp);
      fio_cli_set("-www", tmp);
    }
  }

  if (!fio_cli_get("-redis")) {
    char *tmp = getenv("REDIS_URL");
    if (tmp) {
      fio_cli_set("-redis", tmp);
      fio_cli_set("-r", tmp);
    }
  }

  if (fio_cli_get("-redis") && strlen(fio_cli_get("-redis"))) {
    FIO_LOG_INFO("* Initializing Redis connection to %s\n",
                 fio_cli_get("-redis"));
    http_url_s info =
        http_url_parse(fio_cli_get("-redis"), strlen(fio_cli_get("-redis")));
    fio_pubsub_engine_s *e =
        redis_engine_create(.address = info.host, .port = info.port,
                            .auth = info.password);
    if (e) {
      fio_state_callback_add(FIO_CALL_ON_FINISH, redis_cleanup, e);
      FIO_PUBSUB_DEFAULT = e;
    } else {
      FIO_LOG_ERROR("Failed to create redis engine object.");
    }
  }
}

void free_cli(void) { fio_cli_end(); }
