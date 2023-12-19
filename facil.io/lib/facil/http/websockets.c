/*
copyright: Boaz Segev, 2016-2019
license: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#define FIO_INCLUDE_STR
#include <fio.h>

/* subscription lists have a long lifetime */
#define FIO_FORCE_MALLOC_TMP 1
#define FIO_INCLUDE_LINKED_LIST
#include <fio.h>

#include <fiobj.h>

#include <http.h>
#include <http_internal.h>

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <websocket_parser.h>

#if !defined(__BIG_ENDIAN__) && !defined(__LITTLE_ENDIAN__)
#include <endian.h>
#if !defined(__BIG_ENDIAN__) && !defined(__LITTLE_ENDIAN__) &&                 \
    __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define __BIG_ENDIAN__
#endif
#endif

/*******************************************************************************
Buffer management - update to change the way the buffer is handled.
*/
struct buffer_s {
  void *data;
  size_t size;
};

#pragma weak create_ws_buffer
/** returns a buffer_s struct, with a buffer (at least) `size` long. */
struct buffer_s create_ws_buffer(ws_s *owner);

#pragma weak resize_ws_buffer
/** returns a buffer_s struct, with a buffer (at least) `size` long. */
struct buffer_s resize_ws_buffer(ws_s *owner, struct buffer_s);

#pragma weak free_ws_buffer
/** releases an existing buffer. */
void free_ws_buffer(ws_s *owner, struct buffer_s);

/** Sets the initial buffer size. (4Kb)*/
#define WS_INITIAL_BUFFER_SIZE 4096UL

/*******************************************************************************
Buffer management - simple implementation...
Since Websocket connections have a long life expectancy, optimizing this part of
the code probably wouldn't offer a high performance boost.
*/

// buffer increments by 4,096 Bytes (4Kb)
#define round_up_buffer_size(size) (((size) >> 12) + 1) << 12

struct buffer_s create_ws_buffer(ws_s *owner) {
  (void)(owner);
  struct buffer_s buff;
  buff.size = WS_INITIAL_BUFFER_SIZE;
  buff.data = malloc(buff.size);
  return buff;
}

struct buffer_s resize_ws_buffer(ws_s *owner, struct buffer_s buff) {
  buff.size = round_up_buffer_size(buff.size);
  void *tmp = realloc(buff.data, buff.size);
  if (!tmp) {
    free_ws_buffer(owner, buff);
    buff.data = NULL;
    buff.size = 0;
  }
  buff.data = tmp;
  return buff;
}
void free_ws_buffer(ws_s *owner, struct buffer_s buff) {
  (void)(owner);
  free(buff.data);
}

#undef round_up_buffer_size

/*******************************************************************************
Create/Destroy the websocket object (prototypes)
*/

static ws_s *new_websocket();
static void destroy_ws(ws_s *ws);

/*******************************************************************************
The Websocket object (protocol + parser)
*/
struct ws_s {
  /** The Websocket protocol */
  fio_protocol_s protocol;
  /** connection data */
  intptr_t fd;
  /** callbacks */
  void (*on_message)(ws_s *ws, fio_str_info_s msg, uint8_t is_text);
  void (*on_shutdown)(ws_s *ws);
  void (*on_ready)(ws_s *ws);
  void (*on_open)(ws_s *ws);
  void (*on_close)(intptr_t uuid, void *udata);
  /** Opaque user data. */
  void *udata;
  /** The maximum websocket message size */
  size_t max_msg_size;
  /** active pub/sub subscriptions */
  fio_ls_s subscriptions;
  fio_lock_i sub_lock;
  /** socket buffer. */
  struct buffer_s buffer;
  /** data length (how much of the buffer actually used). */
  size_t length;
  /** message buffer. */
  FIOBJ msg;
  /** latest text state. */
  uint8_t is_text;
  /** websocket connection type. */
  uint8_t is_client;
};

/* *****************************************************************************
Create/Destroy the websocket subscription objects
***************************************************************************** */

static inline void clear_subscriptions(ws_s *ws) {
  fio_lock(&ws->sub_lock);
  while (fio_ls_any(&ws->subscriptions)) {
    fio_unsubscribe(fio_ls_pop(&ws->subscriptions));
  }
  fio_unlock(&ws->sub_lock);
}

/* *****************************************************************************
Callbacks - Required functions for websocket_parser.h
***************************************************************************** */

static void websocket_on_unwrapped(void *ws_p, void *msg, uint64_t len,
                                   char first, char last, char text,
                                   unsigned char rsv) {
  ws_s *ws = ws_p;
  if (last && first) {
    ws->on_message(ws, (fio_str_info_s){.data = msg, .len = len},
                   (uint8_t)text);
    return;
  }
  if (first) {
    ws->is_text = (uint8_t)text;
    if (ws->msg == FIOBJ_INVALID)
      ws->msg = fiobj_str_buf(len);
    fiobj_str_resize(ws->msg, 0);
  }
  fiobj_str_write(ws->msg, msg, len);
  if (last) {
    ws->on_message(ws, fiobj_obj2cstr(ws->msg), ws->is_text);
  }

  (void)rsv;
}
static void websocket_on_protocol_ping(void *ws_p, void *msg_, uint64_t len) {
  ws_s *ws = ws_p;
  if (msg_) {
    void *buff = malloc(len + 16);
    len = (((ws_s *)ws)->is_client
               ? websocket_client_wrap(buff, msg_, len, 10, 1, 1, 0)
               : websocket_server_wrap(buff, msg_, len, 10, 1, 1, 0));
    fio_write2(ws->fd, .data.buffer = buff, .length = len);
  } else {
    if (((ws_s *)ws)->is_client) {
      fio_write2(ws->fd, .data.buffer = "\x89\x80mask", .length = 2,
                 .after.dealloc = FIO_DEALLOC_NOOP);
    } else {
      fio_write2(ws->fd, .data.buffer = "\x89\x00", .length = 2,
                 .after.dealloc = FIO_DEALLOC_NOOP);
    }
  }
}
static void websocket_on_protocol_pong(void *ws_p, void *msg, uint64_t len) {
  (void)len;
  (void)msg;
  (void)ws_p;
}
static void websocket_on_protocol_close(void *ws_p) {
  ws_s *ws = ws_p;
  fio_close(ws->fd);
}
static void websocket_on_protocol_error(void *ws_p) {
  ws_s *ws = ws_p;
  fio_close(ws->fd);
}

/*******************************************************************************
The Websocket Protocol implementation
*/

#define ws_protocol(fd) ((ws_s *)(server_get_protocol(fd)))

static void ws_ping(intptr_t fd, fio_protocol_s *ws) {
  (void)(ws);
  if (((ws_s *)ws)->is_client) {
    fio_write2(fd, .data.buffer = "\x89\x80MASK", .length = 6,
               .after.dealloc = FIO_DEALLOC_NOOP);
  } else {
    fio_write2(fd, .data.buffer = "\x89\x00", .length = 2,
               .after.dealloc = FIO_DEALLOC_NOOP);
  }
}

static void on_close(intptr_t uuid, fio_protocol_s *_ws) {
  destroy_ws((ws_s *)_ws);
  (void)uuid;
}

static void on_ready(intptr_t fduuid, fio_protocol_s *ws) {
  (void)(fduuid);
  if (((ws_s *)ws)->on_ready)
    ((ws_s *)ws)->on_ready((ws_s *)ws);
}

static uint8_t on_shutdown(intptr_t fd, fio_protocol_s *ws) {
  (void)(fd);
  if (ws && ((ws_s *)ws)->on_shutdown)
    ((ws_s *)ws)->on_shutdown((ws_s *)ws);
  if (((ws_s *)ws)->is_client) {
    fio_write2(fd, .data.buffer = "\x8a\x80MASK", .length = 6,
               .after.dealloc = FIO_DEALLOC_NOOP);
  } else {
    fio_write2(fd, .data.buffer = "\x8a\x00", .length = 2,
               .after.dealloc = FIO_DEALLOC_NOOP);
  }
  return 0;
}

static void on_data(intptr_t sockfd, fio_protocol_s *ws_) {
  ws_s *const ws = (ws_s *)ws_;
  if (ws == NULL)
    return;
  struct websocket_packet_info_s info =
      websocket_buffer_peek(ws->buffer.data, ws->length);
  const uint64_t raw_length = info.packet_length + info.head_length;
  /* test expected data amount */
  if (ws->max_msg_size < raw_length) {
    /* too big */
    websocket_close(ws);
    return;
  }
  /* test buffer capacity */
  if (raw_length > ws->buffer.size) {
    ws->buffer.size = (size_t)raw_length;
    ws->buffer = resize_ws_buffer(ws, ws->buffer);
    if (!ws->buffer.data) {
      // no memory.
      websocket_close(ws);
      return;
    }
  }

  const ssize_t len = fio_read(sockfd, (uint8_t *)ws->buffer.data + ws->length,
                               ws->buffer.size - ws->length);
  if (len <= 0) {
    return;
  }
  ws->length = websocket_consume(ws->buffer.data, ws->length + len, ws,
                                 (~(ws->is_client) & 1));

  fio_force_event(sockfd, FIO_EVENT_ON_DATA);
}

static void on_data_first(intptr_t sockfd, fio_protocol_s *ws_) {
  ws_s *const ws = (ws_s *)ws_;
  if (ws->on_open)
    ws->on_open(ws);
  ws->protocol.on_data = on_data;
  ws->protocol.on_ready = on_ready;

  if (ws->length) {
    ws->length = websocket_consume(ws->buffer.data, ws->length, ws,
                                   (~(ws->is_client) & 1));
  }
  fio_force_event(sockfd, FIO_EVENT_ON_DATA);
  fio_force_event(sockfd, FIO_EVENT_ON_READY);
}

/* later */
static void websocket_write_impl(intptr_t fd, void *data, size_t len, char text,
                                 char first, char last, char client);

/*******************************************************************************
Create/Destroy the websocket object
*/

static ws_s *new_websocket(intptr_t uuid) {
  // allocate the protocol object
  ws_s *ws = malloc(sizeof(*ws));
  *ws = (ws_s){
      .protocol.ping = ws_ping,
      .protocol.on_data = on_data_first,
      .protocol.on_close = on_close,
      .protocol.on_ready = NULL /* filled in after `on_open` */,
      .protocol.on_shutdown = on_shutdown,
      .subscriptions = FIO_LS_INIT(ws->subscriptions),
      .is_client = 0,
      .fd = uuid,
  };
  return ws;
}
static void destroy_ws(ws_s *ws) {
  if (ws->on_close)
    ws->on_close(ws->fd, ws->udata);
  if (ws->msg)
    fiobj_free(ws->msg);
  clear_subscriptions(ws);
  free_ws_buffer(ws, ws->buffer);
  free(ws);
}

void websocket_attach(intptr_t uuid, http_settings_s *http_settings,
                      websocket_settings_s *args, void *data, size_t length) {
  ws_s *ws = new_websocket(uuid);
  FIO_ASSERT_ALLOC(ws);
  // we have an active websocket connection - prep the connection buffer
  ws->buffer = create_ws_buffer(ws);
  // Setup ws callbacks
  ws->on_open = args->on_open;
  ws->on_close = args->on_close;
  ws->on_message = args->on_message;
  ws->on_ready = args->on_ready;
  ws->on_shutdown = args->on_shutdown;
  // setup any user data
  ws->udata = args->udata;
  if (http_settings) {
    // client mode?
    ws->is_client = http_settings->is_client;
    // buffer limits
    ws->max_msg_size = http_settings->ws_max_msg_size;
    // update the timeout
    fio_timeout_set(uuid, http_settings->ws_timeout);
  } else {
    ws->max_msg_size = (1024 * 256);
    fio_timeout_set(uuid, 40);
  }

  if (data && length) {
    if (length > ws->buffer.size) {
      ws->buffer.size = length;
      ws->buffer = resize_ws_buffer(ws, ws->buffer);
      if (!ws->buffer.data) {
        // no memory.
        fio_attach(uuid, (fio_protocol_s *)ws);
        websocket_close(ws);
        return;
      }
    }
    memcpy(ws->buffer.data, data, length);
    ws->length = length;
  }
  // update the protocol object, cleaning up the old one
  fio_attach(uuid, (fio_protocol_s *)ws);
  // allow the on_open and on_data to take over the control.
  fio_force_event(uuid, FIO_EVENT_ON_DATA);
}

/*******************************************************************************
Writing to the Websocket
*/
#define WS_MAX_FRAME_SIZE                                                      \
  (FIO_MEMORY_BLOCK_ALLOC_LIMIT - 4096) // should be less then `unsigned short`

static void websocket_write_impl(intptr_t fd, void *data, size_t len, char text,
                                 char first, char last, char client) {
  if (len <= WS_MAX_FRAME_SIZE) {
    void *buff = fio_malloc(len + 16);
    len = (client ? websocket_client_wrap(buff, data, len, (text ? 1 : 2),
                                          first, last, 0)
                  : websocket_server_wrap(buff, data, len, (text ? 1 : 2),
                                          first, last, 0));
    fio_write2(fd, .data.buffer = buff, .length = len,
               .after.dealloc = fio_free);
  } else {
    /* frame fragmentation is better for large data then large frames */
    while (len > WS_MAX_FRAME_SIZE) {
      websocket_write_impl(fd, data, WS_MAX_FRAME_SIZE, text, first, 0, client);
      data = ((uint8_t *)data) + WS_MAX_FRAME_SIZE;
      first = 0;
      len -= WS_MAX_FRAME_SIZE;
    }
    websocket_write_impl(fd, data, len, text, first, 1, client);
  }
  return;
}

/* *****************************************************************************
Multi-client broadcast optimizations
***************************************************************************** */

static void websocket_optimize_free(fio_msg_s *msg, void *metadata) {
  fiobj_free((FIOBJ)metadata);
  (void)msg;
}

static inline fio_msg_metadata_s websocket_optimize(fio_str_info_s msg,
                                                    unsigned char opcode) {
  FIOBJ out = fiobj_str_buf(msg.len + 10);
  fiobj_str_resize(out,
                   websocket_server_wrap(fiobj_obj2cstr(out).data, msg.data,
                                         msg.len, opcode, 1, 1, 0));
  fio_msg_metadata_s ret = {
      .on_finish = websocket_optimize_free,
      .metadata = (void *)out,
  };
  return ret;
}
static fio_msg_metadata_s websocket_optimize_generic(fio_str_info_s ch,
                                                     fio_str_info_s msg,
                                                     uint8_t is_json) {
  fio_str_s tmp = FIO_STR_INIT_EXISTING(ch.data, ch.len, 0); // don't free
  tmp.dealloc = NULL;
  unsigned char opcode = 2;
  if (tmp.len <= (2 << 19) && fio_str_utf8_valid(&tmp)) {
    opcode = 1;
  }
  fio_msg_metadata_s ret = websocket_optimize(msg, opcode);
  ret.type_id = WEBSOCKET_OPTIMIZE_PUBSUB;
  return ret;
  (void)ch;
  (void)is_json;
}

static fio_msg_metadata_s websocket_optimize_text(fio_str_info_s ch,
                                                  fio_str_info_s msg,
                                                  uint8_t is_json) {
  fio_msg_metadata_s ret = websocket_optimize(msg, 1);
  ret.type_id = WEBSOCKET_OPTIMIZE_PUBSUB_TEXT;
  return ret;
  (void)ch;
  (void)is_json;
}

static fio_msg_metadata_s websocket_optimize_binary(fio_str_info_s ch,
                                                    fio_str_info_s msg,
                                                    uint8_t is_json) {
  fio_msg_metadata_s ret = websocket_optimize(msg, 2);
  ret.type_id = WEBSOCKET_OPTIMIZE_PUBSUB_BINARY;
  return ret;
  (void)ch;
  (void)is_json;
}

/**
 * Enables (or disables) broadcast optimizations.
 *
 * When using WebSocket pub/sub system is originally optimized for either
 * non-direct transmission (messages are handled by callbacks) or direct
 * transmission to 1-3 clients per channel (on average), meaning that the
 * majority of the messages are meant for a single recipient (or multiple
 * callback recipients) and only some are expected to be directly transmitted to
 * a group.
 *
 * However, when most messages are intended for direct transmission to more than
 * 3 clients (on average), certain optimizations can be made to improve memory
 * consumption (minimize duplication or WebSocket network data).
 *
 * This function allows enablement (or disablement) of these optimizations.
 * These optimizations include:
 *
 * * WEBSOCKET_OPTIMIZE_PUBSUB - optimize all direct transmission messages,
 *                               best attempt to detect Text vs. Binary data.
 * * WEBSOCKET_OPTIMIZE_PUBSUB_TEXT - optimize direct pub/sub text messages.
 * * WEBSOCKET_OPTIMIZE_PUBSUB_BINARY - optimize direct pub/sub binary messages.
 *
 * Note: to disable an optimization it should be disabled the same amount of
 * times it was enabled - multiple optimization enablements for the same type
 * are merged, but reference counted (disabled when reference is zero).
 */
void websocket_optimize4broadcasts(intptr_t type, int enable) {
  static intptr_t generic = 0;
  static intptr_t text = 0;
  static intptr_t binary = 0;
  fio_msg_metadata_s (*callback)(fio_str_info_s, fio_str_info_s, uint8_t);
  intptr_t *counter;
  switch ((0 - type)) {
  case (0 - WEBSOCKET_OPTIMIZE_PUBSUB):
    counter = &generic;
    callback = websocket_optimize_generic;
    break;
  case (0 - WEBSOCKET_OPTIMIZE_PUBSUB_TEXT):
    counter = &text;
    callback = websocket_optimize_text;
    break;
  case (0 - WEBSOCKET_OPTIMIZE_PUBSUB_BINARY):
    counter = &binary;
    callback = websocket_optimize_binary;
    break;
  default:
    return;
  }
  if (enable) {
    if (fio_atomic_add(counter, 1) == 1) {
      fio_message_metadata_callback_set(callback, 1);
    }
  } else {
    if (fio_atomic_sub(counter, 1) == 0) {
      fio_message_metadata_callback_set(callback, 0);
    }
  }
}

/* *****************************************************************************
Subscription handling
***************************************************************************** */

typedef struct {
  void (*on_message)(ws_s *ws, fio_str_info_s channel, fio_str_info_s msg,
                     void *udata);
  void (*on_unsubscribe)(void *udata);
  void *udata;
} websocket_sub_data_s;

static inline void websocket_on_pubsub_message_direct_internal(fio_msg_s *msg,
                                                               uint8_t txt) {
  fio_protocol_s *pr =
      fio_protocol_try_lock((intptr_t)msg->udata1, FIO_PR_LOCK_WRITE);
  if (!pr) {
    if (errno == EBADF)
      return;
    fio_message_defer(msg);
    return;
  }
  FIOBJ message = FIOBJ_INVALID;
  FIOBJ pre_wrapped = FIOBJ_INVALID;
  if (!((ws_s *)pr)->is_client) {
    /* pre-wrapping is only for client data */
    switch (txt) {
    case 0:
      pre_wrapped =
          (FIOBJ)fio_message_metadata(msg, WEBSOCKET_OPTIMIZE_PUBSUB_BINARY);
      break;
    case 1:
      pre_wrapped =
          (FIOBJ)fio_message_metadata(msg, WEBSOCKET_OPTIMIZE_PUBSUB_TEXT);
      break;
    case 2:
      pre_wrapped = (FIOBJ)fio_message_metadata(msg, WEBSOCKET_OPTIMIZE_PUBSUB);
      break;
    default:
      break;
    }
    if (pre_wrapped) {
      // FIO_LOG_DEBUG(
      //     "pub/sub WebSocket optimization route for pre-wrapped message.");
      fiobj_send_free((intptr_t)msg->udata1, fiobj_dup(pre_wrapped));
      goto finish;
    }
  }
  if (txt == 2) {
    /* unknown text state */
    fio_str_s tmp =
        FIO_STR_INIT_STATIC2(msg->msg.data, msg->msg.len); // don't free
    txt = (tmp.len >= (2 << 14) ? 0 : fio_str_utf8_valid(&tmp));
  }
  websocket_write((ws_s *)pr, msg->msg, txt & 1);
  fiobj_free(message);
finish:
  fio_protocol_unlock(pr, FIO_PR_LOCK_WRITE);
}

static void websocket_on_pubsub_message_direct(fio_msg_s *msg) {
  websocket_on_pubsub_message_direct_internal(msg, 2);
}

static void websocket_on_pubsub_message_direct_txt(fio_msg_s *msg) {
  websocket_on_pubsub_message_direct_internal(msg, 1);
}

static void websocket_on_pubsub_message_direct_bin(fio_msg_s *msg) {
  websocket_on_pubsub_message_direct_internal(msg, 0);
}

static void websocket_on_pubsub_message(fio_msg_s *msg) {
  fio_protocol_s *pr =
      fio_protocol_try_lock((intptr_t)msg->udata1, FIO_PR_LOCK_TASK);
  if (!pr) {
    if (errno == EBADF)
      return;
    fio_message_defer(msg);
    return;
  }
  websocket_sub_data_s *d = msg->udata2;

  if (d->on_message)
    d->on_message((ws_s *)pr, msg->channel, msg->msg, d->udata);
  fio_protocol_unlock(pr, FIO_PR_LOCK_TASK);
}

static void websocket_on_unsubscribe(void *u1, void *u2) {
  websocket_sub_data_s *d = u2;
  if (d->on_unsubscribe) {
    d->on_unsubscribe(d->udata);
  }

  if ((intptr_t)d->on_message == (intptr_t)WEBSOCKET_OPTIMIZE_PUBSUB) {
    websocket_optimize4broadcasts(WEBSOCKET_OPTIMIZE_PUBSUB, 0);
  } else if ((intptr_t)d->on_message ==
             (intptr_t)WEBSOCKET_OPTIMIZE_PUBSUB_TEXT) {
    websocket_optimize4broadcasts(WEBSOCKET_OPTIMIZE_PUBSUB_TEXT, 0);
  } else if ((intptr_t)d->on_message ==
             (intptr_t)WEBSOCKET_OPTIMIZE_PUBSUB_BINARY) {
    websocket_optimize4broadcasts(WEBSOCKET_OPTIMIZE_PUBSUB_BINARY, 0);
  }
  free(d);
  (void)u1;
}

/**
 * Returns a subscription ID on success and 0 on failure.
 */
#undef websocket_subscribe
uintptr_t websocket_subscribe(struct websocket_subscribe_s args) {
  if (!args.ws || !fio_is_valid(args.ws->fd))
    goto error;
  websocket_sub_data_s *d = malloc(sizeof(*d));
  FIO_ASSERT_ALLOC(d);
  *d = (websocket_sub_data_s){
      .udata = args.udata,
      .on_message = args.on_message,
      .on_unsubscribe = args.on_unsubscribe,
  };
  void (*handler)(fio_msg_s *) = websocket_on_pubsub_message;
  if (!args.on_message) {
    intptr_t br_type;
    if (args.force_binary) {
      br_type = WEBSOCKET_OPTIMIZE_PUBSUB_BINARY;
      handler = websocket_on_pubsub_message_direct_bin;
    } else if (args.force_text) {
      br_type = WEBSOCKET_OPTIMIZE_PUBSUB_TEXT;
      handler = websocket_on_pubsub_message_direct_txt;
    } else {
      br_type = WEBSOCKET_OPTIMIZE_PUBSUB;
      handler = websocket_on_pubsub_message_direct;
    }
    websocket_optimize4broadcasts(br_type, 1);
    d->on_message =
        (void (*)(ws_s *, fio_str_info_s, fio_str_info_s, void *))br_type;
  }
  subscription_s *sub =
      fio_subscribe(.channel = args.channel, .match = args.match,
                    .on_unsubscribe = websocket_on_unsubscribe,
                    .on_message = handler, .udata1 = (void *)args.ws->fd,
                    .udata2 = d);
  if (!sub) {
    /* don't free `d`, return (`d` freed by fio_subscribe) */
    return 0;
  }
  fio_ls_s *pos;
  fio_lock(&args.ws->sub_lock);
  pos = fio_ls_push(&args.ws->subscriptions, sub);
  fio_unlock(&args.ws->sub_lock);

  return (uintptr_t)pos;
error:
  if (args.on_unsubscribe)
    args.on_unsubscribe(args.udata);
  return 0;
}

/**
 * Unsubscribes from a channel.
 */
void websocket_unsubscribe(ws_s *ws, uintptr_t subscription_id) {
  fio_unsubscribe((subscription_s *)((fio_ls_s *)subscription_id)->obj);
  fio_lock(&ws->sub_lock);
  fio_ls_remove((fio_ls_s *)subscription_id);
  fio_unlock(&ws->sub_lock);

  (void)ws;
}

/*******************************************************************************
The API implementation
*/

/** Returns the opaque user data associated with the websocket. */
void *websocket_udata_get(ws_s *ws) { return ws->udata; }

/** Returns the the process specific connection's UUID (see `libsock`). */
intptr_t websocket_uuid(ws_s *ws) { return ws->fd; }

/** Sets the opaque user data associated with the websocket.
 * Returns the old value, if any. */
void *websocket_udata_set(ws_s *ws, void *udata) {
  void *old = ws->udata;
  ws->udata = udata;
  return old;
}

/**
 * Returns 1 if the WebSocket connection is in Client mode (connected to a
 * remote server) and 0 if the connection is in Server mode (a connection
 * established using facil.io's HTTP server).
 */
uint8_t websocket_is_client(ws_s *ws) { return ws->is_client; }

/** Writes data to the websocket. Returns -1 on failure (0 on success). */
int websocket_write(ws_s *ws, fio_str_info_s msg, uint8_t is_text) {
  if (fio_is_valid(ws->fd)) {
    websocket_write_impl(ws->fd, msg.data, msg.len, is_text, 1, 1,
                         ws->is_client);
    return 0;
  }
  return -1;
}
/** Closes a websocket connection. */
void websocket_close(ws_s *ws) {
  fio_write2(ws->fd, .data.buffer = "\x88\x00", .length = 2,
             .after.dealloc = FIO_DEALLOC_NOOP);
  fio_close(ws->fd);
  return;
}
