#ifndef H_FIOBJ4SOCK_H
#define H_FIOBJ4SOCK_H
/**
 * Defines a helper for using fiobj with the sock library.
 */

#include <fio.h>
#include <fiobj.h>

static void fiobj4sock_dealloc(void *o) { fiobj_free((FIOBJ)o); }

/** send a FIOBJ  object through a socket. */
static inline __attribute__((unused)) ssize_t fiobj_send_free(intptr_t uuid,
                                                              FIOBJ o) {
  fio_str_info_s s = fiobj_obj2cstr(o);
  return fio_write2(uuid, .data.buffer = (void *)(o),
                    .offset = (uintptr_t)(((intptr_t)s.data) - ((intptr_t)(o))),
                    .length = s.len, .after.dealloc = fiobj4sock_dealloc);
}

#endif
