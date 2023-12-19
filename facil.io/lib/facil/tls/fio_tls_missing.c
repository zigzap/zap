/*
Copyright: Boaz Segev, 2018-2019
License: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#include <fio.h>

/**
 * This implementation of the facil.io SSL/TLS wrapper API is the default
 * implementation that will be used when no SSL/TLS library is available...
 *
 * ... without modification, this implementation crashes the program.
 *
 * The implementation can be USED AS A TEMPLATE for future implementations.
 *
 * This implementation is optimized for ease of development rather than memory
 * consumption.
 */
#include "fio_tls.h"

#if !defined(FIO_TLS_FOUND) /* Library compiler flags */

#define REQUIRE_LIBRARY()
#define FIO_TLS_WEAK

/* TODO: delete me! */
#undef FIO_TLS_WEAK
#define FIO_TLS_WEAK __attribute__((weak))
#if !FIO_IGNORE_TLS_IF_MISSING
#undef REQUIRE_LIBRARY
#define REQUIRE_LIBRARY()                                                      \
  FIO_LOG_FATAL("No supported SSL/TLS library available.");                    \
  exit(-1);
#endif
/* STOP deleting after this line */

/* *****************************************************************************
The SSL/TLS helper data types (can be left as is)
***************************************************************************** */
#define FIO_INCLUDE_STR 1
#define FIO_FORCE_MALLOC_TMP 1
#include <fio.h>

typedef struct {
  fio_str_s private_key;
  fio_str_s public_key;
  fio_str_s password;
} cert_s;

static inline int fio_tls_cert_cmp(const cert_s *dest, const cert_s *src) {
  return fio_str_iseq(&dest->private_key, &src->private_key);
}
static inline void fio_tls_cert_copy(cert_s *dest, cert_s *src) {
  *dest = (cert_s){
      .private_key = FIO_STR_INIT,
      .public_key = FIO_STR_INIT,
      .password = FIO_STR_INIT,
  };
  fio_str_concat(&dest->private_key, &src->private_key);
  fio_str_concat(&dest->public_key, &src->public_key);
  fio_str_concat(&dest->password, &src->password);
}
static inline void fio_tls_cert_destroy(cert_s *obj) {
  fio_str_free(&obj->private_key);
  fio_str_free(&obj->public_key);
  fio_str_free(&obj->password);
}

#define FIO_ARY_NAME cert_ary
#define FIO_ARY_TYPE cert_s
#define FIO_ARY_COMPARE(k1, k2) (fio_tls_cert_cmp(&(k1), &(k2)))
#define FIO_ARY_COPY(dest, obj) fio_tls_cert_copy(&(dest), &(obj))
#define FIO_ARY_DESTROY(key) fio_tls_cert_destroy(&(key))
#define FIO_FORCE_MALLOC_TMP 1
#include <fio.h>

typedef struct {
  fio_str_s pem;
} trust_s;

static inline int fio_tls_trust_cmp(const trust_s *dest, const trust_s *src) {
  return fio_str_iseq(&dest->pem, &src->pem);
}
static inline void fio_tls_trust_copy(trust_s *dest, trust_s *src) {
  *dest = (trust_s){
      .pem = FIO_STR_INIT,
  };
  fio_str_concat(&dest->pem, &src->pem);
}
static inline void fio_tls_trust_destroy(trust_s *obj) {
  fio_str_free(&obj->pem);
}

#define FIO_ARY_NAME trust_ary
#define FIO_ARY_TYPE trust_s
#define FIO_ARY_COMPARE(k1, k2) (fio_tls_trust_cmp(&(k1), &(k2)))
#define FIO_ARY_COPY(dest, obj) fio_tls_trust_copy(&(dest), &(obj))
#define FIO_ARY_DESTROY(key) fio_tls_trust_destroy(&(key))
#define FIO_FORCE_MALLOC_TMP 1
#include <fio.h>

typedef struct {
  fio_str_s name; /* fio_str_s provides cache locality for small strings */
  void (*on_selected)(intptr_t uuid, void *udata_connection, void *udata_tls);
  void *udata_tls;
  void (*on_cleanup)(void *udata_tls);
} alpn_s;

static inline int fio_alpn_cmp(const alpn_s *dest, const alpn_s *src) {
  return fio_str_iseq(&dest->name, &src->name);
}
static inline void fio_alpn_copy(alpn_s *dest, alpn_s *src) {
  *dest = (alpn_s){
      .name = FIO_STR_INIT,
      .on_selected = src->on_selected,
      .udata_tls = src->udata_tls,
      .on_cleanup = src->on_cleanup,
  };
  fio_str_concat(&dest->name, &src->name);
}
static inline void fio_alpn_destroy(alpn_s *obj) {
  if (obj->on_cleanup)
    obj->on_cleanup(obj->udata_tls);
  fio_str_free(&obj->name);
}

#define FIO_SET_NAME alpn_list
#define FIO_SET_OBJ_TYPE alpn_s
#define FIO_SET_OBJ_COMPARE(k1, k2) fio_alpn_cmp(&(k1), &(k2))
#define FIO_SET_OBJ_COPY(dest, obj) fio_alpn_copy(&(dest), &(obj))
#define FIO_SET_OBJ_DESTROY(key) fio_alpn_destroy(&(key))
#define FIO_FORCE_MALLOC_TMP 1
#include <fio.h>

/* *****************************************************************************
The SSL/TLS Context type
***************************************************************************** */

/** An opaque type used for the SSL/TLS functions. */
struct fio_tls_s {
  size_t ref;       /* Reference counter, to guards the ALPN registry */
  alpn_list_s alpn; /* ALPN is the name for the protocol selection extension */

  /*** the next two components could be optimized away with tweaking stuff ***/

  cert_ary_s sni;    /* SNI (server name extension) stores ID certificates */
  trust_ary_s trust; /* Trusted certificate registry (peer verification) */

  /************ TODO: implementation data fields go here ******************/
};

/* *****************************************************************************
ALPN Helpers
***************************************************************************** */

/** Returns a pointer to the ALPN data (callback, etc') IF exists in the TLS. */
FIO_FUNC inline alpn_s *alpn_find(fio_tls_s *tls, char *name, size_t len) {
  alpn_s tmp = {.name = FIO_STR_INIT_STATIC2(name, len)};
  alpn_list__map_s_ *pos =
      alpn_list__find_map_pos_(&tls->alpn, fio_str_hash(&tmp.name), tmp);
  if (!pos || !pos->pos)
    return NULL;
  return &pos->pos->obj;
}

/** Adds an ALPN data object to the ALPN "list" (set) */
FIO_FUNC inline void alpn_add(
    fio_tls_s *tls, const char *protocol_name,
    void (*on_selected)(intptr_t uuid, void *udata_connection, void *udata_tls),
    void *udata_tls, void (*on_cleanup)(void *udata_tls)) {
  alpn_s tmp = {
      .name = FIO_STR_INIT_STATIC(protocol_name),
      .on_selected = on_selected,
      .udata_tls = udata_tls,
      .on_cleanup = on_cleanup,
  };
  if (fio_str_len(&tmp.name) > 255) {
    FIO_LOG_ERROR("ALPN protocol names are limited to 255 bytes.");
    return;
  }
  alpn_list_overwrite(&tls->alpn, fio_str_hash(&tmp.name), tmp, NULL);
  tmp.on_cleanup = NULL;
  fio_alpn_destroy(&tmp);
}

/** Returns a pointer to the default (first) ALPN object in the TLS (if any). */
FIO_FUNC inline alpn_s *alpn_default(fio_tls_s *tls) {
  if (!tls || !alpn_list_count(&tls->alpn) || !tls->alpn.ordered)
    return NULL;
  return &tls->alpn.ordered[0].obj;
}

typedef struct {
  alpn_s alpn;
  intptr_t uuid;
  void *udata_connection;
} alpn_task_s;

FIO_FUNC inline void alpn_select___task(void *t_, void *ignr_) {
  alpn_task_s *t = t_;
  if (fio_is_valid(t->uuid))
    t->alpn.on_selected(t->uuid, t->udata_connection, t->alpn.udata_tls);
  fio_free(t);
  (void)ignr_;
}

/** Schedules the ALPN protocol callback. */
FIO_FUNC inline void alpn_select(alpn_s *alpn, intptr_t uuid,
                                 void *udata_connection) {
  if (!alpn || !alpn->on_selected)
    return;
  alpn_task_s *t = fio_malloc(sizeof(*t));
  *t = (alpn_task_s){
      .alpn = *alpn,
      .uuid = uuid,
      .udata_connection = udata_connection,
  };
  /* move task out of the socket's lock */
  fio_defer(alpn_select___task, t, NULL);
}

/* *****************************************************************************
SSL/TLS Context (re)-building - TODO: add implementation details
***************************************************************************** */

/** Called when the library specific data for the context should be destroyed */
static void fio_tls_destroy_context(fio_tls_s *tls) {
  /* TODO: Library specific implementation */
  FIO_LOG_DEBUG("destroyed TLS context %p", (void *)tls);
}

/** Called when the library specific data for the context should be built */
static void fio_tls_build_context(fio_tls_s *tls) {
  fio_tls_destroy_context(tls);
  /* TODO: Library specific implementation */

  /* Certificates */
  FIO_ARY_FOR(&tls->sni, pos) {
    fio_str_info_s k = fio_str_info(&pos->private_key);
    fio_str_info_s p = fio_str_info(&pos->public_key);
    fio_str_info_s pw = fio_str_info(&pos->password);
    if (p.len && k.len) {
      /* TODO: attache certificate */
      (void)pw;
    } else {
      /* TODO: self signed certificate */
    }
  }

  /* ALPN Protocols */
  FIO_SET_FOR_LOOP(&tls->alpn, pos) {
    fio_str_info_s name = fio_str_info(&pos->obj.name);
    (void)name;
    // map to pos->callback;
  }

  /* Peer Verification / Trust */
  if (trust_ary_count(&tls->trust)) {
    /* TODO: enable peer verification */

    /* TODO: Add each ceriticate in the PEM to the trust "store" */
    FIO_ARY_FOR(&tls->trust, pos) {
      fio_str_info_s pem = fio_str_info(&pos->pem);
      (void)pem;
    }
  }

  FIO_LOG_DEBUG("(re)built TLS context %p", (void *)tls);
}

/* *****************************************************************************
SSL/TLS RW Hooks - TODO: add implementation details
***************************************************************************** */

/* TODO: this is an example implementation - fix for specific library. */

#define TLS_BUFFER_LENGTH (1 << 15)
typedef struct {
  fio_tls_s *tls;
  size_t len;
  uint8_t alpn_ok;
  char buffer[TLS_BUFFER_LENGTH];
} buffer_s;

/**
 * Implement reading from a file descriptor. Should behave like the file
 * system `read` call, including the setup or errno to EAGAIN / EWOULDBLOCK.
 *
 * Note: facil.io library functions MUST NEVER be called by any r/w hook, or a
 * deadlock might occur.
 */
static ssize_t fio_tls_read(intptr_t uuid, void *udata, void *buf,
                            size_t count) {
  ssize_t ret = read(fio_uuid2fd(uuid), buf, count);
  if (ret > 0) {
    FIO_LOG_DEBUG("Read %zd bytes from %p", ret, (void *)uuid);
  }
  return ret;
  (void)udata;
}

/**
 * When implemented, this function will be called to flush any data remaining
 * in the internal buffer.
 *
 * The function should return the number of bytes remaining in the internal
 * buffer (0 is a valid response) or -1 (on error).
 *
 * Note: facil.io library functions MUST NEVER be called by any r/w hook, or a
 * deadlock might occur.
 */
static ssize_t fio_tls_flush(intptr_t uuid, void *udata) {
  buffer_s *buffer = udata;
  if (!buffer->len) {
    FIO_LOG_DEBUG("Flush empty for %p", (void *)uuid);
    return 0;
  }
  ssize_t r = write(fio_uuid2fd(uuid), buffer->buffer, buffer->len);
  if (r < 0)
    return -1;
  if (r == 0) {
    errno = ECONNRESET;
    return -1;
  }
  size_t len = buffer->len - r;
  if (len)
    memmove(buffer->buffer, buffer->buffer + r, len);
  buffer->len = len;
  FIO_LOG_DEBUG("Sent %zd bytes to %p", r, (void *)uuid);
  return r;
}

/**
 * Implement writing to a file descriptor. Should behave like the file system
 * `write` call.
 *
 * If an internal buffer is implemented and it is full, errno should be set to
 * EWOULDBLOCK and the function should return -1.
 *
 * Note: facil.io library functions MUST NEVER be called by any r/w hook, or a
 * deadlock might occur.
 */
static ssize_t fio_tls_write(intptr_t uuid, void *udata, const void *buf,
                             size_t count) {
  buffer_s *buffer = udata;
  size_t can_copy = TLS_BUFFER_LENGTH - buffer->len;
  if (can_copy > count)
    can_copy = count;
  if (!can_copy)
    goto would_block;
  memcpy(buffer->buffer + buffer->len, buf, can_copy);
  buffer->len += can_copy;
  FIO_LOG_DEBUG("Copied %zu bytes to %p", can_copy, (void *)uuid);
  fio_tls_flush(uuid, udata);
  return can_copy;
would_block:
  errno = EWOULDBLOCK;
  return -1;
}

/**
 * The `close` callback should close the underlying socket / file descriptor.
 *
 * If the function returns a non-zero value, it will be called again after an
 * attempt to flush the socket and any pending outgoing buffer.
 *
 * Note: facil.io library functions MUST NEVER be called by any r/w hook, or a
 * deadlock might occur.
 * */
static ssize_t fio_tls_before_close(intptr_t uuid, void *udata) {
  FIO_LOG_DEBUG("The `before_close` callback was called for %p", (void *)uuid);
  return 1;
  (void)udata;
}
/**
 * Called to perform cleanup after the socket was closed.
 * */
static void fio_tls_cleanup(void *udata) {
  buffer_s *buffer = udata;
  /* make sure the ALPN callback was called, just in case cleanup is required */
  if (!buffer->alpn_ok) {
    alpn_select(alpn_default(buffer->tls), -1, NULL /* ALPN udata */);
  }
  fio_tls_destroy(buffer->tls); /* manage reference count */
  fio_free(udata);
}

static fio_rw_hook_s FIO_TLS_HOOKS = {
    .read = fio_tls_read,
    .write = fio_tls_write,
    .before_close = fio_tls_before_close,
    .flush = fio_tls_flush,
    .cleanup = fio_tls_cleanup,
};

static size_t fio_tls_handshake(intptr_t uuid, void *udata) {
  /*TODO: test for handshake completion */
  if (0 /*handshake didn't complete */)
    return 0;
  if (fio_rw_hook_replace_unsafe(uuid, &FIO_TLS_HOOKS, udata) == 0) {
    FIO_LOG_DEBUG("Completed TLS handshake for %p", (void *)uuid);
    /*
     * make sure the connection is re-added to the reactor...
     * in case, while waiting for ALPN, it was suspended for missing a protocol.
     */
    fio_force_event(uuid, FIO_EVENT_ON_DATA);
  } else {
    FIO_LOG_DEBUG("Something went wrong during TLS handshake for %p",
                  (void *)uuid);
  }
  return 1;
}

static ssize_t fio_tls_read4handshake(intptr_t uuid, void *udata, void *buf,
                                      size_t count) {
  FIO_LOG_DEBUG("TLS handshake from read %p", (void *)uuid);
  if (fio_tls_handshake(uuid, udata))
    return fio_tls_read(uuid, udata, buf, count);
  errno = EWOULDBLOCK;
  return -1;
}

static ssize_t fio_tls_write4handshake(intptr_t uuid, void *udata,
                                       const void *buf, size_t count) {
  FIO_LOG_DEBUG("TLS handshake from write %p", (void *)uuid);
  if (fio_tls_handshake(uuid, udata))
    return fio_tls_write(uuid, udata, buf, count);
  errno = EWOULDBLOCK;
  return -1;
}

static ssize_t fio_tls_flush4handshake(intptr_t uuid, void *udata) {
  FIO_LOG_DEBUG("TLS handshake from flush %p", (void *)uuid);
  if (fio_tls_handshake(uuid, udata))
    return fio_tls_flush(uuid, udata);
  /* TODO: return a positive value only if handshake requires a write */
  return 1;
}
static fio_rw_hook_s FIO_TLS_HANDSHAKE_HOOKS = {
    .read = fio_tls_read4handshake,
    .write = fio_tls_write4handshake,
    .before_close = fio_tls_before_close,
    .flush = fio_tls_flush4handshake,
    .cleanup = fio_tls_cleanup,
};

static inline void fio_tls_attach2uuid(intptr_t uuid, fio_tls_s *tls,
                                       void *udata, uint8_t is_server) {
  fio_atomic_add(&tls->ref, 1); /* manage reference count */
  /* TODO: this is only an example implementation - fix for specific library */
  if (is_server) {
    /* Server mode (accept) */
    FIO_LOG_DEBUG("Attaching TLS read/write hook for %p (server mode).",
                  (void *)uuid);
  } else {
    /* Client mode (connect) */
    FIO_LOG_DEBUG("Attaching TLS read/write hook for %p (client mode).",
                  (void *)uuid);
  }
  /* common implementation (TODO) */
  buffer_s *connection_data = fio_malloc(sizeof(*connection_data));
  FIO_ASSERT_ALLOC(connection_data);
  fio_rw_hook_set(uuid, &FIO_TLS_HANDSHAKE_HOOKS,
                  connection_data); /* 32Kb buffer */
  alpn_select(alpn_default(tls), uuid, udata);
  connection_data->alpn_ok = 1;
}

/* *****************************************************************************
SSL/TLS API implementation - this can be pretty much used as is...
***************************************************************************** */

/**
 * Creates a new SSL/TLS context / settings object with a default certificate
 * (if any).
 */
fio_tls_s *FIO_TLS_WEAK fio_tls_new(const char *server_name, const char *cert,
                                    const char *key, const char *pk_password) {
  REQUIRE_LIBRARY();
  fio_tls_s *tls = calloc(sizeof(*tls), 1);
  tls->ref = 1;
  fio_tls_cert_add(tls, server_name, key, cert, pk_password);
  return tls;
}

/**
 * Adds a certificate  a new SSL/TLS context / settings object.
 */
void FIO_TLS_WEAK fio_tls_cert_add(fio_tls_s *tls, const char *server_name,
                                   const char *cert, const char *key,
                                   const char *pk_password) {
  REQUIRE_LIBRARY();
  cert_s c = {
      .private_key = FIO_STR_INIT,
      .public_key = FIO_STR_INIT,
      .password = FIO_STR_INIT_STATIC2(pk_password,
                                       (pk_password ? strlen(pk_password) : 0)),
  };
  if (key && cert) {
    if (fio_str_readfile(&c.private_key, key, 0, 0).data == NULL)
      goto file_missing;
    if (fio_str_readfile(&c.public_key, cert, 0, 0).data == NULL)
      goto file_missing;
    cert_ary_push(&tls->sni, c);
  } else if (server_name) {
    /* Self-Signed TLS Certificates */
    c.private_key = FIO_STR_INIT_STATIC(server_name);
    cert_ary_push(&tls->sni, c);
  }
  fio_tls_cert_destroy(&c);
  fio_tls_build_context(tls);
  return;
file_missing:
  FIO_LOG_FATAL("TLS certificate file missing for either %s or %s or both.",
                key, cert);
  exit(-1);
}

/**
 * Adds an ALPN protocol callback to the SSL/TLS context.
 *
 * The first protocol added will act as the default protocol to be selected.
 *
 * The callback should accept the `uuid`, the user data pointer passed to either
 * `fio_tls_accept` or `fio_tls_connect` (here: `udata_connetcion`) and the user
 * data pointer passed to the `fio_tls_alpn_add` function (`udata_tls`).
 *
 * The `on_cleanup` callback will be called when the TLS object is destroyed (or
 * `fio_tls_alpn_add` is called again with the same protocol name). The
 * `udata_tls` argumrnt will be passed along, as is, to the callback (if set).
 *
 * Except for the `tls` and `protocol_name` arguments, all arguments can be
 * NULL.
 */
void FIO_TLS_WEAK fio_tls_alpn_add(
    fio_tls_s *tls, const char *protocol_name,
    void (*on_selected)(intptr_t uuid, void *udata_connection, void *udata_tls),
    void *udata_tls, void (*on_cleanup)(void *udata_tls)) {
  REQUIRE_LIBRARY();
  alpn_add(tls, protocol_name, on_selected, udata_tls, on_cleanup);
  fio_tls_build_context(tls);
}

/**
 * Returns the number of registered ALPN protocol names.
 *
 * This could be used when deciding if protocol selection should be delegated to
 * the ALPN mechanism, or whether a protocol should be immediately assigned.
 *
 * If no ALPN protocols are registered, zero (0) is returned.
 */
uintptr_t FIO_TLS_WEAK fio_tls_alpn_count(fio_tls_s *tls) {
  return tls ? alpn_list_count(&tls->alpn) : 0;
}

/**
 * Adds a certificate to the "trust" list, which automatically adds a peer
 * verification requirement.
 *
 *      fio_tls_trust(tls, "google-ca.pem" );
 */
void FIO_TLS_WEAK fio_tls_trust(fio_tls_s *tls, const char *public_cert_file) {
  REQUIRE_LIBRARY();
  trust_s c = {
      .pem = FIO_STR_INIT,
  };
  if (!public_cert_file)
    return;
  if (fio_str_readfile(&c.pem, public_cert_file, 0, 0).data == NULL)
    goto file_missing;
  trust_ary_push(&tls->trust, c);
  fio_tls_trust_destroy(&c);
  fio_tls_build_context(tls);
  return;
file_missing:
  FIO_LOG_FATAL("TLS certificate file missing for %s ", public_cert_file);
  exit(-1);
}

/**
 * Establishes an SSL/TLS connection as an SSL/TLS Server, using the specified
 * context / settings object.
 *
 * The `uuid` should be a socket UUID that is already connected to a peer (i.e.,
 * the result of `fio_accept`).
 *
 * The `udata` is an opaque user data pointer that is passed along to the
 * protocol selected (if any protocols were added using `fio_tls_alpn_add`).
 */
void FIO_TLS_WEAK fio_tls_accept(intptr_t uuid, fio_tls_s *tls, void *udata) {
  REQUIRE_LIBRARY();
  fio_tls_attach2uuid(uuid, tls, udata, 1);
}

/**
 * Establishes an SSL/TLS connection as an SSL/TLS Client, using the specified
 * context / settings object.
 *
 * The `uuid` should be a socket UUID that is already connected to a peer (i.e.,
 * one received by a `fio_connect` specified callback `on_connect`).
 *
 * The `udata` is an opaque user data pointer that is passed along to the
 * protocol selected (if any protocols were added using `fio_tls_alpn_add`).
 */
void FIO_TLS_WEAK fio_tls_connect(intptr_t uuid, fio_tls_s *tls, void *udata) {
  REQUIRE_LIBRARY();
  fio_tls_attach2uuid(uuid, tls, udata, 0);
}

/**
 * Increase the reference count for the TLS object.
 *
 * Decrease with `fio_tls_destroy`.
 */
void FIO_TLS_WEAK fio_tls_dup(fio_tls_s *tls) { fio_atomic_add(&tls->ref, 1); }

/**
 * Destroys the SSL/TLS context / settings object and frees any related
 * resources / memory.
 */
void FIO_TLS_WEAK fio_tls_destroy(fio_tls_s *tls) {
  if (!tls)
    return;
  REQUIRE_LIBRARY();
  if (fio_atomic_sub(&tls->ref, 1))
    return;
  fio_tls_destroy_context(tls);
  alpn_list_free(&tls->alpn);
  cert_ary_free(&tls->sni);
  trust_ary_free(&tls->trust);
  free(tls);
}

#endif /* Library compiler flags */
