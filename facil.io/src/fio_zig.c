#include <fio.h>
#include <fiobject.h>

int fiobj_invalid = FIOBJ_INVALID;

int is_invalid(FIOBJ o) {
    if(o == FIOBJ_INVALID) return 1;
    return 0;
}

void fiobj_free_wrapped(FIOBJ o) {
  if (!FIOBJ_IS_ALLOCATED(o))
    return;
  if (fiobj_ref_dec(o))
    return;
  if (FIOBJECT2VTBL(o)->each && FIOBJECT2VTBL(o)->count(o))
    fiobj_free_complex_object(o);
  else
    FIOBJECT2VTBL(o)->dealloc(o, NULL, NULL);
}

void fio_log_debug(const char* msg) {
    FIO_LOG_DEBUG("%s", msg);
}
void fio_log_info(const char* msg) {
    FIO_LOG_INFO("%s", msg);
}
void fio_log_warning(const char* msg) {
    FIO_LOG_WARNING("%s", msg);
}
void fio_log_error(const char* msg) {
    FIO_LOG_ERROR("%s", msg);
}
void fio_log_fatal(const char* msg) {
    FIO_LOG_FATAL("%s", msg);
}

// Log Levels as ints 

/** Logging level of zero (no logging). */
int fio_log_level_none = FIO_LOG_LEVEL_NONE;
/** Log fatal errors. */
int fio_log_level_fatal = FIO_LOG_LEVEL_FATAL;
/** Log errors and fatal errors. */
int fio_log_level_error = FIO_LOG_LEVEL_ERROR;
/** Log warnings, errors and fatal errors. */
int fio_log_level_warning = FIO_LOG_LEVEL_WARNING;
/** Log every message (info, warnings, errors and fatal errors). */
int fio_log_level_info = FIO_LOG_LEVEL_INFO;
/** Log everything, including debug messages. */
int fio_log_level_debug = FIO_LOG_LEVEL_DEBUG;

void fio_set_log_level(int level) {
    FIO_LOG_LEVEL = level;
}

int fio_get_log_level() {
    return FIO_LOG_LEVEL;
}

void fio_log_print(int level, const char* msg) {
    FIO_LOG_PRINT(level, "%s", msg);
}

#include <websockets.h>
struct websocket_subscribe_s_zigcompat {
  /** the websocket receiving the message. REQUIRED. */
  ws_s *ws;
  /** the channel where the message was published. */
  fio_str_info_s channel;
  /**
   * The callback that handles pub/sub notifications.
   *
   * Default: send directly to websocket client.
   */
  void (*on_message)(ws_s *ws, fio_str_info_s channel, fio_str_info_s msg,
                     void *udata);
  /**
   * An optional cleanup callback for the `udata`.
   */
  void (*on_unsubscribe)(void *udata);
  /** User opaque data, passed along to the notification. */
  void *udata;
  /** An optional callback for pattern matching. */
  fio_match_fn match;
  /**
   * When using client forwarding (no `on_message` callback), this indicates if
   * messages should be sent to the client as binary blobs, which is the safest
   * approach.
   *
   * Default: tests for UTF-8 data encoding and sends as text if valid UTF-8.
   * Messages above ~32Kb are always assumed to be binary.
   */
  unsigned char force_binary;
  /**
   * When using client forwarding (no `on_message` callback), this indicates if
   * messages should be sent to the client as text.
   *
   * `force_binary` has precedence.
   *
   * Default: see above.
   *
   */
  unsigned char force_text;
};

/**
 * Subscribes to a channel. See {struct websocket_subscribe_s} for possible
 * arguments.
 *
 * Returns a subscription ID on success and 0 on failure.
 *
 * All subscriptions are automatically revoked once the websocket is closed.
 *
 * If the connections subscribes to the same channel more than once, messages
 * will be merged. However, another subscription ID will be assigned, since two
 * calls to {websocket_unsubscribe} will be required in order to unregister from
 * the channel.
 */
uintptr_t websocket_subscribe_zigcompat(struct websocket_subscribe_s_zigcompat args)
{
    return websocket_subscribe(args.ws,
        // .ws = args.ws,
        .channel = args.channel,
        .on_message = args.on_message,
        .on_unsubscribe = args.on_unsubscribe,
        .udata = args.udata,
        .match = args.match,
        .force_binary = args.force_binary & 1,
        .force_text = args.force_text & 1,
    );
}
