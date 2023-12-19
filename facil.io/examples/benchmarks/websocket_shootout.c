/**
This example emulates the websocket shootout testing requirements, except that
the JSON will not be fully parsed.

See the Websocket-Shootout repository at GitHub:
https://github.com/hashrocket/websocket-shootout

Using the benchmarking tool, try the following benchmarks (binary and text):

websocket-bench broadcast ws://127.0.0.1:3000/ --concurrent 10 \
--sample-size 100 --server-type binary --step-size 1000 --limit-percentile 95 \
--limit-rtt 250ms --initial-clients 1000

websocket-bench broadcast ws://127.0.0.1:3000/ --concurrent 10 \
--sample-size 100 --step-size 1000 --limit-percentile 95 \
--limit-rtt 250ms --initial-clients 1000

*/

#include "http.h"

#include "fio_cli.h"
#include "redis_engine.h"

#ifdef __APPLE__
#include <dlfcn.h>
#define PATCH_ENV()                                                            \
  do {                                                                         \
    void *obj_c_runtime =                                                      \
        dlopen("Foundation.framework/Foundation", RTLD_LAZY);                  \
    (void)obj_c_runtime;                                                       \
  } while (0)
#else
#define PATCH_ENV()
#endif

/* *****************************************************************************
Sunscription related variables and callbacks (used also for testing)
***************************************************************************** */

fio_str_info_s CHANNEL_TEXT = {.len = 4, .data = "text"};
fio_str_info_s CHANNEL_BINARY = {.len = 6, .data = "binary"};

static size_t sub_count;
static size_t unsub_count;

static void on_websocket_unsubscribe(void *udata) {
  (void)udata;
  fio_atomic_add(&unsub_count, 1);
}

static void print_subscription_balance(void *a) {
  FIO_LOG_INFO("(%d) subscribe / on_unsubscribe count (%s): %zu / %zu",
               getpid(), (char *)a, sub_count, unsub_count);
}

/* *****************************************************************************
WebSocket event callbacks
***************************************************************************** */

static void on_open_shootout_websocket(ws_s *ws) {
  fio_atomic_add(&sub_count, 2);
  websocket_subscribe(ws, .channel = CHANNEL_TEXT, .force_text = 1,
                      .on_unsubscribe = on_websocket_unsubscribe);
  websocket_subscribe(ws, .channel = CHANNEL_BINARY, .force_binary = 1,
                      .on_unsubscribe = on_websocket_unsubscribe);
}
static void on_open_shootout_websocket_sse(http_sse_s *sse) {
  http_sse_subscribe(sse, .channel = CHANNEL_TEXT);
}

static void handle_websocket_messages(ws_s *ws, fio_str_info_s msg,
                                      uint8_t is_text) {
  if (msg.data[0] == 'b') {
    fio_publish(.channel = CHANNEL_BINARY, .message = msg);
    // fwrite(".", 1, 1, stderr);
    msg.data[0] = 'r';
    websocket_write(ws, msg, 0);
  } else if (msg.data[9] == 'b') {
    // fwrite(".", 1, 1, stderr);
    fio_publish(.channel = CHANNEL_TEXT, .message = msg);
    /* send result */
    msg.len = msg.len + (25 - 19);
    void *buff = fio_malloc(msg.len);
    memcpy(buff, "{\"type\":\"broadcastResult\"", 25);
    memcpy((void *)(((uintptr_t)buff) + 25), msg.data + 19, msg.len - 25);
    msg.data = buff;
    websocket_write(ws, msg, 1);
    fio_free(buff);
  } else {
    /* perform echo */
    websocket_write(ws, msg, is_text);
  }
}

/* *****************************************************************************
HTTP events
***************************************************************************** */

static void answer_http_request(http_s *request) {
  http_set_header(request, HTTP_HEADER_CONTENT_TYPE,
                  http_mimetype_find("txt", 3));
  http_send_body(request, "This is a Websocket-Shootout example!", 37);
}
static void answer_http_upgrade(http_s *request, char *target, size_t len) {
  if (len >= 9 && target[1] == 'e') {
    http_upgrade2ws(request, .on_message = handle_websocket_messages,
                    .on_open = on_open_shootout_websocket);
  } else if (len >= 3 && target[0] == 's') {
    http_upgrade2sse(request, .on_open = on_open_shootout_websocket_sse);
  } else
    http_send_error(request, 400);
}

/* *****************************************************************************
Pub/Sub logging (for debugging)
***************************************************************************** */

/** Should subscribe channel. Failures are ignored. */
static void logger_subscribe(const fio_pubsub_engine_s *eng,
                             fio_str_info_s channel, fio_match_fn match) {
  FIO_LOG_INFO("(%d) Channel subscription created: %s", getpid(), channel.data);
  (void)eng;
  (void)match;
}
/** Should unsubscribe channel. Failures are ignored. */
static void logger_unsubscribe(const fio_pubsub_engine_s *eng,
                               fio_str_info_s channel, fio_match_fn match) {
  FIO_LOG_INFO("(%d) Channel subscription destroyed: %s", getpid(),
               channel.data);
  fflush(stderr);
  (void)eng;
  (void)match;
}
/** Should publish a message through the engine. Failures are ignored. */
static void logger_publish(const fio_pubsub_engine_s *eng,
                           fio_str_info_s channel, fio_str_info_s msg,
                           uint8_t is_json) {
  (void)eng;
  (void)channel;
  (void)msg;
  (void)is_json;
}

static fio_pubsub_engine_s PUBSUB_LOGGIN_ENGINE = {
    .subscribe = logger_subscribe,
    .unsubscribe = logger_unsubscribe,
    .publish = logger_publish,
};

/* *****************************************************************************
Redis cleanup helpers
***************************************************************************** */

static void redis_cleanup(void *e_) {
  redis_engine_destroy(e_);
  FIO_LOG_DEBUG("Cleaned up redis engine object.");
  FIO_PUBSUB_DEFAULT = FIO_PUBSUB_CLUSTER;
}

static void redis_initialize(void) {
  if (fio_cli_get("-redis") && strlen(fio_cli_get("-redis"))) {
    FIO_LOG_INFO("* Initializing Redis connection to %s\n",
                 fio_cli_get("-redis"));
    fio_url_s info =
        fio_url_parse(fio_cli_get("-redis"), strlen(fio_cli_get("-redis")));
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

/* *****************************************************************************
The main function
***************************************************************************** */

/*
Read available command line details using "-?".
*/
int main(int argc, char const *argv[]) {
  const char *port = "3000";
  const char *public_folder = NULL;
  uint32_t threads = 0;
  uint32_t workers = 0;
  uint8_t print_log = 0;

  /*     ****  Command line arguments ****     */
  fio_cli_start(
      argc, argv, 0, 0,
      "This is a facil.io example application.\n"
      "\nThis example conforms to the "
      "Websocket Shootout requirements at:\n"
      "https://github.com/hashrocket/websocket-shootout\n"
      "\nThe following arguments are supported:",
      FIO_CLI_PRINT_HEADER("Concurrency"),
      FIO_CLI_INT("-threads -t The number of threads to use. "
                  "System dependent default."),
      FIO_CLI_INT("-workers -w The number of processes to use. "
                  "System dependent default."),
      FIO_CLI_PRINT_HEADER("Connectivity"),
      FIO_CLI_INT("-port -p The port number to listen to."),
      FIO_CLI_PRINT_HEADER("HTTP settings"),
      "-public -www A public folder for serve an HTTP static file service.",
      FIO_CLI_BOOL("-log -v Turns logging on."), FIO_CLI_PRINT_HEADER("Misc"),
      "-redis -r add a Redis pub/sub round-trip.",
      FIO_CLI_BOOL("-debug Turns debug notifications on."));

  if (fio_cli_get_bool("-debug"))
    FIO_LOG_LEVEL = FIO_LOG_LEVEL_DEBUG;

  if (fio_cli_get("-p"))
    port = fio_cli_get("-p");
  if (fio_cli_get("-www")) {
    public_folder = fio_cli_get("-www");
    fprintf(stderr, "* serving static files from:%s\n", public_folder);
  }
  if (fio_cli_get_i("-t"))
    threads = fio_cli_get_i("-t");
  if (fio_cli_get_i("-w"))
    workers = fio_cli_get_i("-w");
  print_log = fio_cli_get_i("-v");

  redis_initialize();

  fio_cli_end();

  /*     ****  actual code ****     */
  if (http_listen(port, NULL, .on_request = answer_http_request,
                  .on_upgrade = answer_http_upgrade, .log = print_log,
                  .public_folder = public_folder) == -1) {
    perror("Couldn't initiate Websocket Shootout service");
    exit(1);
  }

  /* patch for dealing with the High Sierra `fork` limitations */
  PATCH_ENV();

  if (FIO_LOG_LEVEL == FIO_LOG_LEVEL_DEBUG) {
    fio_pubsub_attach(&PUBSUB_LOGGIN_ENGINE);
    fio_state_callback_add(FIO_CALL_ON_SHUTDOWN, print_subscription_balance,
                           "on shutdown");
    fio_state_callback_add(FIO_CALL_ON_FINISH, print_subscription_balance,
                           "on finish");
    fio_state_callback_add(FIO_CALL_AT_EXIT, print_subscription_balance,
                           "at exit");
  }

  fio_start(.threads = threads, .workers = workers);
}
