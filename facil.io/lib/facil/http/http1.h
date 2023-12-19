/*
Copyright: Boaz Segev, 2017-2019
License: MIT
*/
#ifndef H_HTTP1_H
#define H_HTTP1_H

#include <http.h>

#ifndef HTTP1_READ_BUFFER
/**
 * The size of a single `read` command, it sets the limit for an HTTP/1.1
 * header line.
 */
#define HTTP1_READ_BUFFER (8 * 1024) /* ~8kb */
#endif

/** Creates an HTTP1 protocol object and handles any unread data in the buffer
 * (if any). */
fio_protocol_s *http1_new(uintptr_t uuid, http_settings_s *settings,
                          void *unread_data, size_t unread_length);

/** Manually destroys the HTTP1 protocol object. */
void http1_destroy(fio_protocol_s *);

/** returns the HTTP/1.1 protocol's VTable. */
void *http1_vtable(void);

#endif
