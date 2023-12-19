#ifndef H_HTTP_H
/*
Copyright: Boaz Segev, 2016-2019
License: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#define H_HTTP_H

#include <fio.h>

#include <fiobj.h>

#include <time.h>

/* support C++ */
#ifdef __cplusplus
extern "C" {
#endif

/* *****************************************************************************
Compile Time Settings
***************************************************************************** */

/** When a new connection is accepted, it will be immediately declined with a
 * 503 service unavailable (server busy) response unless the following number of
 * file descriptors is available.*/
#ifndef HTTP_BUSY_UNLESS_HAS_FDS
#define HTTP_BUSY_UNLESS_HAS_FDS 64
#endif

#ifndef HTTP_DEFAULT_BODY_LIMIT
#define HTTP_DEFAULT_BODY_LIMIT (1024 * 1024 * 50)
#endif

#ifndef HTTP_MAX_HEADER_COUNT
#define HTTP_MAX_HEADER_COUNT 128
#endif

#ifndef HTTP_MAX_HEADER_LENGTH
/** the default maximum length for a single header line */
#define HTTP_MAX_HEADER_LENGTH 8192
#endif

#ifndef FIO_HTTP_EXACT_LOGGING
/**
 * By default, facil.io logs the HTTP request cycle using a fuzzy starting point
 * (a close enough timestamp).
 *
 * The fuzzy timestamp includes delays that aren't related to the HTTP request,
 * sometimes including time that was spent waiting on the client. On the other
 * hand, `FIO_HTTP_EXACT_LOGGING` excludes time that the client might have been
 * waiting for facil.io to read data from the network.
 *
 * Due to the preference to err on the side of causion, fuzzy time-stamping is
 * the default.
 */
#define FIO_HTTP_EXACT_LOGGING 0
#endif

/** the `http_listen settings, see details in the struct definition. */
typedef struct http_settings_s http_settings_s;

/* *****************************************************************************
The Request / Response type and functions
***************************************************************************** */

/**
 * A generic HTTP handle used for HTTP request/response data.
 *
 * The `http_s` data can only be accessed safely from within the `on_request`
 * HTTP callback OR an `http_defer` callback.
 */
typedef struct {
  /** the HTTP request's "head" starts with a private data used by facil.io */
  struct {
    /** the function touting table - used by facil.io, don't use directly! */
    void *vtbl;
    /** the connection's owner / uuid - used by facil.io, don't use directly! */
    uintptr_t flag;
    /** The response headers, if they weren't sent. Don't access directly. */
    FIOBJ out_headers;
  } private_data;
  /** a time merker indicating when the request was received. */
  struct timespec received_at;
  /** a String containing the method data (supports non-standard methods. */
  FIOBJ method;
  /** The status string, for response objects (client mode response). */
  FIOBJ status_str;
  /** The HTTP version string, if any. */
  FIOBJ version;
  /** The status used for the response (or if the object is a response).
   *
   * When sending a request, the status should be set to 0.
   */
  uintptr_t status;
  /** The request path, if any. */
  FIOBJ path;
  /** The request query, if any. */
  FIOBJ query;
  /** a hash of general header data. When a header is set multiple times (such
   * as cookie headers), an Array will be used instead of a String. */
  FIOBJ headers;
  /**
   * a placeholder for a hash of cookie data.
   * the hash will be initialized when parsing the request.
   */
  FIOBJ cookies;
  /**
   * a placeholder for a hash of request data.
   * the hash will be initialized when parsing the request.
   */
  FIOBJ params;
  /**
   * a reader for body data (might be a temporary file or a string or NULL).
   * see fiobj_data.h for details.
   */
  FIOBJ body;
  /** an opaque user data pointer, to be used BEFORE calling `http_defer`. */
  void *udata;
} http_s;

/**
* This is a helper for setting cookie data.

This struct is used together with the `http_response_set_cookie`. i.e.:

      http_response_set_cookie(response,
        .name = "my_cookie",
        .value = "data" );

*/
typedef struct {
  /** The cookie's name (Symbol). */
  const char *name;
  /** The cookie's value (leave blank to delete cookie). */
  const char *value;
  /** The cookie's domain (optional). */
  const char *domain;
  /** The cookie's path (optional). */
  const char *path;
  /** The cookie name's size in bytes or a terminating NUL will be assumed.*/
  size_t name_len;
  /** The cookie value's size in bytes or a terminating NUL will be assumed.*/
  size_t value_len;
  /** The cookie domain's size in bytes or a terminating NUL will be assumed.*/
  size_t domain_len;
  /** The cookie path's size in bytes or a terminating NULL will be assumed.*/
  size_t path_len;
  /** Max Age (how long should the cookie persist), in seconds (0 == session).*/
  int max_age;
  /** Limit cookie to secure connections.*/
  unsigned secure : 1;
  /** Limit cookie to HTTP (intended to prevent javascript access/hijacking).*/
  unsigned http_only : 1;
} http_cookie_args_s;

/**
 * Sets a response header, taking ownership of the value object, but NOT the
 * name object (so name objects could be reused in future responses).
 *
 * Returns -1 on error and 0 on success.
 */
int http_set_header(http_s *h, FIOBJ name, FIOBJ value);

/**
 * Sets a response header.
 *
 * Returns -1 on error and 0 on success.
 */
int http_set_header2(http_s *h, fio_str_info_s name, fio_str_info_s value);

/**
 * Sets a response cookie.
 *
 * Returns -1 on error and 0 on success.
 *
 * Note: Long cookie names and long cookie values will be considered a security
 * violation and an error will be returned. It should be noted that most
 * proxies and servers will refuse long cookie names or values and many impose
 * total header lengths (including cookies) of ~8Kib.
 */
int http_set_cookie(http_s *h, http_cookie_args_s);
#define http_set_cookie(http___handle, ...)                                    \
  http_set_cookie((http___handle), (http_cookie_args_s){__VA_ARGS__})

/**
 * Sends the response headers and body.
 *
 * **Note**: The body is *copied* to the HTTP stream and it's memory should be
 * freed by the calling function.
 *
 * Returns -1 on error and 0 on success.
 *
 * AFTER THIS FUNCTION IS CALLED, THE `http_s` OBJECT IS NO LONGER VALID.
 */
int http_send_body(http_s *h, void *data, uintptr_t length);

/**
 * Sends the response headers and the specified file (the response's body).
 *
 * The file is closed automatically.
 *
 * Returns -1 on error and 0 on success.
 *
 * AFTER THIS FUNCTION IS CALLED, THE `http_s` OBJECT IS NO LONGER VALID.
 */
int http_sendfile(http_s *h, int fd, uintptr_t length, uintptr_t offset);

/**
 * Sends the response headers and the specified file (the response's body).
 *
 * The `local` and `encoded` strings will be joined into a single string that
 * represent the file name. Either or both of these strings can be empty.
 *
 * The `encoded` string will be URL decoded while the `local` string will used
 * as is.
 *
 * Returns 0 on success. A success value WILL CONSUME the `http_s` handle (it
 * will become invalid).
 *
 * Returns -1 on error (The `http_s` handle should still be used).
 */
int http_sendfile2(http_s *h, const char *prefix, size_t prefix_len,
                   const char *encoded, size_t encoded_len);

/**
 * Sends an HTTP error response.
 *
 * Returns -1 on error and 0 on success.
 *
 * AFTER THIS FUNCTION IS CALLED, THE `http_s` OBJECT IS NO LONGER VALID.
 *
 * The `uuid` and `settings` arguments are only required if the `http_s` handle
 * is NULL.
 */
int http_send_error(http_s *h, size_t error_code);

/**
 * Sends the response headers for a header only response.
 *
 * AFTER THIS FUNCTION IS CALLED, THE `http_s` OBJECT IS NO LONGER VALID.
 */
void http_finish(http_s *h);

/**
 * Pushes a data response when supported (HTTP/2 only).
 *
 * Returns -1 on error and 0 on success.
 */
int http_push_data(http_s *h, void *data, uintptr_t length, FIOBJ mime_type);

/**
 * Pushes a file response when supported (HTTP/2 only).
 *
 * If `mime_type` is NULL, an attempt at automatic detection using `filename`
 * will be made.
 *
 * Returns -1 on error and 0 on success.
 */
int http_push_file(http_s *h, FIOBJ filename, FIOBJ mime_type);

/* *****************************************************************************
HTTP evented API (pause / resume HTTp handling)
***************************************************************************** */

typedef struct http_pause_handle_s http_pause_handle_s;
/**
 * Pauses the request / response handling and INVALIDATES the current `http_s`
 * handle (no `http` functions can be called).
 *
 * The `http_resume` function MUST be called (at some point) using the opaque
 * `http` pointer given to the callback `task`.
 *
 * The opaque `http` pointer is only valid for a single call to `http_resume`
 * and can't be used by any other `http` function (it's a different data type).
 *
 * Note: the current `http_s` handle will become invalid once this function is
 *    called and it's data might be deallocated, invalid or used by a different
 *    thread.
 */
void http_pause(http_s *h, void (*task)(http_pause_handle_s *http));

/**
 * Resumes a request / response handling within a task and INVALIDATES the
 * current `http_s` handle.
 *
 * The `task` MUST call one of the `http_send_*`, `http_finish`, or
 * `http_pause`functions.
 *
 * The (optional) `fallback` will receive the opaque `udata` that was stored in
 * the HTTP handle and can be used for cleanup.
 *
 * Note: `http_resume` can only be called after calling `http_pause` and
 * entering it's task.
 *
 * Note: the current `http_s` handle will become invalid once this function is
 *    called and it's data might be deallocated, invalidated or used by a
 *    different thread.
 */
void http_resume(http_pause_handle_s *http, void (*task)(http_s *h),
                 void (*fallback)(void *udata));

/** Returns the `udata` associated with the paused opaque handle */
void *http_paused_udata_get(http_pause_handle_s *http);

/**
 * Sets the `udata` associated with the paused opaque handle, returning the
 * old value.
 */
void *http_paused_udata_set(http_pause_handle_s *http, void *udata);

/* *****************************************************************************
HTTP Connections - Listening / Connecting / Hijacking
***************************************************************************** */

/** The HTTP settings. */
struct http_settings_s {
  /** Callback for normal HTTP requests. */
  void (*on_request)(http_s *request);
  /**
   * Callback for Upgrade and EventSource (SSE) requests.
   *
   * SSE/EventSource requests set the `requested_protocol` string to `"sse"`.
   */
  void (*on_upgrade)(http_s *request, char *requested_protocol, size_t len);
  /** CLIENT REQUIRED: a callback for the HTTP response. */
  void (*on_response)(http_s *response);
  /** (optional) the callback to be performed when the HTTP service closes. */
  void (*on_finish)(struct http_settings_s *settings);
  /** Opaque user data. Facil.io will ignore this field, but you can use it. */
  void *udata;
  /**
   * A public folder for file transfers - allows to circumvent any application
   * layer logic and simply serve static files.
   *
   * Supports automatic `gz` pre-compressed alternatives.
   */
  const char *public_folder;
  /**
   * The length of the public_folder string.
   */
  size_t public_folder_length;
  /**
   * The maximum number of bytes allowed for the request string (method, path,
   * query), header names and fields.
   *
   * Defaults to 32Kib (which is about 4 times more than I would recommend).
   *
   * This reflects the total overall size. On HTTP/1.1, each header line (name +
   * value pair) is also limitied to a hardcoded HTTP_MAX_HEADER_LENGTH bytes.
   */
  size_t max_header_size;
  /**
   * The maximum size of an HTTP request's body (posting / downloading).
   *
   * Defaults to ~ 50Mb.
   */
  size_t max_body_size;
  /**
   * The maximum number of clients that are allowed to connect concurrently.
   *
   * This value's default setting is usually for the best.
   *
   * The default value is computed according to the server's capacity, leaving
   * some breathing room for other network and disk operations.
   *
   * Note: clients, by the nature of socket programming, are counted according
   *       to their internal file descriptor (`fd`) value. Open files and other
   *       sockets count towards a server's limit.
   */
  intptr_t max_clients;
  /** SSL/TLS support. */
  void *tls;
  /** reserved for future use. */
  intptr_t reserved1;
  /** reserved for future use. */
  intptr_t reserved2;
  /** reserved for future use. */
  intptr_t reserved3;
  /**
   * The maximum websocket message size/buffer (in bytes) for Websocket
   * connections. Defaults to ~250KB.
   */
  size_t ws_max_msg_size;
  /**
   * An HTTP/1.x connection timeout.
   *
   * `http_listen` defaults to ~40s and `http_connect` defaults to ~30s.
   *
   * Note: the connection might be closed (by other side) before timeout occurs.
   */
  uint8_t timeout;
  /**
   * Timeout for the websocket connections, a ping will be sent whenever the
   * timeout is reached. Defaults to 40 seconds.
   *
   * Connections are only closed when a ping cannot be sent (the network layer
   * fails). Pongs are ignored.
   */
  uint8_t ws_timeout;
  /** Logging flag - set to TRUE to log HTTP requests. */
  uint8_t log;
  /** a read only flag set automatically to indicate the protocol's mode. */
  uint8_t is_client;
};

/**
 * Listens to HTTP connections at the specified `port`.
 *
 * Leave as NULL to ignore IP binding.
 *
 * Returns -1 on error and the socket's uuid on success.
 *
 * the `on_finish` callback is always called.
 */
intptr_t http_listen(const char *port, const char *binding,
                     struct http_settings_s);
/** Listens to HTTP connections at the specified `port` and `binding`. */
#define http_listen(port, binding, ...)                                        \
  http_listen((port), (binding), (struct http_settings_s){__VA_ARGS__})

/**
 * Connects to an HTTP server as a client.
 *
 * Upon a successful connection, the `on_response` callback is called with an
 * empty `http_s*` handler (status == 0). Use the same API to set it's content
 * and send the request to the server. The next`on_response` will contain the
 * response.
 *
 * `address` should contain a full URL style address for the server. i.e.:
 *
 *           "http:/www.example.com:8080/"
 *
 * If an `address` includes a path or query data, they will be automatically
 * attached (both of them) to the HTTP handl'es `path` property. i.e.
 *
 *           "http:/www.example.com:8080/my_path?foo=bar"
 *           // will result in:
 *           fiobj_obj2cstr(h->path).data; //=> "/my_path?foo=bar"
 *
 * To open a Websocket connection, it's possible to use the `ws` protocol
 * signature. However, it would be better to use the `websocket_connect`
 * function instead.
 *
 * Returns -1 on error and the socket's uuid on success.
 *
 * The `on_finish` callback is always called.
 */
intptr_t http_connect(const char *url, const char *unix_address,
                      struct http_settings_s);
#define http_connect(url, unix_address, ...)                                   \
  http_connect((url), (unix_address), (struct http_settings_s){__VA_ARGS__})

/**
 * Returns the settings used to setup the connection or NULL on error.
 */
struct http_settings_s *http_settings(http_s *h);

/**
 * Returns the direct address of the connected peer (likely an intermediary).
 */
fio_str_info_s http_peer_addr(http_s *h);

/**
 * Hijacks the socket away from the HTTP protocol and away from facil.io.
 *
 * It's possible to hijack the socket and than reconnect it to a new protocol
 * object.
 *
 * It's possible to call `http_finish` immediately after calling `http_hijack`
 * in order to send the outgoing headers.
 *
 * If any additional HTTP functions are called after the hijacking, the protocol
 * object might attempt to continue reading data from the buffer.
 *
 * Returns the underlining socket connection's uuid. If `leftover` isn't NULL,
 * it will be populated with any remaining data in the HTTP buffer (the data
 * will be automatically deallocated, so copy the data when in need).
 *
 * WARNING: this isn't a good way to handle HTTP connections, especially as
 * HTTP/2 enters the picture.
 */
intptr_t http_hijack(http_s *h, fio_str_info_s *leftover);

/* *****************************************************************************
Websocket Upgrade (Server and Client connection establishment)
***************************************************************************** */

/**
 * The type for a Websocket handle, used to identify a Websocket connection.
 *
 * Similar to an `http_s` handle, it is only valid within the scope of the
 * specific connection (the callbacks / tasks) and shouldn't be stored or
 * accessed otherwise.
 */
typedef struct ws_s ws_s;

/**
 * This struct is used for the named arguments in the `http_upgrade2ws`
 * function and macro.
 */
typedef struct {
  /**
   * The (optional) on_message callback will be called whenever a websocket
   * message is received for this connection.
   *
   * The data received points to the websocket's message buffer and it will be
   * overwritten once the function exits (it cannot be saved for later, but it
   * can be copied).
   */
  void (*on_message)(ws_s *ws, fio_str_info_s msg, uint8_t is_text);
  /**
   * The (optional) on_open callback will be called once the websocket
   * connection is established and before is is registered with `facil`, so no
   * `on_message` events are raised before `on_open` returns.
   */
  void (*on_open)(ws_s *ws);
  /**
   * The (optional) on_ready callback will be after a the underlying socket's
   * buffer changes it's state from full to empty.
   *
   * If the socket's buffer is never used, the callback is never called.
   */
  void (*on_ready)(ws_s *ws);
  /**
   * The (optional) on_shutdown callback will be called if a websocket
   * connection is still open while the server is shutting down (called before
   * `on_close`).
   */
  void (*on_shutdown)(ws_s *ws);
  /**
   * The (optional) on_close callback will be called once a websocket connection
   * is terminated or failed to be established.
   *
   * The `uuid` is the connection's unique ID that can identify the Websocket. A
   * value of `uuid == 0` indicates the Websocket connection wasn't established
   * (an error occurred).
   *
   * The `udata` is the user data as set during the upgrade or using the
   * `websocket_udata_set` function.
   */
  void (*on_close)(intptr_t uuid, void *udata);
  /** Opaque user data. */
  void *udata;
} websocket_settings_s;

/**
 * Upgrades an HTTP/1.1 connection to a Websocket connection.
 *
 * This function will end the HTTP stage of the connection and attempt to
 * "upgrade" to a Websockets connection.
 *
 * Thie `http_s` handle will be invalid after this call and the `udata` will be
 * set to the new Websocket `udata`.
 *
 * A client connection's `on_finish` callback will be called (since the HTTP
 * stage has finished).
 */
int http_upgrade2ws(http_s *http, websocket_settings_s);

/** This macro allows easy access to the `http_upgrade2ws` function. The macro
 * allows the use of named arguments, using the `websocket_settings_s` struct
 * members. i.e.:
 *
 *     on_message(ws_s * ws, char * data, size_t size, int is_text) {
 *        ; // ... this is the websocket on_message callback
 *        websocket_write(ws, data, size, is_text); // a simple echo example
 *     }
 *
 *     on_upgrade(http_s* h) {
 *        http_upgrade2ws( .http = h, .on_message = on_message);
 *     }
 */
#define http_upgrade2ws(http, ...)                                             \
  http_upgrade2ws((http), (websocket_settings_s){__VA_ARGS__})

/**
 * Connects to a Websocket service according to the provided address.
 *
 * This is a somewhat naive connector object, it doesn't perform any
 * authentication or other logical handling. However, it's quire easy to author
 * a complext authentication logic using a combination of `http_connect` and
 * `http_upgrade2ws`.
 *
 * Returns the uuid for the future websocket on success.
 *
 * Returns -1 on error;
 */
int websocket_connect(const char *url, websocket_settings_s settings);
#define websocket_connect(url, ...)                                            \
  websocket_connect((url), (websocket_settings_s){__VA_ARGS__})

#include <websockets.h>

/* *****************************************************************************
EventSource Support (SSE)
***************************************************************************** */

/**
 * The type for the EventSource (SSE) handle, used to identify an SSE
 * connection.
 */
typedef struct http_sse_s http_sse_s;

/**
 * This struct is used for the named arguments in the `http_upgrade2sse`
 * function and macro.
 */
struct http_sse_s {
  /**
   * The (optional) on_open callback will be called once the EventSource
   * connection is established.
   */
  void (*on_open)(http_sse_s *sse);
  /**
   * The (optional) on_ready callback will be after a the underlying socket's
   * buffer changes it's state to empty.
   *
   * If the socket's buffer is never used, the callback is never called.
   */
  void (*on_ready)(http_sse_s *sse);
  /**
   * The (optional) on_shutdown callback will be called if a connection is still
   * open while the server is shutting down (called before `on_close`).
   */
  void (*on_shutdown)(http_sse_s *sse);
  /**
   * The (optional) on_close callback will be called once a connection is
   * terminated or failed to be established.
   *
   * The `udata` passed to the `http_upgrade2sse` function is available
   * through the `http_sse_s` pointer (`sse->udata`).
   */
  void (*on_close)(http_sse_s *sse);
  /** Opaque user data. */
  void *udata;
};

/**
 * Upgrades an HTTP connection to an EventSource (SSE) connection.
 *
 * The `http_s` handle will be invalid after this call.
 *
 * On HTTP/1.1 connections, this will preclude future requests using the same
 * connection.
 */
int http_upgrade2sse(http_s *h, http_sse_s);

/** This macro allows easy access to the `http_upgrade2sse` function. The macro
 * allows the use of named arguments, using the `websocket_settings_s` struct
 * members. i.e.:
 *
 *     on_open_sse(sse_s * sse) {
 *        http_sse_subscribe(sse, .channel = CHANNEL_NAME);
 *     }
 *
 *     on_upgrade(http_s* h) {
 *        http_upgrade2sse(h, .on_open = on_open_sse);
 *     }
 */
#define http_upgrade2sse(h, ...)                                               \
  http_upgrade2sse((h), (http_sse_s){__VA_ARGS__})

/**
 * Sets the ping interval for SSE connections.
 */
void http_sse_set_timout(http_sse_s *sse, uint8_t timeout);

struct http_sse_subscribe_args {
  /** The channel name used for the subscription. */
  fio_str_info_s channel;
  /** The optional on message callback. If missing, Data is directly writen. */
  void (*on_message)(http_sse_s *sse, fio_str_info_s channel,
                     fio_str_info_s msg, void *udata);
  /** An optional callback for when a subscription is fully canceled. */
  void (*on_unsubscribe)(void *udata);
  /** Opaque user */
  void *udata;
  /** A callback for pattern matching. */
  fio_match_fn match;
};

/**
 * Subscribes to a channel for direct message deliverance. See {struct
 * http_sse_subscribe_args} for possible arguments.
 *
 * Returns a subscription ID on success and 0 on failure.
 *
 * To unsubscripbe from the channel, use `http_sse_unsubscribe` (NOT
 * `fio_unsubscribe`).
 *
 * All subscriptions are automatically cleared once the connection is closed.
 */
uintptr_t http_sse_subscribe(http_sse_s *sse,
                             struct http_sse_subscribe_args args);

/** This macro allows easy access to the `http_sse_subscribe` function. */
#define http_sse_subscribe(sse, ...)                                           \
  http_sse_subscribe((sse), (struct http_sse_subscribe_args){__VA_ARGS__})

/**
 * Cancels a subscription and invalidates the subscription object.
 */
void http_sse_unsubscribe(http_sse_s *sse, uintptr_t subscription);

/**
 * Named arguments for the {http_sse_write} function.
 *
 * These arguments list the possible fields for the SSE event.
 *
 * Event fields listed here:
 * https://developer.mozilla.org/en-US/docs/Web/API/Server-sent_events/Using_server-sent_events
 */
struct http_sse_write_args {
  fio_str_info_s id;    /* (optional) sets the `id` event property. */
  fio_str_info_s event; /* (optional) sets the `event` event property. */
  fio_str_info_s data;  /* (optional) sets the `data` event property. */
  intptr_t retry;       /* (optional) sets the `retry` event property. */
};

/**
 * Writes data to an EventSource (SSE) connection.
 *
 * See the {struct http_sse_write_args} for possible named arguments.
 */
int http_sse_write(http_sse_s *sse, struct http_sse_write_args);
#define http_sse_write(sse, ...)                                               \
  http_sse_write((sse), (struct http_sse_write_args){__VA_ARGS__})

/**
 * Get the connection's UUID (for `fio_defer_io_task`, pub/sub, etc').
 */
intptr_t http_sse2uuid(http_sse_s *sse);

/**
 * Closes an EventSource (SSE) connection.
 */
int http_sse_close(http_sse_s *sse);

/**
 * Duplicates an SSE handle by reference, remember to http_sse_free.
 *
 * Returns the same object (increases a reference count, no allocation is made).
 */
http_sse_s *http_sse_dup(http_sse_s *sse);

/**
 * Frees an SSE handle by reference (decreases the reference count).
 */
void http_sse_free(http_sse_s *sse);

/* *****************************************************************************
HTTP GET and POST parsing helpers
***************************************************************************** */

/**
 * Attempts to decode the request's body.
 *
 * Supported Types include:
 * * application/x-www-form-urlencoded
 * * application/json
 * * multipart/form-data
 *
 * This should be called before `http_parse_query`, in order to support JSON
 * data.
 *
 * If the JSON data isn't an object, it will be saved under the key "JSON" in
 * the `params` hash.
 *
 * If the `multipart/form-data` type contains JSON files, they will NOT be
 * parsed (they will behave like any other file, with `data`, `type` and
 * `filename` keys assigned). This allows non-object JSON data (such as array)
 * to be handled by the app.
 */
int http_parse_body(http_s *h);

/**
 * Parses the query part of an HTTP request/response. Uses `http_add2hash`.
 *
 * This should be called after the `http_parse_body` function, just in case the
 * body is JSON that doesn't have an object at it's root.
 */
void http_parse_query(http_s *h);

/** Parses any Cookie / Set-Cookie headers, using the `http_add2hash` scheme. */
void http_parse_cookies(http_s *h, uint8_t is_url_encoded);

/**
 * Adds a named parameter to the hash, converting a string to an object and
 * resolving nesting references and URL decoding if required.
 *
 * i.e.:
 *
 * * "name[]" references a nested Array (nested in the Hash).
 * * "name[key]" references a nested Hash.
 * * "name[][key]" references a nested Hash within an array. Hash keys will be
 *   unique (repeating a key advances the hash).
 * * These rules can be nested (i.e. "name[][key1][][key2]...")
 * * "name[][]" is an error (there's no way for the parser to analyze
 *    dimensions)
 *
 * Note: names can't begin with "[" or end with "]" as these are reserved
 *       characters.
 */
int http_add2hash(FIOBJ dest, char *name, size_t name_len, char *value,
                  size_t value_len, uint8_t encoded);

/**
 * Adds a named parameter to the hash, using an existing object and resolving
 * nesting references.
 *
 * i.e.:
 *
 * * "name[]" references a nested Array (nested in the Hash).
 * * "name[key]" references a nested Hash.
 * * "name[][key]" references a nested Hash within an array. Hash keys will be
 *   unique (repeating a key advances the array).
 * * These rules can be nested (i.e. "name[][key1][][key2]...")
 * * "name[][]" is an error (there's no way for the parser to analyze
 *    dimensions)
 *
 * Note: names can't begin with "[" or end with "]" as these are reserved
 *       characters.
 */
int http_add2hash2(FIOBJ dest, char *name, size_t name_len, FIOBJ value,
                   uint8_t encoded);

/* *****************************************************************************
HTTP Status Strings and Mime-Type helpers
***************************************************************************** */

/** Returns a human readable string related to the HTTP status number. */
fio_str_info_s http_status2str(uintptr_t status);

/** Registers a Mime-Type to be associated with the file extension. */
void http_mimetype_register(char *file_ext, size_t file_ext_len,
                            FIOBJ mime_type_str);

/**
 * Finds the mime-type associated with the file extension, returning a String on
 * success and FIOBJ_INVALID on failure.
 *
 * Remember to call `fiobj_free`.
 */
FIOBJ http_mimetype_find(char *file_ext, size_t file_ext_len);

/**
 * Returns the mime-type associated with the URL or the default mime-type for
 * HTTP.
 *
 * Remember to call `fiobj_free`.
 */
FIOBJ http_mimetype_find2(FIOBJ url);

/** Clears the Mime-Type registry (it will be empty after this call). */
void http_mimetype_clear(void);

/* *****************************************************************************
Commonly used headers (fiobj Symbol objects)
***************************************************************************** */

extern FIOBJ HTTP_HEADER_ACCEPT;
extern FIOBJ HTTP_HEADER_CACHE_CONTROL;
extern FIOBJ HTTP_HEADER_CONNECTION;
extern FIOBJ HTTP_HEADER_CONTENT_ENCODING;
extern FIOBJ HTTP_HEADER_CONTENT_LENGTH;
extern FIOBJ HTTP_HEADER_CONTENT_RANGE;
extern FIOBJ HTTP_HEADER_CONTENT_TYPE;
extern FIOBJ HTTP_HEADER_COOKIE;
extern FIOBJ HTTP_HEADER_DATE;
extern FIOBJ HTTP_HEADER_ETAG;
extern FIOBJ HTTP_HEADER_HOST;
extern FIOBJ HTTP_HEADER_LAST_MODIFIED;
extern FIOBJ HTTP_HEADER_ORIGIN;
extern FIOBJ HTTP_HEADER_SET_COOKIE;
extern FIOBJ HTTP_HEADER_UPGRADE;

/* *****************************************************************************
HTTP General Helper functions that could be used globally
***************************************************************************** */

/**
 * Returns a String object representing the unparsed HTTP request (HTTP version
 * is capped at HTTP/1.1). Mostly usable for proxy usage and debugging.
 */
FIOBJ http_req2str(http_s *h);

/**
 * Writes a log line to `stderr` about the request / response object.
 *
 * This function is called automatically if the `.log` setting is enabled.
 */
void http_write_log(http_s *h);
/* *****************************************************************************
HTTP Time related helper functions that could be used globally
***************************************************************************** */

/**
A faster (yet less localized) alternative to `gmtime_r`.

See the libc `gmtime_r` documentation for details.

Falls back to `gmtime_r` for dates before epoch.
*/
struct tm *http_gmtime(time_t timer, struct tm *tmbuf);

/** Writes an RFC 7231 date representation (HTTP date format) to target. */
size_t http_date2rfc7231(char *target, struct tm *tmbuf);
/** Writes an RFC 2109 date representation to target. */
size_t http_date2rfc2109(char *target, struct tm *tmbuf);
/** Writes an RFC 2822 date representation to target. */
size_t http_date2rfc2822(char *target, struct tm *tmbuf);
/**
Writes an HTTP date string to the `target` buffer.

This requires ~32 bytes of space to be available at the target buffer (unless
it's a super funky year, 32 bytes is about 3 more than you need).

Returns the number of bytes actually written.
*/
static inline size_t http_date2str(char *target, struct tm *tmbuf) {
  return http_date2rfc7231(target, tmbuf);
}

/**
 * Prints Unix time to a HTTP time formatted string.
 *
 * This variation implements cached results for faster processing, at the
 * price of a less accurate string.
 */
size_t http_time2str(char *target, const time_t t);

/* *****************************************************************************
HTTP URL decoding helper functions that might be used globally
***************************************************************************** */

/** Decodes a URL encoded string, no buffer overflow protection. */
ssize_t http_decode_url_unsafe(char *dest, const char *url_data);

/** Decodes a URL encoded string (query / form data). */
ssize_t http_decode_url(char *dest, const char *url_data, size_t length);

/** Decodes the "path" part of a request, no buffer overflow protection. */
ssize_t http_decode_path_unsafe(char *dest, const char *url_data);

/**
 * Decodes the "path" part of an HTTP request, no buffer overflow protection.
 */
ssize_t http_decode_path(char *dest, const char *url_data, size_t length);

/* *****************************************************************************
HTTP URL parsing
***************************************************************************** */

/** the result returned by `http_url_parse` */
typedef fio_url_s http_url_s
    __attribute__((deprecated("use fio_url_s instead")));

/**
 * Parses the URI returning it's components and their lengths (no decoding
 * performed, doesn't accept decoded URIs).
 *
 * The returned string are NOT NUL terminated, they are merely locations within
 * the original string.
 *
 * This function expects any of the following formats:
 *
 * * `/complete_path?query#target`
 *
 *   i.e.: /index.html?page=1#list
 *
 * * `host:port/complete_path?query#target`
 *
 *   i.e.:
 *      example.com
 *      example.com/index.html
 *      example.com:8080/index.html
 *      example.com:8080/index.html?key=val#target
 *
 * * `user:password@host:port/path?query#target`
 *
 *   i.e.: user:1234@example.com:8080/index.html
 *
 * * `schema://user:password@host:port/path?query#target`
 *
 *   i.e.: http://example.com/index.html?page=1#list
 *
 * Invalid formats might produce unexpected results. No error testing performed.
 */
#define http_url_parse(url, len) fio_url_parse((url), (len))

#if DEBUG
void http_tests(void);
#endif

/* support C++ */
#ifdef __cplusplus
}
#endif

#endif /* H_HTTP_H */
