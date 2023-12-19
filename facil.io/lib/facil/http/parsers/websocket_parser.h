/*
copyright: Boaz Segev, 2017-2019
license: MIT

Feel free to copy, use and enjoy according to the license specified.
*/
#ifndef H_WEBSOCKET_PARSER_H
/**\file

A single file WebSocket message parser and WebSocket message wrapper, decoupled
from any IO layer.

Notice that this header file library includes static funnction declerations that
must be implemented by the including file (the callbacks).

*/
#define H_WEBSOCKET_PARSER_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#if DEBUG
#include <stdio.h>
#endif
/* *****************************************************************************
API - Message Wrapping
***************************************************************************** */

/** returns the length of the buffer required to wrap a message `len` long */
static inline __attribute__((unused)) uint64_t
websocket_wrapped_len(uint64_t len);

/**
 * Wraps a WebSocket server message and writes it to the target buffer.
 *
 * The `first` and `last` flags can be used to support message fragmentation.
 *
 * * target: the target buffer to write to.
 * * msg:    the message to be wrapped.
 * * len:    the message length.
 * * opcode: set to 1 for UTF-8 message, 2 for binary, etc'.
 * * first:  set to 1 if `msg` points the beginning of the message.
 * * last:   set to 1 if `msg + len` ends the message.
 * * client: set to 1 to use client mode (data  masking).
 *
 * Further opcode values:
 * * %x0 denotes a continuation frame
 * *  %x1 denotes a text frame
 * *  %x2 denotes a binary frame
 * *  %x3-7 are reserved for further non-control frames
 * *  %x8 denotes a connection close
 * *  %x9 denotes a ping
 * *  %xA denotes a pong
 * *  %xB-F are reserved for further control frames
 *
 * Returns the number of bytes written. Always `websocket_wrapped_len(len)`
 */
inline static uint64_t __attribute__((unused))
websocket_server_wrap(void *target, void *msg, uint64_t len,
                      unsigned char opcode, unsigned char first,
                      unsigned char last, unsigned char rsv);

/**
 * Wraps a WebSocket client message and writes it to the target buffer.
 *
 * The `first` and `last` flags can be used to support message fragmentation.
 *
 * * target: the target buffer to write to.
 * * msg:    the message to be wrapped.
 * * len:    the message length.
 * * opcode: set to 1 for UTF-8 message, 2 for binary, etc'.
 * * first:  set to 1 if `msg` points the beginning of the message.
 * * last:   set to 1 if `msg + len` ends the message.
 * * client: set to 1 to use client mode (data  masking).
 *
 * Returns the number of bytes written. Always `websocket_wrapped_len(len) + 4`
 */
inline static __attribute__((unused)) uint64_t
websocket_client_wrap(void *target, void *msg, uint64_t len,
                      unsigned char opcode, unsigned char first,
                      unsigned char last, unsigned char rsv);

/* *****************************************************************************
Callbacks - Required functions that must be inplemented to use this header
***************************************************************************** */

static void websocket_on_unwrapped(void *udata, void *msg, uint64_t len,
                                   char first, char last, char text,
                                   unsigned char rsv);
static void websocket_on_protocol_ping(void *udata, void *msg, uint64_t len);
static void websocket_on_protocol_pong(void *udata, void *msg, uint64_t len);
static void websocket_on_protocol_close(void *udata);
static void websocket_on_protocol_error(void *udata);

/* *****************************************************************************
API - Parsing (unwrapping)
***************************************************************************** */

/** the returned value for `websocket_buffer_required` */
struct websocket_packet_info_s {
  /** the expected packet length */
  uint64_t packet_length;
  /** the packet's "head" size (before the data) */
  uint8_t head_length;
  /** a flag indicating if the packet is masked */
  uint8_t masked;
};

/**
 * Returns all known information regarding the upcoming message.
 *
 * @returns a struct websocket_packet_info_s.
 *
 * On protocol error, the `head_length` value is 0 (no valid head detected).
 */
inline static struct websocket_packet_info_s
websocket_buffer_peek(void *buffer, uint64_t len);

/**
 * Consumes the data in the buffer, calling any callbacks required.
 *
 * Returns the remaining data in the existing buffer (can be 0).
 *
 * Notice: if there's any data in the buffer that can't be parsed
 * just yet, `memmove` is used to place the data at the beginning of the buffer.
 */
inline static __attribute__((unused)) uint64_t
websocket_consume(void *buffer, uint64_t len, void *udata,
                  uint8_t require_masking);

/* *****************************************************************************
API - Internal Helpers
***************************************************************************** */

/** used internally to mask and unmask client messages. */
inline static void websocket_xmask(void *msg, uint64_t len, uint32_t mask);

/* *****************************************************************************

                                Implementation

***************************************************************************** */

/* *****************************************************************************
Message masking
***************************************************************************** */
/** used internally to mask and unmask client messages. */
void websocket_xmask(void *msg, uint64_t len, uint32_t mask) {
  if (len > 7) {
    { /* XOR any unaligned memory (4 byte alignment) */
      const uintptr_t offset = 4 - ((uintptr_t)msg & 3);
      switch (offset) {
      case 3:
        ((uint8_t *)msg)[2] ^= ((uint8_t *)(&mask))[2];
      /* fallthrough */
      case 2:
        ((uint8_t *)msg)[1] ^= ((uint8_t *)(&mask))[1];
      /* fallthrough */
      case 1:
        ((uint8_t *)msg)[0] ^= ((uint8_t *)(&mask))[0];
        /* rotate mask and move pointer to first 4 byte alignment */
        uint64_t comb = mask | ((uint64_t)mask << 32);
        ((uint8_t *)(&mask))[0] = ((uint8_t *)(&comb))[0 + offset];
        ((uint8_t *)(&mask))[1] = ((uint8_t *)(&comb))[1 + offset];
        ((uint8_t *)(&mask))[2] = ((uint8_t *)(&comb))[2 + offset];
        ((uint8_t *)(&mask))[3] = ((uint8_t *)(&comb))[3 + offset];
        msg = (void *)((uintptr_t)msg + offset);
        len -= offset;
      }
    }
#if UINTPTR_MAX <= 0xFFFFFFFF
    /* handle  4 byte XOR alignment in 32 bit mnachine*/
    while (len >= 4) {
      *((uint32_t *)msg) ^= mask;
      len -= 4;
      msg = (void *)((uintptr_t)msg + 4);
    }
#else
    /* handle first 4 byte XOR alignment and move on to 64 bits */
    if ((uintptr_t)msg & 7) {
      *((uint32_t *)msg) ^= mask;
      len -= 4;
      msg = (void *)((uintptr_t)msg + 4);
    }
    /* intrinsic / XOR by 8 byte block, memory aligned */
    const uint64_t xmask = (((uint64_t)mask) << 32) | mask;
    while (len >= 8) {
      *((uint64_t *)msg) ^= xmask;
      len -= 8;
      msg = (void *)((uintptr_t)msg + 8);
    }
#endif
  }

  /* XOR any leftover bytes (might be non aligned)  */
  switch (len) {
  case 7:
    ((uint8_t *)msg)[6] ^= ((uint8_t *)(&mask))[2];
  /* fallthrough */
  case 6:
    ((uint8_t *)msg)[5] ^= ((uint8_t *)(&mask))[1];
  /* fallthrough */
  case 5:
    ((uint8_t *)msg)[4] ^= ((uint8_t *)(&mask))[0];
  /* fallthrough */
  case 4:
    ((uint8_t *)msg)[3] ^= ((uint8_t *)(&mask))[3];
  /* fallthrough */
  case 3:
    ((uint8_t *)msg)[2] ^= ((uint8_t *)(&mask))[2];
  /* fallthrough */
  case 2:
    ((uint8_t *)msg)[1] ^= ((uint8_t *)(&mask))[1];
  /* fallthrough */
  case 1:
    ((uint8_t *)msg)[0] ^= ((uint8_t *)(&mask))[0];
    /* fallthrough */
  }
}

/* *****************************************************************************
Message wrapping
***************************************************************************** */

/** Converts an unaligned network ordered byte stream to a 16 bit number. */
#define websocket_str2u16(c)                                                   \
  ((uint16_t)(((uint16_t)(((uint8_t *)(c))[0]) << 8) |                         \
              (uint16_t)(((uint8_t *)(c))[1])))

/** Converts an unaligned network ordered byte stream to a 64 bit number. */
#define websocket_str2u64(c)                                                   \
  ((uint64_t)((((uint64_t)((uint8_t *)(c))[0]) << 56) |                        \
              (((uint64_t)((uint8_t *)(c))[1]) << 48) |                        \
              (((uint64_t)((uint8_t *)(c))[2]) << 40) |                        \
              (((uint64_t)((uint8_t *)(c))[3]) << 32) |                        \
              (((uint64_t)((uint8_t *)(c))[4]) << 24) |                        \
              (((uint64_t)((uint8_t *)(c))[5]) << 16) |                        \
              (((uint64_t)((uint8_t *)(c))[6]) << 8) | (((uint8_t *)(c))[7])))

/** Writes a local 16 bit number to an unaligned buffer in network order. */
#define websocket_u2str16(buffer, i)                                           \
  do {                                                                         \
    ((uint8_t *)(buffer))[0] = ((uint16_t)(i) >> 8) & 0xFF;                    \
    ((uint8_t *)(buffer))[1] = ((uint16_t)(i)) & 0xFF;                         \
  } while (0);

/** Writes a local 64 bit number to an unaligned buffer in network order. */
#define websocket_u2str64(buffer, i)                                           \
  do {                                                                         \
    ((uint8_t *)(buffer))[0] = ((uint64_t)(i) >> 56) & 0xFF;                   \
    ((uint8_t *)(buffer))[1] = ((uint64_t)(i) >> 48) & 0xFF;                   \
    ((uint8_t *)(buffer))[2] = ((uint64_t)(i) >> 40) & 0xFF;                   \
    ((uint8_t *)(buffer))[3] = ((uint64_t)(i) >> 32) & 0xFF;                   \
    ((uint8_t *)(buffer))[4] = ((uint64_t)(i) >> 24) & 0xFF;                   \
    ((uint8_t *)(buffer))[5] = ((uint64_t)(i) >> 16) & 0xFF;                   \
    ((uint8_t *)(buffer))[6] = ((uint64_t)(i) >> 8) & 0xFF;                    \
    ((uint8_t *)(buffer))[7] = ((uint64_t)(i)) & 0xFF;                         \
  } while (0);

/** returns the length of the buffer required to wrap a message `len` long */
static inline uint64_t websocket_wrapped_len(uint64_t len) {
  if (len < 126)
    return len + 2;
  if (len < (1UL << 16))
    return len + 4;
  return len + 10;
}

/**
 * Wraps a WebSocket server message and writes it to the target buffer.
 *
 * The `first` and `last` flags can be used to support message fragmentation.
 *
 * * target: the target buffer to write to.
 * * msg:    the message to be wrapped.
 * * len:    the message length.
 * * opcode: set to 1 for UTF-8 message, 2 for binary, etc'.
 * * first:  set to 1 if `msg` points the beginning of the message.
 * * last:   set to 1 if `msg + len` ends the message.
 * * client: set to 1 to use client mode (data  masking).
 *
 * Further opcode values:
 * * %x0 denotes a continuation frame
 * *  %x1 denotes a text frame
 * *  %x2 denotes a binary frame
 * *  %x3-7 are reserved for further non-control frames
 * *  %x8 denotes a connection close
 * *  %x9 denotes a ping
 * *  %xA denotes a pong
 * *  %xB-F are reserved for further control frames
 *
 * Returns the number of bytes written. Always `websocket_wrapped_len(len)`
 */
static uint64_t websocket_server_wrap(void *target, void *msg, uint64_t len,
                                      unsigned char opcode, unsigned char first,
                                      unsigned char last, unsigned char rsv) {
  ((uint8_t *)target)[0] = 0 |
                           /* opcode */ (((first ? opcode : 0) & 15)) |
                           /* rsv */ ((rsv & 7) << 4) |
                           /*fin*/ ((last & 1) << 7);
  if (len < 126) {
    ((uint8_t *)target)[1] = len;
    memcpy(((uint8_t *)target) + 2, msg, len);
    return len + 2;
  } else if (len < (1UL << 16)) {
    /* head is 4 bytes */
    ((uint8_t *)target)[1] = 126;
    websocket_u2str16(((uint8_t *)target + 2), len);
    memcpy((uint8_t *)target + 4, msg, len);
    return len + 4;
  }
  /* Really Long Message  */
  ((uint8_t *)target)[1] = 127;
  websocket_u2str64(((uint8_t *)target + 2), len);
  memcpy((uint8_t *)target + 10, msg, len);
  return len + 10;
}

/**
 * Wraps a WebSocket client message and writes it to the target buffer.
 *
 * The `first` and `last` flags can be used to support message fragmentation.
 *
 * * target: the target buffer to write to.
 * * msg:    the message to be wrapped.
 * * len:    the message length.
 * * opcode: set to 1 for UTF-8 message, 2 for binary, etc'.
 * * first:  set to 1 if `msg` points the beginning of the message.
 * * last:   set to 1 if `msg + len` ends the message.
 *
 * Returns the number of bytes written. Always `websocket_wrapped_len(len) +
 * 4`
 */
static uint64_t websocket_client_wrap(void *target, void *msg, uint64_t len,
                                      unsigned char opcode, unsigned char first,
                                      unsigned char last, unsigned char rsv) {
  uint32_t mask = rand() | 0x01020408;
  ((uint8_t *)target)[0] = 0 |
                           /* opcode */ (((first ? opcode : 0) & 15)) |
                           /* rsv */ ((rsv & 7) << 4) |
                           /*fin*/ ((last & 1) << 7);
  if (len < 126) {
    ((uint8_t *)target)[1] = len | 128;
    ((uint8_t *)target)[2] = ((uint8_t *)(&mask))[0];
    ((uint8_t *)target)[3] = ((uint8_t *)(&mask))[1];
    ((uint8_t *)target)[4] = ((uint8_t *)(&mask))[2];
    ((uint8_t *)target)[5] = ((uint8_t *)(&mask))[3];
    memcpy(((uint8_t *)target) + 6, msg, len);
    websocket_xmask((uint8_t *)target + 6, len, mask);
    return len + 6;
  } else if (len < (1UL << 16)) {
    /* head is 4 bytes */
    ((uint8_t *)target)[1] = 126 | 128;
    websocket_u2str16(((uint8_t *)target + 2), len);
    ((uint8_t *)target)[4] = ((uint8_t *)(&mask))[0];
    ((uint8_t *)target)[5] = ((uint8_t *)(&mask))[1];
    ((uint8_t *)target)[6] = ((uint8_t *)(&mask))[2];
    ((uint8_t *)target)[7] = ((uint8_t *)(&mask))[3];
    memcpy((uint8_t *)target + 8, msg, len);
    websocket_xmask((uint8_t *)target + 8, len, mask);
    return len + 8;
  }
  /* Really Long Message  */
  ((uint8_t *)target)[1] = 255;
  websocket_u2str64(((uint8_t *)target + 2), len);
  ((uint8_t *)target)[10] = ((uint8_t *)(&mask))[0];
  ((uint8_t *)target)[11] = ((uint8_t *)(&mask))[1];
  ((uint8_t *)target)[12] = ((uint8_t *)(&mask))[2];
  ((uint8_t *)target)[13] = ((uint8_t *)(&mask))[3];
  memcpy((uint8_t *)target + 14, msg, len);
  websocket_xmask((uint8_t *)target + 14, len, mask);
  return len + 14;
}

/* *****************************************************************************
Message unwrapping
***************************************************************************** */

/**
 * Returns all known information regarding the upcoming message.
 *
 * @returns a struct websocket_packet_info_s.
 *
 * On protocol error, the `head_length` value is 0 (no valid head detected).
 */
inline static struct websocket_packet_info_s
websocket_buffer_peek(void *buffer, uint64_t len) {
  if (len < 2) {
    const struct websocket_packet_info_s info = {0 /* packet */, 2 /* head */,
                                                 0 /* masked? */};
    return info;
  }
  const uint8_t mask_f = (((uint8_t *)buffer)[1] >> 7) & 1;
  const uint8_t mask_l = (mask_f << 2);
  uint8_t len_indicator = ((((uint8_t *)buffer)[1]) & 127);
  switch (len_indicator) {
  case 126:
    if (len < 4)
      return (struct websocket_packet_info_s){0, (uint8_t)(4 + mask_l), mask_f};
    return (struct websocket_packet_info_s){
        (uint64_t)websocket_str2u16(((uint8_t *)buffer + 2)),
        (uint8_t)(4 + mask_l), mask_f};
  case 127:
    if (len < 10)
      return (struct websocket_packet_info_s){0, (uint8_t)(10 + mask_l),
                                              mask_f};
    {
      uint64_t msg_len = websocket_str2u64(((uint8_t *)buffer + 2));
      if (msg_len >> 62)
        return (struct websocket_packet_info_s){0, 0, 0};
      return (struct websocket_packet_info_s){msg_len, (uint8_t)(10 + mask_l),
                                              mask_f};
    }
  default:
    return (struct websocket_packet_info_s){len_indicator,
                                            (uint8_t)(2 + mask_l), mask_f};
  }
}

/**
 * Consumes the data in the buffer, calling any callbacks required.
 *
 * Returns the remaining data in the existing buffer (can be 0).
 */
static uint64_t websocket_consume(void *buffer, uint64_t len, void *udata,
                                  uint8_t require_masking) {
  volatile struct websocket_packet_info_s info =
      websocket_buffer_peek(buffer, len);
  if (!info.head_length) {
#if DEBUG
    fprintf(stderr, "ERROR: WebSocket protocol error - malicious header.\n");
#endif
    websocket_on_protocol_error(udata);
    return 0;
  }
  if (info.head_length + info.packet_length > len)
    return len;
  uint64_t reminder = len;
  uint8_t *pos = (uint8_t *)buffer;
  while (info.head_length + info.packet_length <= reminder) {
    /* parse head */
    void *payload = (void *)(pos + info.head_length);
    /* unmask? */
    if (info.masked) {
      /* masked */
      uint32_t mask; // = ((uint32_t *)payload)[-1];
      ((uint8_t *)(&mask))[0] = ((uint8_t *)(payload))[-4];
      ((uint8_t *)(&mask))[1] = ((uint8_t *)(payload))[-3];
      ((uint8_t *)(&mask))[2] = ((uint8_t *)(payload))[-2];
      ((uint8_t *)(&mask))[3] = ((uint8_t *)(payload))[-1];
      websocket_xmask(payload, info.packet_length, mask);
    } else if (require_masking && info.packet_length) {
#if DEBUG
      fprintf(stderr, "ERROR: WebSocket protocol error - unmasked data.\n");
#endif
      websocket_on_protocol_error(udata);
    }
    /* call callback */
    switch (pos[0] & 15) {
    case 0:
      /* continuation frame */
      websocket_on_unwrapped(udata, payload, info.packet_length, 0,
                             ((pos[0] >> 7) & 1), 0, ((pos[0] >> 4) & 7));
      break;
    case 1:
      /* text frame */
      websocket_on_unwrapped(udata, payload, info.packet_length, 1,
                             ((pos[0] >> 7) & 1), 1, ((pos[0] >> 4) & 7));
      break;
    case 2:
      /* data frame */
      websocket_on_unwrapped(udata, payload, info.packet_length, 1,
                             ((pos[0] >> 7) & 1), 0, ((pos[0] >> 4) & 7));
      break;
    case 8:
      /* close frame */
      websocket_on_protocol_close(udata);
      break;
    case 9:
      /* ping frame */
      websocket_on_protocol_ping(udata, payload, info.packet_length);
      break;
    case 10:
      /* pong frame */
      websocket_on_protocol_pong(udata, payload, info.packet_length);
      break;
    default:
#if DEBUG
      fprintf(stderr, "ERROR: WebSocket protocol error - unknown opcode %u\n",
              (unsigned int)(pos[0] & 15));
#endif
      websocket_on_protocol_error(udata);
    }
    /* step forward */
    reminder = reminder - (info.head_length + info.packet_length);
    if (!reminder)
      return 0;
    pos += info.head_length + info.packet_length;
    info = websocket_buffer_peek(pos, reminder);
  }
  /* reset buffer state - support pipelining */
  memmove(buffer, (uint8_t *)buffer + len - reminder, reminder);
  return reminder;
}

#endif
