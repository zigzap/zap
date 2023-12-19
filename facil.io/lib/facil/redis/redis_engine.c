/*
Copyright: Boaz segev, 2016-2019
License: MIT

Feel free to copy, use and enjoy according to the license provided.
*/

#define FIO_INCLUDE_LINKED_LIST
#define FIO_INCLUDE_STR
// #define DEBUG 1
#include <fio.h>

#include <fiobj.h>

#include <redis_engine.h>
#include <resp_parser.h>

#define REDIS_READ_BUFFER 8192
/* *****************************************************************************
The Redis Engine and Callbacks Object
***************************************************************************** */

typedef struct {
  fio_pubsub_engine_s en;
  struct redis_engine_internal_s {
    fio_protocol_s protocol;
    intptr_t uuid;
    resp_parser_s parser;
    void (*on_message)(struct redis_engine_internal_s *parser, FIOBJ msg);
    FIOBJ str;
    FIOBJ ary;
    uint32_t ary_count;
    uint16_t buf_pos;
    uint16_t nesting;
  } pub_data, sub_data;
  subscription_s *publication_forwarder;
  subscription_s *cmd_forwarder;
  subscription_s *cmd_reply;
  char *address;
  char *port;
  char *auth;
  FIOBJ last_ch;
  size_t auth_len;
  size_t ref;
  fio_ls_embd_s queue;
  fio_lock_i lock;
  fio_lock_i lock_connection;
  uint8_t ping_int;
  volatile uint8_t pub_sent;
  volatile uint8_t flag;
  uint8_t buf[];
} redis_engine_s;

typedef struct {
  fio_ls_embd_s node;
  void (*callback)(fio_pubsub_engine_s *e, FIOBJ reply, void *udata);
  void *udata;
  size_t cmd_len;
  uint8_t cmd[];
} redis_commands_s;

/** converts from a publishing protocol to an `redis_engine_s`. */
#define pub2redis(pr) FIO_LS_EMBD_OBJ(redis_engine_s, pub_data, (pr))
/** converts from a subscribing protocol to an `redis_engine_s`. */
#define sub2redis(pr) FIO_LS_EMBD_OBJ(redis_engine_s, sub_data, (pr))

/** converts from a `resp_parser_s` to the internal data structure. */
#define parser2data(prsr)                                                      \
  FIO_LS_EMBD_OBJ(struct redis_engine_internal_s, parser, (prsr))

/* releases any resources used by an internal engine*/
static inline void redis_internal_reset(struct redis_engine_internal_s *i) {
  i->buf_pos = 0;
  i->parser = (resp_parser_s){.obj_countdown = 0, .expecting = 0};
  fiobj_free((FIOBJ)fio_ct_if(i->ary == FIOBJ_INVALID, (uintptr_t)i->str,
                              (uintptr_t)i->ary));
  i->str = FIOBJ_INVALID;
  i->ary = FIOBJ_INVALID;
  i->ary_count = 0;
  i->nesting = 0;
  i->uuid = -1;
}

/** cleans up and frees the engine data. */
static inline void redis_free(redis_engine_s *r) {
  if (fio_atomic_sub(&r->ref, 1))
    return;
  FIO_LOG_DEBUG("freeing redis engine for %s:%s", r->address, r->port);
  redis_internal_reset(&r->pub_data);
  redis_internal_reset(&r->sub_data);
  fiobj_free(r->last_ch);
  while (fio_ls_embd_any(&r->queue)) {
    fio_free(
        FIO_LS_EMBD_OBJ(redis_commands_s, node, fio_ls_embd_pop(&r->queue)));
  }
  fio_unsubscribe(r->publication_forwarder);
  r->publication_forwarder = NULL;
  fio_unsubscribe(r->cmd_forwarder);
  r->cmd_forwarder = NULL;
  fio_unsubscribe(r->cmd_reply);
  r->cmd_reply = NULL;
  fio_free(r);
}

/* *****************************************************************************
Simple RESP formatting
***************************************************************************** */

inline static void fiobj2resp___internal(FIOBJ dest, FIOBJ obj) {
  fio_str_info_s s;
  switch (FIOBJ_TYPE(obj)) {
  case FIOBJ_T_NULL:
    fiobj_str_write(dest, "$-1\r\n", 5);
    break;
  case FIOBJ_T_ARRAY:
    fiobj_str_write(dest, "*", 1);
    fiobj_str_write_i(dest, fiobj_ary_count(obj));
    fiobj_str_write(dest, "\r\n", 2);
    break;
  case FIOBJ_T_HASH:
    fiobj_str_write(dest, "*", 1);
    fiobj_str_write_i(dest, fiobj_hash_count(obj) * 2);
    fiobj_str_write(dest, "\r\n", 2);
    break;
  case FIOBJ_T_TRUE:
    fiobj_str_write(dest, "$4\r\ntrue\r\n", 10);
    break;
  case FIOBJ_T_FALSE:
    fiobj_str_write(dest, "$4\r\nfalse\r\n", 11);
    break;
#if 0
    /* Numbers aren't as good for commands as one might think... */
  case FIOBJ_T_NUMBER:
    fiobj_str_write(dest, ":", 1);
    fiobj_str_write_i(dest, fiobj_obj2num(obj));
    fiobj_str_write(dest, "\r\n", 2);
    break;
#else
  case FIOBJ_T_NUMBER: /* overflow */
#endif
  case FIOBJ_T_FLOAT:   /* overflow */
  case FIOBJ_T_UNKNOWN: /* overflow */
  case FIOBJ_T_STRING:  /* overflow */
  case FIOBJ_T_DATA:
    s = fiobj_obj2cstr(obj);
    fiobj_str_write(dest, "$", 1);
    fiobj_str_write_i(dest, s.len);
    fiobj_str_write(dest, "\r\n", 2);
    fiobj_str_write(dest, s.data, s.len);
    fiobj_str_write(dest, "\r\n", 2);
    break;
  }
}

static int fiobj2resp_task(FIOBJ o, void *dest_) {
  if (fiobj_hash_key_in_loop())
    fiobj2resp___internal((FIOBJ)dest_, fiobj_hash_key_in_loop());
  fiobj2resp___internal((FIOBJ)dest_, o);
  return 0;
}

/**
 * Converts FIOBJ objects into a RESP string (client mode).
 */
static FIOBJ fiobj2resp(FIOBJ dest, FIOBJ obj) {
  fiobj_each2(obj, fiobj2resp_task, (void *)dest);
  return dest;
}

/**
 * Converts FIOBJ objects into a RESP string (client mode).
 *
 * Don't call `fiobj_free`, object will self-destruct.
 */
static inline FIOBJ fiobj2resp_tmp(FIOBJ obj) {
  return fiobj2resp(fiobj_str_tmp(), obj);
}

/* *****************************************************************************
RESP parser callbacks
***************************************************************************** */

/** a local static callback, called when a parser / protocol error occurs. */
static int resp_on_parser_error(resp_parser_s *parser) {
  struct redis_engine_internal_s *i = parser2data(parser);
  FIO_LOG_ERROR("(redis) parser error - attempting to restart connection.\n");
  fio_close(i->uuid);
  return -1;
}

/** a local static callback, called when the RESP message is complete. */
static int resp_on_message(resp_parser_s *parser) {
  struct redis_engine_internal_s *i = parser2data(parser);
  FIOBJ msg = i->ary ? i->ary : i->str;
  i->on_message(i, msg);
  /* cleanup */
  fiobj_free(msg);
  i->ary = FIOBJ_INVALID;
  i->str = FIOBJ_INVALID;
  return 0;
}

/** a local helper to add parsed objects to the data store. */
static inline void resp_add_obj(struct redis_engine_internal_s *dest, FIOBJ o) {
  if (dest->ary) {
    fiobj_ary_push(dest->ary, o);
    --dest->ary_count;
    if (!dest->ary_count && dest->nesting) {
      FIOBJ tmp = fiobj_ary_shift(dest->ary);
      dest->ary_count = fiobj_obj2num(tmp);
      fiobj_free(tmp);
      dest->ary = fiobj_ary_shift(dest->ary);
      --dest->nesting;
    }
  }
  dest->str = o;
}

/** a local static callback, called when a Number object is parsed. */
static int resp_on_number(resp_parser_s *parser, int64_t num) {
  struct redis_engine_internal_s *data = parser2data(parser);
  resp_add_obj(data, fiobj_num_new(num));
  return 0;
}
/** a local static callback, called when a OK message is received. */
static int resp_on_okay(resp_parser_s *parser) {
  struct redis_engine_internal_s *data = parser2data(parser);
  resp_add_obj(data, fiobj_true());
  return 0;
}
/** a local static callback, called when NULL is received. */
static int resp_on_null(resp_parser_s *parser) {
  struct redis_engine_internal_s *data = parser2data(parser);
  resp_add_obj(data, fiobj_null());
  return 0;
}

/**
 * a local static callback, called when a String should be allocated.
 *
 * `str_len` is the expected number of bytes that will fill the final string
 * object, without any NUL byte marker (the string might be binary).
 *
 * If this function returns any value besides 0, parsing is stopped.
 */
static int resp_on_start_string(resp_parser_s *parser, size_t str_len) {
  struct redis_engine_internal_s *data = parser2data(parser);
  resp_add_obj(data, fiobj_str_buf(str_len));
  return 0;
}
/** a local static callback, called as String objects are streamed. */
static int resp_on_string_chunk(resp_parser_s *parser, void *data, size_t len) {
  struct redis_engine_internal_s *i = parser2data(parser);
  fiobj_str_write(i->str, data, len);
  return 0;
}
/** a local static callback, called when a String object had finished
 * streaming.
 */
static int resp_on_end_string(resp_parser_s *parser) {
  return 0;
  (void)parser;
}

/** a local static callback, called an error message is received. */
static int resp_on_err_msg(resp_parser_s *parser, void *data, size_t len) {
  struct redis_engine_internal_s *i = parser2data(parser);
  resp_add_obj(i, fiobj_str_new(data, len));
  return 0;
}

/**
 * a local static callback, called when an Array should be allocated.
 *
 * `array_len` is the expected number of objects that will fill the Array
 * object.
 *
 * There's no `resp_on_end_array` callback since the RESP protocol assumes the
 * message is finished along with the Array (`resp_on_message` is called).
 * However, just in case a non-conforming client/server sends nested Arrays,
 * the callback should test against possible overflow or nested Array endings.
 *
 * If this function returns any value besides 0, parsing is stopped.
 */
static int resp_on_start_array(resp_parser_s *parser, size_t array_len) {
  struct redis_engine_internal_s *i = parser2data(parser);
  if (i->ary) {
    ++i->nesting;
    FIOBJ tmp = fiobj_ary_new2(array_len + 2);
    fiobj_ary_push(tmp, fiobj_num_new(i->ary_count));
    fiobj_ary_push(tmp, fiobj_num_new(i->ary));
    i->ary = tmp;
  } else {
    i->ary = fiobj_ary_new2(array_len + 2);
  }
  i->ary_count = array_len;
  return 0;
}

/* *****************************************************************************
Publication and Command Handling
***************************************************************************** */

/* the deferred callback handler */
static void redis_perform_callback(void *e, void *cmd_) {
  redis_commands_s *cmd = cmd_;
  FIOBJ reply = (FIOBJ)cmd->node.next;
  if (cmd->callback)
    cmd->callback(e, reply, cmd->udata);
  fiobj_free(reply);
  FIO_LOG_DEBUG("Handled: %s\n", cmd->cmd);
  fio_free(cmd);
}

/* send command within lock, to ensure flag integrity */
static void redis_send_next_command_unsafe(redis_engine_s *r) {
  if (!r->pub_sent && fio_ls_embd_any(&r->queue)) {
    r->pub_sent = 1;
    redis_commands_s *cmd =
        FIO_LS_EMBD_OBJ(redis_commands_s, node, r->queue.next);
    fio_write2(r->pub_data.uuid, .data.buffer = cmd->cmd,
               .length = cmd->cmd_len, .after.dealloc = FIO_DEALLOC_NOOP);
    FIO_LOG_DEBUG("(redis %d) Sending (%zu bytes):\n%s\n", (int)getpid(),
                  cmd->cmd_len, cmd->cmd);
  }
}

/* attach a command to the queue */
static void redis_attach_cmd(redis_engine_s *r, redis_commands_s *cmd) {
  fio_lock(&r->lock);
  fio_ls_embd_push(&r->queue, &cmd->node);
  redis_send_next_command_unsafe(r);
  fio_unlock(&r->lock);
}

/** a local static callback, called when the RESP message is complete. */
static void resp_on_pub_message(struct redis_engine_internal_s *i, FIOBJ msg) {
  redis_engine_s *r = pub2redis(i);
  // #if DEBUG
  if (FIO_LOG_LEVEL >= FIO_LOG_LEVEL_DEBUG) {
    FIOBJ json = fiobj_obj2json(msg, 1);
    FIO_LOG_DEBUG("Redis reply:\n%s\n", fiobj_obj2cstr(json).data);
    fiobj_free(json);
  }
  // #endif
  /* publishing / command parser */
  fio_lock(&r->lock);
  fio_ls_embd_s *node = fio_ls_embd_shift(&r->queue);
  r->pub_sent = 0;
  redis_send_next_command_unsafe(r);
  fio_unlock(&r->lock);
  if (!node) {
    /* TODO: possible ping? from server?! not likely... */
    FIO_LOG_WARNING("(redis %d) received a reply when no command was sent.",
                    (int)getpid());
    return;
  }
  node->next = (void *)fiobj_dup(msg);
  fio_defer(redis_perform_callback, &r->en,
            FIO_LS_EMBD_OBJ(redis_commands_s, node, node));
}

/* *****************************************************************************
Subscription Message Handling
***************************************************************************** */

/** a local static callback, called when the RESP message is complete. */
static void resp_on_sub_message(struct redis_engine_internal_s *i, FIOBJ msg) {
  redis_engine_s *r = sub2redis(i);
  /* subscriotion parser */
  if (FIOBJ_TYPE(msg) != FIOBJ_T_ARRAY) {
    if (FIOBJ_TYPE(msg) != FIOBJ_T_STRING || fiobj_obj2cstr(msg).len != 4 ||
        fiobj_obj2cstr(msg).data[0] != 'P') {
      FIO_LOG_WARNING("(redis) unexpected data format in "
                      "subscription stream (%zu bytes):\n     %s\n",
                      fiobj_obj2cstr(msg).len, fiobj_obj2cstr(msg).data);
    }
  } else {
    // FIOBJ *ary = fiobj_ary2ptr(msg);
    // for (size_t i = 0; i < fiobj_ary_count(msg); ++i) {
    //   fio_str_info_s tmp = fiobj_obj2cstr(ary[i]);
    //   fprintf(stderr, "(%lu) %s\n", (unsigned long)i, tmp.data);
    // }
    fio_str_info_s tmp = fiobj_obj2cstr(fiobj_ary_index(msg, 0));
    if (tmp.len == 7) { /* "message"  */
      fiobj_free(r->last_ch);
      r->last_ch = fiobj_dup(fiobj_ary_index(msg, 1));
      fio_publish(.channel = fiobj_obj2cstr(r->last_ch),
                  .message = fiobj_obj2cstr(fiobj_ary_index(msg, 2)),
                  .engine = FIO_PUBSUB_CLUSTER);
    } else if (tmp.len == 8) { /* "pmessage" */
      if (!fiobj_iseq(r->last_ch, fiobj_ary_index(msg, 2)))
        fio_publish(.channel = fiobj_obj2cstr(fiobj_ary_index(msg, 2)),
                    .message = fiobj_obj2cstr(fiobj_ary_index(msg, 3)),
                    .engine = FIO_PUBSUB_CLUSTER);
    }
  }
}

/* *****************************************************************************
Connection Callbacks (fio_protocol_s) and Engine
***************************************************************************** */

/** defined later - connects to Redis */
static void redis_connect(void *r, void *i);

#define defer_redis_connect(r, i)                                              \
  do {                                                                         \
    fio_atomic_add(&(r)->ref, 1);                                              \
    fio_defer(redis_connect, (r), (i));                                        \
  } while (0);

/** Called when a data is available, but will not run concurrently */
static void redis_on_data(intptr_t uuid, fio_protocol_s *pr) {
  struct redis_engine_internal_s *internal =
      (struct redis_engine_internal_s *)pr;
  uint8_t *buf;
  if (internal->on_message == resp_on_sub_message) {
    buf = sub2redis(pr)->buf + REDIS_READ_BUFFER;
  } else {
    buf = pub2redis(pr)->buf;
  }
  ssize_t i = fio_read(uuid, buf + internal->buf_pos,
                       REDIS_READ_BUFFER - internal->buf_pos);
  if (i <= 0)
    return;

  internal->buf_pos += i;
  i = resp_parse(&internal->parser, buf, internal->buf_pos);
  if (i) {
    memmove(buf, buf + internal->buf_pos - i, i);
  }
  internal->buf_pos = i;
}

/** Called when the connection was closed, but will not run concurrently */
static void redis_on_close(intptr_t uuid, fio_protocol_s *pr) {
  struct redis_engine_internal_s *internal =
      (struct redis_engine_internal_s *)pr;
  redis_internal_reset(internal);
  redis_engine_s *r;
  if (internal->on_message == resp_on_sub_message) {
    r = sub2redis(pr);
    fiobj_free(r->last_ch);
    r->last_ch = FIOBJ_INVALID;
    if (r->flag) {
      /* reconnection for subscription connection. */
      if (uuid != -1) {
        FIO_LOG_WARNING("(redis %d) subscription connection lost. "
                        "Reconnecting...",
                        (int)getpid());
      }
      fio_atomic_sub(&r->ref, 1);
      defer_redis_connect(r, internal);
    } else {
      redis_free(r);
    }
  } else {
    r = pub2redis(pr);
    if (r->flag && uuid != -1) {
      FIO_LOG_WARNING("(redis %d) publication connection lost. "
                      "Reconnecting...",
                      (int)getpid());
    }
    r->pub_sent = 0;
    fio_close(r->sub_data.uuid);
    redis_free(r);
  }
  (void)uuid;
}

/** Called before the facil.io reactor is shut down. */
static uint8_t redis_on_shutdown(intptr_t uuid, fio_protocol_s *pr) {
  fio_write2(uuid, .data.buffer = "*1\r\n$4\r\nQUIT\r\n", .length = 14,
             .after.dealloc = FIO_DEALLOC_NOOP);
  return 0;
  (void)pr;
}

/** Called on connection timeout. */
static void redis_sub_ping(intptr_t uuid, fio_protocol_s *pr) {
  fio_write2(uuid, .data.buffer = "*1\r\n$4\r\nPING\r\n", .length = 14,
             .after.dealloc = FIO_DEALLOC_NOOP);
  (void)pr;
}

/** Called on connection timeout. */
static void redis_pub_ping(intptr_t uuid, fio_protocol_s *pr) {
  redis_engine_s *r = pub2redis(pr);
  if (fio_ls_embd_any(&r->queue)) {
    FIO_LOG_WARNING("(redis) Redis server unresponsive, disconnecting.");
    fio_close(uuid);
    return;
  }
  redis_commands_s *cmd = fio_malloc(sizeof(*cmd) + 15);
  *cmd = (redis_commands_s){.cmd_len = 14};
  memcpy(cmd->cmd, "*1\r\n$4\r\nPING\r\n\0", 15);
  redis_attach_cmd(r, cmd);
}

/* *****************************************************************************
Connecting to Redis
***************************************************************************** */

static void redis_on_auth(fio_pubsub_engine_s *e, FIOBJ reply, void *udata) {
  if (FIOBJ_TYPE_IS(reply, FIOBJ_T_TRUE)) {
    fio_str_info_s s = fiobj_obj2cstr(reply);
    FIO_LOG_WARNING("(redis) Authentication FAILED."
                    "        %.*s",
                    (int)s.len, s.data);
  }
  (void)e;
  (void)udata;
}

static void redis_on_connect(intptr_t uuid, void *i_) {
  struct redis_engine_internal_s *i = i_;
  redis_engine_s *r;
  i->uuid = uuid;

  if (i->on_message == resp_on_sub_message) {
    r = sub2redis(i);
    if (r->auth_len) {
      fio_write2(uuid, .data.buffer = r->auth, .length = r->auth_len,
                 .after.dealloc = FIO_DEALLOC_NOOP);
    }
    fio_pubsub_reattach(&r->en);
    if (r->pub_data.uuid == -1) {
      defer_redis_connect(r, &r->pub_data);
    }
    FIO_LOG_INFO("(redis %d) subscription connection established.",
                 (int)getpid());
  } else {
    r = pub2redis(i);
    if (r->auth_len) {
      redis_commands_s *cmd = fio_malloc(sizeof(*cmd) + r->auth_len);
      *cmd =
          (redis_commands_s){.cmd_len = r->auth_len, .callback = redis_on_auth};
      memcpy(cmd->cmd, r->auth, r->auth_len);
      fio_lock(&r->lock);
      r->pub_sent = 0;
      fio_ls_embd_unshift(&r->queue, &cmd->node);
      redis_send_next_command_unsafe(r);
      fio_unlock(&r->lock);
    } else {
      fio_lock(&r->lock);
      r->pub_sent = 0;
      redis_send_next_command_unsafe(r);
      fio_unlock(&r->lock);
    }
    FIO_LOG_INFO("(redis %d) publication connection established.",
                 (int)getpid());
  }

  i->protocol.rsv = 0;
  fio_attach(uuid, &i->protocol);
  fio_timeout_set(uuid, r->ping_int);

  return;
}

static void redis_on_connect_failed(intptr_t uuid, void *i_) {
  struct redis_engine_internal_s *i = i_;
  i->uuid = -1;
  i->protocol.on_close(-1, &i->protocol);
  (void)uuid;
}

static void redis_connect(void *r_, void *i_) {
  redis_engine_s *r = r_;
  struct redis_engine_internal_s *i = i_;
  fio_lock(&r->lock_connection);
  if (r->flag == 0 || i->uuid != -1 || !fio_is_running()) {
    fio_unlock(&r->lock_connection);
    redis_free(r);
    return;
  }
  // fio_atomic_add(&r->ref, 1);
  i->uuid = fio_connect(.address = r->address, .port = r->port,
                        .on_connect = redis_on_connect, .udata = i,
                        .on_fail = redis_on_connect_failed);
  fio_unlock(&r->lock_connection);
}

/* *****************************************************************************
Engine / Bridge Callbacks (Root Process)
***************************************************************************** */

static void redis_on_subscribe_root(const fio_pubsub_engine_s *eng,
                                    fio_str_info_s channel,
                                    fio_match_fn match) {
  redis_engine_s *r = (redis_engine_s *)eng;
  if (r->sub_data.uuid != -1) {
    FIOBJ cmd = fiobj_str_buf(96 + channel.len);
    if (match == FIO_MATCH_GLOB)
      fiobj_str_write(cmd, "*2\r\n$10\r\nPSUBSCRIBE\r\n$", 22);
    else
      fiobj_str_write(cmd, "*2\r\n$9\r\nSUBSCRIBE\r\n$", 20);
    fiobj_str_write_i(cmd, channel.len);
    fiobj_str_write(cmd, "\r\n", 2);
    fiobj_str_write(cmd, channel.data, channel.len);
    fiobj_str_write(cmd, "\r\n", 2);
    // {
    //   fio_str_info_s s = fiobj_obj2cstr(cmd);
    //   fprintf(stderr, "(%d) Sending Subscription (%p):\n%s\n", getpid(),
    //           (void *)r->sub_data.uuid, s.data);
    // }
    fiobj_send_free(r->sub_data.uuid, cmd);
  }
}

static void redis_on_unsubscribe_root(const fio_pubsub_engine_s *eng,
                                      fio_str_info_s channel,
                                      fio_match_fn match) {
  redis_engine_s *r = (redis_engine_s *)eng;
  if (r->sub_data.uuid != -1) {
    fio_str_s *cmd = fio_str_new2();
    fio_str_capa_assert(cmd, 96 + channel.len);
    if (match == FIO_MATCH_GLOB)
      fio_str_write(cmd, "*2\r\n$12\r\nPUNSUBSCRIBE\r\n$", 24);
    else
      fio_str_write(cmd, "*2\r\n$11\r\nUNSUBSCRIBE\r\n$", 23);
    fio_str_write_i(cmd, channel.len);
    fio_str_write(cmd, "\r\n", 2);
    fio_str_write(cmd, channel.data, channel.len);
    fio_str_write(cmd, "\r\n", 2);
    // {
    //   fio_str_info_s s = fio_str_info(cmd);
    //   fprintf(stderr, "(%d) Cancel Subscription (%p):\n%s\n", getpid(),
    //           (void *)r->sub_data.uuid, s.data);
    // }
    fio_str_send_free2(r->sub_data.uuid, cmd);
  }
}

static void redis_on_publish_root(const fio_pubsub_engine_s *eng,
                                  fio_str_info_s channel, fio_str_info_s msg,
                                  uint8_t is_json) {
  redis_engine_s *r = (redis_engine_s *)eng;
  redis_commands_s *cmd = fio_malloc(sizeof(*cmd) + channel.len + msg.len + 96);
  *cmd = (redis_commands_s){.cmd_len = 0};
  memcpy(cmd->cmd, "*3\r\n$7\r\nPUBLISH\r\n$", 18);
  char *buf = (char *)cmd->cmd + 18;
  buf += fio_ltoa((void *)buf, channel.len, 10);
  *buf++ = '\r';
  *buf++ = '\n';
  memcpy(buf, channel.data, channel.len);
  buf += channel.len;
  *buf++ = '\r';
  *buf++ = '\n';
  *buf++ = '$';
  buf += fio_ltoa(buf, msg.len, 10);
  *buf++ = '\r';
  *buf++ = '\n';
  memcpy(buf, msg.data, msg.len);
  buf += msg.len;
  *buf++ = '\r';
  *buf++ = '\n';
  *buf = 0;
  FIO_LOG_DEBUG("(%d) Publishing:\n%s", (int)getpid(), cmd->cmd);
  cmd->cmd_len = (uintptr_t)buf - (uintptr_t)(cmd + 1);
  redis_attach_cmd(r, cmd);
  return;
  (void)is_json;
}

/* *****************************************************************************
Engine / Bridge Stub Callbacks (Child Process)
***************************************************************************** */

static void redis_on_mock_subscribe_child(const fio_pubsub_engine_s *eng,
                                          fio_str_info_s channel,
                                          fio_match_fn match) {
  /* do nothing, root process is notified about (un)subscriptions by facil.io */
  (void)eng;
  (void)channel;
  (void)match;
}

static void redis_on_publish_child(const fio_pubsub_engine_s *eng,
                                   fio_str_info_s channel, fio_str_info_s msg,
                                   uint8_t is_json) {
  /* attach engine data to channel (prepend) */
  fio_str_s tmp = FIO_STR_INIT;
  /* by using fio_str_s, short names are allocated on the stack */
  fio_str_info_s tmp_info = fio_str_resize(&tmp, channel.len + 8);
  fio_u2str64(tmp_info.data, (uint64_t)eng);
  memcpy(tmp_info.data + 8, channel.data, channel.len);
  /* forward publication request to Root */
  fio_publish(.filter = -1, .channel = tmp_info, .message = msg,
              .engine = FIO_PUBSUB_ROOT, .is_json = is_json);
  fio_str_free(&tmp);
  (void)eng;
}

/* *****************************************************************************
Root Publication Handler
***************************************************************************** */

/* listens to filter -1 and publishes and messages */
static void redis_on_internal_publish(fio_msg_s *msg) {
  if (msg->channel.len < 8)
    return; /* internal error, unexpected data */
  void *en = (void *)fio_str2u64(msg->channel.data);
  if (en != msg->udata1)
    return; /* should be delivered by a different engine */
  /* step after the engine data */
  msg->channel.len -= 8;
  msg->channel.data += 8;
  /* forward to publishing */
  FIO_LOG_DEBUG("Forwarding to engine %p, on channel %s", msg->udata1,
                msg->channel.data);
  redis_on_publish_root(msg->udata1, msg->channel, msg->msg, msg->is_json);
}

/* *****************************************************************************
Sending commands using the Root connection
***************************************************************************** */

/* callback from the Redis reply */
static void redis_forward_reply(fio_pubsub_engine_s *e, FIOBJ reply,
                                void *udata) {
  uint8_t *data = udata;
  fio_pubsub_engine_s *engine = (fio_pubsub_engine_s *)fio_str2u64(data + 0);
  void *callback = (void *)fio_str2u64(data + 8);
  if (engine != e || !callback) {
    FIO_LOG_DEBUG("Redis reply not forwarded (callback: %p)", callback);
    return;
  }
  int32_t pid = (int32_t)fio_str2u32(data + 24);
  FIOBJ rp = fiobj_obj2json(reply, 0);
  fio_publish(.filter = (-10 - (int32_t)pid), .channel.data = (char *)data,
              .channel.len = 28, .message = fiobj_obj2cstr(rp), .is_json = 1);
  fiobj_free(rp);
}

/* listens to channel -2 for commands that need to be sent (only ROOT) */
static void redis_on_internal_cmd(fio_msg_s *msg) {
  // void*(void *)fio_str2u64(msg->msg.data);
  fio_pubsub_engine_s *engine =
      (fio_pubsub_engine_s *)fio_str2u64(msg->channel.data + 0);
  if (engine != msg->udata1) {
    return;
  }
  redis_commands_s *cmd = fio_malloc(sizeof(*cmd) + msg->msg.len + 1 + 28);
  FIO_ASSERT_ALLOC(cmd);
  *cmd = (redis_commands_s){.callback = redis_forward_reply,
                            .udata = (cmd->cmd + msg->msg.len + 1),
                            .cmd_len = msg->msg.len};
  memcpy(cmd->cmd, msg->msg.data, msg->msg.len);
  memcpy(cmd->cmd + msg->msg.len + 1, msg->channel.data, 28);
  redis_attach_cmd((redis_engine_s *)engine, cmd);
  // fprintf(stderr, " *** Attached CMD (%d) ***\n%s\n", getpid(), cmd->cmd);
}

/* Listens on filter `-10 -getpid()` for incoming reply data */
static void redis_on_internal_reply(fio_msg_s *msg) {
  fio_pubsub_engine_s *engine =
      (fio_pubsub_engine_s *)fio_str2u64(msg->channel.data + 0);
  if (engine != msg->udata1) {
    FIO_LOG_DEBUG("Redis reply not forwarded (engine mismatch: %p != %p)",
                  (void *)engine, msg->udata1);
    return;
  }
  FIOBJ reply;
  fiobj_json2obj(&reply, msg->msg.data, msg->msg.len);
  void (*callback)(fio_pubsub_engine_s *, FIOBJ, void *) = (void (*)(
      fio_pubsub_engine_s *, FIOBJ, void *))fio_str2u64(msg->channel.data + 8);
  void *udata = (void *)fio_str2u64(msg->channel.data + 16);
  callback(engine, reply, udata);
  fiobj_free(reply);
}

/* publishes a Redis command to Root's filter -2 */
intptr_t redis_engine_send(fio_pubsub_engine_s *engine, FIOBJ command,
                           void (*callback)(fio_pubsub_engine_s *e, FIOBJ reply,
                                            void *udata),
                           void *udata) {
  if ((uintptr_t)engine < 4) {
    FIO_LOG_WARNING("(redis send) trying to use one of the core engines");
    return -1;
  }
  // if(fio_is_master()) {
  // FIOBJ resp = fiobj2resp_tmp(fio_str_info_s obj1, FIOBJ obj2);
  // TODO...
  // } else {
  /* forward publication request to Root */
  fio_str_s tmp = FIO_STR_INIT;
  fio_str_info_s ti = fio_str_resize(&tmp, 28);
  /* combine metadata */
  fio_u2str64(ti.data + 0, (uint64_t)engine);
  fio_u2str64(ti.data + 8, (uint64_t)callback);
  fio_u2str64(ti.data + 16, (uint64_t)udata);
  fio_u2str32(ti.data + 24, (uint32_t)getpid());
  FIOBJ cmd = fiobj2resp_tmp(command);
  fio_publish(.filter = -2, .channel = ti, .message = fiobj_obj2cstr(cmd),
              .engine = FIO_PUBSUB_ROOT, .is_json = 0);
  fio_str_free(&tmp);
  // }
  return 0;
}

/* *****************************************************************************
Redis Engine Creation
***************************************************************************** */

static void redis_on_facil_start(void *r_) {
  redis_engine_s *r = r_;
  r->flag = 1;
  if (!fio_is_valid(r->sub_data.uuid)) {
    defer_redis_connect(r, &r->sub_data);
  }
}
static void redis_on_facil_shutdown(void *r_) {
  redis_engine_s *r = r_;
  r->flag = 0;
}

static void redis_on_engine_fork(void *r_) {
  redis_engine_s *r = r_;
  r->flag = 0;
  r->lock = FIO_LOCK_INIT;
  fio_force_close(r->sub_data.uuid);
  r->sub_data.uuid = -1;
  fio_force_close(r->pub_data.uuid);
  r->pub_data.uuid = -1;
  while (fio_ls_embd_any(&r->queue)) {
    redis_commands_s *cmd =
        FIO_LS_EMBD_OBJ(redis_commands_s, node, fio_ls_embd_pop(&r->queue));
    fio_free(cmd);
  }
  r->en = (fio_pubsub_engine_s){
      .subscribe = redis_on_mock_subscribe_child,
      .unsubscribe = redis_on_mock_subscribe_child,
      .publish = redis_on_publish_child,
  };
  fio_unsubscribe(r->publication_forwarder);
  r->publication_forwarder = NULL;
  fio_unsubscribe(r->cmd_forwarder);
  r->cmd_forwarder = NULL;
  fio_unsubscribe(r->cmd_reply);
  r->cmd_reply =
      fio_subscribe(.filter = -10 - (int32_t)getpid(),
                    .on_message = redis_on_internal_reply, .udata1 = r);
}

fio_pubsub_engine_s *redis_engine_create
FIO_IGNORE_MACRO(struct redis_engine_create_args args) {
  if (getpid() != fio_parent_pid()) {
    FIO_LOG_FATAL("(redis) Redis engine initialization can only "
                  "be performed in the Root process.");
    kill(0, SIGINT);
    fio_stop();
    return NULL;
  }
  if (!args.address.len && args.address.data)
    args.address.len = strlen(args.address.data);
  if (!args.port.len && args.port.data)
    args.port.len = strlen(args.port.data);
  if (!args.auth.len && args.auth.data) {
    args.auth.len = strlen(args.auth.data);
  }

  if (!args.address.data || !args.address.len) {
    args.address = (fio_str_info_s){.len = 9, .data = (char *)"localhost"};
  }
  if (!args.port.data || !args.port.len) {
    args.port = (fio_str_info_s){.len = 4, .data = (char *)"6379"};
  }
  redis_engine_s *r =
      fio_malloc(sizeof(*r) + args.port.len + 1 + args.address.len + 1 +
                 args.auth.len + 1 + (REDIS_READ_BUFFER * 2));
  FIO_ASSERT_ALLOC(r);
  *r = (redis_engine_s){
      .en =
          {
              .subscribe = redis_on_subscribe_root,
              .unsubscribe = redis_on_unsubscribe_root,
              .publish = redis_on_publish_root,
          },
      .pub_data =
          {
              .protocol =
                  {
                      .on_data = redis_on_data,
                      .on_close = redis_on_close,
                      .on_shutdown = redis_on_shutdown,
                      .ping = redis_pub_ping,
                  },
              .uuid = -1,
              .on_message = resp_on_pub_message,
          },
      .sub_data =
          {
              .protocol =
                  {
                      .on_data = redis_on_data,
                      .on_close = redis_on_close,
                      .on_shutdown = redis_on_shutdown,
                      .ping = redis_sub_ping,
                  },
              .on_message = resp_on_sub_message,
              .uuid = -1,
          },
      .publication_forwarder =
          fio_subscribe(.filter = -1, .udata1 = r,
                        .on_message = redis_on_internal_publish),
      .cmd_forwarder = fio_subscribe(.filter = -2, .udata1 = r,
                                     .on_message = redis_on_internal_cmd),
      .cmd_reply =
          fio_subscribe(.filter = -10 - (uint32_t)getpid(), .udata1 = r,
                        .on_message = redis_on_internal_reply),
      .address = ((char *)(r + 1) + (REDIS_READ_BUFFER * 2)),
      .port =
          ((char *)(r + 1) + (REDIS_READ_BUFFER * 2) + args.address.len + 1),
      .auth = ((char *)(r + 1) + (REDIS_READ_BUFFER * 2) + args.address.len +
               args.port.len + 2),
      .auth_len = args.auth.len,
      .ref = 1,
      .queue = FIO_LS_INIT(r->queue),
      .lock = FIO_LOCK_INIT,
      .lock_connection = FIO_LOCK_INIT,
      .ping_int = args.ping_interval,
      .flag = 1,
  };
  memcpy(r->address, args.address.data, args.address.len);
  memcpy(r->port, args.port.data, args.port.len);
  if (args.auth.len)
    memcpy(r->auth, args.auth.data, args.auth.len);
  fio_pubsub_attach(&r->en);
  redis_on_facil_start(r);
  fio_state_callback_add(FIO_CALL_IN_CHILD, redis_on_engine_fork, r);
  fio_state_callback_add(FIO_CALL_ON_SHUTDOWN, redis_on_facil_shutdown, r);
  /* if restarting */
  fio_state_callback_add(FIO_CALL_PRE_START, redis_on_facil_start, r);

  FIO_LOG_DEBUG("Redis engine initialized %p", (void *)r);
  return &r->en;
}

/* *****************************************************************************
Redis Engine Destruction
***************************************************************************** */

void redis_engine_destroy(fio_pubsub_engine_s *engine) {
  redis_engine_s *r = (redis_engine_s *)engine;
  r->flag = 0;
  fio_pubsub_detach(&r->en);
  fio_state_callback_remove(FIO_CALL_IN_CHILD, redis_on_engine_fork, r);
  fio_state_callback_remove(FIO_CALL_ON_SHUTDOWN, redis_on_facil_shutdown, r);
  fio_state_callback_remove(FIO_CALL_PRE_START, redis_on_facil_start, r);
  FIO_LOG_DEBUG("Redis engine destroyed %p", (void *)r);
  redis_free(r);
}
