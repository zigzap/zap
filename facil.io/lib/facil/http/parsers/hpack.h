#ifndef H_HPACK_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <fio.h>

#ifndef MAYBE_UNUSED
#define MAYBE_UNUSED __attribute__((unused))
#endif

/**
 * Sets the limit for both a single header value and a packed header group.
 * Must be less than 2^16 -1
 */
#define HPACK_BUFFER_SIZE 16384

/**
 * Sets the limit for the amount of data an HPACK dynamic table can reference.
 * Should be less then 65,535 (2^16 -1 is the type size limit).
 */
#define HPACK_MAX_TABLE_SIZE 65535

/* *****************************************************************************
Required Callbacks
***************************************************************************** */

/* *****************************************************************************
Types
***************************************************************************** */

/** The HPACK context. */
typedef struct hpack_context_s hpack_context_s;

/* *****************************************************************************
Context API
***************************************************************************** */

/* *****************************************************************************
Primitive Types API
***************************************************************************** */

/**
 * Encodes an integer.
 *
 * Returns the number of bytes written to the destination buffer. If the buffer
 * was too small, returns the number of bytes that would have been written.
 */
static inline int hpack_int_pack(void *dest, size_t limit, uint64_t i,
                                 uint8_t prefix);

/**
 * Decodes an integer, updating the `pos` marker to the next unprocessed byte.
 *
 * The position marker may start as non-zero, meaning that `len - (*pos)` is the
 * actual length.
 *
 * An encoding / decoding error results in a return value of -1.
 */
static inline int64_t hpack_int_unpack(void *data, size_t len, uint8_t prefix,
                                       size_t *pos);

/**
 * Encodes a String.
 *
 * Returns the number of bytes written to the destination buffer. If the buffer
 * was too small, returns the number of bytes that would have been written.
 */
static inline int hpack_string_pack(void *dest, size_t limit, void *data,
                                    size_t len, uint8_t compress);

/**
 * Decodes a String.
 *
 * Returns the number of bytes written to the destination buffer. If the buffer
 * was too small, returns the number of bytes that would have been written.
 *
 * An encoding / decoding error results in a return value of -1.
 *
 * The position marker may start as non-zero, meaning that `len - (*pos)` is the
 * actual length.
 */
static inline int hpack_string_unpack(void *dest, size_t limit, void *encoded,
                                      size_t len, size_t *pos);

/* *****************************************************************************
Static table API
***************************************************************************** */

/**
 * Sets the provided pointers with the information in the static header table.
 *
 * The `index` is 1..61 (not zero based).
 *
 * Set `get_value` to 1 to collect the value data rather then the header name.
 *
 * Returns -1 if request is out of bounds.
 */
static int hpack_header_static_find(uint8_t index, uint8_t get_value,
                                    const char **name, size_t *len);

/* *****************************************************************************
Huffman API (internal)
***************************************************************************** */

/* the huffman encoding map */
typedef const struct {
  const uint32_t code;
  const uint8_t bits;
} huffman_encode_s;
static const huffman_encode_s huffman_encode_table[257];

/* the huffman decoding binary tree type */
typedef struct {
  const int16_t value;     // value, -1 == none.
  const uint8_t offset[2]; // offset for 0 and one. 0 == leaf node.
} huffman_decode_s;
static const huffman_decode_s huffman_decode_tree[513];

/**
 * Unpack (de-compress) using HPACK huffman - returns the number of bytes
 * written and advances the position marker.
 */
static MAYBE_UNUSED int hpack_huffman_unpack(void *dest, size_t limit,
                                             void *encoded, size_t len,
                                             size_t *pos);

/**
 *  Pack (compress) using HPACK huffman - returns the number of bytes written or
 * required.
 */
static MAYBE_UNUSED int hpack_huffman_pack(void *dest, const int limit,
                                           void *data, size_t len);

/* *****************************************************************************





                                  Implementation






***************************************************************************** */

/* *****************************************************************************
Integer encoding
***************************************************************************** */

static inline int hpack_int_pack(void *dest_, size_t limit, uint64_t i,
                                 uint8_t prefix) {
  uint8_t mask = ((1 << (prefix)) - 1);
  uint8_t *dest = (uint8_t *)dest_;
  int len = 1;

  if (!dest_ || !limit)
    goto calc_final_length;

  if (i < mask) {
    // zero out prefix bits
    dest[0] &= ~mask;
    // fill in i;
    dest[0] |= i;
    return 1;
  }

  dest[0] |= mask;

  if ((size_t)len >= limit)
    goto calc_final_length;

  i -= mask;

  while (i > 127) {
    dest[len] = 128 | (i & 127);
    ++len;
    if ((size_t)len >= limit)
      goto calc_final_length;
    i >>= 7;
  }

  dest[len] = i & 0x7fU;
  ++len;

  return len;

calc_final_length:
  len = 1;
  if (i < mask)
    return len;
  i -= mask;
  while (i) {
    ++len;
    i >>= 7;
  }
  return len;
}

static inline int64_t hpack_int_unpack(void *data_, size_t len, uint8_t prefix,
                                       size_t *pos) {
  uint8_t *data = (uint8_t *)data_;
  len -= *pos;
  if (len > 8)
    len = 8;
  uint64_t result = 0;
  uint64_t bit = 0;
  uint8_t mask = ((1 << (prefix)) - 1);

  if ((mask & (data[*pos])) != mask) {
    result = (mask & (data[(*pos)++]));
    return (int64_t)result;
  }

  ++(*pos);
  --len;

  while (len && (data[*pos] & 128)) {
    result |= ((data[*pos] & 0x7fU) << (bit));
    bit += 7;
    ++(*pos);
    --len;
  }
  if (!len) {
    return -1;
  }
  result |= ((data[*pos] & 0x7fU) << bit);
  result += mask;

  ++(*pos);
  return (int64_t)result;
}

/* *****************************************************************************
String encoding
***************************************************************************** */

static MAYBE_UNUSED int hpack_string_pack(void *dest_, size_t limit,
                                          void *data_, size_t len,
                                          uint8_t compress) {
  uint8_t *dest = (uint8_t *)dest_;
  uint8_t *buf = (uint8_t *)data_;
  int encoded_int_len = 0;
  int pos = 0;
  if (compress) {
    dest[pos] = 128;
    int comp_len = hpack_huffman_pack(NULL, 0, buf, len);
    encoded_int_len = hpack_int_pack(dest, limit, comp_len, 7);
    if (encoded_int_len + comp_len > (int)limit)
      return comp_len + encoded_int_len;
    comp_len = hpack_huffman_pack(dest + encoded_int_len,
                                  limit - encoded_int_len, buf, len);
    return encoded_int_len + comp_len;
  }
  dest[pos] = 0;
  encoded_int_len = hpack_int_pack(dest, limit, len, 7);
  if (encoded_int_len + (int)len > (int)limit)
    return len + encoded_int_len;
  memcpy(dest + encoded_int_len, buf, len);
  return len + encoded_int_len;
}

static MAYBE_UNUSED int hpack_string_unpack(void *dest_, size_t limit,
                                            void *encoded_, size_t len,
                                            size_t *pos) {
  uint8_t *dest = (uint8_t *)dest_;
  uint8_t *buf = (uint8_t *)encoded_;
  const size_t org_pos = *pos;
  uint8_t compressed = buf[*pos] & 128;
  int64_t l = hpack_int_unpack(buf, len, 7, pos);
  if (!l) {
    return 0;
  }
  if (l == -1 || l > (int64_t)len - (int64_t)*pos) {
    return -1;
  }
  len = l;
  if (compressed) {
    len = hpack_huffman_unpack(dest, limit, buf, len + (*pos), pos);
    if (len > limit)
      goto overflow;
  } else {
    if (len > limit)
      goto overflow;
    memcpy(dest, buf + (*pos), len);
    *pos += len;
  }
  return len;

overflow:
  *pos = org_pos;
  return len;
}

/* *****************************************************************************
Huffman encoding
***************************************************************************** */

static MAYBE_UNUSED int hpack_huffman_unpack(void *dest_, size_t limit,
                                             void *encoded_, size_t len,
                                             size_t *r_pos) {
  uint8_t *dest = (uint8_t *)dest_;
  uint8_t *encoded = (uint8_t *)encoded_;
  size_t pos = 0;
  uint8_t expect = 0;
  len -= *r_pos;
  register const huffman_decode_s *node = huffman_decode_tree;
  while (len) {
    register const uint8_t byte = encoded[(*r_pos)++];
    --len;
    expect = 1;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      node += node->offset[(byte >> (7 - bit)) & 1];
      if (node->offset[0])
        continue;
      switch (node->value) {
      case 256U:
        goto done;
      case -1:
        goto error;
      }
      if (pos < limit)
        dest[pos] = (uint8_t)node->value;
      ++pos;
      /* test if all remaining bits are set (possible padding) */
      expect = ((uint8_t)(byte | (0xFF << (7 - bit))) & 0xFF) ^ 0xFF;
      node = huffman_decode_tree;
    }
  }
done:
  if (expect) {
    /* padding error */
    return -1;
  }
  return pos;
error:
  return -1;
}

static MAYBE_UNUSED int hpack_huffman_pack(void *dest_, const int limit,
                                           void *data_, size_t len) {
  uint8_t *dest = (uint8_t *)dest_;
  uint8_t *data = (uint8_t *)data_;
  int comp_len = 0;
  uint8_t *pos = data;
  const uint8_t *end = pos + len;
  uint8_t offset = 0;
  if (!len)
    return 0;
  if (!limit)
    goto calc_final_length;

  dest[comp_len] = 0;
  do {
    uint32_t code = huffman_encode_table[*pos].code;
    uint8_t bits = huffman_encode_table[*pos].bits;
    ++pos;

    if (offset) {
      /* does the code fit in the existing byte */
      if (bits + offset <= 8) {
        dest[comp_len] |= code >> (24 + offset);
        offset = offset + bits;
        continue;
      }
      /* fill in current byte */
      dest[comp_len] |= (code >> (24 + offset)) & 0xFF;
      code <<= 8 - offset;
      bits -= 8 - offset;
      offset = 0;
      ++comp_len;
      dest[comp_len] = 0;
    }

    /* make sure we have enough space */
    if (((bits + (comp_len << 3) + 7) >> 3) >= limit)
      goto calc_final_length;

    /* copy full bytes */
    switch (bits >> 3) {
    case 3:
      dest[comp_len + 2] = (uint8_t)(code >> 8) & 0xFF;
    /* fallthrough */
    case 2:
      dest[comp_len + 1] = (uint8_t)(code >> 16) & 0xFF;
    /* fallthrough */
    case 1:
      dest[comp_len + 0] = (uint8_t)(code >> 24) & 0xFF;
      comp_len += (bits >> 3);
      code <<= (bits & (~7));
      dest[comp_len] = 0;
    }

    /* copy partial bits */
    dest[comp_len] |= (uint8_t)(code >> 24) & ((uint8_t)0xFF);
    offset = bits & 7;
  } while (pos < end);

  if (offset & 7) {
    /* pad last bits as 1 */
    dest[comp_len] |= (uint8_t)(0xFFUL >> (offset & 7));
    ++comp_len;
  }
  return comp_len;

calc_final_length:

  comp_len = 0;
  for (size_t i = 0; i < len; i++) {
    comp_len += huffman_encode_table[data[i]].bits;
  }
  comp_len += 7;
  comp_len >>= 3;
  return comp_len;
}

/* *****************************************************************************
Header static table lookup
***************************************************************************** */

static const struct {
  struct hpack_static_data_s {
    const char *val;
    const size_t len;
  } data[2];
} MAYBE_UNUSED hpack_static_table[] = {
    /* [0] */ {.data = {{.len = 0}, {.len = 0}}},
    {.data = {{.val = ":authority", .len = 10}, {.len = 0}}},
    {.data = {{.val = ":method", .len = 7}, {.val = "GET", .len = 3}}},
    {.data = {{.val = ":method", .len = 7}, {.val = "POST", .len = 4}}},
    {.data = {{.val = ":path", .len = 5}, {.val = "/", .len = 1}}},
    {.data = {{.val = ":path", .len = 5}, {.val = "/index.html", .len = 11}}},
    {.data = {{.val = ":scheme", .len = 7}, {.val = "http", .len = 0}}},
    {.data = {{.val = ":scheme", .len = 7}, {.val = "https", .len = 0}}},
    {.data = {{.val = ":status", .len = 7}, {.val = "200", .len = 0}}},
    {.data = {{.val = ":status", .len = 7}, {.val = "204", .len = 0}}},
    {.data = {{.val = ":status", .len = 7}, {.val = "206", .len = 0}}},
    {.data = {{.val = ":status", .len = 7}, {.val = "304", .len = 0}}},
    {.data = {{.val = ":status", .len = 7}, {.val = "400", .len = 0}}},
    {.data = {{.val = ":status", .len = 7}, {.val = "404", .len = 0}}},
    {.data = {{.val = ":status", .len = 7}, {.val = "500", .len = 0}}},
    {.data = {{.val = "accept-charset", .len = 14}, {.len = 0}}},
    {.data = {{.val = "accept-encoding", .len = 15},
              {.val = "gzip, deflate", .len = 13}}},
    {.data = {{.val = "accept-language", .len = 15}, {.len = 0}}},
    {.data = {{.val = "accept-ranges", .len = 13}, {.len = 0}}},
    {.data = {{.val = "accept", .len = 6}, {.len = 0}}},
    {.data = {{.val = "access-control-allow-origin", .len = 27}, {.len = 0}}},
    {.data = {{.val = "age", .len = 3}, {.len = 0}}},
    {.data = {{.val = "allow", .len = 5}, {.len = 0}}},
    {.data = {{.val = "authorization", .len = 13}, {.len = 0}}},
    {.data = {{.val = "cache-control", .len = 13}, {.len = 0}}},
    {.data = {{.val = "content-disposition", .len = 0}, {.len = 0}}},
    {.data = {{.val = "content-encoding", .len = 16}, {.len = 0}}},
    {.data = {{.val = "content-language", .len = 16}, {.len = 0}}},
    {.data = {{.val = "content-length", .len = 14}, {.len = 0}}},
    {.data = {{.val = "content-location", .len = 16}, {.len = 0}}},
    {.data = {{.val = "content-range", .len = 13}, {.len = 0}}},
    {.data = {{.val = "content-type", .len = 12}, {.len = 0}}},
    {.data = {{.val = "cookie", .len = 6}, {.len = 0}}},
    {.data = {{.val = "date", .len = 4}, {.len = 0}}},
    {.data = {{.val = "etag", .len = 4}, {.len = 0}}},
    {.data = {{.val = "expect", .len = 6}, {.len = 0}}},
    {.data = {{.val = "expires", .len = 7}, {.len = 0}}},
    {.data = {{.val = "from", .len = 4}, {.len = 0}}},
    {.data = {{.val = "host", .len = 4}, {.len = 0}}},
    {.data = {{.val = "if-match", .len = 8}, {.len = 0}}},
    {.data = {{.val = "if-modified-since", .len = 17}, {.len = 0}}},
    {.data = {{.val = "if-none-match", .len = 13}, {.len = 0}}},
    {.data = {{.val = "if-range", .len = 8}, {.len = 0}}},
    {.data = {{.val = "if-unmodified-since", .len = 19}, {.len = 0}}},
    {.data = {{.val = "last-modified", .len = 13}, {.len = 0}}},
    {.data = {{.val = "link", .len = 4}, {.len = 0}}},
    {.data = {{.val = "location", .len = 8}, {.len = 0}}},
    {.data = {{.val = "max-forwards", .len = 12}, {.len = 0}}},
    {.data = {{.val = "proxy-authenticate", .len = 18}, {.len = 0}}},
    {.data = {{.val = "proxy-authorization", .len = 19}, {.len = 0}}},
    {.data = {{.val = "range", .len = 5}, {.len = 0}}},
    {.data = {{.val = "referer", .len = 7}, {.len = 0}}},
    {.data = {{.val = "refresh", .len = 7}, {.len = 0}}},
    {.data = {{.val = "retry-after", .len = 11}, {.len = 0}}},
    {.data = {{.val = "server", .len = 6}, {.len = 0}}},
    {.data = {{.val = "set-cookie", .len = 10}, {.len = 0}}},
    {.data = {{.val = "strict-transport-security", .len = 25}, {.len = 0}}},
    {.data = {{.val = "transfer-encoding", .len = 17}, {.len = 0}}},
    {.data = {{.val = "user-agent", .len = 10}, {.len = 0}}},
    {.data = {{.val = "vary", .len = 4}, {.len = 0}}},
    {.data = {{.val = "via", .len = 3}, {.len = 0}}},
    {.data = {{.val = "www-authenticate", .len = 16}, {.len = 0}}},
};

static MAYBE_UNUSED int hpack_header_static_find(uint8_t index,
                                                 uint8_t requested_type,
                                                 const char **name,
                                                 size_t *len) {
  if (requested_type > 1 ||
      index >= (sizeof(hpack_static_table) / sizeof(hpack_static_table[0])))
    goto err;
  struct hpack_static_data_s d = hpack_static_table[index].data[requested_type];
  *name = d.val;
  *len = d.len;
  return 0;
err:

  *name = NULL;
  *len = 0;
  return -1;
}

/* *****************************************************************************






                                  Testing







***************************************************************************** */

#if DEBUG

#include <inttypes.h>
#include <stdio.h>

void hpack_test(void) {
  uint8_t buffer[1 << 15];
  const size_t limit = (1 << 15);
  size_t buf_pos = 0;
  {
    /* test integer packing */
    int64_t result;
    size_t pos = 0;
    fprintf(stderr, "* HPACK testing integer primitive packing.\n");
    if ((result = hpack_int_unpack((uint8_t *)"\x0c", 1, 4, &pos)) != 12) {
      fprintf(stderr,
              "* HPACK INTEGER DECODER ERROR ex. 0c 12 != %" PRId64 "\n",
              result);
      exit(-1);
    }

    pos = 0;
    if ((result = hpack_int_unpack((uint8_t *)"\x1f\x9a\x0a", 3, 5, &pos)) !=
        1337) {
      fprintf(
          stderr,
          "* HPACK INTEGER DECODER ERROR ex. \\x1f\\x9a\\x0a 1337 != %" PRId64
          "\n",
          result);
      exit(-1);
    }

    for (size_t i = 0; i < (1 << 21); ++i) {
      buf_pos = 0;
      int pack_bytes =
          hpack_int_pack(buffer + buf_pos, limit - buf_pos, i, i & 7);
      if (pack_bytes == -1) {
        fprintf(stderr,
                "* HPACK INTEGER ENCODE ERROR 1 ( %zu) (prefix == %zu)\n", i,
                i & 7);
        exit(-1);
      }
      buf_pos += pack_bytes;
      pack_bytes =
          hpack_int_pack(buffer + buf_pos, limit - buf_pos, (i << 4), i & 7);
      if (pack_bytes == -1) {
        fprintf(stderr,
                "* HPACK INTEGER ENCODE ERROR 1 ( %zu) (prefix == %zu)\n", i,
                i & 7);
        exit(-1);
      }
      buf_pos = 0;
      result = hpack_int_unpack(buffer, limit, (i & 7), &buf_pos);
      if ((size_t)result != i) {
        fprintf(stderr,
                "* HPACK INTEGER DECODE ERROR 2 expected %zu got %" PRId64
                " (prefix == %zu)\n",
                i, result, (i & 7));
        exit(-1);
      }
      result = hpack_int_unpack(buffer, limit, (i & 7), &buf_pos);
      if ((size_t)result != (i << 4)) {
        fprintf(stderr,
                "* HPACK INTEGER DECODE ERROR 2 expected %zu got %" PRId64
                " (prefix == %zu)\n",
                (i << 4), result, (i & 7));
        exit(-1);
      }
    }
    fprintf(stderr, "* HPACK integer primitive test complete.\n");
  }
  buf_pos = 0;
  {
    /* validate huffman tree */
    for (int i = 0; i < 257; ++i) {
      const huffman_decode_s *node = huffman_decode_tree;
      uint32_t code = huffman_encode_table[i].code;
      uint8_t consumed = 32 - huffman_encode_table[i].bits;
      while (consumed < 32) {
        node += node->offset[(code >> 31) & 1];
        code <<= 1;
        ++consumed;
      }
      if (i != node->value) {
        fprintf(stderr,
                "ERROR validating huffman tree - validation error for %d "
                "(value: %d != "
                "%d)\n",
                i, node->value, i);
        exit(-1);
      }
    }
    fprintf(stderr, "* HPACK Huffman tree validated.\n");
    /* test huffman encoding / decoding packing */
    const size_t results_limit = 1024;
    uint8_t results[1024];
    size_t pos = 0;
    memset(results, 0, results_limit);
    int tmp = hpack_huffman_unpack(
        results, results_limit,
        "\x9d\x29\xad\x17\x18\x63\xc7\x8f\x0b\x97\xc8\xe9\xae\x82"
        "\xae\x43\xd3",
        17, &pos);
    if (tmp == -1) {
      fprintf(stderr, "* HPACK HUFFMAN TEST FAILED unpacking error (1).\n");
      exit(-1);
    } else if ((size_t)tmp > (limit - buf_pos)) {
      fprintf(stderr, "* HPACK HUFFMAN TEST buffer full error (1).\n");
    } else if (memcmp(results, "https://www.example.com", 23) || tmp != 23) {
      fprintf(stderr,
              "* HPACK HUFFMAN TEST FAILED result error (1).\n(%d) %.*s\n", tmp,
              tmp, results);
      exit(-1);
    }
    memset(results, 0, results_limit);
    pos = 0;
    tmp = hpack_huffman_unpack(
        results, results_limit,
        "\xf1\xe3\xc2\xe5\xf2\x3a\x6b\xa0\xab\x90\xf4\xff", 12, &pos);
    if (tmp == -1) {
      fprintf(stderr, "* HPACK HUFFMAN TEST FAILED unpacking error (2).\n");
      exit(-1);
    } else if ((size_t)tmp > results_limit) {
      fprintf(stderr, "* HPACK HUFFMAN TEST buffer full error (2).\n");
    } else if (memcmp(results, "www.example.com", 15) || tmp != 15) {
      fprintf(stderr, "* HPACK HUFFMAN TEST FAILED result error (2).\n");
      exit(-1);
    }

    memset(results, 0, results_limit);
    tmp = hpack_huffman_pack(results, results_limit, "https://www.example.com",
                             23);
    if (tmp == -1) {
      fprintf(stderr, "* HPACK HUFFMAN TEST FAILED packing error!.\n");
      exit(-1);
    } else if ((size_t)tmp > limit - buf_pos) {
      fprintf(stderr, "* HPACK HUFFMAN TEST packing buffer full!\n");
    } else if (tmp != 17 || memcmp("\x9d\x29\xad\x17\x18\x63\xc7\x8f\x0b\x97"
                                   "\xc8\xe9\xae\x82\xae\x43\xd3",
                                   results, 17)) {
      fprintf(stderr,
              "* HPACK HUFFMAN TEST FAILED packing result error!\n(%d) ", tmp);
      for (int i = 0; i < tmp; ++i) {
        fprintf(stderr, "\\x%.2X", results[i]);
      }
      fprintf(stderr, "\n");
      exit(-1);
    }
    memset(results, 0, results_limit);
    memset(buffer, 0, 128);
    tmp = hpack_huffman_pack(
        buffer, limit,
        "I want to go home... but I have to write tests... woohoo!", 57);
    if (tmp == -1) {
      fprintf(stderr, "* HPACK HUFFMAN TEST FAILED packing error (3).\n");
      exit(-1);
    } else if ((size_t)tmp > limit) {
      fprintf(stderr, "* HPACK HUFFMAN TEST buffer full (3).\n");
    } else {
      int old_tmp = tmp;
      pos = 0;
      tmp = hpack_huffman_unpack(results, results_limit, buffer, tmp, &pos);
      if (tmp == -1) {
        fprintf(
            stderr,
            "* HPACK HUFFMAN TEST FAILED unpacking error (3) for %d bytes.\n"
            "*    Got (%d): %.*s\n",
            old_tmp, tmp, (int)tmp, results);
        exit(-1);
      } else if (memcmp(results,
                        "I want to go home... but I have to write tests... "
                        "woohoo!",
                        57) ||
                 tmp != 57) {
        fprintf(stderr,
                "* HPACK HUFFMAN TEST FAILED result error (3).\n*    Got "
                "(%u): %.*s\n",
                tmp, (int)tmp, results);
        exit(-1);
      }
    }
    fprintf(stderr, "* HPACK Huffman compression test finished.\n");
  }
  buf_pos = 0;
  memset(buffer, 0, 128);
  if (1) {
    /* test string packing */
    size_t pos = 0;
    int tmp = hpack_string_unpack(
        buffer, limit, "\x0a\x63\x75\x73\x74\x6f\x6d\x2d\x6b\x65\x79", 11,
        &pos);
    if (pos != 11) {
      fprintf(stderr,
              "* HPACK STRING UNPACKING FAILED(!) wrong reading position %zu "
              "!= 11\n",
              pos);
      exit(-1);
    }
    if (tmp == -1) {
      fprintf(stderr, "* HPACK STRING UNPACKING FAILED(!) for example.\n");
      exit(-1);
    } else {
      if (tmp != 10)
        fprintf(stderr,
                "* HPACK STRING UNPACKING ERROR example len %d != 10.\n", tmp);
      if (memcmp(buffer, "\x63\x75\x73\x74\x6f\x6d\x2d\x6b\x65\x79", 10))
        fprintf(stderr,
                "* HPACK STRING UNPACKING ERROR example returned: %.*s\n",
                (int)tmp, buffer);
    }

    pos = 0;
    memset(buffer, 0, 128);
    tmp = hpack_string_unpack(
        buffer, limit, "\x8c\xf1\xe3\xc2\xe5\xf2\x3a\x6b\xa0\xab\x90\xf4\xff",
        13, &pos);
    if (tmp == -1) {
      fprintf(stderr,
              "* HPACK STRING UNPACKING FAILED(!) for compressed example. %s\n",
              buffer);
      exit(-1);
    } else {
      if (tmp != 15) {
        fprintf(
            stderr,
            "* HPACK STRING UNPACKING ERROR compressed example len %d != 15.\n",
            tmp);
        exit(-1);
      }
      if (memcmp(buffer, "www.example.com", 10)) {
        fprintf(stderr,
                "* HPACK STRING UNPACKING ERROR compressed example returned: "
                "%.*s\n",
                tmp, buffer);
        exit(-1);
      }
      if (pos != 13) {
        fprintf(stderr,
                "* HPACK STRING UNPACKING FAILED(!) wrong reading position %zu "
                "!= 13\n",
                pos);
        exit(-1);
      }
    }

    if (1) {
      char *str1 = "This is a string to be packed, either compressed or not.";
      buf_pos = 0;
      size_t i = 0;
      const size_t repeats = 1024;
      for (i = 0; i < repeats; i++) {
        tmp = hpack_string_pack(buffer + buf_pos, limit - buf_pos, str1, 56,
                                (i & 1) == 1);
        if (tmp == -1)
          fprintf(stderr, "* HPACK STRING PACKING FAIL AT %zu\n", i);
        else if ((size_t)tmp > limit - buf_pos)
          break;
        buf_pos += tmp;
      }
      int count = i;
      buf_pos = 0;
      while (i) {
        char result[56];
        memset(result, 0, 56);
        --i;
        tmp = hpack_string_unpack(result, 56, buffer, limit, &buf_pos);
        if (tmp == -1) {
          fprintf(stderr, "* HPACK STRING UNPACKING FAIL AT %zu\n",
                  (repeats - 1) - i);
          exit(-1);
        } else if (tmp != 56) {
          fprintf(stderr,
                  "* HPACK STRING UNPACKING ERROR AT %zu - got string "
                  "length %u instead of 56: %.*s\n",
                  (repeats - 1) - i, tmp, 56, result);
          exit(-1);
        }
        if (memcmp(str1, result, 56)) {
          fprintf(stderr,
                  "* HPACK STRING UNPACKING ERROR AT %zu. Got (%u) %.*s\n",
                  (repeats - 1) - i, tmp, tmp, result);
          exit(-1);
        }
      }
      fprintf(stderr,
              "* HPACK string primitive test complete (buffer used %d/%zu "
              "strings)\n",
              count, repeats);
    }
  }
}
#else

#define hpack_test()

#endif /* DEBUG */

/* *****************************************************************************






                  Auto-generate binary tree from table data






***************************************************************************** */

#if HPACK_BUILD_HPACK_STRUCT

/*
This section prints out the C code required to create a static, Array based,
binary tree with the following type / fields:
*/

#include <stdio.h>

typedef struct {
  uint32_t code;
  uint8_t bits;
  int16_t value;
} huffman_code_s;

/* the huffman decoding binary tree type */
typedef struct {
  int16_t value;     // value, -1 == none.
  uint8_t offset[2]; // offset for 0 and one. 0 == leaf node.
} huffman_decode_nc_s;

/** used to print the binary reverse testing */
static MAYBE_UNUSED void huffman__print_bin_num(uint32_t num, uint8_t bits) {
  fprintf(stderr, "0b");
  if (((32 - bits) & 31))
    num <<= ((32 - bits) & 31);
  for (size_t i = 0; i < bits; i++) {
    if (num & (1 << (31 - i)))
      fprintf(stderr, "1");
    else
      fprintf(stderr, "0");
  }
}

static void huffman__print_unit(huffman_decode_nc_s d, size_t index,
                                size_t code, size_t bits) {
  if (d.value != -1) {
    fprintf(stderr,
            " {.value = %d, .offset = {%zu, %zu}}, // [%zu]:", (int)d.value,
            (size_t)d.offset[0], (size_t)d.offset[1], index);
    huffman__print_bin_num(code, bits);
    fprintf(stderr, "\n");
  } else {
    fprintf(stderr, " {.value = %d, .offset = {%zu, %zu}}, // [%zu]\n",
            (int)d.value, (size_t)d.offset[0], (size_t)d.offset[1], index);
  }
}

#define HUFFMAN_TREE_BUFFER (1 << 12)

void huffman__print_tree(void) {
  /* The Huffman Encoding table was copied from
   * http://httpwg.org/specs/rfc7541.html#huffman.code
   */
  const huffman_encode_s encode_table[] = {
      /* 257 elements, 0..256 all sym + EOS */
      {0x1ff8U, 13},     {0x7fffd8U, 23},   {0xfffffe2U, 28},  {0xfffffe3U, 28},
      {0xfffffe4U, 28},  {0xfffffe5U, 28},  {0xfffffe6U, 28},  {0xfffffe7U, 28},
      {0xfffffe8U, 28},  {0xffffeaU, 24},   {0x3ffffffcU, 30}, {0xfffffe9U, 28},
      {0xfffffeaU, 28},  {0x3ffffffdU, 30}, {0xfffffebU, 28},  {0xfffffecU, 28},
      {0xfffffedU, 28},  {0xfffffeeU, 28},  {0xfffffefU, 28},  {0xffffff0U, 28},
      {0xffffff1U, 28},  {0xffffff2U, 28},  {0x3ffffffeU, 30}, {0xffffff3U, 28},
      {0xffffff4U, 28},  {0xffffff5U, 28},  {0xffffff6U, 28},  {0xffffff7U, 28},
      {0xffffff8U, 28},  {0xffffff9U, 28},  {0xffffffaU, 28},  {0xffffffbU, 28},
      {0x14U, 6},        {0x3f8U, 10},      {0x3f9U, 10},      {0xffaU, 12},
      {0x1ff9U, 13},     {0x15U, 6},        {0xf8U, 8},        {0x7faU, 11},
      {0x3faU, 10},      {0x3fbU, 10},      {0xf9U, 8},        {0x7fbU, 11},
      {0xfaU, 8},        {0x16U, 6},        {0x17U, 6},        {0x18U, 6},
      {0x0U, 5},         {0x1U, 5},         {0x2U, 5},         {0x19U, 6},
      {0x1aU, 6},        {0x1bU, 6},        {0x1cU, 6},        {0x1dU, 6},
      {0x1eU, 6},        {0x1fU, 6},        {0x5cU, 7},        {0xfbU, 8},
      {0x7ffcU, 15},     {0x20U, 6},        {0xffbU, 12},      {0x3fcU, 10},
      {0x1ffaU, 13},     {0x21U, 6},        {0x5dU, 7},        {0x5eU, 7},
      {0x5fU, 7},        {0x60U, 7},        {0x61U, 7},        {0x62U, 7},
      {0x63U, 7},        {0x64U, 7},        {0x65U, 7},        {0x66U, 7},
      {0x67U, 7},        {0x68U, 7},        {0x69U, 7},        {0x6aU, 7},
      {0x6bU, 7},        {0x6cU, 7},        {0x6dU, 7},        {0x6eU, 7},
      {0x6fU, 7},        {0x70U, 7},        {0x71U, 7},        {0x72U, 7},
      {0xfcU, 8},        {0x73U, 7},        {0xfdU, 8},        {0x1ffbU, 13},
      {0x7fff0U, 19},    {0x1ffcU, 13},     {0x3ffcU, 14},     {0x22U, 6},
      {0x7ffdU, 15},     {0x3U, 5},         {0x23U, 6},        {0x4U, 5},
      {0x24U, 6},        {0x5U, 5},         {0x25U, 6},        {0x26U, 6},
      {0x27U, 6},        {0x6U, 5},         {0x74U, 7},        {0x75U, 7},
      {0x28U, 6},        {0x29U, 6},        {0x2aU, 6},        {0x7U, 5},
      {0x2bU, 6},        {0x76U, 7},        {0x2cU, 6},        {0x8U, 5},
      {0x9U, 5},         {0x2dU, 6},        {0x77U, 7},        {0x78U, 7},
      {0x79U, 7},        {0x7aU, 7},        {0x7bU, 7},        {0x7ffeU, 15},
      {0x7fcU, 11},      {0x3ffdU, 14},     {0x1ffdU, 13},     {0xffffffcU, 28},
      {0xfffe6U, 20},    {0x3fffd2U, 22},   {0xfffe7U, 20},    {0xfffe8U, 20},
      {0x3fffd3U, 22},   {0x3fffd4U, 22},   {0x3fffd5U, 22},   {0x7fffd9U, 23},
      {0x3fffd6U, 22},   {0x7fffdaU, 23},   {0x7fffdbU, 23},   {0x7fffdcU, 23},
      {0x7fffddU, 23},   {0x7fffdeU, 23},   {0xffffebU, 24},   {0x7fffdfU, 23},
      {0xffffecU, 24},   {0xffffedU, 24},   {0x3fffd7U, 22},   {0x7fffe0U, 23},
      {0xffffeeU, 24},   {0x7fffe1U, 23},   {0x7fffe2U, 23},   {0x7fffe3U, 23},
      {0x7fffe4U, 23},   {0x1fffdcU, 21},   {0x3fffd8U, 22},   {0x7fffe5U, 23},
      {0x3fffd9U, 22},   {0x7fffe6U, 23},   {0x7fffe7U, 23},   {0xffffefU, 24},
      {0x3fffdaU, 22},   {0x1fffddU, 21},   {0xfffe9U, 20},    {0x3fffdbU, 22},
      {0x3fffdcU, 22},   {0x7fffe8U, 23},   {0x7fffe9U, 23},   {0x1fffdeU, 21},
      {0x7fffeaU, 23},   {0x3fffddU, 22},   {0x3fffdeU, 22},   {0xfffff0U, 24},
      {0x1fffdfU, 21},   {0x3fffdfU, 22},   {0x7fffebU, 23},   {0x7fffecU, 23},
      {0x1fffe0U, 21},   {0x1fffe1U, 21},   {0x3fffe0U, 22},   {0x1fffe2U, 21},
      {0x7fffedU, 23},   {0x3fffe1U, 22},   {0x7fffeeU, 23},   {0x7fffefU, 23},
      {0xfffeaU, 20},    {0x3fffe2U, 22},   {0x3fffe3U, 22},   {0x3fffe4U, 22},
      {0x7ffff0U, 23},   {0x3fffe5U, 22},   {0x3fffe6U, 22},   {0x7ffff1U, 23},
      {0x3ffffe0U, 26},  {0x3ffffe1U, 26},  {0xfffebU, 20},    {0x7fff1U, 19},
      {0x3fffe7U, 22},   {0x7ffff2U, 23},   {0x3fffe8U, 22},   {0x1ffffecU, 25},
      {0x3ffffe2U, 26},  {0x3ffffe3U, 26},  {0x3ffffe4U, 26},  {0x7ffffdeU, 27},
      {0x7ffffdfU, 27},  {0x3ffffe5U, 26},  {0xfffff1U, 24},   {0x1ffffedU, 25},
      {0x7fff2U, 19},    {0x1fffe3U, 21},   {0x3ffffe6U, 26},  {0x7ffffe0U, 27},
      {0x7ffffe1U, 27},  {0x3ffffe7U, 26},  {0x7ffffe2U, 27},  {0xfffff2U, 24},
      {0x1fffe4U, 21},   {0x1fffe5U, 21},   {0x3ffffe8U, 26},  {0x3ffffe9U, 26},
      {0xffffffdU, 28},  {0x7ffffe3U, 27},  {0x7ffffe4U, 27},  {0x7ffffe5U, 27},
      {0xfffecU, 20},    {0xfffff3U, 24},   {0xfffedU, 20},    {0x1fffe6U, 21},
      {0x3fffe9U, 22},   {0x1fffe7U, 21},   {0x1fffe8U, 21},   {0x7ffff3U, 23},
      {0x3fffeaU, 22},   {0x3fffebU, 22},   {0x1ffffeeU, 25},  {0x1ffffefU, 25},
      {0xfffff4U, 24},   {0xfffff5U, 24},   {0x3ffffeaU, 26},  {0x7ffff4U, 23},
      {0x3ffffebU, 26},  {0x7ffffe6U, 27},  {0x3ffffecU, 26},  {0x3ffffedU, 26},
      {0x7ffffe7U, 27},  {0x7ffffe8U, 27},  {0x7ffffe9U, 27},  {0x7ffffeaU, 27},
      {0x7ffffebU, 27},  {0xffffffeU, 28},  {0x7ffffecU, 27},  {0x7ffffedU, 27},
      {0x7ffffeeU, 27},  {0x7ffffefU, 27},  {0x7fffff0U, 27},  {0x3ffffeeU, 26},
      {0x3fffffffU, 30},
  };
  /* copy code list */
  huffman_code_s ordered[257];
  for (uint16_t i = 0; i < 257; ++i) {
    ordered[i] = (huffman_code_s){
        .value = i,
        .bits = encode_table[i].bits,
        .code = encode_table[i].code,
    };
  }
  /* order list by code's bit order (0100 > 0011), use a bunch of CPU... */
  {
    uint16_t i = 0;
    while (i < 256) {
      if (ordered[i].code > ordered[i + 1].code) {
        huffman_code_s tmp = ordered[i + 1];
        ++i;
        do {
          ordered[i] = ordered[i - 1];
        } while (--i && ordered[i - 1].code > tmp.code);
        ordered[i] = tmp;
      }
      ++i;
    }
  }
  /* build tree */
  huffman_decode_nc_s tree[HUFFMAN_TREE_BUFFER];
  size_t tree_len = 0;
  for (int i = 0; i < HUFFMAN_TREE_BUFFER; ++i) {
    tree[i] = (huffman_decode_nc_s){.value = -1,
                                    .offset = {(uint8_t)-1, (uint8_t)-1}};
  }
  {
    size_t max_offset = 0;
    size_t next = 1;
    for (int i = 0; i < 257; ++i) {
      /* for each code point, map a tree path */
      size_t pos = 0;
      uint32_t code = ordered[i].code;
      for (int b = 0; b < ordered[i].bits; ++b) {
        if (code & (1ULL << (ordered[i].bits - 1))) {
          /* map 1 branch */
          if (tree[pos].offset[1] != (uint8_t)-1)
            pos += tree[pos].offset[1];
          else {
            if (next - pos > max_offset)
              max_offset = next - pos;
            tree[pos].offset[1] = next - pos;
            pos = next;
            ++next;
          }
        } else {
          /* map 0 branch */
          if (tree[pos].offset[0] != (uint8_t)-1)
            pos += tree[pos].offset[0];
          else {
            if (next - pos > max_offset)
              max_offset = next - pos;
            tree[pos].offset[0] = next - pos;
            pos = next;
            ++next;
          }
        }
        code <<= 1;
      }
      tree[pos] = (huffman_decode_nc_s){.value = ordered[i].value};
    }
    fprintf(stderr, "Total tree length = %zu, max offset = %zu\n", next,
            max_offset);
    tree_len = next;
  }
  {
    /* Validate tree */
    for (int i = 0; i < 257; ++i) {
      huffman_decode_nc_s *node = tree;
      uint32_t code = ordered[i].code;
      uint8_t consumed = 32 - ordered[i].bits;
      code <<= consumed;
      while (consumed < 32) {
        node += node->offset[(code >> 31) & 1];
        code <<= 1;
        ++consumed;
      }
      if (ordered[i].value != node->value) {
        fprintf(stderr,
                "ERROR building tree - validation error for %d (value: %d != "
                "%d)\n",
                i, node->value, ordered[i].value);
        exit(-1);
      }
    }
  }
  fprintf(stderr,
          "***** Copy after this line ****\n\n"
          "/** Static Huffman encoding map, left aligned */\n"

          "static const huffman_encode_s huffman_encode_table[257] = {\n");
  for (size_t i = 0; i < 257; ++i) {
    /* print huffman code left align */
    fprintf(stderr, " {.code = 0x%.08X, .bits = %u}, // [%zu] \n",
            (encode_table[i].code << (32 - encode_table[i].bits)),
            encode_table[i].bits, i);
  }
  fprintf(stderr,
          "};\n\n/** Static Huffman decoding tree, flattened as an array */\n"

          "static const huffman_decode_s huffman_decode_tree[%zu] = {\n",
          tree_len);
  for (size_t i = 0; i < tree_len; ++i) {
    huffman__print_unit(
        tree[i], i,
        (tree[i].value == -1) ? 0 : encode_table[tree[i].value].code,
        (tree[i].value == -1) ? 0 : encode_table[tree[i].value].bits);
  }
  fprintf(stderr, "};\n\n\n**************( stop copying )**************\n\n");
  for (int i = 0; i < 256; ++i) {
    uint8_t data[4] = {0};
    uint8_t result = 0;
    size_t r_pos = 0;
    uint32_t code = ordered[i].code;
    code <<= 32 - ordered[i].bits;
    code |= (1UL << (32 - ordered[i].bits)) - 1;
    data[0] = (code >> 24) & 0xFF;
    data[1] = (code >> 16) & 0xFF;
    data[2] = (code >> 8) & 0xFF;
    data[3] = (code >> 0) & 0xFF;
    hpack_huffman_unpack(&result, 1, &data, 4, &r_pos);
    r_pos = 0;
    if (result != ordered[i].value) {
      fprintf(stderr, "ERR: (%u) %u != %u (%d, %d)\n", data[0], result,
              ordered[i].value,
              hpack_huffman_unpack(&result, 1, &data, 1, &r_pos), i);
      exit(-1);
    }
  }
  hpack_test();
}

int main(void) {
  huffman__print_tree();
  return 0;
}

#endif

/* *****************************************************************************






                      Paste auto-generated data here







***************************************************************************** */

/** Static Huffman encoding map, left aligned */
static const huffman_encode_s huffman_encode_table[257] = {
    {.code = 0xFFC00000, .bits = 13}, // [0]
    {.code = 0xFFFFB000, .bits = 23}, // [1]
    {.code = 0xFFFFFE20, .bits = 28}, // [2]
    {.code = 0xFFFFFE30, .bits = 28}, // [3]
    {.code = 0xFFFFFE40, .bits = 28}, // [4]
    {.code = 0xFFFFFE50, .bits = 28}, // [5]
    {.code = 0xFFFFFE60, .bits = 28}, // [6]
    {.code = 0xFFFFFE70, .bits = 28}, // [7]
    {.code = 0xFFFFFE80, .bits = 28}, // [8]
    {.code = 0xFFFFEA00, .bits = 24}, // [9]
    {.code = 0xFFFFFFF0, .bits = 30}, // [10]
    {.code = 0xFFFFFE90, .bits = 28}, // [11]
    {.code = 0xFFFFFEA0, .bits = 28}, // [12]
    {.code = 0xFFFFFFF4, .bits = 30}, // [13]
    {.code = 0xFFFFFEB0, .bits = 28}, // [14]
    {.code = 0xFFFFFEC0, .bits = 28}, // [15]
    {.code = 0xFFFFFED0, .bits = 28}, // [16]
    {.code = 0xFFFFFEE0, .bits = 28}, // [17]
    {.code = 0xFFFFFEF0, .bits = 28}, // [18]
    {.code = 0xFFFFFF00, .bits = 28}, // [19]
    {.code = 0xFFFFFF10, .bits = 28}, // [20]
    {.code = 0xFFFFFF20, .bits = 28}, // [21]
    {.code = 0xFFFFFFF8, .bits = 30}, // [22]
    {.code = 0xFFFFFF30, .bits = 28}, // [23]
    {.code = 0xFFFFFF40, .bits = 28}, // [24]
    {.code = 0xFFFFFF50, .bits = 28}, // [25]
    {.code = 0xFFFFFF60, .bits = 28}, // [26]
    {.code = 0xFFFFFF70, .bits = 28}, // [27]
    {.code = 0xFFFFFF80, .bits = 28}, // [28]
    {.code = 0xFFFFFF90, .bits = 28}, // [29]
    {.code = 0xFFFFFFA0, .bits = 28}, // [30]
    {.code = 0xFFFFFFB0, .bits = 28}, // [31]
    {.code = 0x50000000, .bits = 6},  // [32]
    {.code = 0xFE000000, .bits = 10}, // [33]
    {.code = 0xFE400000, .bits = 10}, // [34]
    {.code = 0xFFA00000, .bits = 12}, // [35]
    {.code = 0xFFC80000, .bits = 13}, // [36]
    {.code = 0x54000000, .bits = 6},  // [37]
    {.code = 0xF8000000, .bits = 8},  // [38]
    {.code = 0xFF400000, .bits = 11}, // [39]
    {.code = 0xFE800000, .bits = 10}, // [40]
    {.code = 0xFEC00000, .bits = 10}, // [41]
    {.code = 0xF9000000, .bits = 8},  // [42]
    {.code = 0xFF600000, .bits = 11}, // [43]
    {.code = 0xFA000000, .bits = 8},  // [44]
    {.code = 0x58000000, .bits = 6},  // [45]
    {.code = 0x5C000000, .bits = 6},  // [46]
    {.code = 0x60000000, .bits = 6},  // [47]
    {.code = 0x00000000, .bits = 5},  // [48]
    {.code = 0x08000000, .bits = 5},  // [49]
    {.code = 0x10000000, .bits = 5},  // [50]
    {.code = 0x64000000, .bits = 6},  // [51]
    {.code = 0x68000000, .bits = 6},  // [52]
    {.code = 0x6C000000, .bits = 6},  // [53]
    {.code = 0x70000000, .bits = 6},  // [54]
    {.code = 0x74000000, .bits = 6},  // [55]
    {.code = 0x78000000, .bits = 6},  // [56]
    {.code = 0x7C000000, .bits = 6},  // [57]
    {.code = 0xB8000000, .bits = 7},  // [58]
    {.code = 0xFB000000, .bits = 8},  // [59]
    {.code = 0xFFF80000, .bits = 15}, // [60]
    {.code = 0x80000000, .bits = 6},  // [61]
    {.code = 0xFFB00000, .bits = 12}, // [62]
    {.code = 0xFF000000, .bits = 10}, // [63]
    {.code = 0xFFD00000, .bits = 13}, // [64]
    {.code = 0x84000000, .bits = 6},  // [65]
    {.code = 0xBA000000, .bits = 7},  // [66]
    {.code = 0xBC000000, .bits = 7},  // [67]
    {.code = 0xBE000000, .bits = 7},  // [68]
    {.code = 0xC0000000, .bits = 7},  // [69]
    {.code = 0xC2000000, .bits = 7},  // [70]
    {.code = 0xC4000000, .bits = 7},  // [71]
    {.code = 0xC6000000, .bits = 7},  // [72]
    {.code = 0xC8000000, .bits = 7},  // [73]
    {.code = 0xCA000000, .bits = 7},  // [74]
    {.code = 0xCC000000, .bits = 7},  // [75]
    {.code = 0xCE000000, .bits = 7},  // [76]
    {.code = 0xD0000000, .bits = 7},  // [77]
    {.code = 0xD2000000, .bits = 7},  // [78]
    {.code = 0xD4000000, .bits = 7},  // [79]
    {.code = 0xD6000000, .bits = 7},  // [80]
    {.code = 0xD8000000, .bits = 7},  // [81]
    {.code = 0xDA000000, .bits = 7},  // [82]
    {.code = 0xDC000000, .bits = 7},  // [83]
    {.code = 0xDE000000, .bits = 7},  // [84]
    {.code = 0xE0000000, .bits = 7},  // [85]
    {.code = 0xE2000000, .bits = 7},  // [86]
    {.code = 0xE4000000, .bits = 7},  // [87]
    {.code = 0xFC000000, .bits = 8},  // [88]
    {.code = 0xE6000000, .bits = 7},  // [89]
    {.code = 0xFD000000, .bits = 8},  // [90]
    {.code = 0xFFD80000, .bits = 13}, // [91]
    {.code = 0xFFFE0000, .bits = 19}, // [92]
    {.code = 0xFFE00000, .bits = 13}, // [93]
    {.code = 0xFFF00000, .bits = 14}, // [94]
    {.code = 0x88000000, .bits = 6},  // [95]
    {.code = 0xFFFA0000, .bits = 15}, // [96]
    {.code = 0x18000000, .bits = 5},  // [97]
    {.code = 0x8C000000, .bits = 6},  // [98]
    {.code = 0x20000000, .bits = 5},  // [99]
    {.code = 0x90000000, .bits = 6},  // [100]
    {.code = 0x28000000, .bits = 5},  // [101]
    {.code = 0x94000000, .bits = 6},  // [102]
    {.code = 0x98000000, .bits = 6},  // [103]
    {.code = 0x9C000000, .bits = 6},  // [104]
    {.code = 0x30000000, .bits = 5},  // [105]
    {.code = 0xE8000000, .bits = 7},  // [106]
    {.code = 0xEA000000, .bits = 7},  // [107]
    {.code = 0xA0000000, .bits = 6},  // [108]
    {.code = 0xA4000000, .bits = 6},  // [109]
    {.code = 0xA8000000, .bits = 6},  // [110]
    {.code = 0x38000000, .bits = 5},  // [111]
    {.code = 0xAC000000, .bits = 6},  // [112]
    {.code = 0xEC000000, .bits = 7},  // [113]
    {.code = 0xB0000000, .bits = 6},  // [114]
    {.code = 0x40000000, .bits = 5},  // [115]
    {.code = 0x48000000, .bits = 5},  // [116]
    {.code = 0xB4000000, .bits = 6},  // [117]
    {.code = 0xEE000000, .bits = 7},  // [118]
    {.code = 0xF0000000, .bits = 7},  // [119]
    {.code = 0xF2000000, .bits = 7},  // [120]
    {.code = 0xF4000000, .bits = 7},  // [121]
    {.code = 0xF6000000, .bits = 7},  // [122]
    {.code = 0xFFFC0000, .bits = 15}, // [123]
    {.code = 0xFF800000, .bits = 11}, // [124]
    {.code = 0xFFF40000, .bits = 14}, // [125]
    {.code = 0xFFE80000, .bits = 13}, // [126]
    {.code = 0xFFFFFFC0, .bits = 28}, // [127]
    {.code = 0xFFFE6000, .bits = 20}, // [128]
    {.code = 0xFFFF4800, .bits = 22}, // [129]
    {.code = 0xFFFE7000, .bits = 20}, // [130]
    {.code = 0xFFFE8000, .bits = 20}, // [131]
    {.code = 0xFFFF4C00, .bits = 22}, // [132]
    {.code = 0xFFFF5000, .bits = 22}, // [133]
    {.code = 0xFFFF5400, .bits = 22}, // [134]
    {.code = 0xFFFFB200, .bits = 23}, // [135]
    {.code = 0xFFFF5800, .bits = 22}, // [136]
    {.code = 0xFFFFB400, .bits = 23}, // [137]
    {.code = 0xFFFFB600, .bits = 23}, // [138]
    {.code = 0xFFFFB800, .bits = 23}, // [139]
    {.code = 0xFFFFBA00, .bits = 23}, // [140]
    {.code = 0xFFFFBC00, .bits = 23}, // [141]
    {.code = 0xFFFFEB00, .bits = 24}, // [142]
    {.code = 0xFFFFBE00, .bits = 23}, // [143]
    {.code = 0xFFFFEC00, .bits = 24}, // [144]
    {.code = 0xFFFFED00, .bits = 24}, // [145]
    {.code = 0xFFFF5C00, .bits = 22}, // [146]
    {.code = 0xFFFFC000, .bits = 23}, // [147]
    {.code = 0xFFFFEE00, .bits = 24}, // [148]
    {.code = 0xFFFFC200, .bits = 23}, // [149]
    {.code = 0xFFFFC400, .bits = 23}, // [150]
    {.code = 0xFFFFC600, .bits = 23}, // [151]
    {.code = 0xFFFFC800, .bits = 23}, // [152]
    {.code = 0xFFFEE000, .bits = 21}, // [153]
    {.code = 0xFFFF6000, .bits = 22}, // [154]
    {.code = 0xFFFFCA00, .bits = 23}, // [155]
    {.code = 0xFFFF6400, .bits = 22}, // [156]
    {.code = 0xFFFFCC00, .bits = 23}, // [157]
    {.code = 0xFFFFCE00, .bits = 23}, // [158]
    {.code = 0xFFFFEF00, .bits = 24}, // [159]
    {.code = 0xFFFF6800, .bits = 22}, // [160]
    {.code = 0xFFFEE800, .bits = 21}, // [161]
    {.code = 0xFFFE9000, .bits = 20}, // [162]
    {.code = 0xFFFF6C00, .bits = 22}, // [163]
    {.code = 0xFFFF7000, .bits = 22}, // [164]
    {.code = 0xFFFFD000, .bits = 23}, // [165]
    {.code = 0xFFFFD200, .bits = 23}, // [166]
    {.code = 0xFFFEF000, .bits = 21}, // [167]
    {.code = 0xFFFFD400, .bits = 23}, // [168]
    {.code = 0xFFFF7400, .bits = 22}, // [169]
    {.code = 0xFFFF7800, .bits = 22}, // [170]
    {.code = 0xFFFFF000, .bits = 24}, // [171]
    {.code = 0xFFFEF800, .bits = 21}, // [172]
    {.code = 0xFFFF7C00, .bits = 22}, // [173]
    {.code = 0xFFFFD600, .bits = 23}, // [174]
    {.code = 0xFFFFD800, .bits = 23}, // [175]
    {.code = 0xFFFF0000, .bits = 21}, // [176]
    {.code = 0xFFFF0800, .bits = 21}, // [177]
    {.code = 0xFFFF8000, .bits = 22}, // [178]
    {.code = 0xFFFF1000, .bits = 21}, // [179]
    {.code = 0xFFFFDA00, .bits = 23}, // [180]
    {.code = 0xFFFF8400, .bits = 22}, // [181]
    {.code = 0xFFFFDC00, .bits = 23}, // [182]
    {.code = 0xFFFFDE00, .bits = 23}, // [183]
    {.code = 0xFFFEA000, .bits = 20}, // [184]
    {.code = 0xFFFF8800, .bits = 22}, // [185]
    {.code = 0xFFFF8C00, .bits = 22}, // [186]
    {.code = 0xFFFF9000, .bits = 22}, // [187]
    {.code = 0xFFFFE000, .bits = 23}, // [188]
    {.code = 0xFFFF9400, .bits = 22}, // [189]
    {.code = 0xFFFF9800, .bits = 22}, // [190]
    {.code = 0xFFFFE200, .bits = 23}, // [191]
    {.code = 0xFFFFF800, .bits = 26}, // [192]
    {.code = 0xFFFFF840, .bits = 26}, // [193]
    {.code = 0xFFFEB000, .bits = 20}, // [194]
    {.code = 0xFFFE2000, .bits = 19}, // [195]
    {.code = 0xFFFF9C00, .bits = 22}, // [196]
    {.code = 0xFFFFE400, .bits = 23}, // [197]
    {.code = 0xFFFFA000, .bits = 22}, // [198]
    {.code = 0xFFFFF600, .bits = 25}, // [199]
    {.code = 0xFFFFF880, .bits = 26}, // [200]
    {.code = 0xFFFFF8C0, .bits = 26}, // [201]
    {.code = 0xFFFFF900, .bits = 26}, // [202]
    {.code = 0xFFFFFBC0, .bits = 27}, // [203]
    {.code = 0xFFFFFBE0, .bits = 27}, // [204]
    {.code = 0xFFFFF940, .bits = 26}, // [205]
    {.code = 0xFFFFF100, .bits = 24}, // [206]
    {.code = 0xFFFFF680, .bits = 25}, // [207]
    {.code = 0xFFFE4000, .bits = 19}, // [208]
    {.code = 0xFFFF1800, .bits = 21}, // [209]
    {.code = 0xFFFFF980, .bits = 26}, // [210]
    {.code = 0xFFFFFC00, .bits = 27}, // [211]
    {.code = 0xFFFFFC20, .bits = 27}, // [212]
    {.code = 0xFFFFF9C0, .bits = 26}, // [213]
    {.code = 0xFFFFFC40, .bits = 27}, // [214]
    {.code = 0xFFFFF200, .bits = 24}, // [215]
    {.code = 0xFFFF2000, .bits = 21}, // [216]
    {.code = 0xFFFF2800, .bits = 21}, // [217]
    {.code = 0xFFFFFA00, .bits = 26}, // [218]
    {.code = 0xFFFFFA40, .bits = 26}, // [219]
    {.code = 0xFFFFFFD0, .bits = 28}, // [220]
    {.code = 0xFFFFFC60, .bits = 27}, // [221]
    {.code = 0xFFFFFC80, .bits = 27}, // [222]
    {.code = 0xFFFFFCA0, .bits = 27}, // [223]
    {.code = 0xFFFEC000, .bits = 20}, // [224]
    {.code = 0xFFFFF300, .bits = 24}, // [225]
    {.code = 0xFFFED000, .bits = 20}, // [226]
    {.code = 0xFFFF3000, .bits = 21}, // [227]
    {.code = 0xFFFFA400, .bits = 22}, // [228]
    {.code = 0xFFFF3800, .bits = 21}, // [229]
    {.code = 0xFFFF4000, .bits = 21}, // [230]
    {.code = 0xFFFFE600, .bits = 23}, // [231]
    {.code = 0xFFFFA800, .bits = 22}, // [232]
    {.code = 0xFFFFAC00, .bits = 22}, // [233]
    {.code = 0xFFFFF700, .bits = 25}, // [234]
    {.code = 0xFFFFF780, .bits = 25}, // [235]
    {.code = 0xFFFFF400, .bits = 24}, // [236]
    {.code = 0xFFFFF500, .bits = 24}, // [237]
    {.code = 0xFFFFFA80, .bits = 26}, // [238]
    {.code = 0xFFFFE800, .bits = 23}, // [239]
    {.code = 0xFFFFFAC0, .bits = 26}, // [240]
    {.code = 0xFFFFFCC0, .bits = 27}, // [241]
    {.code = 0xFFFFFB00, .bits = 26}, // [242]
    {.code = 0xFFFFFB40, .bits = 26}, // [243]
    {.code = 0xFFFFFCE0, .bits = 27}, // [244]
    {.code = 0xFFFFFD00, .bits = 27}, // [245]
    {.code = 0xFFFFFD20, .bits = 27}, // [246]
    {.code = 0xFFFFFD40, .bits = 27}, // [247]
    {.code = 0xFFFFFD60, .bits = 27}, // [248]
    {.code = 0xFFFFFFE0, .bits = 28}, // [249]
    {.code = 0xFFFFFD80, .bits = 27}, // [250]
    {.code = 0xFFFFFDA0, .bits = 27}, // [251]
    {.code = 0xFFFFFDC0, .bits = 27}, // [252]
    {.code = 0xFFFFFDE0, .bits = 27}, // [253]
    {.code = 0xFFFFFE00, .bits = 27}, // [254]
    {.code = 0xFFFFFB80, .bits = 26}, // [255]
    {.code = 0xFFFFFFFC, .bits = 30}, // [256]
};

/** Static Huffman decoding tree, flattened as an array */
static const huffman_decode_s huffman_decode_tree[513] = {
    {.value = -1, .offset = {1, 44}}, // [0]
    {.value = -1, .offset = {1, 16}}, // [1]
    {.value = -1, .offset = {1, 8}},  // [2]
    {.value = -1, .offset = {1, 4}},  // [3]
    {.value = -1, .offset = {1, 2}},  // [4]
    {.value = 48, .offset = {0, 0}},  // [5]:0b00000
    {.value = 49, .offset = {0, 0}},  // [6]:0b00001
    {.value = -1, .offset = {1, 2}},  // [7]
    {.value = 50, .offset = {0, 0}},  // [8]:0b00010
    {.value = 97, .offset = {0, 0}},  // [9]:0b00011
    {.value = -1, .offset = {1, 4}},  // [10]
    {.value = -1, .offset = {1, 2}},  // [11]
    {.value = 99, .offset = {0, 0}},  // [12]:0b00100
    {.value = 101, .offset = {0, 0}}, // [13]:0b00101
    {.value = -1, .offset = {1, 2}},  // [14]
    {.value = 105, .offset = {0, 0}}, // [15]:0b00110
    {.value = 111, .offset = {0, 0}}, // [16]:0b00111
    {.value = -1, .offset = {1, 12}}, // [17]
    {.value = -1, .offset = {1, 4}},  // [18]
    {.value = -1, .offset = {1, 2}},  // [19]
    {.value = 115, .offset = {0, 0}}, // [20]:0b01000
    {.value = 116, .offset = {0, 0}}, // [21]:0b01001
    {.value = -1, .offset = {1, 4}},  // [22]
    {.value = -1, .offset = {1, 2}},  // [23]
    {.value = 32, .offset = {0, 0}},  // [24]:0b010100
    {.value = 37, .offset = {0, 0}},  // [25]:0b010101
    {.value = -1, .offset = {1, 2}},  // [26]
    {.value = 45, .offset = {0, 0}},  // [27]:0b010110
    {.value = 46, .offset = {0, 0}},  // [28]:0b010111
    {.value = -1, .offset = {1, 8}},  // [29]
    {.value = -1, .offset = {1, 4}},  // [30]
    {.value = -1, .offset = {1, 2}},  // [31]
    {.value = 47, .offset = {0, 0}},  // [32]:0b011000
    {.value = 51, .offset = {0, 0}},  // [33]:0b011001
    {.value = -1, .offset = {1, 2}},  // [34]
    {.value = 52, .offset = {0, 0}},  // [35]:0b011010
    {.value = 53, .offset = {0, 0}},  // [36]:0b011011
    {.value = -1, .offset = {1, 4}},  // [37]
    {.value = -1, .offset = {1, 2}},  // [38]
    {.value = 54, .offset = {0, 0}},  // [39]:0b011100
    {.value = 55, .offset = {0, 0}},  // [40]:0b011101
    {.value = -1, .offset = {1, 2}},  // [41]
    {.value = 56, .offset = {0, 0}},  // [42]:0b011110
    {.value = 57, .offset = {0, 0}},  // [43]:0b011111
    {.value = -1, .offset = {1, 36}}, // [44]
    {.value = -1, .offset = {1, 16}}, // [45]
    {.value = -1, .offset = {1, 8}},  // [46]
    {.value = -1, .offset = {1, 4}},  // [47]
    {.value = -1, .offset = {1, 2}},  // [48]
    {.value = 61, .offset = {0, 0}},  // [49]:0b100000
    {.value = 65, .offset = {0, 0}},  // [50]:0b100001
    {.value = -1, .offset = {1, 2}},  // [51]
    {.value = 95, .offset = {0, 0}},  // [52]:0b100010
    {.value = 98, .offset = {0, 0}},  // [53]:0b100011
    {.value = -1, .offset = {1, 4}},  // [54]
    {.value = -1, .offset = {1, 2}},  // [55]
    {.value = 100, .offset = {0, 0}}, // [56]:0b100100
    {.value = 102, .offset = {0, 0}}, // [57]:0b100101
    {.value = -1, .offset = {1, 2}},  // [58]
    {.value = 103, .offset = {0, 0}}, // [59]:0b100110
    {.value = 104, .offset = {0, 0}}, // [60]:0b100111
    {.value = -1, .offset = {1, 8}},  // [61]
    {.value = -1, .offset = {1, 4}},  // [62]
    {.value = -1, .offset = {1, 2}},  // [63]
    {.value = 108, .offset = {0, 0}}, // [64]:0b101000
    {.value = 109, .offset = {0, 0}}, // [65]:0b101001
    {.value = -1, .offset = {1, 2}},  // [66]
    {.value = 110, .offset = {0, 0}}, // [67]:0b101010
    {.value = 112, .offset = {0, 0}}, // [68]:0b101011
    {.value = -1, .offset = {1, 4}},  // [69]
    {.value = -1, .offset = {1, 2}},  // [70]
    {.value = 114, .offset = {0, 0}}, // [71]:0b101100
    {.value = 117, .offset = {0, 0}}, // [72]:0b101101
    {.value = -1, .offset = {1, 4}},  // [73]
    {.value = -1, .offset = {1, 2}},  // [74]
    {.value = 58, .offset = {0, 0}},  // [75]:0b1011100
    {.value = 66, .offset = {0, 0}},  // [76]:0b1011101
    {.value = -1, .offset = {1, 2}},  // [77]
    {.value = 67, .offset = {0, 0}},  // [78]:0b1011110
    {.value = 68, .offset = {0, 0}},  // [79]:0b1011111
    {.value = -1, .offset = {1, 32}}, // [80]
    {.value = -1, .offset = {1, 16}}, // [81]
    {.value = -1, .offset = {1, 8}},  // [82]
    {.value = -1, .offset = {1, 4}},  // [83]
    {.value = -1, .offset = {1, 2}},  // [84]
    {.value = 69, .offset = {0, 0}},  // [85]:0b1100000
    {.value = 70, .offset = {0, 0}},  // [86]:0b1100001
    {.value = -1, .offset = {1, 2}},  // [87]
    {.value = 71, .offset = {0, 0}},  // [88]:0b1100010
    {.value = 72, .offset = {0, 0}},  // [89]:0b1100011
    {.value = -1, .offset = {1, 4}},  // [90]
    {.value = -1, .offset = {1, 2}},  // [91]
    {.value = 73, .offset = {0, 0}},  // [92]:0b1100100
    {.value = 74, .offset = {0, 0}},  // [93]:0b1100101
    {.value = -1, .offset = {1, 2}},  // [94]
    {.value = 75, .offset = {0, 0}},  // [95]:0b1100110
    {.value = 76, .offset = {0, 0}},  // [96]:0b1100111
    {.value = -1, .offset = {1, 8}},  // [97]
    {.value = -1, .offset = {1, 4}},  // [98]
    {.value = -1, .offset = {1, 2}},  // [99]
    {.value = 77, .offset = {0, 0}},  // [100]:0b1101000
    {.value = 78, .offset = {0, 0}},  // [101]:0b1101001
    {.value = -1, .offset = {1, 2}},  // [102]
    {.value = 79, .offset = {0, 0}},  // [103]:0b1101010
    {.value = 80, .offset = {0, 0}},  // [104]:0b1101011
    {.value = -1, .offset = {1, 4}},  // [105]
    {.value = -1, .offset = {1, 2}},  // [106]
    {.value = 81, .offset = {0, 0}},  // [107]:0b1101100
    {.value = 82, .offset = {0, 0}},  // [108]:0b1101101
    {.value = -1, .offset = {1, 2}},  // [109]
    {.value = 83, .offset = {0, 0}},  // [110]:0b1101110
    {.value = 84, .offset = {0, 0}},  // [111]:0b1101111
    {.value = -1, .offset = {1, 16}}, // [112]
    {.value = -1, .offset = {1, 8}},  // [113]
    {.value = -1, .offset = {1, 4}},  // [114]
    {.value = -1, .offset = {1, 2}},  // [115]
    {.value = 85, .offset = {0, 0}},  // [116]:0b1110000
    {.value = 86, .offset = {0, 0}},  // [117]:0b1110001
    {.value = -1, .offset = {1, 2}},  // [118]
    {.value = 87, .offset = {0, 0}},  // [119]:0b1110010
    {.value = 89, .offset = {0, 0}},  // [120]:0b1110011
    {.value = -1, .offset = {1, 4}},  // [121]
    {.value = -1, .offset = {1, 2}},  // [122]
    {.value = 106, .offset = {0, 0}}, // [123]:0b1110100
    {.value = 107, .offset = {0, 0}}, // [124]:0b1110101
    {.value = -1, .offset = {1, 2}},  // [125]
    {.value = 113, .offset = {0, 0}}, // [126]:0b1110110
    {.value = 118, .offset = {0, 0}}, // [127]:0b1110111
    {.value = -1, .offset = {1, 8}},  // [128]
    {.value = -1, .offset = {1, 4}},  // [129]
    {.value = -1, .offset = {1, 2}},  // [130]
    {.value = 119, .offset = {0, 0}}, // [131]:0b1111000
    {.value = 120, .offset = {0, 0}}, // [132]:0b1111001
    {.value = -1, .offset = {1, 2}},  // [133]
    {.value = 121, .offset = {0, 0}}, // [134]:0b1111010
    {.value = 122, .offset = {0, 0}}, // [135]:0b1111011
    {.value = -1, .offset = {1, 8}},  // [136]
    {.value = -1, .offset = {1, 4}},  // [137]
    {.value = -1, .offset = {1, 2}},  // [138]
    {.value = 38, .offset = {0, 0}},  // [139]:0b11111000
    {.value = 42, .offset = {0, 0}},  // [140]:0b11111001
    {.value = -1, .offset = {1, 2}},  // [141]
    {.value = 44, .offset = {0, 0}},  // [142]:0b11111010
    {.value = 59, .offset = {0, 0}},  // [143]:0b11111011
    {.value = -1, .offset = {1, 4}},  // [144]
    {.value = -1, .offset = {1, 2}},  // [145]
    {.value = 88, .offset = {0, 0}},  // [146]:0b11111100
    {.value = 90, .offset = {0, 0}},  // [147]:0b11111101
    {.value = -1, .offset = {1, 8}},  // [148]
    {.value = -1, .offset = {1, 4}},  // [149]
    {.value = -1, .offset = {1, 2}},  // [150]
    {.value = 33, .offset = {0, 0}},  // [151]:0b1111111000
    {.value = 34, .offset = {0, 0}},  // [152]:0b1111111001
    {.value = -1, .offset = {1, 2}},  // [153]
    {.value = 40, .offset = {0, 0}},  // [154]:0b1111111010
    {.value = 41, .offset = {0, 0}},  // [155]:0b1111111011
    {.value = -1, .offset = {1, 6}},  // [156]
    {.value = -1, .offset = {1, 2}},  // [157]
    {.value = 63, .offset = {0, 0}},  // [158]:0b1111111100
    {.value = -1, .offset = {1, 2}},  // [159]
    {.value = 39, .offset = {0, 0}},  // [160]:0b11111111010
    {.value = 43, .offset = {0, 0}},  // [161]:0b11111111011
    {.value = -1, .offset = {1, 6}},  // [162]
    {.value = -1, .offset = {1, 2}},  // [163]
    {.value = 124, .offset = {0, 0}}, // [164]:0b11111111100
    {.value = -1, .offset = {1, 2}},  // [165]
    {.value = 35, .offset = {0, 0}},  // [166]:0b111111111010
    {.value = 62, .offset = {0, 0}},  // [167]:0b111111111011
    {.value = -1, .offset = {1, 8}},  // [168]
    {.value = -1, .offset = {1, 4}},  // [169]
    {.value = -1, .offset = {1, 2}},  // [170]
    {.value = 0, .offset = {0, 0}},   // [171]:0b1111111111000
    {.value = 36, .offset = {0, 0}},  // [172]:0b1111111111001
    {.value = -1, .offset = {1, 2}},  // [173]
    {.value = 64, .offset = {0, 0}},  // [174]:0b1111111111010
    {.value = 91, .offset = {0, 0}},  // [175]:0b1111111111011
    {.value = -1, .offset = {1, 4}},  // [176]
    {.value = -1, .offset = {1, 2}},  // [177]
    {.value = 93, .offset = {0, 0}},  // [178]:0b1111111111100
    {.value = 126, .offset = {0, 0}}, // [179]:0b1111111111101
    {.value = -1, .offset = {1, 4}},  // [180]
    {.value = -1, .offset = {1, 2}},  // [181]
    {.value = 94, .offset = {0, 0}},  // [182]:0b11111111111100
    {.value = 125, .offset = {0, 0}}, // [183]:0b11111111111101
    {.value = -1, .offset = {1, 4}},  // [184]
    {.value = -1, .offset = {1, 2}},  // [185]
    {.value = 60, .offset = {0, 0}},  // [186]:0b111111111111100
    {.value = 96, .offset = {0, 0}},  // [187]:0b111111111111101
    {.value = -1, .offset = {1, 2}},  // [188]
    {.value = 123, .offset = {0, 0}}, // [189]:0b111111111111110
    {.value = -1, .offset = {1, 30}}, // [190]
    {.value = -1, .offset = {1, 10}}, // [191]
    {.value = -1, .offset = {1, 4}},  // [192]
    {.value = -1, .offset = {1, 2}},  // [193]
    {.value = 92, .offset = {0, 0}},  // [194]:0b1111111111111110000
    {.value = 195, .offset = {0, 0}}, // [195]:0b1111111111111110001
    {.value = -1, .offset = {1, 2}},  // [196]
    {.value = 208, .offset = {0, 0}}, // [197]:0b1111111111111110010
    {.value = -1, .offset = {1, 2}},  // [198]
    {.value = 128, .offset = {0, 0}}, // [199]:0b11111111111111100110
    {.value = 130, .offset = {0, 0}}, // [200]:0b11111111111111100111
    {.value = -1, .offset = {1, 8}},  // [201]
    {.value = -1, .offset = {1, 4}},  // [202]
    {.value = -1, .offset = {1, 2}},  // [203]
    {.value = 131, .offset = {0, 0}}, // [204]:0b11111111111111101000
    {.value = 162, .offset = {0, 0}}, // [205]:0b11111111111111101001
    {.value = -1, .offset = {1, 2}},  // [206]
    {.value = 184, .offset = {0, 0}}, // [207]:0b11111111111111101010
    {.value = 194, .offset = {0, 0}}, // [208]:0b11111111111111101011
    {.value = -1, .offset = {1, 4}},  // [209]
    {.value = -1, .offset = {1, 2}},  // [210]
    {.value = 224, .offset = {0, 0}}, // [211]:0b11111111111111101100
    {.value = 226, .offset = {0, 0}}, // [212]:0b11111111111111101101
    {.value = -1, .offset = {1, 4}},  // [213]
    {.value = -1, .offset = {1, 2}},  // [214]
    {.value = 153, .offset = {0, 0}}, // [215]:0b111111111111111011100
    {.value = 161, .offset = {0, 0}}, // [216]:0b111111111111111011101
    {.value = -1, .offset = {1, 2}},  // [217]
    {.value = 167, .offset = {0, 0}}, // [218]:0b111111111111111011110
    {.value = 172, .offset = {0, 0}}, // [219]:0b111111111111111011111
    {.value = -1, .offset = {1, 46}}, // [220]
    {.value = -1, .offset = {1, 16}}, // [221]
    {.value = -1, .offset = {1, 8}},  // [222]
    {.value = -1, .offset = {1, 4}},  // [223]
    {.value = -1, .offset = {1, 2}},  // [224]
    {.value = 176, .offset = {0, 0}}, // [225]:0b111111111111111100000
    {.value = 177, .offset = {0, 0}}, // [226]:0b111111111111111100001
    {.value = -1, .offset = {1, 2}},  // [227]
    {.value = 179, .offset = {0, 0}}, // [228]:0b111111111111111100010
    {.value = 209, .offset = {0, 0}}, // [229]:0b111111111111111100011
    {.value = -1, .offset = {1, 4}},  // [230]
    {.value = -1, .offset = {1, 2}},  // [231]
    {.value = 216, .offset = {0, 0}}, // [232]:0b111111111111111100100
    {.value = 217, .offset = {0, 0}}, // [233]:0b111111111111111100101
    {.value = -1, .offset = {1, 2}},  // [234]
    {.value = 227, .offset = {0, 0}}, // [235]:0b111111111111111100110
    {.value = 229, .offset = {0, 0}}, // [236]:0b111111111111111100111
    {.value = -1, .offset = {1, 14}}, // [237]
    {.value = -1, .offset = {1, 6}},  // [238]
    {.value = -1, .offset = {1, 2}},  // [239]
    {.value = 230, .offset = {0, 0}}, // [240]:0b111111111111111101000
    {.value = -1, .offset = {1, 2}},  // [241]
    {.value = 129, .offset = {0, 0}}, // [242]:0b1111111111111111010010
    {.value = 132, .offset = {0, 0}}, // [243]:0b1111111111111111010011
    {.value = -1, .offset = {1, 4}},  // [244]
    {.value = -1, .offset = {1, 2}},  // [245]
    {.value = 133, .offset = {0, 0}}, // [246]:0b1111111111111111010100
    {.value = 134, .offset = {0, 0}}, // [247]:0b1111111111111111010101
    {.value = -1, .offset = {1, 2}},  // [248]
    {.value = 136, .offset = {0, 0}}, // [249]:0b1111111111111111010110
    {.value = 146, .offset = {0, 0}}, // [250]:0b1111111111111111010111
    {.value = -1, .offset = {1, 8}},  // [251]
    {.value = -1, .offset = {1, 4}},  // [252]
    {.value = -1, .offset = {1, 2}},  // [253]
    {.value = 154, .offset = {0, 0}}, // [254]:0b1111111111111111011000
    {.value = 156, .offset = {0, 0}}, // [255]:0b1111111111111111011001
    {.value = -1, .offset = {1, 2}},  // [256]
    {.value = 160, .offset = {0, 0}}, // [257]:0b1111111111111111011010
    {.value = 163, .offset = {0, 0}}, // [258]:0b1111111111111111011011
    {.value = -1, .offset = {1, 4}},  // [259]
    {.value = -1, .offset = {1, 2}},  // [260]
    {.value = 164, .offset = {0, 0}}, // [261]:0b1111111111111111011100
    {.value = 169, .offset = {0, 0}}, // [262]:0b1111111111111111011101
    {.value = -1, .offset = {1, 2}},  // [263]
    {.value = 170, .offset = {0, 0}}, // [264]:0b1111111111111111011110
    {.value = 173, .offset = {0, 0}}, // [265]:0b1111111111111111011111
    {.value = -1, .offset = {1, 40}}, // [266]
    {.value = -1, .offset = {1, 16}}, // [267]
    {.value = -1, .offset = {1, 8}},  // [268]
    {.value = -1, .offset = {1, 4}},  // [269]
    {.value = -1, .offset = {1, 2}},  // [270]
    {.value = 178, .offset = {0, 0}}, // [271]:0b1111111111111111100000
    {.value = 181, .offset = {0, 0}}, // [272]:0b1111111111111111100001
    {.value = -1, .offset = {1, 2}},  // [273]
    {.value = 185, .offset = {0, 0}}, // [274]:0b1111111111111111100010
    {.value = 186, .offset = {0, 0}}, // [275]:0b1111111111111111100011
    {.value = -1, .offset = {1, 4}},  // [276]
    {.value = -1, .offset = {1, 2}},  // [277]
    {.value = 187, .offset = {0, 0}}, // [278]:0b1111111111111111100100
    {.value = 189, .offset = {0, 0}}, // [279]:0b1111111111111111100101
    {.value = -1, .offset = {1, 2}},  // [280]
    {.value = 190, .offset = {0, 0}}, // [281]:0b1111111111111111100110
    {.value = 196, .offset = {0, 0}}, // [282]:0b1111111111111111100111
    {.value = -1, .offset = {1, 8}},  // [283]
    {.value = -1, .offset = {1, 4}},  // [284]
    {.value = -1, .offset = {1, 2}},  // [285]
    {.value = 198, .offset = {0, 0}}, // [286]:0b1111111111111111101000
    {.value = 228, .offset = {0, 0}}, // [287]:0b1111111111111111101001
    {.value = -1, .offset = {1, 2}},  // [288]
    {.value = 232, .offset = {0, 0}}, // [289]:0b1111111111111111101010
    {.value = 233, .offset = {0, 0}}, // [290]:0b1111111111111111101011
    {.value = -1, .offset = {1, 8}},  // [291]
    {.value = -1, .offset = {1, 4}},  // [292]
    {.value = -1, .offset = {1, 2}},  // [293]
    {.value = 1, .offset = {0, 0}},   // [294]:0b11111111111111111011000
    {.value = 135, .offset = {0, 0}}, // [295]:0b11111111111111111011001
    {.value = -1, .offset = {1, 2}},  // [296]
    {.value = 137, .offset = {0, 0}}, // [297]:0b11111111111111111011010
    {.value = 138, .offset = {0, 0}}, // [298]:0b11111111111111111011011
    {.value = -1, .offset = {1, 4}},  // [299]
    {.value = -1, .offset = {1, 2}},  // [300]
    {.value = 139, .offset = {0, 0}}, // [301]:0b11111111111111111011100
    {.value = 140, .offset = {0, 0}}, // [302]:0b11111111111111111011101
    {.value = -1, .offset = {1, 2}},  // [303]
    {.value = 141, .offset = {0, 0}}, // [304]:0b11111111111111111011110
    {.value = 143, .offset = {0, 0}}, // [305]:0b11111111111111111011111
    {.value = -1, .offset = {1, 32}}, // [306]
    {.value = -1, .offset = {1, 16}}, // [307]
    {.value = -1, .offset = {1, 8}},  // [308]
    {.value = -1, .offset = {1, 4}},  // [309]
    {.value = -1, .offset = {1, 2}},  // [310]
    {.value = 147, .offset = {0, 0}}, // [311]:0b11111111111111111100000
    {.value = 149, .offset = {0, 0}}, // [312]:0b11111111111111111100001
    {.value = -1, .offset = {1, 2}},  // [313]
    {.value = 150, .offset = {0, 0}}, // [314]:0b11111111111111111100010
    {.value = 151, .offset = {0, 0}}, // [315]:0b11111111111111111100011
    {.value = -1, .offset = {1, 4}},  // [316]
    {.value = -1, .offset = {1, 2}},  // [317]
    {.value = 152, .offset = {0, 0}}, // [318]:0b11111111111111111100100
    {.value = 155, .offset = {0, 0}}, // [319]:0b11111111111111111100101
    {.value = -1, .offset = {1, 2}},  // [320]
    {.value = 157, .offset = {0, 0}}, // [321]:0b11111111111111111100110
    {.value = 158, .offset = {0, 0}}, // [322]:0b11111111111111111100111
    {.value = -1, .offset = {1, 8}},  // [323]
    {.value = -1, .offset = {1, 4}},  // [324]
    {.value = -1, .offset = {1, 2}},  // [325]
    {.value = 165, .offset = {0, 0}}, // [326]:0b11111111111111111101000
    {.value = 166, .offset = {0, 0}}, // [327]:0b11111111111111111101001
    {.value = -1, .offset = {1, 2}},  // [328]
    {.value = 168, .offset = {0, 0}}, // [329]:0b11111111111111111101010
    {.value = 174, .offset = {0, 0}}, // [330]:0b11111111111111111101011
    {.value = -1, .offset = {1, 4}},  // [331]
    {.value = -1, .offset = {1, 2}},  // [332]
    {.value = 175, .offset = {0, 0}}, // [333]:0b11111111111111111101100
    {.value = 180, .offset = {0, 0}}, // [334]:0b11111111111111111101101
    {.value = -1, .offset = {1, 2}},  // [335]
    {.value = 182, .offset = {0, 0}}, // [336]:0b11111111111111111101110
    {.value = 183, .offset = {0, 0}}, // [337]:0b11111111111111111101111
    {.value = -1, .offset = {1, 22}}, // [338]
    {.value = -1, .offset = {1, 8}},  // [339]
    {.value = -1, .offset = {1, 4}},  // [340]
    {.value = -1, .offset = {1, 2}},  // [341]
    {.value = 188, .offset = {0, 0}}, // [342]:0b11111111111111111110000
    {.value = 191, .offset = {0, 0}}, // [343]:0b11111111111111111110001
    {.value = -1, .offset = {1, 2}},  // [344]
    {.value = 197, .offset = {0, 0}}, // [345]:0b11111111111111111110010
    {.value = 231, .offset = {0, 0}}, // [346]:0b11111111111111111110011
    {.value = -1, .offset = {1, 6}},  // [347]
    {.value = -1, .offset = {1, 2}},  // [348]
    {.value = 239, .offset = {0, 0}}, // [349]:0b11111111111111111110100
    {.value = -1, .offset = {1, 2}},  // [350]
    {.value = 9, .offset = {0, 0}},   // [351]:0b111111111111111111101010
    {.value = 142, .offset = {0, 0}}, // [352]:0b111111111111111111101011
    {.value = -1, .offset = {1, 4}},  // [353]
    {.value = -1, .offset = {1, 2}},  // [354]
    {.value = 144, .offset = {0, 0}}, // [355]:0b111111111111111111101100
    {.value = 145, .offset = {0, 0}}, // [356]:0b111111111111111111101101
    {.value = -1, .offset = {1, 2}},  // [357]
    {.value = 148, .offset = {0, 0}}, // [358]:0b111111111111111111101110
    {.value = 159, .offset = {0, 0}}, // [359]:0b111111111111111111101111
    {.value = -1, .offset = {1, 20}}, // [360]
    {.value = -1, .offset = {1, 8}},  // [361]
    {.value = -1, .offset = {1, 4}},  // [362]
    {.value = -1, .offset = {1, 2}},  // [363]
    {.value = 171, .offset = {0, 0}}, // [364]:0b111111111111111111110000
    {.value = 206, .offset = {0, 0}}, // [365]:0b111111111111111111110001
    {.value = -1, .offset = {1, 2}},  // [366]
    {.value = 215, .offset = {0, 0}}, // [367]:0b111111111111111111110010
    {.value = 225, .offset = {0, 0}}, // [368]:0b111111111111111111110011
    {.value = -1, .offset = {1, 4}},  // [369]
    {.value = -1, .offset = {1, 2}},  // [370]
    {.value = 236, .offset = {0, 0}}, // [371]:0b111111111111111111110100
    {.value = 237, .offset = {0, 0}}, // [372]:0b111111111111111111110101
    {.value = -1, .offset = {1, 4}},  // [373]
    {.value = -1, .offset = {1, 2}},  // [374]
    {.value = 199, .offset = {0, 0}}, // [375]:0b1111111111111111111101100
    {.value = 207, .offset = {0, 0}}, // [376]:0b1111111111111111111101101
    {.value = -1, .offset = {1, 2}},  // [377]
    {.value = 234, .offset = {0, 0}}, // [378]:0b1111111111111111111101110
    {.value = 235, .offset = {0, 0}}, // [379]:0b1111111111111111111101111
    {.value = -1, .offset = {1, 34}}, // [380]
    {.value = -1, .offset = {1, 16}}, // [381]
    {.value = -1, .offset = {1, 8}},  // [382]
    {.value = -1, .offset = {1, 4}},  // [383]
    {.value = -1, .offset = {1, 2}},  // [384]
    {.value = 192, .offset = {0, 0}}, // [385]:0b11111111111111111111100000
    {.value = 193, .offset = {0, 0}}, // [386]:0b11111111111111111111100001
    {.value = -1, .offset = {1, 2}},  // [387]
    {.value = 200, .offset = {0, 0}}, // [388]:0b11111111111111111111100010
    {.value = 201, .offset = {0, 0}}, // [389]:0b11111111111111111111100011
    {.value = -1, .offset = {1, 4}},  // [390]
    {.value = -1, .offset = {1, 2}},  // [391]
    {.value = 202, .offset = {0, 0}}, // [392]:0b11111111111111111111100100
    {.value = 205, .offset = {0, 0}}, // [393]:0b11111111111111111111100101
    {.value = -1, .offset = {1, 2}},  // [394]
    {.value = 210, .offset = {0, 0}}, // [395]:0b11111111111111111111100110
    {.value = 213, .offset = {0, 0}}, // [396]:0b11111111111111111111100111
    {.value = -1, .offset = {1, 8}},  // [397]
    {.value = -1, .offset = {1, 4}},  // [398]
    {.value = -1, .offset = {1, 2}},  // [399]
    {.value = 218, .offset = {0, 0}}, // [400]:0b11111111111111111111101000
    {.value = 219, .offset = {0, 0}}, // [401]:0b11111111111111111111101001
    {.value = -1, .offset = {1, 2}},  // [402]
    {.value = 238, .offset = {0, 0}}, // [403]:0b11111111111111111111101010
    {.value = 240, .offset = {0, 0}}, // [404]:0b11111111111111111111101011
    {.value = -1, .offset = {1, 4}},  // [405]
    {.value = -1, .offset = {1, 2}},  // [406]
    {.value = 242, .offset = {0, 0}}, // [407]:0b11111111111111111111101100
    {.value = 243, .offset = {0, 0}}, // [408]:0b11111111111111111111101101
    {.value = -1, .offset = {1, 2}},  // [409]
    {.value = 255, .offset = {0, 0}}, // [410]:0b11111111111111111111101110
    {.value = -1, .offset = {1, 2}},  // [411]
    {.value = 203, .offset = {0, 0}}, // [412]:0b111111111111111111111011110
    {.value = 204, .offset = {0, 0}}, // [413]:0b111111111111111111111011111
    {.value = -1, .offset = {1, 32}}, // [414]
    {.value = -1, .offset = {1, 16}}, // [415]
    {.value = -1, .offset = {1, 8}},  // [416]
    {.value = -1, .offset = {1, 4}},  // [417]
    {.value = -1, .offset = {1, 2}},  // [418]
    {.value = 211, .offset = {0, 0}}, // [419]:0b111111111111111111111100000
    {.value = 212, .offset = {0, 0}}, // [420]:0b111111111111111111111100001
    {.value = -1, .offset = {1, 2}},  // [421]
    {.value = 214, .offset = {0, 0}}, // [422]:0b111111111111111111111100010
    {.value = 221, .offset = {0, 0}}, // [423]:0b111111111111111111111100011
    {.value = -1, .offset = {1, 4}},  // [424]
    {.value = -1, .offset = {1, 2}},  // [425]
    {.value = 222, .offset = {0, 0}}, // [426]:0b111111111111111111111100100
    {.value = 223, .offset = {0, 0}}, // [427]:0b111111111111111111111100101
    {.value = -1, .offset = {1, 2}},  // [428]
    {.value = 241, .offset = {0, 0}}, // [429]:0b111111111111111111111100110
    {.value = 244, .offset = {0, 0}}, // [430]:0b111111111111111111111100111
    {.value = -1, .offset = {1, 8}},  // [431]
    {.value = -1, .offset = {1, 4}},  // [432]
    {.value = -1, .offset = {1, 2}},  // [433]
    {.value = 245, .offset = {0, 0}}, // [434]:0b111111111111111111111101000
    {.value = 246, .offset = {0, 0}}, // [435]:0b111111111111111111111101001
    {.value = -1, .offset = {1, 2}},  // [436]
    {.value = 247, .offset = {0, 0}}, // [437]:0b111111111111111111111101010
    {.value = 248, .offset = {0, 0}}, // [438]:0b111111111111111111111101011
    {.value = -1, .offset = {1, 4}},  // [439]
    {.value = -1, .offset = {1, 2}},  // [440]
    {.value = 250, .offset = {0, 0}}, // [441]:0b111111111111111111111101100
    {.value = 251, .offset = {0, 0}}, // [442]:0b111111111111111111111101101
    {.value = -1, .offset = {1, 2}},  // [443]
    {.value = 252, .offset = {0, 0}}, // [444]:0b111111111111111111111101110
    {.value = 253, .offset = {0, 0}}, // [445]:0b111111111111111111111101111
    {.value = -1, .offset = {1, 30}}, // [446]
    {.value = -1, .offset = {1, 14}}, // [447]
    {.value = -1, .offset = {1, 6}},  // [448]
    {.value = -1, .offset = {1, 2}},  // [449]
    {.value = 254, .offset = {0, 0}}, // [450]:0b111111111111111111111110000
    {.value = -1, .offset = {1, 2}},  // [451]
    {.value = 2, .offset = {0, 0}},   // [452]:0b1111111111111111111111100010
    {.value = 3, .offset = {0, 0}},   // [453]:0b1111111111111111111111100011
    {.value = -1, .offset = {1, 4}},  // [454]
    {.value = -1, .offset = {1, 2}},  // [455]
    {.value = 4, .offset = {0, 0}},   // [456]:0b1111111111111111111111100100
    {.value = 5, .offset = {0, 0}},   // [457]:0b1111111111111111111111100101
    {.value = -1, .offset = {1, 2}},  // [458]
    {.value = 6, .offset = {0, 0}},   // [459]:0b1111111111111111111111100110
    {.value = 7, .offset = {0, 0}},   // [460]:0b1111111111111111111111100111
    {.value = -1, .offset = {1, 8}},  // [461]
    {.value = -1, .offset = {1, 4}},  // [462]
    {.value = -1, .offset = {1, 2}},  // [463]
    {.value = 8, .offset = {0, 0}},   // [464]:0b1111111111111111111111101000
    {.value = 11, .offset = {0, 0}},  // [465]:0b1111111111111111111111101001
    {.value = -1, .offset = {1, 2}},  // [466]
    {.value = 12, .offset = {0, 0}},  // [467]:0b1111111111111111111111101010
    {.value = 14, .offset = {0, 0}},  // [468]:0b1111111111111111111111101011
    {.value = -1, .offset = {1, 4}},  // [469]
    {.value = -1, .offset = {1, 2}},  // [470]
    {.value = 15, .offset = {0, 0}},  // [471]:0b1111111111111111111111101100
    {.value = 16, .offset = {0, 0}},  // [472]:0b1111111111111111111111101101
    {.value = -1, .offset = {1, 2}},  // [473]
    {.value = 17, .offset = {0, 0}},  // [474]:0b1111111111111111111111101110
    {.value = 18, .offset = {0, 0}},  // [475]:0b1111111111111111111111101111
    {.value = -1, .offset = {1, 16}}, // [476]
    {.value = -1, .offset = {1, 8}},  // [477]
    {.value = -1, .offset = {1, 4}},  // [478]
    {.value = -1, .offset = {1, 2}},  // [479]
    {.value = 19, .offset = {0, 0}},  // [480]:0b1111111111111111111111110000
    {.value = 20, .offset = {0, 0}},  // [481]:0b1111111111111111111111110001
    {.value = -1, .offset = {1, 2}},  // [482]
    {.value = 21, .offset = {0, 0}},  // [483]:0b1111111111111111111111110010
    {.value = 23, .offset = {0, 0}},  // [484]:0b1111111111111111111111110011
    {.value = -1, .offset = {1, 4}},  // [485]
    {.value = -1, .offset = {1, 2}},  // [486]
    {.value = 24, .offset = {0, 0}},  // [487]:0b1111111111111111111111110100
    {.value = 25, .offset = {0, 0}},  // [488]:0b1111111111111111111111110101
    {.value = -1, .offset = {1, 2}},  // [489]
    {.value = 26, .offset = {0, 0}},  // [490]:0b1111111111111111111111110110
    {.value = 27, .offset = {0, 0}},  // [491]:0b1111111111111111111111110111
    {.value = -1, .offset = {1, 8}},  // [492]
    {.value = -1, .offset = {1, 4}},  // [493]
    {.value = -1, .offset = {1, 2}},  // [494]
    {.value = 28, .offset = {0, 0}},  // [495]:0b1111111111111111111111111000
    {.value = 29, .offset = {0, 0}},  // [496]:0b1111111111111111111111111001
    {.value = -1, .offset = {1, 2}},  // [497]
    {.value = 30, .offset = {0, 0}},  // [498]:0b1111111111111111111111111010
    {.value = 31, .offset = {0, 0}},  // [499]:0b1111111111111111111111111011
    {.value = -1, .offset = {1, 4}},  // [500]
    {.value = -1, .offset = {1, 2}},  // [501]
    {.value = 127, .offset = {0, 0}}, // [502]:0b1111111111111111111111111100
    {.value = 220, .offset = {0, 0}}, // [503]:0b1111111111111111111111111101
    {.value = -1, .offset = {1, 2}},  // [504]
    {.value = 249, .offset = {0, 0}}, // [505]:0b1111111111111111111111111110
    {.value = -1, .offset = {1, 4}},  // [506]
    {.value = -1, .offset = {1, 2}},  // [507]
    {.value = 10, .offset = {0, 0}},  // [508]:0b111111111111111111111111111100
    {.value = 13, .offset = {0, 0}},  // [509]:0b111111111111111111111111111101
    {.value = -1, .offset = {1, 2}},  // [510]
    {.value = 22, .offset = {0, 0}},  // [511]:0b111111111111111111111111111110
    {.value = 256, .offset = {0, 0}}, // [512]:0b111111111111111111111111111111
};

/* *****************************************************************************





                              Don't overwrite this





***************************************************************************** */

#endif /* H_HPACK_H */
