/*
Copyright: Boaz Segev, 2016-2019
License: MIT

Feel free to copy, use and enjoy according to the license provided.
*/

#include <fio.h>

#include <http1.h>
#include <http_internal.h>

#include <ctype.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef HAVE_TM_TM_ZONE
#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) ||     \
    defined(__DragonFly__) || defined(__bsdi__) || defined(__ultrix) ||        \
    (defined(__APPLE__) && defined(__MACH__)) ||                               \
    (defined(__sun) && !defined(__SVR4))
/* Known BSD systems */
#define HAVE_TM_TM_ZONE 1
#elif defined(__GLIBC__) && defined(_BSD_SOURCE)
/* GNU systems with _BSD_SOURCE */
#define HAVE_TM_TM_ZONE 1
#else
#define HAVE_TM_TM_ZONE 0
#endif
#endif

/* *****************************************************************************
SSL/TLS patch
***************************************************************************** */

/**
 * Adds an ALPN protocol callback to the SSL/TLS context.
 *
 * The first protocol added will act as the default protocol to be selected.
 */
void __attribute__((weak))
fio_tls_alpn_add(void *tls, const char *protocol_name,
                 void (*callback)(intptr_t uuid, void *udata_connection,
                                  void *udata_tls),
                 void *udata_tls, void (*on_cleanup)(void *udata_tls)) {
  FIO_LOG_FATAL("HTTP SSL/TLS required but unavailable!");
  exit(-1);
  (void)tls;
  (void)protocol_name;
  (void)callback;
  (void)on_cleanup;
  (void)udata_tls;
}
#pragma weak fio_tls_alpn_add

/* *****************************************************************************
Small Helpers
***************************************************************************** */
static inline int hex2byte(uint8_t *dest, const uint8_t *source);

static inline void add_content_length(http_s *r, uintptr_t length) {
  static uint64_t cl_hash = 0;
  if (!cl_hash)
    cl_hash = fiobj_hash_string("content-length", 14);
  if (!fiobj_hash_get2(r->private_data.out_headers, cl_hash)) {
    fiobj_hash_set(r->private_data.out_headers, HTTP_HEADER_CONTENT_LENGTH,
                   fiobj_num_new(length));
  }
}
static inline void add_content_type(http_s *r) {
  static uint64_t ct_hash = 0;
  if (!ct_hash)
    ct_hash = fiobj_hash_string("content-type", 12);
  if (!fiobj_hash_get2(r->private_data.out_headers, ct_hash)) {
    fiobj_hash_set(r->private_data.out_headers, HTTP_HEADER_CONTENT_TYPE,
                   http_mimetype_find2(r->path));
  }
}

static FIOBJ current_date;
static time_t last_date_added;
static fio_lock_i date_lock;
static inline void add_date(http_s *r) {
  static uint64_t date_hash = 0;
  if (!date_hash)
    date_hash = fiobj_hash_string("date", 4);
  static uint64_t mod_hash = 0;
  if (!mod_hash)
    mod_hash = fiobj_hash_string("last-modified", 13);

  if (fio_last_tick().tv_sec > last_date_added) {
    fio_lock(&date_lock);
    if (fio_last_tick().tv_sec > last_date_added) { /* retest inside lock */
      FIOBJ tmp = fiobj_str_buf(32);
      FIOBJ old = current_date;
      fiobj_str_resize(
          tmp, http_time2str(fiobj_obj2cstr(tmp).data, fio_last_tick().tv_sec));
      last_date_added = fio_last_tick().tv_sec;
      current_date = tmp;
      fiobj_free(old);
    }
    fio_unlock(&date_lock);
  }

  if (!fiobj_hash_get2(r->private_data.out_headers, date_hash)) {
    fiobj_hash_set(r->private_data.out_headers, HTTP_HEADER_DATE,
                   fiobj_dup(current_date));
  }
  if (r->status_str == FIOBJ_INVALID &&
      !fiobj_hash_get2(r->private_data.out_headers, mod_hash)) {
    fiobj_hash_set(r->private_data.out_headers, HTTP_HEADER_LAST_MODIFIED,
                   fiobj_dup(current_date));
  }
}

struct header_writer_s {
  FIOBJ dest;
  FIOBJ name;
  FIOBJ value;
};

static int write_header(FIOBJ o, void *w_) {
  struct header_writer_s *w = w_;
  if (!o)
    return 0;
  if (fiobj_hash_key_in_loop()) {
    w->name = fiobj_hash_key_in_loop();
  }
  if (FIOBJ_TYPE_IS(o, FIOBJ_T_ARRAY)) {
    fiobj_each1(o, 0, write_header, w);
    return 0;
  }
  fio_str_info_s name = fiobj_obj2cstr(w->name);
  fio_str_info_s str = fiobj_obj2cstr(o);
  if (!str.data)
    return 0;
  fiobj_str_write(w->dest, name.data, name.len);
  fiobj_str_write(w->dest, ":", 1);
  fiobj_str_write(w->dest, str.data, str.len);
  fiobj_str_write(w->dest, "\r\n", 2);
  return 0;
}

static char invalid_cookie_name_char[256];

static char invalid_cookie_value_char[256];
/* *****************************************************************************
The Request / Response type and functions
***************************************************************************** */
static const char hex_chars[] = {'0', '1', '2', '3', '4', '5', '6', '7',
                                 '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

/**
 * Sets a response header, taking ownership of the value object, but NOT the
 * name object (so name objects could be reused in future responses).
 *
 * Returns -1 on error and 0 on success.
 */
int http_set_header(http_s *r, FIOBJ name, FIOBJ value) {
  if (HTTP_INVALID_HANDLE(r) || !name) {
    fiobj_free(value);
    return -1;
  }
  set_header_add(r->private_data.out_headers, name, value);
  return 0;
}
/**
 * Sets a response header.
 *
 * Returns -1 on error and 0 on success.
 */
int http_set_header2(http_s *r, fio_str_info_s n, fio_str_info_s v) {
  if (HTTP_INVALID_HANDLE(r) || !n.data || !n.len || (v.data && !v.len))
    return -1;
  FIOBJ tmp = fiobj_str_new(n.data, n.len);
  int ret = http_set_header(r, tmp, fiobj_str_new(v.data, v.len));
  fiobj_free(tmp);
  return ret;
}

/**
 * Sets a response cookie, taking ownership of the value object, but NOT the
 * name object (so name objects could be reused in future responses).
 *
 * Returns -1 on error and 0 on success.
 */
#undef http_set_cookie
int http_set_cookie(http_s *h, http_cookie_args_s cookie) {
#if DEBUG
  FIO_ASSERT(h, "Can't set cookie for NULL HTTP handler!");
#endif
  if (HTTP_INVALID_HANDLE(h) || cookie.name_len >= 32768 ||
      cookie.value_len >= 131072) {
    return -1;
  }

  static int warn_illegal = 0;

  /* write name and value while auto-correcting encoding issues */
  size_t capa = cookie.name_len + cookie.value_len + 128;
  size_t len = 0;
  FIOBJ c = fiobj_str_buf(capa);
  fio_str_info_s t = fiobj_obj2cstr(c);

#define copy_cookie_ch(ch_var)                                                 \
  if (invalid_cookie_##ch_var##_char[(uint8_t)cookie.ch_var[tmp]]) {           \
    if (!warn_illegal) {                                                       \
      ++warn_illegal;                                                          \
      FIO_LOG_WARNING("illegal char 0x%.2x in cookie " #ch_var " (in %s)\n"    \
                      "         automatic %% encoding applied",                \
                      cookie.ch_var[tmp], cookie.ch_var);                      \
    }                                                                          \
    t.data[len++] = '%';                                                       \
    t.data[len++] = hex_chars[((uint8_t)cookie.ch_var[tmp] >> 4) & 0x0F];      \
    t.data[len++] = hex_chars[(uint8_t)cookie.ch_var[tmp] & 0x0F];             \
  } else {                                                                     \
    t.data[len++] = cookie.ch_var[tmp];                                        \
  }                                                                            \
  tmp += 1;                                                                    \
  if (capa <= len + 3) {                                                       \
    capa += 32;                                                                \
    fiobj_str_capa_assert(c, capa);                                            \
    t = fiobj_obj2cstr(c);                                                     \
  }

  if (cookie.name) {
    size_t tmp = 0;
    if (cookie.name_len) {
      while (tmp < cookie.name_len) {
        copy_cookie_ch(name);
      }
    } else {
      while (cookie.name[tmp]) {
        copy_cookie_ch(name);
      }
    }
  }
  t.data[len++] = '=';
  if (cookie.value) {
    size_t tmp = 0;
    if (cookie.value_len) {
      while (tmp < cookie.value_len) {
        copy_cookie_ch(value);
      }
    } else {
      while (cookie.value[tmp]) {
        copy_cookie_ch(value);
      }
    }
  } else
    cookie.max_age = -1;

  if (http_settings(h) && http_settings(h)->is_client) {
    if (!cookie.value) {
      fiobj_free(c);
      return -1;
    }
    set_header_add(h->private_data.out_headers, HTTP_HEADER_COOKIE, c);
    return 0;
  }

  t.data[len++] = ';';
  t.data[len++] = ' ';

  if (h->status_str || !h->status) { /* on first request status == 0 */
    static uint64_t cookie_hash;
    if (!cookie_hash)
      cookie_hash = fiobj_hash_string("cookie", 6);
    FIOBJ tmp = fiobj_hash_get2(h->private_data.out_headers, cookie_hash);
    if (!tmp) {
      set_header_add(h->private_data.out_headers, HTTP_HEADER_COOKIE, c);
    } else {
      fiobj_str_join(tmp, c);
      fiobj_free(c);
    }
    return 0;
  }

  if (capa <= len + 40) {
    capa = len + 40;
    fiobj_str_capa_assert(c, capa);
    t = fiobj_obj2cstr(c);
  }
  if (cookie.max_age) {
    memcpy(t.data + len, "Max-Age=", 8);
    len += 8;
    len += fio_ltoa(t.data + len, cookie.max_age, 10);
    t.data[len++] = ';';
    t.data[len++] = ' ';
  }
  fiobj_str_resize(c, len);

  if (cookie.domain && cookie.domain_len) {
    fiobj_str_write(c, "domain=", 7);
    len += 7;
    fiobj_str_write(c, cookie.domain, cookie.domain_len);
    len += cookie.domain_len;
    fiobj_str_write(c, ";", 1);
    len += 1;
    t.data[len++] = ' ';
    fiobj_str_resize(c, len);
  }
  if (cookie.path && cookie.path_len) {
    fiobj_str_write(c, "path=", 5);
    len += 5;
    fiobj_str_write(c, cookie.path, cookie.path_len);
    len += cookie.path_len;
    fiobj_str_write(c, ";", 1);
    len += 1;
    t.data[len++] = ' ';
    fiobj_str_resize(c, len);
  }
  if (cookie.http_only) {
    fiobj_str_write(c, "HttpOnly;", 9);
  }
  if (cookie.secure) {
    fiobj_str_write(c, "secure;", 7);
  }
  set_header_add(h->private_data.out_headers, HTTP_HEADER_SET_COOKIE, c);
  return 0;
}
#define http_set_cookie(http__req__, ...)                                      \
  http_set_cookie((http__req__), (http_cookie_args_s){__VA_ARGS__})

/**
 * Sends the response headers and body.
 *
 * Returns -1 on error and 0 on success.
 *
 * AFTER THIS FUNCTION IS CALLED, THE `http_s` OBJECT IS NO LONGER VALID.
 */
int http_send_body(http_s *r, void *data, uintptr_t length) {
  if (HTTP_INVALID_HANDLE(r))
    return -1;
  if (!length || !data) {
    http_finish(r);
    return 0;
  }
  add_content_length(r, length);
  // add_content_type(r);
  add_date(r);
  return ((http_vtable_s *)r->private_data.vtbl)
      ->http_send_body(r, data, length);
}
/**
 * Sends the response headers and the specified file (the response's body).
 *
 * Returns -1 on error and 0 on success.
 *
 * AFTER THIS FUNCTION IS CALLED, THE `http_s` OBJECT IS NO LONGER VALID.
 */
int http_sendfile(http_s *r, int fd, uintptr_t length, uintptr_t offset) {
  if (HTTP_INVALID_HANDLE(r)) {
    close(fd);
    return -1;
  };
  add_content_length(r, length);
  add_content_type(r);
  add_date(r);
  return ((http_vtable_s *)r->private_data.vtbl)
      ->http_sendfile(r, fd, length, offset);
}

static inline int http_test_encoded_path(const char *mem, size_t len) {
  const char *pos = NULL;
  const char *end = mem + len;
  while (mem < end && (pos = memchr(mem, '/', (size_t)len))) {
    len = end - pos;
    mem = pos + 1;
    if (pos[1] == '/')
      return -1;
    if (len > 3 && pos[1] == '.' && pos[2] == '.' && pos[3] == '/')
      return -1;
  }
  return 0;
}

/**
 * Sends the response headers and the specified file (the response's body).
 *
 * Returns -1 eton error and 0 on success.
 *
 * AFTER THIS FUNCTION IS CALLED, THE `http_s` OBJECT IS NO LONGER VALID.
 */
int http_sendfile2(http_s *h, const char *prefix, size_t prefix_len,
                   const char *encoded, size_t encoded_len) {
  if (HTTP_INVALID_HANDLE(h))
    return -1;
  struct stat file_data = {.st_size = 0};
  static uint64_t accept_enc_hash = 0;
  if (!accept_enc_hash)
    accept_enc_hash = fiobj_hash_string("accept-encoding", 15);
  static uint64_t range_hash = 0;
  if (!range_hash)
    range_hash = fiobj_hash_string("range", 5);

  /* create filename string */
  FIOBJ filename = fiobj_str_tmp();
  if (prefix && prefix_len) {
    /* start with prefix path */
    if (encoded && prefix[prefix_len - 1] == '/' && encoded[0] == '/')
      --prefix_len;
    fiobj_str_capa_assert(filename, prefix_len + encoded_len + 4);
    fiobj_str_write(filename, prefix, prefix_len);
  }
  {
    /* decode filename in cases where it's URL encoded */
    fio_str_info_s tmp = fiobj_obj2cstr(filename);
    if (encoded) {
      char *pos = (char *)encoded;
      const char *end = encoded + encoded_len;
      while (pos < end) {
        if (*pos == '%') {
          // decode hex value (this is a percent encoded value).
          if (hex2byte((uint8_t *)tmp.data + tmp.len, (uint8_t *)pos + 1))
            return -1;
          tmp.len++;
          pos += 3;
        } else
          tmp.data[tmp.len++] = *(pos++);
      }
      tmp.data[tmp.len] = 0;
      fiobj_str_resize(filename, tmp.len);
      /* test for path manipulations after decoding */
      if (http_test_encoded_path(tmp.data + prefix_len, tmp.len - prefix_len))
        return -1;
    }
    if (tmp.data[tmp.len - 1] == '/')
      fiobj_str_write(filename, "index.html", 10);
  }
  /* test for file existance  */

  int file = -1;
  uint8_t is_gz = 0;

  fio_str_info_s s = fiobj_obj2cstr(filename);
  {
    FIOBJ tmp = fiobj_hash_get2(h->headers, accept_enc_hash);
    if (!tmp)
      goto no_gzip_support;
    fio_str_info_s ac_str = fiobj_obj2cstr(tmp);
    if (!ac_str.data || !strstr(ac_str.data, "gzip"))
      goto no_gzip_support;
    if (s.data[s.len - 3] != '.' || s.data[s.len - 2] != 'g' ||
        s.data[s.len - 1] != 'z') {
      fiobj_str_write(filename, ".gz", 3);
      s = fiobj_obj2cstr(filename);
      if (!stat(s.data, &file_data) &&
          (S_ISREG(file_data.st_mode) || S_ISLNK(file_data.st_mode))) {
        is_gz = 1;
        goto found_file;
      }
      fiobj_str_resize(filename, s.len - 3);
    }
  }
no_gzip_support:
  if (stat(s.data, &file_data) ||
      !(S_ISREG(file_data.st_mode) || S_ISLNK(file_data.st_mode)))
    return -1;
found_file:
  /* set last-modified */
  {
    FIOBJ tmp = fiobj_str_buf(32);
    fiobj_str_resize(
        tmp, http_time2str(fiobj_obj2cstr(tmp).data, file_data.st_mtime));
    http_set_header(h, HTTP_HEADER_LAST_MODIFIED, tmp);
  }
  /* set cache-control */
  http_set_header(h, HTTP_HEADER_CACHE_CONTROL, fiobj_dup(HTTP_HVALUE_MAX_AGE));
  /* set & test etag */
  uint64_t etag = (uint64_t)file_data.st_size;
  etag ^= (uint64_t)file_data.st_mtime;
  etag = fiobj_hash_string(&etag, sizeof(uint64_t));
  FIOBJ etag_str = fiobj_str_buf(32);
  fiobj_str_resize(etag_str,
                   fio_base64_encode(fiobj_obj2cstr(etag_str).data,
                                     (void *)&etag, sizeof(uint64_t)));
  /* set */
  http_set_header(h, HTTP_HEADER_ETAG, etag_str);
  /* test */
  {
    static uint64_t none_match_hash = 0;
    if (!none_match_hash)
      none_match_hash = fiobj_hash_string("if-none-match", 13);
    FIOBJ tmp2 = fiobj_hash_get2(h->headers, none_match_hash);
    if (tmp2 && fiobj_iseq(tmp2, etag_str)) {
      h->status = 304;
      http_finish(h);
      return 0;
    }
  }
  /* handle range requests */
  int64_t offset = 0;
  int64_t length = file_data.st_size;
  {
    static uint64_t ifrange_hash = 0;
    if (!ifrange_hash)
      ifrange_hash = fiobj_hash_string("if-range", 8);
    FIOBJ tmp = fiobj_hash_get2(h->headers, ifrange_hash);
    if (tmp && fiobj_iseq(tmp, etag_str)) {
      fiobj_hash_delete2(h->headers, range_hash);
    } else {
      tmp = fiobj_hash_get2(h->headers, range_hash);
      if (tmp) {
        /* range ahead... */
        if (FIOBJ_TYPE_IS(tmp, FIOBJ_T_ARRAY))
          tmp = fiobj_ary_index(tmp, 0);
        fio_str_info_s range = fiobj_obj2cstr(tmp);
        if (!range.data || memcmp("bytes=", range.data, 6))
          goto open_file;
        char *pos = range.data + 6;
        int64_t start_at = 0, end_at = 0;
        start_at = fio_atol(&pos);
        if (start_at >= file_data.st_size)
          goto open_file;
        if (start_at >= 0) {
          pos++;
          end_at = fio_atol(&pos);
          if (end_at <= 0)
            goto open_file;
        }
        /* we ignore multimple ranges, only responding with the first range. */
        if (start_at < 0) {
          if (0 - start_at < file_data.st_size) {
            offset = file_data.st_size - start_at;
            length = 0 - start_at;
          }
        } else if (end_at) {
          offset = start_at;
          length = end_at - start_at + 1;
          if (length + start_at > file_data.st_size || length <= 0)
            length = length - start_at;
        } else {
          offset = start_at;
          length = length - start_at;
        }
        h->status = 206;

        {
          FIOBJ cranges = fiobj_str_buf(1);
          fiobj_str_printf(cranges, "bytes %lu-%lu/%lu",
                           (unsigned long)start_at,
                           (unsigned long)(start_at + length - 1),
                           (unsigned long)file_data.st_size);
          http_set_header(h, HTTP_HEADER_CONTENT_RANGE, cranges);
        }
        http_set_header(h, HTTP_HEADER_ACCEPT_RANGES,
                        fiobj_dup(HTTP_HVALUE_BYTES));
      }
    }
  }
  /* test for an OPTIONS request or invalid methods */
  s = fiobj_obj2cstr(h->method);
  switch (s.len) {
  case 7:
    if (!strncasecmp("options", s.data, 7)) {
      http_set_header2(h, (fio_str_info_s){.data = (char *)"allow", .len = 5},
                       (fio_str_info_s){.data = (char *)"GET, HEAD", .len = 9});
      h->status = 200;
      http_finish(h);
      return 0;
    }
    break;
  case 3:
    if (!strncasecmp("get", s.data, 3))
      goto open_file;
    break;
  case 4:
    if (!strncasecmp("head", s.data, 4)) {
      http_set_header(h, HTTP_HEADER_CONTENT_LENGTH, fiobj_num_new(length));
      http_finish(h);
      return 0;
    }
    break;
  }
  http_send_error(h, 403);
  return 0;
open_file:
  s = fiobj_obj2cstr(filename);
  file = open(s.data, O_RDONLY);
  if (file == -1) {
    FIO_LOG_ERROR("(HTTP) couldn't open file %s!\n", s.data);
    perror("     ");
    http_send_error(h, 500);
    return 0;
  }
  {
    FIOBJ tmp = 0;
    uintptr_t pos = 0;
    if (is_gz) {
      http_set_header(h, HTTP_HEADER_CONTENT_ENCODING,
                      fiobj_dup(HTTP_HVALUE_GZIP));

      pos = s.len - 4;
      while (pos && s.data[pos] != '.')
        pos--;
      pos++; /* assuming, but that's fine. */
      tmp = http_mimetype_find(s.data + pos, s.len - pos - 3);

    } else {
      pos = s.len - 1;
      while (pos && s.data[pos] != '.')
        pos--;
      pos++; /* assuming, but that's fine. */
      tmp = http_mimetype_find(s.data + pos, s.len - pos);
    }
    if (tmp)
      http_set_header(h, HTTP_HEADER_CONTENT_TYPE, tmp);
  }
  http_sendfile(h, file, length, offset);
  return 0;
}

/**
 * Sends an HTTP error response.
 *
 * Returns -1 on error and 0 on success.
 *
 * AFTER THIS FUNCTION IS CALLED, THE `http_s` OBJECT IS NO LONGER VALID.
 *
 * The `uuid` argument is optional and will be used only if the `http_s`
 * argument is set to NULL.
 */
int http_send_error(http_s *r, size_t error) {
  if (!r || !r->private_data.out_headers) {
    return -1;
  }
  if (error < 100 || error >= 1000)
    error = 500;
  r->status = error;
  char buffer[16];
  buffer[0] = '/';
  size_t pos = 1 + fio_ltoa(buffer + 1, error, 10);
  buffer[pos++] = '.';
  buffer[pos++] = 'h';
  buffer[pos++] = 't';
  buffer[pos++] = 'm';
  buffer[pos++] = 'l';
  buffer[pos] = 0;
  if (http_sendfile2(r, http2protocol(r)->settings->public_folder,
                     http2protocol(r)->settings->public_folder_length, buffer,
                     pos)) {
    http_set_header(r, HTTP_HEADER_CONTENT_TYPE,
                    http_mimetype_find((char *)"txt", 3));
    fio_str_info_s t = http_status2str(error);
    http_send_body(r, t.data, t.len);
  }
  return 0;
}

/**
 * Sends the response headers for a header only response.
 *
 * AFTER THIS FUNCTION IS CALLED, THE `http_s` OBJECT IS NO LONGER VALID.
 */
void http_finish(http_s *r) {
  if (!r || !r->private_data.vtbl) {
    return;
  }
  add_content_length(r, 0);
  add_date(r);
  ((http_vtable_s *)r->private_data.vtbl)->http_finish(r);
}
/**
 * Pushes a data response when supported (HTTP/2 only).
 *
 * Returns -1 on error and 0 on success.
 */
int http_push_data(http_s *r, void *data, uintptr_t length, FIOBJ mime_type) {
  if (!r || !(http_fio_protocol_s *)r->private_data.flag)
    return -1;
  return ((http_vtable_s *)r->private_data.vtbl)
      ->http_push_data(r, data, length, mime_type);
}
/**
 * Pushes a file response when supported (HTTP/2 only).
 *
 * If `mime_type` is NULL, an attempt at automatic detection using
 * `filename` will be made.
 *
 * Returns -1 on error and 0 on success.
 */
int http_push_file(http_s *h, FIOBJ filename, FIOBJ mime_type) {
  if (HTTP_INVALID_HANDLE(h))
    return -1;
  return ((http_vtable_s *)h->private_data.vtbl)
      ->http_push_file(h, filename, mime_type);
}

/**
 * Upgrades an HTTP/1.1 connection to a Websocket connection.
 */
#undef http_upgrade2ws
int http_upgrade2ws(http_s *h, websocket_settings_s args) {
  if (!h) {
    FIO_LOG_ERROR("`http_upgrade2ws` requires a valid `http_s` handle.");
    goto error;
  }
  if (HTTP_INVALID_HANDLE(h))
    goto error;
  return ((http_vtable_s *)h->private_data.vtbl)->http2websocket(h, &args);
error:
  if (args.on_close)
    args.on_close(-1, args.udata);
  return -1;
}

/* *****************************************************************************
Pause / Resume
***************************************************************************** */
struct http_pause_handle_s {
  uintptr_t uuid;
  http_s *h;
  void *udata;
  void (*task)(http_s *);
  void (*fallback)(void *);
};

/** Returns the `udata` associated with the paused opaque handle */
void *http_paused_udata_get(http_pause_handle_s *http) { return http->udata; }

/**
 * Sets the `udata` associated with the paused opaque handle, returning the
 * old value.
 */
void *http_paused_udata_set(http_pause_handle_s *http, void *udata) {
  void *old = http->udata;
  http->udata = udata;
  return old;
}

/* perform the pause task outside of the connection's lock */
static void http_pause_wrapper(void *h_, void *task_) {
  void (*task)(void *h) = (void (*)(void *h))((uintptr_t)task_);
  task(h_);
}

/* perform the resume task within of the connection's lock */
static void http_resume_wrapper(intptr_t uuid, fio_protocol_s *p_, void *arg) {
  http_fio_protocol_s *p = (http_fio_protocol_s *)p_;
  http_pause_handle_s *http = arg;
  http_s *h = http->h;
  h->udata = http->udata;
  http_vtable_s *vtbl = (http_vtable_s *)h->private_data.vtbl;
  if (http->task)
    http->task(h);
  vtbl->http_on_resume(h, p);
  fio_free(http);
  (void)uuid;
}

/* perform the resume task fallback */
static void http_resume_fallback_wrapper(intptr_t uuid, void *arg) {
  http_pause_handle_s *http = arg;
  if (http->fallback)
    http->fallback(http->udata);
  fio_free(http);
  (void)uuid;
}

/**
 * Defers the request / response handling for later.
 */
void http_pause(http_s *h, void (*task)(http_pause_handle_s *http)) {
  if (HTTP_INVALID_HANDLE(h)) {
    return;
  }
  http_fio_protocol_s *p = (http_fio_protocol_s *)h->private_data.flag;
  http_vtable_s *vtbl = (http_vtable_s *)h->private_data.vtbl;
  http_pause_handle_s *http = fio_malloc(sizeof(*http));
  *http = (http_pause_handle_s){
      .uuid = p->uuid,
      .h = h,
      .udata = h->udata,
  };
  vtbl->http_on_pause(h, p);
  fio_defer(http_pause_wrapper, http, (void *)((uintptr_t)task));
}

/**
 * Defers the request / response handling for later.
 */
void http_resume(http_pause_handle_s *http, void (*task)(http_s *h),
                 void (*fallback)(void *udata)) {
  if (!http)
    return;
  http->task = task;
  http->fallback = fallback;
  fio_defer_io_task(http->uuid, .udata = http, .type = FIO_PR_LOCK_TASK,
                    .task = http_resume_wrapper,
                    .fallback = http_resume_fallback_wrapper);
}

/**
 * Hijacks the socket away from the HTTP protocol and away from facil.io.
 */
intptr_t http_hijack(http_s *h, fio_str_info_s *leftover) {
  if (!h)
    return -1;
  return ((http_vtable_s *)h->private_data.vtbl)->http_hijack(h, leftover);
}

/* *****************************************************************************
Setting the default settings and allocating a persistent copy
***************************************************************************** */

static void http_on_request_fallback(http_s *h) { http_send_error(h, 404); }
static void http_on_upgrade_fallback(http_s *h, char *p, size_t i) {
  http_send_error(h, 400);
  (void)p;
  (void)i;
}
static void http_on_response_fallback(http_s *h) { http_send_error(h, 400); }

static http_settings_s *http_settings_new(http_settings_s arg_settings) {
  /* TODO: improve locality by unifying malloc to a single call */
  if (!arg_settings.on_request)
    arg_settings.on_request = http_on_request_fallback;
  if (!arg_settings.on_response)
    arg_settings.on_response = http_on_response_fallback;
  if (!arg_settings.on_upgrade)
    arg_settings.on_upgrade = http_on_upgrade_fallback;

  if (!arg_settings.max_body_size)
    arg_settings.max_body_size = HTTP_DEFAULT_BODY_LIMIT;
  if (!arg_settings.timeout)
    arg_settings.timeout = 40;
  if (!arg_settings.ws_max_msg_size)
    arg_settings.ws_max_msg_size = 262144; /** defaults to ~250KB */
  if (!arg_settings.ws_timeout)
    arg_settings.ws_timeout = 40; /* defaults to 40 seconds */
  if (!arg_settings.max_header_size)
    arg_settings.max_header_size = 32 * 1024; /* defaults to 32Kib seconds */
  if (arg_settings.max_clients <= 0 ||
      (size_t)(arg_settings.max_clients + HTTP_BUSY_UNLESS_HAS_FDS) >
          fio_capa()) {
    arg_settings.max_clients = fio_capa();
    if ((ssize_t)arg_settings.max_clients - HTTP_BUSY_UNLESS_HAS_FDS > 0)
      arg_settings.max_clients -= HTTP_BUSY_UNLESS_HAS_FDS;
  }

  http_settings_s *settings = malloc(sizeof(*settings) + sizeof(void *));
  *settings = arg_settings;

  if (settings->public_folder) {
    settings->public_folder_length = strlen(settings->public_folder);
    if (settings->public_folder[0] == '~' &&
        settings->public_folder[1] == '/' && getenv("HOME")) {
      char *home = getenv("HOME");
      size_t home_len = strlen(home);
      char *tmp = malloc(settings->public_folder_length + home_len + 1);
      memcpy(tmp, home, home_len);
      if (home[home_len - 1] == '/')
        --home_len;
      memcpy(tmp + home_len, settings->public_folder + 1,
             settings->public_folder_length); // copy also the NULL
      settings->public_folder = tmp;
      settings->public_folder_length = strlen(settings->public_folder);
    } else {
      settings->public_folder = malloc(settings->public_folder_length + 1);
      memcpy((void *)settings->public_folder, arg_settings.public_folder,
             settings->public_folder_length);
      ((uint8_t *)settings->public_folder)[settings->public_folder_length] = 0;
    }
  }
  return settings;
}

static void http_settings_free(http_settings_s *s) {
  free((void *)s->public_folder);
  free(s);
}
/* *****************************************************************************
Listening to HTTP connections
***************************************************************************** */

static uint8_t fio_http_at_capa = 0;

static void http_on_server_protocol_http1(intptr_t uuid, void *set,
                                          void *ignr_) {
  fio_timeout_set(uuid, ((http_settings_s *)set)->timeout);
  if (fio_uuid2fd(uuid) >= ((http_settings_s *)set)->max_clients) {
    if (!fio_http_at_capa)
      FIO_LOG_WARNING("HTTP server at capacity");
    fio_http_at_capa = 1;
    http_send_error2(uuid, 503, set);
    fio_close(uuid);
    return;
  }
  fio_http_at_capa = 0;
  fio_protocol_s *pr = http1_new(uuid, set, NULL, 0);
  if (!pr)
    fio_close(uuid);
  (void)ignr_;
}

static void http_on_open(intptr_t uuid, void *set) {
  http_on_server_protocol_http1(uuid, set, NULL);
}

static void http_on_finish(intptr_t uuid, void *set) {
  http_settings_s *settings = set;

  if (settings->on_finish)
    settings->on_finish(settings);

  http_settings_free(settings);
  (void)uuid;
}

/**
 * Listens to HTTP connections at the specified `port`.
 *
 * Leave as NULL to ignore IP binding.
 *
 * Returns -1 on error and 0 on success.
 */
#undef http_listen
intptr_t http_listen(const char *port, const char *binding,
                     struct http_settings_s arg_settings) {
  if (arg_settings.on_request == NULL) {
    FIO_LOG_ERROR("http_listen requires the .on_request parameter "
                  "to be set\n");
    kill(0, SIGINT);
    exit(11);
  }

  http_settings_s *settings = http_settings_new(arg_settings);
  settings->is_client = 0;
  if (settings->tls) {
    fio_tls_alpn_add(settings->tls, "http/1.1", http_on_server_protocol_http1,
                     NULL, NULL);
  }

  return fio_listen(.port = port, .address = binding, .tls = arg_settings.tls,
                    .on_finish = http_on_finish, .on_open = http_on_open,
                    .udata = settings);
}
/** Listens to HTTP connections at the specified `port` and `binding`. */
#define http_listen(port, binding, ...)                                        \
  http_listen((port), (binding), (struct http_settings_s)(__VA_ARGS__))

/**
 * Returns the settings used to setup the connection.
 *
 * Returns NULL on error (i.e., connection was lost).
 */
struct http_settings_s *http_settings(http_s *r) {
  return ((http_fio_protocol_s *)r->private_data.flag)->settings;
}

/**
 * Returns the direct address of the connected peer (likely an intermediary).
 */
fio_str_info_s http_peer_addr(http_s *h) {
  return fio_peer_addr(((http_fio_protocol_s *)h->private_data.flag)->uuid);
}

/* *****************************************************************************
HTTP client connections
***************************************************************************** */

static void http_on_close_client(intptr_t uuid, fio_protocol_s *protocol) {
  http_fio_protocol_s *p = (http_fio_protocol_s *)protocol;
  http_settings_s *set = p->settings;
  void (**original)(intptr_t, fio_protocol_s *) =
      (void (**)(intptr_t, fio_protocol_s *))(set + 1);
  if (set->on_finish)
    set->on_finish(set);

  original[0](uuid, protocol);
  http_settings_free(set);
}

static void http_on_open_client_perform(http_settings_s *set) {
  http_s *h = set->udata;
  set->on_response(h);
}
static void http_on_open_client_http1(intptr_t uuid, void *set_,
                                      void *ignore_) {
  http_settings_s *set = set_;
  http_s *h = set->udata;
  fio_timeout_set(uuid, set->timeout);
  fio_protocol_s *pr = http1_new(uuid, set, NULL, 0);
  if (!pr) {
    fio_close(uuid);
    return;
  }
  { /* store the original on_close at the end of the struct, we wrap it. */
    void (**original)(intptr_t, fio_protocol_s *) =
        (void (**)(intptr_t, fio_protocol_s *))(set + 1);
    *original = pr->on_close;
    pr->on_close = http_on_close_client;
  }
  h->private_data.flag = (uintptr_t)pr;
  h->private_data.vtbl = http1_vtable();
  http_on_open_client_perform(set);
  (void)ignore_;
}

static void http_on_open_client(intptr_t uuid, void *set_) {
  http_on_open_client_http1(uuid, set_, NULL);
}

static void http_on_client_failed(intptr_t uuid, void *set_) {
  http_settings_s *set = set_;
  http_s *h = set->udata;
  set->udata = h->udata;
  http_s_destroy(h, 0);
  fio_free(h);
  if (set->on_finish)
    set->on_finish(set);
  http_settings_free(set);
  (void)uuid;
}

intptr_t http_connect__(void); /* sublime text marker */
/**
 * Connects to an HTTP server as a client.
 *
 * Upon a successful connection, the `on_response` callback is called with an
 * empty `http_s*` handler (status == 0). Use the same API to set it's content
 * and send the request to the server. The next`on_response` will contain the
 * response.
 *
 * `address` should contain a full URL style address for the server. i.e.:
 *           "http:/www.example.com:8080/"
 *
 * Returns -1 on error and 0 on success. the `on_finish` callback is always
 * called.
 */
intptr_t http_connect FIO_IGNORE_MACRO(const char *url,
                                       const char *unix_address,
                                       struct http_settings_s arg_settings) {
  if (!arg_settings.on_response && !arg_settings.on_upgrade) {
    FIO_LOG_ERROR("http_connect requires either an on_response "
                  " or an on_upgrade callback.\n");
    errno = EINVAL;
    goto on_error;
  }
  size_t len = 0, h_len = 0;
  char *a = NULL, *p = NULL, *host = NULL;
  uint8_t is_websocket = 0;
  uint8_t is_secure = 0;
  FIOBJ path = FIOBJ_INVALID;
  if (!url && !unix_address) {
    FIO_LOG_ERROR("http_connect requires a valid address.");
    errno = EINVAL;
    goto on_error;
  }
  if (url) {
    fio_url_s u = fio_url_parse(url, strlen(url));
    if (u.scheme.data &&
        (u.scheme.len == 2 || (u.scheme.len == 3 && u.scheme.data[2] == 's')) &&
        u.scheme.data[0] == 'w' && u.scheme.data[1] == 's') {
      is_websocket = 1;
      is_secure = (u.scheme.len == 3);
    } else if (u.scheme.data &&
               (u.scheme.len == 4 ||
                (u.scheme.len == 5 && u.scheme.data[4] == 's')) &&
               u.scheme.data[0] == 'h' && u.scheme.data[1] == 't' &&
               u.scheme.data[2] == 't' && u.scheme.data[3] == 'p') {
      is_secure = (u.scheme.len == 5);
    }
    if (is_secure && !arg_settings.tls) {
      FIO_LOG_ERROR("Secure connections (%.*s) require a TLS object.",
                    (int)u.scheme.len, u.scheme.data);
      errno = EINVAL;
      goto on_error;
    }
    if (u.path.data) {
      path = fiobj_str_new(
          u.path.data, strlen(u.path.data)); /* copy query and target as well */
    }
    if (unix_address) {
      a = (char *)unix_address;
      h_len = len = strlen(a);
      host = a;
    } else {
      if (!u.host.data) {
        FIO_LOG_ERROR("http_connect requires a valid address.");
        errno = EINVAL;
        goto on_error;
      }
      /***** no more error handling, since memory is allocated *****/
      /* copy address */
      a = fio_malloc(u.host.len + 1);
      memcpy(a, u.host.data, u.host.len);
      a[u.host.len] = 0;
      len = u.host.len;
      /* copy port */
      if (u.port.data) {
        p = fio_malloc(u.port.len + 1);
        memcpy(p, u.port.data, u.port.len);
        p[u.port.len] = 0;
      } else if (is_secure) {
        p = fio_malloc(3 + 1);
        memcpy(p, "443", 3);
        p[3] = 0;
      } else {
        p = fio_malloc(2 + 1);
        memcpy(p, "80", 2);
        p[2] = 0;
      }
    }
    if (u.host.data) {
      host = u.host.data;
      h_len = u.host.len;
    }
  }

  /* set settings */
  if (!arg_settings.timeout)
    arg_settings.timeout = 30;
  http_settings_s *settings = http_settings_new(arg_settings);
  settings->is_client = 1;
  // if (settings->tls) {
  //   fio_tls_alpn_add(settings->tls, "http/1.1", http_on_open_client_http1,
  //                     NULL, NULL);
  // }

  if (!arg_settings.ws_timeout)
    settings->ws_timeout = 0; /* allow server to dictate timeout */
  if (!arg_settings.timeout)
    settings->timeout = 0; /* allow server to dictate timeout */
  http_s *h = fio_malloc(sizeof(*h));
  FIO_ASSERT(h, "HTTP Client handler allocation failed");
  http_s_new(h, 0, http1_vtable());
  h->udata = arg_settings.udata;
  h->status = 0;
  h->path = path;
  settings->udata = h;
  settings->tls = arg_settings.tls;
  if (host)
    http_set_header2(h, (fio_str_info_s){.data = (char *)"host", .len = 4},
                     (fio_str_info_s){.data = host, .len = h_len});
  intptr_t ret;
  if (is_websocket) {
    /* force HTTP/1.1 */
    ret = fio_connect(.address = a, .port = p, .on_fail = http_on_client_failed,
                      .on_connect = http_on_open_client, .udata = settings,
                      .tls = arg_settings.tls);
    (void)0;
  } else {
    /* Allow for any HTTP version */
    ret = fio_connect(.address = a, .port = p, .on_fail = http_on_client_failed,
                      .on_connect = http_on_open_client, .udata = settings,
                      .tls = arg_settings.tls);
    (void)0;
  }
  if (a != unix_address)
    fio_free(a);
  fio_free(p);
  return ret;
on_error:
  if (arg_settings.on_finish)
    arg_settings.on_finish(&arg_settings);
  return -1;
}

/* *****************************************************************************
HTTP Websocket Connect
***************************************************************************** */

#undef http_upgrade2ws
static void on_websocket_http_connected(http_s *h) {
  websocket_settings_s *s = h->udata;
  h->udata = http_settings(h)->udata = NULL;
  if (!h->path) {
    FIO_LOG_WARNING("(websocket client) path not specified in "
                    "address, assuming root!");
    h->path = fiobj_str_new("/", 1);
  }
  http_upgrade2ws(h, *s);
  fio_free(s);
}

static void on_websocket_http_connection_finished(http_settings_s *settings) {
  websocket_settings_s *s = settings->udata;
  if (s) {
    if (s->on_close)
      s->on_close(0, s->udata);
    fio_free(s);
  }
}

#undef websocket_connect
int websocket_connect(const char *address, websocket_settings_s settings) {
  websocket_settings_s *s = fio_malloc(sizeof(*s));
  *s = settings;
  return http_connect(address, NULL, .on_request = on_websocket_http_connected,
                      .on_response = on_websocket_http_connected,
                      .on_finish = on_websocket_http_connection_finished,
                      .udata = s);
}
#define websocket_connect(address, ...)                                        \
  websocket_connect((address), (websocket_settings_s){__VA_ARGS__})

/* *****************************************************************************
EventSource Support (SSE)

Note:

* `http_sse_subscribe` and `http_sse_unsubscribe` are implemented in the
  http_internal logical unit.

***************************************************************************** */

static inline void http_sse_copy2str(FIOBJ dest, char *prefix, size_t pre_len,
                                     fio_str_info_s data) {
  if (!data.len)
    return;
  const char *stop = data.data + data.len;
  while (data.len) {
    fiobj_str_write(dest, prefix, pre_len);
    char *pos = data.data;
    while (pos < stop && *pos != '\n' && *pos != '\r')
      ++pos;
    fiobj_str_write(dest, data.data, (uintptr_t)(pos - data.data));
    fiobj_str_write(dest, "\r\n", 2);
    if (*pos == '\r')
      ++pos;
    if (*pos == '\n')
      ++pos;
    data.len -= (uintptr_t)(pos - data.data);
    data.data = pos;
  }
}

/** The on message callback. the `*msg` pointer is to a temporary object. */
static void http_sse_on_message(fio_msg_s *msg) {
  http_sse_internal_s *sse = msg->udata1;
  struct http_sse_subscribe_args *args = msg->udata2;
  /* perform a callback */
  fio_protocol_s *pr = fio_protocol_try_lock(sse->uuid, FIO_PR_LOCK_TASK);
  if (!pr)
    goto postpone;
  args->on_message(&sse->sse, msg->channel, msg->msg, args->udata);
  fio_protocol_unlock(pr, FIO_PR_LOCK_TASK);
  return;
postpone:
  if (errno == EBADF)
    return;
  fio_message_defer(msg);
  return;
}

static void http_sse_on_message__direct(http_sse_s *sse, fio_str_info_s channel,
                                        fio_str_info_s msg, void *udata) {
  http_sse_write(sse, .data = msg);
  (void)udata;
  (void)channel;
}
/** An optional callback for when a subscription is fully canceled. */
static void http_sse_on_unsubscribe(void *sse_, void *args_) {
  http_sse_internal_s *sse = sse_;
  struct http_sse_subscribe_args *args = args_;
  if (args->on_unsubscribe)
    args->on_unsubscribe(args->udata);
  fio_free(args);
  http_sse_try_free(sse);
}

/** This macro allows easy access to the `http_sse_subscribe` function. */
#undef http_sse_subscribe
/**
 * Subscribes to a channel. See {struct http_sse_subscribe_args} for possible
 * arguments.
 *
 * Returns a subscription ID on success and 0 on failure.
 *
 * All subscriptions are automatically revoked once the connection is closed.
 *
 * If the connections subscribes to the same channel more than once, messages
 * will be merged. However, another subscription ID will be assigned, and two
 * calls to {http_sse_unsubscribe} will be required in order to unregister from
 * the channel.
 */
uintptr_t http_sse_subscribe(http_sse_s *sse_,
                             struct http_sse_subscribe_args args) {
  http_sse_internal_s *sse = FIO_LS_EMBD_OBJ(http_sse_internal_s, sse, sse_);
  if (sse->uuid == -1)
    return 0;
  if (!args.on_message)
    args.on_message = http_sse_on_message__direct;
  struct http_sse_subscribe_args *udata = fio_malloc(sizeof(*udata));
  FIO_ASSERT_ALLOC(udata);
  *udata = args;

  fio_atomic_add(&sse->ref, 1);
  subscription_s *sub =
      fio_subscribe(.channel = args.channel, .on_message = http_sse_on_message,
                    .on_unsubscribe = http_sse_on_unsubscribe, .udata1 = sse,
                    .udata2 = udata, .match = args.match);
  if (!sub)
    return 0;

  fio_lock(&sse->lock);
  fio_ls_s *pos = fio_ls_push(&sse->subscriptions, sub);
  fio_unlock(&sse->lock);
  return (uintptr_t)pos;
}

/**
 * Cancels a subscription and invalidates the subscription object.
 */
void http_sse_unsubscribe(http_sse_s *sse_, uintptr_t subscription) {
  if (!sse_ || !subscription)
    return;
  http_sse_internal_s *sse = FIO_LS_EMBD_OBJ(http_sse_internal_s, sse, sse_);
  subscription_s *sub = (subscription_s *)((fio_ls_s *)subscription)->obj;
  fio_lock(&sse->lock);
  fio_ls_remove((fio_ls_s *)subscription);
  fio_unlock(&sse->lock);
  fio_unsubscribe(sub);
}

#undef http_upgrade2sse
/**
 * Upgrades an HTTP connection to an EventSource (SSE) connection.
 *
 * Thie `http_s` handle will be invalid after this call.
 *
 * On HTTP/1.1 connections, this will preclude future requests using the same
 * connection.
 */
int http_upgrade2sse(http_s *h, http_sse_s sse) {
  if (HTTP_INVALID_HANDLE(h)) {
    if (sse.on_close)
      sse.on_close(&sse);
    return -1;
  }
  return ((http_vtable_s *)h->private_data.vtbl)->http_upgrade2sse(h, &sse);
}

/**
 * Sets the ping interval for SSE connections.
 */
void http_sse_set_timout(http_sse_s *sse_, uint8_t timeout) {
  if (!sse_)
    return;
  http_sse_internal_s *sse = FIO_LS_EMBD_OBJ(http_sse_internal_s, sse, sse_);
  fio_timeout_set(sse->uuid, timeout);
}

#undef http_sse_write
/**
 * Writes data to an EventSource (SSE) connection.
 */
int http_sse_write(http_sse_s *sse, struct http_sse_write_args args) {
  if (!sse || !(args.id.len + args.data.len + args.event.len) ||
      fio_is_closed(FIO_LS_EMBD_OBJ(http_sse_internal_s, sse, sse)->uuid))
    return -1;
  FIOBJ buf;
  {
    /* best guess at data length, ignoring missing fields and multiline data */
    const size_t total = 4 + args.id.len + 2 + 7 + args.event.len + 2 + 6 +
                         args.data.len + 2 + 7 + 10 + 4;
    buf = fiobj_str_buf(total);
  }
  http_sse_copy2str(buf, (char *)"id: ", 4, args.id);
  http_sse_copy2str(buf, (char *)"event: ", 7, args.event);
  if (args.retry) {
    FIOBJ i = fiobj_num_new(args.retry);
    fiobj_str_write(buf, (char *)"retry: ", 7);
    fiobj_str_join(buf, i);
    fiobj_free(i);
  }
  http_sse_copy2str(buf, (char *)"data: ", 6, args.data);
  fiobj_str_write(buf, "\r\n", 2);
  return FIO_LS_EMBD_OBJ(http_sse_internal_s, sse, sse)
      ->vtable->http_sse_write(sse, buf);
}

/**
 * Get the connection's UUID (for fio_defer and similar use cases).
 */
intptr_t http_sse2uuid(http_sse_s *sse) {
  if (!sse ||
      fio_is_closed(FIO_LS_EMBD_OBJ(http_sse_internal_s, sse, sse)->uuid))
    return -1;
  return FIO_LS_EMBD_OBJ(http_sse_internal_s, sse, sse)->uuid;
}

/**
 * Closes an EventSource (SSE) connection.
 */
int http_sse_close(http_sse_s *sse) {
  if (!sse ||
      fio_is_closed(FIO_LS_EMBD_OBJ(http_sse_internal_s, sse, sse)->uuid))
    return -1;
  return FIO_LS_EMBD_OBJ(http_sse_internal_s, sse, sse)
      ->vtable->http_sse_close(sse);
}

/**
 * Duplicates an SSE handle by reference, remember to http_sse_free.
 *
 * Returns the same object (increases a reference count, no allocation is made).
 */
http_sse_s *http_sse_dup(http_sse_s *sse) {
  fio_atomic_add(&FIO_LS_EMBD_OBJ(http_sse_internal_s, sse, sse)->ref, 1);
  return sse;
}

/**
 * Frees an SSE handle by reference (decreases the reference count).
 */
void http_sse_free(http_sse_s *sse) {
  http_sse_try_free(FIO_LS_EMBD_OBJ(http_sse_internal_s, sse, sse));
}

/* *****************************************************************************
HTTP GET and POST parsing helpers
***************************************************************************** */

/** URL decodes a string, returning a `FIOBJ`. */
static inline FIOBJ http_urlstr2fiobj(char *s, size_t len) {
  FIOBJ o = fiobj_str_buf(len);
  ssize_t l = http_decode_url(fiobj_obj2cstr(o).data, s, len);
  if (l < 0) {
    fiobj_free(o);
    return fiobj_str_new(NULL, 0); /* empty string */
  }
  fiobj_str_resize(o, (size_t)l);
  return o;
}

/** converts a string into a `FIOBJ`. */
static inline FIOBJ http_str2fiobj(char *s, size_t len, uint8_t encoded) {
  switch (len) {
  case 0:
    return fiobj_str_new(NULL, 0); /* empty string */
  case 4:
    if (!strncasecmp(s, "true", 4))
      return fiobj_true();
    if (!strncasecmp(s, "null", 4))
      return fiobj_null();
    break;
  case 5:
    if (!strncasecmp(s, "false", 5))
      return fiobj_false();
  }
  {
    char *end = s;
    const uint64_t tmp = fio_atol(&end);
    if (end == s + len)
      return fiobj_num_new(tmp);
  }
  {
    char *end = s;
    const double tmp = fio_atof(&end);
    if (end == s + len)
      return fiobj_float_new(tmp);
  }
  if (encoded)
    return http_urlstr2fiobj(s, len);
  return fiobj_str_new(s, len);
}

/** Parses the query part of an HTTP request/response. Uses `http_add2hash`. */
void http_parse_query(http_s *h) {
  if (!h->query)
    return;
  if (!h->params)
    h->params = fiobj_hash_new();
  fio_str_info_s q = fiobj_obj2cstr(h->query);
  do {
    char *cut = memchr(q.data, '&', q.len);
    if (!cut)
      cut = q.data + q.len;
    char *cut2 = memchr(q.data, '=', (cut - q.data));
    if (cut2) {
      /* we only add named elements... */
      http_add2hash(h->params, q.data, (size_t)(cut2 - q.data), (cut2 + 1),
                    (size_t)(cut - (cut2 + 1)), 1);
    }
    if (cut[0] == '&') {
      /* protecting against some ...less informed... clients */
      if (cut[1] == 'a' && cut[2] == 'm' && cut[3] == 'p' && cut[4] == ';')
        cut += 5;
      else
        cut += 1;
    }
    q.len -= (uintptr_t)(cut - q.data);
    q.data = cut;
  } while (q.len);
}

static inline void http_parse_cookies_cookie_str(FIOBJ dest, FIOBJ str,
                                                 uint8_t is_url_encoded) {
  if (!FIOBJ_TYPE_IS(str, FIOBJ_T_STRING))
    return;
  fio_str_info_s s = fiobj_obj2cstr(str);
  while (s.len) {
    if (s.data[0] == ' ') {
      ++s.data;
      --s.len;
      continue;
    }
    char *cut = memchr(s.data, '=', s.len);
    if (!cut)
      cut = s.data;
    char *cut2 = memchr(cut, ';', s.len - (cut - s.data));
    if (!cut2)
      cut2 = s.data + s.len;
    http_add2hash(dest, s.data, cut - s.data, cut + 1, (cut2 - (cut + 1)),
                  is_url_encoded);
    if ((size_t)((cut2 + 1) - s.data) > s.len)
      s.len = 0;
    else
      s.len -= ((cut2 + 1) - s.data);
    s.data = cut2 + 1;
  }
}
static inline void http_parse_cookies_setcookie_str(FIOBJ dest, FIOBJ str,
                                                    uint8_t is_url_encoded) {
  if (!FIOBJ_TYPE_IS(str, FIOBJ_T_STRING))
    return;
  fio_str_info_s s = fiobj_obj2cstr(str);
  char *cut = memchr(s.data, '=', s.len);
  if (!cut)
    cut = s.data;
  char *cut2 = memchr(cut, ';', s.len - (cut - s.data));
  if (!cut2)
    cut2 = s.data + s.len;
  if (cut2 > cut)
    http_add2hash(dest, s.data, cut - s.data, cut + 1, (cut2 - (cut + 1)),
                  is_url_encoded);
}

/** Parses any Cookie / Set-Cookie headers, using the `http_add2hash` scheme. */
void http_parse_cookies(http_s *h, uint8_t is_url_encoded) {
  if (!h->headers)
    return;
  if (h->cookies && fiobj_hash_count(h->cookies)) {
    FIO_LOG_WARNING("(http) attempting to parse cookies more than once.");
    return;
  }
  static uint64_t setcookie_header_hash;
  if (!setcookie_header_hash)
    setcookie_header_hash = fiobj_obj2hash(HTTP_HEADER_SET_COOKIE);
  FIOBJ c = fiobj_hash_get2(h->headers, fiobj_obj2hash(HTTP_HEADER_COOKIE));
  if (c) {
    if (!h->cookies)
      h->cookies = fiobj_hash_new();
    if (FIOBJ_TYPE_IS(c, FIOBJ_T_ARRAY)) {
      /* Array of Strings */
      size_t count = fiobj_ary_count(c);
      for (size_t i = 0; i < count; ++i) {
        http_parse_cookies_cookie_str(
            h->cookies, fiobj_ary_index(c, (int64_t)i), is_url_encoded);
      }
    } else {
      /* single string */
      http_parse_cookies_cookie_str(h->cookies, c, is_url_encoded);
    }
  }
  c = fiobj_hash_get2(h->headers, fiobj_obj2hash(HTTP_HEADER_SET_COOKIE));
  if (c) {
    if (!h->cookies)
      h->cookies = fiobj_hash_new();
    if (FIOBJ_TYPE_IS(c, FIOBJ_T_ARRAY)) {
      /* Array of Strings */
      size_t count = fiobj_ary_count(c);
      for (size_t i = 0; i < count; ++i) {
        http_parse_cookies_setcookie_str(
            h->cookies, fiobj_ary_index(c, (int64_t)i), is_url_encoded);
      }
    } else {
      /* single string */
      http_parse_cookies_setcookie_str(h->cookies, c, is_url_encoded);
    }
  }
}

/**
 * Adds a named parameter to the hash, resolving nesting references.
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
int http_add2hash2(FIOBJ dest, char *name, size_t name_len, FIOBJ val,
                   uint8_t encoded) {
  if (!name)
    goto error;
  FIOBJ nested_ary = FIOBJ_INVALID;
  char *cut1;
  /* we can't start with an empty object name */
  while (name_len && name[0] == '[') {
    --name_len;
    ++name;
  }
  if (!name_len) {
    /* an empty name is an error */
    goto error;
  }
  uint32_t nesting = ((uint32_t)~0);
rebase:
  /* test for nesting level limit (limit at 32) */
  if (!nesting)
    goto error;
  /* start clearing away bits. */
  nesting >>= 1;
  /* since we might be rebasing, notice that "name" might be "name]" */
  cut1 = memchr(name, '[', name_len);
  if (!cut1)
    goto place_in_hash;
  /* simple case "name=" (the "=" was already removed) */
  if (cut1 == name) {
    /* an empty name is an error */
    goto error;
  }
  if (cut1 + 1 == name + name_len) {
    /* we have name[= ... autocorrect */
    name_len -= 1;
    goto place_in_array;
  }

  if (cut1[1] == ']') {
    /* Nested Array "name[]..." */

    /* Test for name[]= */
    if ((cut1 + 2) == name + name_len) {
      name_len -= 2;
      goto place_in_array;
    }

    /* Test for a nested Array format error */
    if (cut1[2] != '[' || cut1[3] == ']') { /* error, we can't parse this */
      goto error;
    }

    /* we have name[][key...= */

    /* ensure array exists and it's an array + set nested_ary */
    const size_t len = ((cut1[-1] == ']') ? (size_t)((cut1 - 1) - name)
                                          : (size_t)(cut1 - name));
    const uint64_t hash =
        fiobj_hash_string(name, len); /* hash the current name */
    nested_ary = fiobj_hash_get2(dest, hash);
    if (!nested_ary) {
      /* create a new nested array */
      FIOBJ key =
          encoded ? http_urlstr2fiobj(name, len) : fiobj_str_new(name, len);
      nested_ary = fiobj_ary_new2(4);
      fiobj_hash_set(dest, key, nested_ary);
      fiobj_free(key);
    } else if (!FIOBJ_TYPE_IS(nested_ary, FIOBJ_T_ARRAY)) {
      /* convert existing object to an array - auto error correction */
      FIOBJ key =
          encoded ? http_urlstr2fiobj(name, len) : fiobj_str_new(name, len);
      FIOBJ tmp = fiobj_ary_new2(4);
      fiobj_ary_push(tmp, nested_ary);
      nested_ary = tmp;
      fiobj_hash_set(dest, key, nested_ary);
      fiobj_free(key);
    }

    /* test if last object in the array is a hash - create hash if not */
    dest = fiobj_ary_index(nested_ary, -1);
    if (!dest || !FIOBJ_TYPE_IS(dest, FIOBJ_T_HASH)) {
      dest = fiobj_hash_new();
      fiobj_ary_push(nested_ary, dest);
    }

    /* rebase `name` to `key` and restart. */
    cut1 += 3; /* consume "[][" */
    name_len -= (size_t)(cut1 - name);
    name = cut1;
    goto rebase;

  } else {
    /* we have name[key]... */
    const size_t len = ((cut1[-1] == ']') ? (size_t)((cut1 - 1) - name)
                                          : (size_t)(cut1 - name));
    const uint64_t hash =
        fiobj_hash_string(name, len); /* hash the current name */
    FIOBJ tmp = fiobj_hash_get2(dest, hash);
    if (!tmp) {
      /* hash doesn't exist, create it */
      FIOBJ key =
          encoded ? http_urlstr2fiobj(name, len) : fiobj_str_new(name, len);
      tmp = fiobj_hash_new();
      fiobj_hash_set(dest, key, tmp);
      fiobj_free(key);
    } else if (!FIOBJ_TYPE_IS(tmp, FIOBJ_T_HASH)) {
      /* type error, referencing an existing object that isn't a Hash */
      goto error;
    }
    dest = tmp;
    /* no rollback is possible once we enter the new nesting level... */
    nested_ary = FIOBJ_INVALID;
    /* rebase `name` to `key` and restart. */
    cut1 += 1; /* consume "[" */
    name_len -= (size_t)(cut1 - name);
    name = cut1;
    goto rebase;
  }

place_in_hash:
  if (name[name_len - 1] == ']')
    --name_len;
  {
    FIOBJ key = encoded ? http_urlstr2fiobj(name, name_len)
                        : fiobj_str_new(name, name_len);
    FIOBJ old = fiobj_hash_replace(dest, key, val);
    if (old) {
      if (nested_ary) {
        fiobj_hash_replace(dest, key, old);
        old = fiobj_hash_new();
        fiobj_hash_set(old, key, val);
        fiobj_ary_push(nested_ary, old);
      } else {
        if (!FIOBJ_TYPE_IS(old, FIOBJ_T_ARRAY)) {
          FIOBJ tmp = fiobj_ary_new2(4);
          fiobj_ary_push(tmp, old);
          old = tmp;
        }
        fiobj_ary_push(old, val);
        fiobj_hash_replace(dest, key, old);
      }
    }
    fiobj_free(key);
  }
  return 0;

place_in_array:
  if (name[name_len - 1] == ']')
    --name_len;
  {
    uint64_t hash = fiobj_hash_string(name, name_len);
    FIOBJ ary = fiobj_hash_get2(dest, hash);
    if (!ary) {
      FIOBJ key = encoded ? http_urlstr2fiobj(name, name_len)
                          : fiobj_str_new(name, name_len);
      ary = fiobj_ary_new2(4);
      fiobj_hash_set(dest, key, ary);
      fiobj_free(key);
    } else if (!FIOBJ_TYPE_IS(ary, FIOBJ_T_ARRAY)) {
      FIOBJ tmp = fiobj_ary_new2(4);
      fiobj_ary_push(tmp, ary);
      ary = tmp;
      FIOBJ key = encoded ? http_urlstr2fiobj(name, name_len)
                          : fiobj_str_new(name, name_len);
      fiobj_hash_replace(dest, key, ary);
      fiobj_free(key);
    }
    fiobj_ary_push(ary, val);
  }
  return 0;
error:
  fiobj_free(val);
  errno = EOPNOTSUPP;
  return -1;
}

/**
 * Adds a named parameter to the hash, resolving nesting references.
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
                  size_t value_len, uint8_t encoded) {
  return http_add2hash2(dest, name, name_len,
                        http_str2fiobj(value, value_len, encoded), encoded);
}

/* *****************************************************************************
HTTP Body Parsing
***************************************************************************** */
#include <http_mime_parser.h>

typedef struct {
  http_mime_parser_s p;
  http_s *h;
  fio_str_info_s buffer;
  size_t pos;
  size_t partial_offset;
  size_t partial_length;
  FIOBJ partial_name;
} http_fio_mime_s;

#define http_mime_parser2fio(parser) ((http_fio_mime_s *)(parser))

/** Called when all the data is available at once. */
static void http_mime_parser_on_data(http_mime_parser_s *parser, void *name,
                                     size_t name_len, void *filename,
                                     size_t filename_len, void *mimetype,
                                     size_t mimetype_len, void *value,
                                     size_t value_len) {
  if (!filename_len) {
    http_add2hash(http_mime_parser2fio(parser)->h->params, name, name_len,
                  value, value_len, 0);
    return;
  }
  FIOBJ n = fiobj_str_new(name, name_len);
  fiobj_str_write(n, "[data]", 6);
  fio_str_info_s tmp = fiobj_obj2cstr(n);
  http_add2hash(http_mime_parser2fio(parser)->h->params, tmp.data, tmp.len,
                value, value_len, 0);
  fiobj_str_resize(n, name_len);
  fiobj_str_write(n, "[name]", 6);
  tmp = fiobj_obj2cstr(n);
  http_add2hash(http_mime_parser2fio(parser)->h->params, tmp.data, tmp.len,
                filename, filename_len, 0);
  if (mimetype_len) {
    fiobj_str_resize(n, name_len);
    fiobj_str_write(n, "[type]", 6);
    tmp = fiobj_obj2cstr(n);
    http_add2hash(http_mime_parser2fio(parser)->h->params, tmp.data, tmp.len,
                  mimetype, mimetype_len, 0);
  }
  fiobj_free(n);
}

/** Called when the data didn't fit in the buffer. Data will be streamed. */
static void http_mime_parser_on_partial_start(
    http_mime_parser_s *parser, void *name, size_t name_len, void *filename,
    size_t filename_len, void *mimetype, size_t mimetype_len) {
  http_mime_parser2fio(parser)->partial_length = 0;
  http_mime_parser2fio(parser)->partial_offset = 0;
  http_mime_parser2fio(parser)->partial_name = fiobj_str_new(name, name_len);

  if (!filename)
    return;

  fiobj_str_write(http_mime_parser2fio(parser)->partial_name, "[type]", 6);
  fio_str_info_s tmp =
      fiobj_obj2cstr(http_mime_parser2fio(parser)->partial_name);
  http_add2hash(http_mime_parser2fio(parser)->h->params, tmp.data, tmp.len,
                mimetype, mimetype_len, 0);

  fiobj_str_resize(http_mime_parser2fio(parser)->partial_name, name_len);
  fiobj_str_write(http_mime_parser2fio(parser)->partial_name, "[name]", 6);
  tmp = fiobj_obj2cstr(http_mime_parser2fio(parser)->partial_name);
  http_add2hash(http_mime_parser2fio(parser)->h->params, tmp.data, tmp.len,
                filename, filename_len, 0);

  fiobj_str_resize(http_mime_parser2fio(parser)->partial_name, name_len);
  fiobj_str_write(http_mime_parser2fio(parser)->partial_name, "[data]", 6);
}

/** Called when partial data is available. */
static void http_mime_parser_on_partial_data(http_mime_parser_s *parser,
                                             void *value, size_t value_len) {
  if (!http_mime_parser2fio(parser)->partial_offset)
    http_mime_parser2fio(parser)->partial_offset =
        http_mime_parser2fio(parser)->pos +
        ((uintptr_t)value -
         (uintptr_t)http_mime_parser2fio(parser)->buffer.data);
  http_mime_parser2fio(parser)->partial_length += value_len;
  (void)value;
}

/** Called when the partial data is complete. */
static void http_mime_parser_on_partial_end(http_mime_parser_s *parser) {

  fio_str_info_s tmp =
      fiobj_obj2cstr(http_mime_parser2fio(parser)->partial_name);
  FIOBJ o = FIOBJ_INVALID;
  if (!http_mime_parser2fio(parser)->partial_length)
    return;
  if (http_mime_parser2fio(parser)->partial_length < 42) {
    /* short data gets a new object */
    o = fiobj_str_new(http_mime_parser2fio(parser)->buffer.data +
                          http_mime_parser2fio(parser)->partial_offset,
                      http_mime_parser2fio(parser)->partial_length);
  } else {
    /* longer data gets a reference object (memory collision concerns) */
    o = fiobj_data_slice(http_mime_parser2fio(parser)->h->body,
                         http_mime_parser2fio(parser)->partial_offset,
                         http_mime_parser2fio(parser)->partial_length);
  }
  http_add2hash2(http_mime_parser2fio(parser)->h->params, tmp.data, tmp.len, o,
                 0);
  fiobj_free(http_mime_parser2fio(parser)->partial_name);
  http_mime_parser2fio(parser)->partial_name = FIOBJ_INVALID;
  http_mime_parser2fio(parser)->partial_offset = 0;
}

/**
 * Called when URL decoding is required.
 *
 * Should support inplace decoding (`dest == encoded`).
 *
 * Should return the length of the decoded string.
 */
static inline size_t http_mime_decode_url(char *dest, const char *encoded,
                                          size_t length) {
  return http_decode_url(dest, encoded, length);
}

/**
 * Attempts to decode the request's body.
 *
 * Supported Types include:
 * * application/x-www-form-urlencoded
 * * application/json
 * * multipart/form-data
 */
int http_parse_body(http_s *h) {
  static uint64_t content_type_hash;
  if (!h->body)
    return -1;
  if (!content_type_hash)
    content_type_hash = fiobj_hash_string("content-type", 12);
  FIOBJ ct = fiobj_hash_get2(h->headers, content_type_hash);
  fio_str_info_s content_type = fiobj_obj2cstr(ct);
  if (content_type.len < 16)
    return -1;
  if (content_type.len >= 33 &&
      !strncasecmp("application/x-www-form-urlencoded", content_type.data,
                   33)) {
    if (!h->params)
      h->params = fiobj_hash_new();
    FIOBJ tmp = h->query;
    h->query = h->body;
    http_parse_query(h);
    h->query = tmp;
    return 0;
  }
  if (content_type.len >= 16 &&
      !strncasecmp("application/json", content_type.data, 16)) {
    content_type = fiobj_obj2cstr(h->body);
    if (h->params)
      return -1;
    if (fiobj_json2obj(&h->params, content_type.data, content_type.len) == 0)
      return -1;
    if (FIOBJ_TYPE_IS(h->params, FIOBJ_T_HASH))
      return 0;
    FIOBJ tmp = h->params;
    FIOBJ key = fiobj_str_new("JSON", 4);
    h->params = fiobj_hash_new2(4);
    fiobj_hash_set(h->params, key, tmp);
    fiobj_free(key);
    return 0;
  }

  http_fio_mime_s p = {.h = h};
  if (http_mime_parser_init(&p.p, content_type.data, content_type.len))
    return -1;
  if (!h->params)
    h->params = fiobj_hash_new();

  do {
    size_t cons = http_mime_parse(&p.p, p.buffer.data, p.buffer.len);
    p.pos += cons;
    p.buffer = fiobj_data_pread(h->body, p.pos, 4096);
  } while (p.buffer.data && !p.p.done && !p.p.error);
  fiobj_free(p.partial_name);
  p.partial_name = FIOBJ_INVALID;
  return 0;
}

/* *****************************************************************************
HTTP Helper functions that could be used globally
***************************************************************************** */

/**
 * Returns a String object representing the unparsed HTTP request (HTTP
 * version is capped at HTTP/1.1). Mostly usable for proxy usage and
 * debugging.
 */
FIOBJ http_req2str(http_s *h) {
  if (HTTP_INVALID_HANDLE(h) || !fiobj_hash_count(h->headers))
    return FIOBJ_INVALID;

  struct header_writer_s w;
  w.dest = fiobj_str_buf(0);
  if (h->status_str) {
    fiobj_str_join(w.dest, h->version);
    fiobj_str_write(w.dest, " ", 1);
    fiobj_str_join(w.dest, fiobj_num_tmp(h->status));
    fiobj_str_write(w.dest, " ", 1);
    fiobj_str_join(w.dest, h->status_str);
    fiobj_str_write(w.dest, "\r\n", 2);
  } else {
    fiobj_str_join(w.dest, h->method);
    fiobj_str_write(w.dest, " ", 1);
    fiobj_str_join(w.dest, h->path);
    if (h->query) {
      fiobj_str_write(w.dest, "?", 1);
      fiobj_str_join(w.dest, h->query);
    }
    {
      fio_str_info_s t = fiobj_obj2cstr(h->version);
      if (t.len < 6 || t.data[5] != '1')
        fiobj_str_write(w.dest, " HTTP/1.1\r\n", 10);
      else {
        fiobj_str_write(w.dest, " ", 1);
        fiobj_str_join(w.dest, h->version);
        fiobj_str_write(w.dest, "\r\n", 2);
      }
    }
  }

  fiobj_each1(h->headers, 0, write_header, &w);
  fiobj_str_write(w.dest, "\r\n", 2);
  if (h->body) {
    // fiobj_data_seek(h->body, 0);
    // fio_str_info_s t = fiobj_data_read(h->body, 0);
    // fiobj_str_write(w.dest, t.data, t.len);
    fiobj_str_join(w.dest, h->body);
  }
  return w.dest;
}

void http_write_log(http_s *h) {
  FIOBJ l = fiobj_str_buf(128);

  intptr_t bytes_sent = fiobj_obj2num(fiobj_hash_get2(
      h->private_data.out_headers, fiobj_obj2hash(HTTP_HEADER_CONTENT_LENGTH)));

  struct timespec start, end;
  clock_gettime(CLOCK_REALTIME, &end);
  start = h->received_at;

  {
    // TODO Guess IP address from headers (forwarded) where possible
    fio_str_info_s peer = fio_peer_addr(http2protocol(h)->uuid);
    fiobj_str_write(l, peer.data, peer.len);
  }
  fio_str_info_s buff = fiobj_obj2cstr(l);

  if (buff.len == 0) {
    memcpy(buff.data, "[unknown]", 9);
    buff.len = 9;
  }
  memcpy(buff.data + buff.len, " - - [", 6);
  buff.len += 6;
  fiobj_str_resize(l, buff.len);
  {
    FIOBJ date;
    fio_lock(&date_lock);
    date = fiobj_dup(current_date);
    fio_unlock(&date_lock);
    fiobj_str_join(l, current_date);
    fiobj_free(date);
  }
  fiobj_str_write(l, "] \"", 3);
  fiobj_str_join(l, h->method);
  fiobj_str_write(l, " ", 1);
  fiobj_str_join(l, h->path);
  fiobj_str_write(l, " ", 1);
  fiobj_str_join(l, h->version);
  fiobj_str_write(l, "\" ", 2);
  if (bytes_sent > 0) {
    fiobj_str_write_i(l, h->status);
    fiobj_str_write(l, " ", 1);
    fiobj_str_write_i(l, bytes_sent);
    fiobj_str_write(l, "b ", 2);
  } else {
    fiobj_str_join(l, fiobj_num_tmp(h->status));
    fiobj_str_write(l, " -- ", 4);
  }

  bytes_sent = ((end.tv_sec - start.tv_sec) * 1000000) +
               ((end.tv_nsec - start.tv_nsec) / 1000);
  fiobj_str_write_i(l, bytes_sent);
  fiobj_str_write(l, "us\r\n", 4);

  buff = fiobj_obj2cstr(l);
  fwrite(buff.data, 1, buff.len, stderr);
  fiobj_free(l);
}

/**
A faster (yet less localized) alternative to `gmtime_r`.

See the libc `gmtime_r` documentation for details.

Falls back to `gmtime_r` for dates before epoch.
*/
struct tm *http_gmtime(time_t timer, struct tm *tm) {
  ssize_t a, b;
#if HAVE_TM_TM_ZONE || defined(BSD)
  *tm = (struct tm){
      .tm_isdst = 0,
      .tm_zone = (char *)"UTC",
  };
#else
  *tm = (struct tm){
      .tm_isdst = 0,
  };
#endif

  // convert seconds from epoch to days from epoch + extract data
  if (timer >= 0) {
    // for seconds up to weekdays, we reduce the reminder every step.
    a = (ssize_t)timer;
    b = a / 60; // b == time in minutes
    tm->tm_sec = a - (b * 60);
    a = b / 60; // b == time in hours
    tm->tm_min = b - (a * 60);
    b = a / 24; // b == time in days since epoch
    tm->tm_hour = a - (b * 24);
    // b == number of days since epoch
    // day of epoch was a thursday. Add + 4 so sunday == 0...
    tm->tm_wday = (b + 4) % 7;
  } else {
    // for seconds up to weekdays, we reduce the reminder every step.
    a = (ssize_t)timer;
    b = a / 60; // b == time in minutes
    if (b * 60 != a) {
      /* seconds passed */
      tm->tm_sec = (a - (b * 60)) + 60;
      --b;
    } else {
      /* no seconds */
      tm->tm_sec = 0;
    }
    a = b / 60; // b == time in hours
    if (a * 60 != b) {
      /* minutes passed */
      tm->tm_min = (b - (a * 60)) + 60;
      --a;
    } else {
      /* no minutes */
      tm->tm_min = 0;
    }
    b = a / 24; // b == time in days since epoch?
    if (b * 24 != a) {
      /* hours passed */
      tm->tm_hour = (a - (b * 24)) + 24;
      --b;
    } else {
      /* no hours */
      tm->tm_hour = 0;
    }
    // day of epoch was a thursday. Add + 4 so sunday == 0...
    tm->tm_wday = ((b - 3) % 7);
    if (tm->tm_wday)
      tm->tm_wday += 7;
    /* b == days from epoch */
  }

  // at this point we can apply the algorithm described here:
  // http://howardhinnant.github.io/date_algorithms.html#civil_from_days
  // Credit to Howard Hinnant.
  {
    b += 719468L; // adjust to March 1st, 2000 (post leap of 400 year era)
    // 146,097 = days in era (400 years)
    const size_t era = (b >= 0 ? b : b - 146096) / 146097;
    const uint32_t doe = (b - (era * 146097)); // day of era
    const uint16_t yoe =
        (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; // year of era
    a = yoe;
    a += era * 400; // a == year number, assuming year starts on March 1st...
    const uint16_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const uint16_t mp = (5U * doy + 2) / 153;
    const uint16_t d = doy - (153U * mp + 2) / 5 + 1;
    const uint8_t m = mp + (mp < 10 ? 2 : -10);
    a += (m <= 1);
    tm->tm_year = a - 1900; // tm_year == years since 1900
    tm->tm_mon = m;
    tm->tm_mday = d;
    const uint8_t is_leap = (a % 4 == 0 && (a % 100 != 0 || a % 400 == 0));
    tm->tm_yday = (doy + (is_leap) + 28 + 31) % (365 + is_leap);
  }

  return tm;
}

static const char *DAY_NAMES[] = {"Sun", "Mon", "Tue", "Wed",
                                  "Thu", "Fri", "Sat"};
static const char *MONTH_NAMES[] = {"Jan ", "Feb ", "Mar ", "Apr ",
                                    "May ", "Jun ", "Jul ", "Aug ",
                                    "Sep ", "Oct ", "Nov ", "Dec "};
static const char *GMT_STR = "GMT";

size_t http_date2rfc7231(char *target, struct tm *tmbuf) {
  /* note: day of month is always 2 digits */
  char *pos = target;
  uint16_t tmp;
  pos[0] = DAY_NAMES[tmbuf->tm_wday][0];
  pos[1] = DAY_NAMES[tmbuf->tm_wday][1];
  pos[2] = DAY_NAMES[tmbuf->tm_wday][2];
  pos[3] = ',';
  pos[4] = ' ';
  pos += 5;
  tmp = tmbuf->tm_mday / 10;
  pos[0] = '0' + tmp;
  pos[1] = '0' + (tmbuf->tm_mday - (tmp * 10));
  pos += 2;
  *(pos++) = ' ';
  pos[0] = MONTH_NAMES[tmbuf->tm_mon][0];
  pos[1] = MONTH_NAMES[tmbuf->tm_mon][1];
  pos[2] = MONTH_NAMES[tmbuf->tm_mon][2];
  pos[3] = ' ';
  pos += 4;
  // write year.
  pos += fio_ltoa(pos, tmbuf->tm_year + 1900, 10);
  *(pos++) = ' ';
  tmp = tmbuf->tm_hour / 10;
  pos[0] = '0' + tmp;
  pos[1] = '0' + (tmbuf->tm_hour - (tmp * 10));
  pos[2] = ':';
  tmp = tmbuf->tm_min / 10;
  pos[3] = '0' + tmp;
  pos[4] = '0' + (tmbuf->tm_min - (tmp * 10));
  pos[5] = ':';
  tmp = tmbuf->tm_sec / 10;
  pos[6] = '0' + tmp;
  pos[7] = '0' + (tmbuf->tm_sec - (tmp * 10));
  pos += 8;
  pos[0] = ' ';
  pos[1] = GMT_STR[0];
  pos[2] = GMT_STR[1];
  pos[3] = GMT_STR[2];
  pos[4] = 0;
  pos += 4;
  return pos - target;
}

size_t http_date2str(char *target, struct tm *tmbuf);

size_t http_date2rfc2822(char *target, struct tm *tmbuf) {
  /* note: day of month is either 1 or 2 digits */
  char *pos = target;
  uint16_t tmp;
  pos[0] = DAY_NAMES[tmbuf->tm_wday][0];
  pos[1] = DAY_NAMES[tmbuf->tm_wday][1];
  pos[2] = DAY_NAMES[tmbuf->tm_wday][2];
  pos[3] = ',';
  pos[4] = ' ';
  pos += 5;
  if (tmbuf->tm_mday < 10) {
    *pos = '0' + tmbuf->tm_mday;
    ++pos;
  } else {
    tmp = tmbuf->tm_mday / 10;
    pos[0] = '0' + tmp;
    pos[1] = '0' + (tmbuf->tm_mday - (tmp * 10));
    pos += 2;
  }
  *(pos++) = '-';
  pos[0] = MONTH_NAMES[tmbuf->tm_mon][0];
  pos[1] = MONTH_NAMES[tmbuf->tm_mon][1];
  pos[2] = MONTH_NAMES[tmbuf->tm_mon][2];
  pos += 3;
  *(pos++) = '-';
  // write year.
  pos += fio_ltoa(pos, tmbuf->tm_year + 1900, 10);
  *(pos++) = ' ';
  tmp = tmbuf->tm_hour / 10;
  pos[0] = '0' + tmp;
  pos[1] = '0' + (tmbuf->tm_hour - (tmp * 10));
  pos[2] = ':';
  tmp = tmbuf->tm_min / 10;
  pos[3] = '0' + tmp;
  pos[4] = '0' + (tmbuf->tm_min - (tmp * 10));
  pos[5] = ':';
  tmp = tmbuf->tm_sec / 10;
  pos[6] = '0' + tmp;
  pos[7] = '0' + (tmbuf->tm_sec - (tmp * 10));
  pos += 8;
  pos[0] = ' ';
  pos[1] = GMT_STR[0];
  pos[2] = GMT_STR[1];
  pos[3] = GMT_STR[2];
  pos[4] = 0;
  pos += 4;
  return pos - target;
}

/* HTTP header format for Cookie ages */
size_t http_date2rfc2109(char *target, struct tm *tmbuf) {
  /* note: day of month is always 2 digits */
  char *pos = target;
  uint16_t tmp;
  pos[0] = DAY_NAMES[tmbuf->tm_wday][0];
  pos[1] = DAY_NAMES[tmbuf->tm_wday][1];
  pos[2] = DAY_NAMES[tmbuf->tm_wday][2];
  pos[3] = ',';
  pos[4] = ' ';
  pos += 5;
  tmp = tmbuf->tm_mday / 10;
  pos[0] = '0' + tmp;
  pos[1] = '0' + (tmbuf->tm_mday - (tmp * 10));
  pos += 2;
  *(pos++) = ' ';
  pos[0] = MONTH_NAMES[tmbuf->tm_mon][0];
  pos[1] = MONTH_NAMES[tmbuf->tm_mon][1];
  pos[2] = MONTH_NAMES[tmbuf->tm_mon][2];
  pos[3] = ' ';
  pos += 4;
  // write year.
  pos += fio_ltoa(pos, tmbuf->tm_year + 1900, 10);
  *(pos++) = ' ';
  tmp = tmbuf->tm_hour / 10;
  pos[0] = '0' + tmp;
  pos[1] = '0' + (tmbuf->tm_hour - (tmp * 10));
  pos[2] = ':';
  tmp = tmbuf->tm_min / 10;
  pos[3] = '0' + tmp;
  pos[4] = '0' + (tmbuf->tm_min - (tmp * 10));
  pos[5] = ':';
  tmp = tmbuf->tm_sec / 10;
  pos[6] = '0' + tmp;
  pos[7] = '0' + (tmbuf->tm_sec - (tmp * 10));
  pos += 8;
  *pos++ = ' ';
  *pos++ = '-';
  *pos++ = '0';
  *pos++ = '0';
  *pos++ = '0';
  *pos++ = '0';
  *pos = 0;
  return pos - target;
}

/**
 * Prints Unix time to a HTTP time formatted string.
 *
 * This variation implements cached results for faster processing, at the
 * price of a less accurate string.
 */
size_t http_time2str(char *target, const time_t t) {
  /* pre-print time every 1 or 2 seconds or so. */
  static __thread time_t cached_tick;
  static __thread char cached_httpdate[48];
  static __thread size_t cached_len;
  time_t last_tick = fio_last_tick().tv_sec;
  if ((t | 7) < last_tick) {
    /* this is a custom time, not "now", pass through */
    struct tm tm;
    http_gmtime(t, &tm);
    return http_date2str(target, &tm);
  }
  if (last_tick > cached_tick) {
    struct tm tm;
    cached_tick = last_tick; /* refresh every second */
    http_gmtime(last_tick, &tm);
    cached_len = http_date2str(cached_httpdate, &tm);
  }
  memcpy(target, cached_httpdate, cached_len);
  return cached_len;
}

/* Credit to Jonathan Leffler for the idea of a unified conditional */
#define hex_val(c)                                                             \
  (((c) >= '0' && (c) <= '9')                                                  \
       ? ((c)-48)                                                              \
       : (((c) >= 'a' && (c) <= 'f') || ((c) >= 'A' && (c) <= 'F'))            \
             ? (((c) | 32) - 87)                                               \
             : ({                                                              \
                 return -1;                                                    \
                 0;                                                            \
               }))
static inline int hex2byte(uint8_t *dest, const uint8_t *source) {
  if (source[0] >= '0' && source[0] <= '9')
    *dest = (source[0] - '0');
  else if ((source[0] >= 'a' && source[0] <= 'f') ||
           (source[0] >= 'A' && source[0] <= 'F'))
    *dest = (source[0] | 32) - 87;
  else
    return -1;
  *dest <<= 4;
  if (source[1] >= '0' && source[1] <= '9')
    *dest |= (source[1] - '0');
  else if ((source[1] >= 'a' && source[1] <= 'f') ||
           (source[1] >= 'A' && source[1] <= 'F'))
    *dest |= (source[1] | 32) - 87;
  else
    return -1;
  return 0;
}

ssize_t http_decode_url(char *dest, const char *url_data, size_t length) {
  char *pos = dest;
  const char *end = url_data + length;
  while (url_data < end) {
    if (*url_data == '+') {
      // decode space
      *(pos++) = ' ';
      ++url_data;
    } else if (*url_data == '%') {
      // decode hex value
      // this is a percent encoded value.
      if (hex2byte((uint8_t *)pos, (uint8_t *)&url_data[1]))
        return -1;
      pos++;
      url_data += 3;
    } else
      *(pos++) = *(url_data++);
  }
  *pos = 0;
  return pos - dest;
}

ssize_t http_decode_url_unsafe(char *dest, const char *url_data) {
  char *pos = dest;
  while (*url_data) {
    if (*url_data == '+') {
      // decode space
      *(pos++) = ' ';
      ++url_data;
    } else if (*url_data == '%') {
      // decode hex value
      // this is a percent encoded value.
      if (hex2byte((uint8_t *)pos, (uint8_t *)&url_data[1]))
        return -1;
      pos++;
      url_data += 3;
    } else
      *(pos++) = *(url_data++);
  }
  *pos = 0;
  return pos - dest;
}

ssize_t http_decode_path(char *dest, const char *url_data, size_t length) {
  char *pos = dest;
  const char *end = url_data + length;
  while (url_data < end) {
    if (*url_data == '%') {
      // decode hex value
      // this is a percent encoded value.
      if (hex2byte((uint8_t *)pos, (uint8_t *)&url_data[1]))
        return -1;
      pos++;
      url_data += 3;
    } else
      *(pos++) = *(url_data++);
  }
  *pos = 0;
  return pos - dest;
}

ssize_t http_decode_path_unsafe(char *dest, const char *url_data) {
  char *pos = dest;
  while (*url_data) {
    if (*url_data == '%') {
      // decode hex value
      // this is a percent encoded value.
      if (hex2byte((uint8_t *)pos, (uint8_t *)&url_data[1]))
        return -1;
      pos++;
      url_data += 3;
    } else
      *(pos++) = *(url_data++);
  }
  *pos = 0;
  return pos - dest;
}

/* *****************************************************************************
Lookup Tables / functions
***************************************************************************** */

#define FIO_FORCE_MALLOC_TMP 1 /* use malloc for the mime registry */
#define FIO_SET_NAME fio_mime_set
#define FIO_SET_OBJ_TYPE FIOBJ
#define FIO_SET_OBJ_COMPARE(o1, o2) (1)
#define FIO_SET_OBJ_COPY(dest, o) (dest) = fiobj_dup((o))
#define FIO_SET_OBJ_DESTROY(o) fiobj_free((o))

#include <fio.h>

static fio_mime_set_s fio_http_mime_types = FIO_SET_INIT;

#define LONGEST_FILE_EXTENSION_LENGTH 15

/** Registers a Mime-Type to be associated with the file extension. */
void http_mimetype_register(char *file_ext, size_t file_ext_len,
                            FIOBJ mime_type_str) {
  uintptr_t hash = FIO_HASH_FN(file_ext, file_ext_len, 0, 0);
  if (mime_type_str == FIOBJ_INVALID) {
    fio_mime_set_remove(&fio_http_mime_types, hash, FIOBJ_INVALID, NULL);
  } else {
    FIOBJ old = FIOBJ_INVALID;
    fio_mime_set_overwrite(&fio_http_mime_types, hash, mime_type_str, &old);
    if (old != FIOBJ_INVALID) {
      FIO_LOG_WARNING("mime-type collision: %.*s was %s, now %s",
                      (int)file_ext_len, file_ext, fiobj_obj2cstr(old).data,
                      fiobj_obj2cstr(mime_type_str).data);
      fiobj_free(old);
    }
    fiobj_free(mime_type_str); /* move ownership to the registry */
  }
}

/** Registers a Mime-Type to be associated with the file extension. */
void http_mimetype_stats(void) {
  FIO_LOG_DEBUG("HTTP MIME hash storage count/capa: %zu / %zu",
                fio_mime_set_count(&fio_http_mime_types),
                fio_mime_set_capa(&fio_http_mime_types));
}

/**
 * Finds the mime-type associated with the file extension.
 *  Remember to call `fiobj_free`.
 */
FIOBJ http_mimetype_find(char *file_ext, size_t file_ext_len) {
  uintptr_t hash = FIO_HASH_FN(file_ext, file_ext_len, 0, 0);
  return fiobj_dup(
      fio_mime_set_find(&fio_http_mime_types, hash, FIOBJ_INVALID));
}

/**
 * Finds the mime-type associated with the URL.
 *  Remember to call `fiobj_free`.
 */
FIOBJ http_mimetype_find2(FIOBJ url) {
  static __thread char buffer[LONGEST_FILE_EXTENSION_LENGTH + 1];
  fio_str_info_s ext = {.data = NULL};
  FIOBJ mimetype;
  if (!url)
    goto finish;
  fio_str_info_s tmp = fiobj_obj2cstr(url);
  uint8_t steps = 1;
  while (tmp.len > steps || steps >= LONGEST_FILE_EXTENSION_LENGTH) {
    switch (tmp.data[tmp.len - steps]) {
    case '.':
      --steps;
      if (steps) {
        ext.len = steps;
        ext.data = buffer;
        buffer[steps] = 0;
        for (size_t i = 1; i <= steps; ++i) {
          buffer[steps - i] = tolower(tmp.data[tmp.len - i]);
        }
      }
    /* fallthrough */
    case '/':
      goto finish;
      break;
    }
    ++steps;
  }
finish:
  mimetype = http_mimetype_find(ext.data, ext.len);
  if (!mimetype)
    mimetype = fiobj_dup(HTTP_HVALUE_CONTENT_TYPE_DEFAULT);
  return mimetype;
}

/** Clears the Mime-Type registry (it will be empty afterthis call). */
void http_mimetype_clear(void) {
  fio_mime_set_free(&fio_http_mime_types);
  fiobj_free(current_date);
  current_date = FIOBJ_INVALID;
  last_date_added = 0;
}

/**
* Create with Ruby using:

a = []
256.times {|i| a[i] = 1;}
('a'.ord..'z'.ord).each {|i| a[i] = 0;}
('A'.ord..'Z'.ord).each {|i| a[i] = 0;}
('0'.ord..'9'.ord).each {|i| a[i] = 0;}
"!#$%&'*+-.^_`|~".bytes.each {|i| a[i] = 0;}
p a; nil
"!#$%&'()*+-./:<=>?@[]^_`{|}~".bytes.each {|i| a[i] = 0;} # for values
p a; nil
*/
static char invalid_cookie_name_char[256] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0, 1,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 1, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};

static char invalid_cookie_value_char[256] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};

// clang-format off
#define HTTP_SET_STATUS_STR(status, str) [status-100] = { .data = (char *)(str), .len = (sizeof(str) - 1) }
// clang-format on

/** Returns the status as a C string struct */
fio_str_info_s http_status2str(uintptr_t status) {
  static const fio_str_info_s status2str[] = {
      HTTP_SET_STATUS_STR(100, "Continue"),
      HTTP_SET_STATUS_STR(101, "Switching Protocols"),
      HTTP_SET_STATUS_STR(102, "Processing"),
      HTTP_SET_STATUS_STR(103, "Early Hints"),
      HTTP_SET_STATUS_STR(200, "OK"),
      HTTP_SET_STATUS_STR(201, "Created"),
      HTTP_SET_STATUS_STR(202, "Accepted"),
      HTTP_SET_STATUS_STR(203, "Non-Authoritative Information"),
      HTTP_SET_STATUS_STR(204, "No Content"),
      HTTP_SET_STATUS_STR(205, "Reset Content"),
      HTTP_SET_STATUS_STR(206, "Partial Content"),
      HTTP_SET_STATUS_STR(207, "Multi-Status"),
      HTTP_SET_STATUS_STR(208, "Already Reported"),
      HTTP_SET_STATUS_STR(226, "IM Used"),
      HTTP_SET_STATUS_STR(300, "Multiple Choices"),
      HTTP_SET_STATUS_STR(301, "Moved Permanently"),
      HTTP_SET_STATUS_STR(302, "Found"),
      HTTP_SET_STATUS_STR(303, "See Other"),
      HTTP_SET_STATUS_STR(304, "Not Modified"),
      HTTP_SET_STATUS_STR(305, "Use Proxy"),
      HTTP_SET_STATUS_STR(306, "(Unused), "),
      HTTP_SET_STATUS_STR(307, "Temporary Redirect"),
      HTTP_SET_STATUS_STR(308, "Permanent Redirect"),
      HTTP_SET_STATUS_STR(400, "Bad Request"),
      HTTP_SET_STATUS_STR(403, "Forbidden"),
      HTTP_SET_STATUS_STR(404, "Not Found"),
      HTTP_SET_STATUS_STR(401, "Unauthorized"),
      HTTP_SET_STATUS_STR(402, "Payment Required"),
      HTTP_SET_STATUS_STR(405, "Method Not Allowed"),
      HTTP_SET_STATUS_STR(406, "Not Acceptable"),
      HTTP_SET_STATUS_STR(407, "Proxy Authentication Required"),
      HTTP_SET_STATUS_STR(408, "Request Timeout"),
      HTTP_SET_STATUS_STR(409, "Conflict"),
      HTTP_SET_STATUS_STR(410, "Gone"),
      HTTP_SET_STATUS_STR(411, "Length Required"),
      HTTP_SET_STATUS_STR(412, "Precondition Failed"),
      HTTP_SET_STATUS_STR(413, "Payload Too Large"),
      HTTP_SET_STATUS_STR(414, "URI Too Long"),
      HTTP_SET_STATUS_STR(415, "Unsupported Media Type"),
      HTTP_SET_STATUS_STR(416, "Range Not Satisfiable"),
      HTTP_SET_STATUS_STR(417, "Expectation Failed"),
      HTTP_SET_STATUS_STR(421, "Misdirected Request"),
      HTTP_SET_STATUS_STR(422, "Unprocessable Entity"),
      HTTP_SET_STATUS_STR(423, "Locked"),
      HTTP_SET_STATUS_STR(424, "Failed Dependency"),
      HTTP_SET_STATUS_STR(425, "Unassigned"),
      HTTP_SET_STATUS_STR(426, "Upgrade Required"),
      HTTP_SET_STATUS_STR(427, "Unassigned"),
      HTTP_SET_STATUS_STR(428, "Precondition Required"),
      HTTP_SET_STATUS_STR(429, "Too Many Requests"),
      HTTP_SET_STATUS_STR(430, "Unassigned"),
      HTTP_SET_STATUS_STR(431, "Request Header Fields Too Large"),
      HTTP_SET_STATUS_STR(500, "Internal Server Error"),
      HTTP_SET_STATUS_STR(501, "Not Implemented"),
      HTTP_SET_STATUS_STR(502, "Bad Gateway"),
      HTTP_SET_STATUS_STR(503, "Service Unavailable"),
      HTTP_SET_STATUS_STR(504, "Gateway Timeout"),
      HTTP_SET_STATUS_STR(505, "HTTP Version Not Supported"),
      HTTP_SET_STATUS_STR(506, "Variant Also Negotiates"),
      HTTP_SET_STATUS_STR(507, "Insufficient Storage"),
      HTTP_SET_STATUS_STR(508, "Loop Detected"),
      HTTP_SET_STATUS_STR(509, "Unassigned"),
      HTTP_SET_STATUS_STR(510, "Not Extended"),
      HTTP_SET_STATUS_STR(511, "Network Authentication Required"),
  };
  fio_str_info_s ret = (fio_str_info_s){.len = 0, .data = NULL};
  if (status >= 100 &&
      (status - 100) < sizeof(status2str) / sizeof(status2str[0]))
    ret = status2str[status - 100];
  if (!ret.data) {
    ret = status2str[400];
  }
  return ret;
}
#undef HTTP_SET_STATUS_STR

#if DEBUG
void http_tests(void) {
  fprintf(stderr, "=== Testing HTTP helpers\n");
  FIOBJ html_mime = http_mimetype_find("html", 4);
  FIO_ASSERT(html_mime,
             "HTML mime-type not found! Mime-Type registry invalid!\n");
  fiobj_free(html_mime);
}
#endif
