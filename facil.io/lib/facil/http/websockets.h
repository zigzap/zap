/*
copyright: Boaz Segev, 2016-2019
license: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#ifndef H_WEBSOCKETS_H
#define H_WEBSOCKETS_H

#include <http.h>

/* support C++ */
#ifdef __cplusplus
extern "C" {
#endif

/** used internally: attaches the Websocket protocol to the socket. */
void websocket_attach(intptr_t uuid, http_settings_s *http_settings,
                      websocket_settings_s *args, void *data, size_t length);

/* *****************************************************************************
Websocket information
***************************************************************************** */

/** Returns the opaque user data associated with the websocket. */
void *websocket_udata_get(ws_s *ws);

/**
 * Sets the opaque user data associated with the websocket.
 *
 * Returns the old value, if any.
 */
void *websocket_udata_set(ws_s *ws, void *udata);

/**
 * Returns the underlying socket UUID.
 *
 * This is only relevant for collecting the protocol object from outside of
 * websocket events, as the socket shouldn't be written to.
 */
intptr_t websocket_uuid(ws_s *ws);

/**
 * Returns 1 if the WebSocket connection is in Client mode (connected to a
 * remote server) and 0 if the connection is in Server mode (a connection
 * established using facil.io's HTTP server).
 */
uint8_t websocket_is_client(ws_s *ws);

/* *****************************************************************************
Websocket Connection Management (write / close)
***************************************************************************** */

/** Writes data to the websocket. Returns -1 on failure (0 on success). */
int websocket_write(ws_s *ws, fio_str_info_s msg, uint8_t is_text);
/** Closes a websocket connection. */
void websocket_close(ws_s *ws);

/* *****************************************************************************
Websocket Pub/Sub
=================

API for websocket pub/sub that can be used to publish messages across process
boundries.

Supports pub/sub engines (see {pubsub.h}) that can connect to a backend service
such as Redis.

The default pub/sub engine (if `NULL` or unspecified) will publish the messages
to the process cluster (all the processes in `fio_run`).

To publish to a channel, use the API provided in {pubsub.h}.
***************************************************************************** */

/** Possible arguments for the {websocket_subscribe} function. */
struct websocket_subscribe_s {
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
  unsigned force_binary : 1;
  /**
   * When using client forwarding (no `on_message` callback), this indicates if
   * messages should be sent to the client as text.
   *
   * `force_binary` has precedence.
   *
   * Default: see above.
   *
   */
  unsigned force_text : 1;
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
uintptr_t websocket_subscribe(struct websocket_subscribe_s args);

#define websocket_subscribe(wbsckt, ...)                                       \
  websocket_subscribe((struct websocket_subscribe_s){.ws = wbsckt, __VA_ARGS__})

/**
 * Unsubscribes from a channel.
 *
 * Failures are silent.
 *
 * All subscriptions are automatically revoked once the websocket is closed. So
 * only use this function to unsubscribe while the websocket is open.
 */
void websocket_unsubscribe(ws_s *ws, uintptr_t subscription_id);

/** Optimize generic broadcasts, for use in websocket_optimize4broadcasts. */
#define WEBSOCKET_OPTIMIZE_PUBSUB (-32)
/** Optimize text broadcasts, for use in websocket_optimize4broadcasts. */
#define WEBSOCKET_OPTIMIZE_PUBSUB_TEXT (-33)
/** Optimize binary broadcasts, for use in websocket_optimize4broadcasts. */
#define WEBSOCKET_OPTIMIZE_PUBSUB_BINARY (-34)

/**
 * Enables (or disables) broadcast optimizations.
 *
 * This is performed automatically by the `websocket_subscribe` function.
 * However, this function is provided for enabling the pub/sub metadata based
 * optimizations for external connections / subscriptions.
 *
 * This function allows enablement (or disablement) of these optimizations:
 *
 * * WEBSOCKET_OPTIMIZE_PUBSUB - optimize all direct transmission messages,
 *                               best attempt to detect Text vs. Binary data.
 * * WEBSOCKET_OPTIMIZE_PUBSUB_TEXT - optimize direct pub/sub text messages.
 * * WEBSOCKET_OPTIMIZE_PUBSUB_BINARY - optimize direct pub/sub binary messages.
 *
 * Note: to disable an optimization it should be disabled the same amount of
 * times it was enabled - multiple optimization enablements for the same type
 * are merged, but reference counted (disabled when reference is zero).
 *
 * Note2: The pub/sub metadata type ID will match the optimnization type
 * requested (i.e., `WEBSOCKET_OPTIMIZE_PUBSUB`) and the optimized data is a
 * FIOBJ String containing a pre-encoded WebSocket packet ready to be sent.
 * i.e.:
 *
 *     FIOBJ pre_wrapped = (FIOBJ)fio_message_metadata(msg,
 *                               WEBSOCKET_OPTIMIZE_PUBSUB);
 *     fiobj_send_free((intptr_t)msg->udata1, fiobj_dup(pre_wrapped));
 */
void websocket_optimize4broadcasts(intptr_t type, int enable);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
