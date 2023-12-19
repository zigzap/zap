/*
Copyright: Boaz Segev, 2017-2019
License: MIT
*/
#include <fiobj_json.h>
#define FIO_ARY_NAME fio_json_stack
#define FIO_ARY_TYPE FIOBJ
#include <fio.h>

#include <fio_json_parser.h>

#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* *****************************************************************************
JSON API
***************************************************************************** */

/**
 * Parses JSON, setting `pobj` to point to the new Object.
 *
 * Returns the number of bytes consumed. On Error, 0 is returned and no data is
 * consumed.
 */
size_t fiobj_json2obj(FIOBJ *pobj, const void *data, size_t len);
/* Formats an object into a JSON string. Remember to `fiobj_free`. */
FIOBJ fiobj_obj2json(FIOBJ, uint8_t);

/* *****************************************************************************
FIOBJ Parser
***************************************************************************** */

typedef struct {
  json_parser_s p;
  FIOBJ key;
  FIOBJ top;
  FIOBJ target;
  fio_json_stack_s stack;
  uint8_t is_hash;
} fiobj_json_parser_s;

/* *****************************************************************************
FIOBJ Callacks
***************************************************************************** */

static inline void fiobj_json_add2parser(fiobj_json_parser_s *p, FIOBJ o) {
  if (p->top) {
    if (p->is_hash) {
      if (p->key) {
        fiobj_hash_set(p->top, p->key, o);
        fiobj_free(p->key);
        p->key = FIOBJ_INVALID;
      } else {
        p->key = o;
      }
    } else {
      fiobj_ary_push(p->top, o);
    }
  } else {
    p->top = o;
  }
}

/** a NULL object was detected */
static void fio_json_on_null(json_parser_s *p) {
  fiobj_json_add2parser((fiobj_json_parser_s *)p, fiobj_null());
}
/** a TRUE object was detected */
static void fio_json_on_true(json_parser_s *p) {
  fiobj_json_add2parser((fiobj_json_parser_s *)p, fiobj_true());
}
/** a FALSE object was detected */
static void fio_json_on_false(json_parser_s *p) {
  fiobj_json_add2parser((fiobj_json_parser_s *)p, fiobj_false());
}
/** a Numberl was detected (long long). */
static void fio_json_on_number(json_parser_s *p, long long i) {
  fiobj_json_add2parser((fiobj_json_parser_s *)p, fiobj_num_new(i));
}
/** a Float was detected (double). */
static void fio_json_on_float(json_parser_s *p, double f) {
  fiobj_json_add2parser((fiobj_json_parser_s *)p, fiobj_float_new(f));
}
/** a String was detected (int / float). update `pos` to point at ending */
static void fio_json_on_string(json_parser_s *p, void *start, size_t length) {
  FIOBJ str = fiobj_str_buf(length);
  fiobj_str_resize(
      str, fio_json_unescape_str(fiobj_obj2cstr(str).data, start, length));
  fiobj_json_add2parser((fiobj_json_parser_s *)p, str);
}
/** a dictionary object was detected */
static int fio_json_on_start_object(json_parser_s *p) {
  fiobj_json_parser_s *pr = (fiobj_json_parser_s *)p;
  if (pr->target) {
    /* push NULL, don't free the objects */
    fio_json_stack_push(&pr->stack, pr->top);
    pr->top = pr->target;
    pr->target = FIOBJ_INVALID;
  } else {
    FIOBJ hash = fiobj_hash_new();
    fiobj_json_add2parser(pr, hash);
    fio_json_stack_push(&pr->stack, pr->top);
    pr->top = hash;
  }
  pr->is_hash = 1;
  return 0;
}
/** a dictionary object closure detected */
static void fio_json_on_end_object(json_parser_s *p) {
  fiobj_json_parser_s *pr = (fiobj_json_parser_s *)p;
  if (pr->key) {
    FIO_LOG_WARNING("(JSON parsing) malformed JSON, "
                    "ignoring dangling Hash key.");
    fiobj_free(pr->key);
    pr->key = FIOBJ_INVALID;
  }
  pr->top = FIOBJ_INVALID;
  fio_json_stack_pop(&pr->stack, &pr->top);
  pr->is_hash = FIOBJ_TYPE_IS(pr->top, FIOBJ_T_HASH);
}
/** an array object was detected */
static int fio_json_on_start_array(json_parser_s *p) {
  fiobj_json_parser_s *pr = (fiobj_json_parser_s *)p;
  if (pr->target)
    return -1;
  FIOBJ ary = fiobj_ary_new();
  fiobj_json_add2parser(pr, ary);
  fio_json_stack_push(&pr->stack, pr->top);
  pr->top = ary;
  pr->is_hash = 0;
  return 0;
}
/** an array closure was detected */
static void fio_json_on_end_array(json_parser_s *p) {
  fiobj_json_parser_s *pr = (fiobj_json_parser_s *)p;
  pr->top = FIOBJ_INVALID;
  fio_json_stack_pop(&pr->stack, &pr->top);
  pr->is_hash = FIOBJ_TYPE_IS(pr->top, FIOBJ_T_HASH);
}
/** the JSON parsing is complete */
static void fio_json_on_json(json_parser_s *p) {
  // fiobj_json_parser_s *pr = (fiobj_json_parser_s *)p;
  // FIO_ARY_FOR(&pr->stack, pos) { fiobj_free((FIOBJ)pos.obj); }
  // fio_json_stack_free(&pr->stack);
  (void)p; /* nothing special... right? */
}
/** the JSON parsing is complete */
static void fio_json_on_error(json_parser_s *p) {
  fiobj_json_parser_s *pr = (fiobj_json_parser_s *)p;
#if DEBUG
  FIO_LOG_DEBUG("JSON on error called.");
#endif
  fiobj_free((FIOBJ)fio_json_stack_get(&pr->stack, 0));
  fiobj_free(pr->key);
  fio_json_stack_free(&pr->stack);
  *pr = (fiobj_json_parser_s){.top = FIOBJ_INVALID};
}

/* *****************************************************************************
JSON formatting
***************************************************************************** */

/** Writes a JSON friendly version of the src String */
static void write_safe_str(FIOBJ dest, const FIOBJ str) {
  fio_str_info_s s = fiobj_obj2cstr(str);
  fio_str_info_s t = fiobj_obj2cstr(dest);
  t.data[t.len] = '"';
  t.len++;
  fiobj_str_resize(dest, t.len);
  t = fiobj_obj2cstr(dest);
  const uint8_t *restrict src = (const uint8_t *)s.data;
  size_t len = s.len;
  uint64_t end = t.len;
  /* make sure we have some room */
  size_t added = 0;
  size_t capa = fiobj_str_capa(dest);
  if (capa <= end + s.len + 64) {
    if (0) {
      capa = (((capa >> 12) + 1) << 12) - 1;
      capa = fiobj_str_capa_assert(dest, capa);
    } else {
      capa = fiobj_str_capa_assert(dest, (end + s.len + 64));
    }
    fio_str_info_s tmp = fiobj_obj2cstr(dest);
    t = tmp;
  }
  while (len) {
    char *restrict writer = (char *)t.data;
    while (len && src[0] > 32 && src[0] != '"' && src[0] != '\\') {
      len--;
      writer[end++] = *(src++);
    }
    if (!len)
      break;
    switch (src[0]) {
    case '\b':
      writer[end++] = '\\';
      writer[end++] = 'b';
      added++;
      break; /* from switch */
    case '\f':
      writer[end++] = '\\';
      writer[end++] = 'f';
      added++;
      break; /* from switch */
    case '\n':
      writer[end++] = '\\';
      writer[end++] = 'n';
      added++;
      break; /* from switch */
    case '\r':
      writer[end++] = '\\';
      writer[end++] = 'r';
      added++;
      break; /* from switch */
    case '\t':
      writer[end++] = '\\';
      writer[end++] = 't';
      added++;
      break; /* from switch */
    case '"':
    case '\\':
    case '/':
      writer[end++] = '\\';
      writer[end++] = src[0];
      added++;
      break; /* from switch */
    default:
      if (src[0] <= 31) {
        /* MUST escape all control values less than 32 */
        writer[end++] = '\\';
        writer[end++] = 'u';
        writer[end++] = '0';
        writer[end++] = '0';
        writer[end++] = hex_chars[src[0] >> 4];
        writer[end++] = hex_chars[src[0] & 15];
        added += 4;
      } else
        writer[end++] = src[0];
      break; /* from switch */
    }
    src++;
    len--;
    if (added >= 48 && capa <= end + len + 64) {
      writer[end] = 0;
      fiobj_str_resize(dest, end);
      fiobj_str_capa_assert(dest, (end + len + 64));
      t = fiobj_obj2cstr(dest);
      writer = (char *)t.data;
      capa = t.capa;
      added = 0;
    }
  }
  t.data[end++] = '"';
  fiobj_str_resize(dest, end);
}

typedef struct {
  FIOBJ dest;
  FIOBJ parent;
  fio_json_stack_s *stack;
  uintptr_t count;
  uint8_t pretty;
} obj2json_data_s;

static int fiobj_obj2json_task(FIOBJ o, void *data_) {
  obj2json_data_s *data = data_;
  uint8_t add_seperator = 1;
  if (fiobj_hash_key_in_loop()) {
    write_safe_str(data->dest, fiobj_hash_key_in_loop());
    fiobj_str_write(data->dest, ":", 1);
  }
  switch (FIOBJ_TYPE(o)) {
  case FIOBJ_T_NUMBER:
  case FIOBJ_T_NULL:
  case FIOBJ_T_TRUE:
  case FIOBJ_T_FALSE:
  case FIOBJ_T_FLOAT:
    fiobj_str_join(data->dest, o);
    --data->count;
    break;

  case FIOBJ_T_DATA:
  case FIOBJ_T_UNKNOWN:
  case FIOBJ_T_STRING:
    write_safe_str(data->dest, o);
    --data->count;
    break;

  case FIOBJ_T_ARRAY:
    --data->count;
    fio_json_stack_push(data->stack, data->parent);
    fio_json_stack_push(data->stack, (FIOBJ)data->count);
    data->parent = o;
    data->count = fiobj_ary_count(o);
    fiobj_str_write(data->dest, "[", 1);
    add_seperator = 0;
    break;

  case FIOBJ_T_HASH:
    --data->count;
    fio_json_stack_push(data->stack, data->parent);
    fio_json_stack_push(data->stack, (FIOBJ)data->count);
    data->parent = o;
    data->count = fiobj_hash_count(o);
    fiobj_str_write(data->dest, "{", 1);
    add_seperator = 0;
    break;
  }
  if (data->pretty) {
    fiobj_str_capa_assert(data->dest,
                          fiobj_obj2cstr(data->dest).len +
                              (fio_json_stack_count(data->stack) * 5));
    while (!data->count && data->parent) {
      if (FIOBJ_TYPE_IS(data->parent, FIOBJ_T_HASH)) {
        fiobj_str_write(data->dest, "}", 1);
      } else {
        fiobj_str_write(data->dest, "]", 1);
      }
      add_seperator = 1;
      data->count = 0;
      fio_json_stack_pop(data->stack, &data->count);
      data->parent = FIOBJ_INVALID;
      fio_json_stack_pop(data->stack, &data->parent);
    }

    if (add_seperator && data->parent) {
      fiobj_str_write(data->dest, ",\n", 2);
      uintptr_t indent = fio_json_stack_count(data->stack) - 1;
      fiobj_str_capa_assert(data->dest,
                            fiobj_obj2cstr(data->dest).len + (indent * 2));
      fio_str_info_s buf = fiobj_obj2cstr(data->dest);
      while (indent--) {
        buf.data[buf.len++] = ' ';
        buf.data[buf.len++] = ' ';
      }
      fiobj_str_resize(data->dest, buf.len);
    }
  } else {
    fiobj_str_capa_assert(data->dest,
                          fiobj_obj2cstr(data->dest).len +
                              (fio_json_stack_count(data->stack) << 1));
    while (!data->count && data->parent) {
      if (FIOBJ_TYPE_IS(data->parent, FIOBJ_T_HASH)) {
        fiobj_str_write(data->dest, "}", 1);
      } else {
        fiobj_str_write(data->dest, "]", 1);
      }
      add_seperator = 1;
      data->count = 0;
      data->parent = FIOBJ_INVALID;
      fio_json_stack_pop(data->stack, &data->count);
      fio_json_stack_pop(data->stack, &data->parent);
    }

    if (add_seperator && data->parent) {
      fiobj_str_write(data->dest, ",", 1);
    }
  }

  return 0;
}

/* *****************************************************************************
FIOBJ API
***************************************************************************** */

/**
 * Parses JSON, setting `pobj` to point to the new Object.
 *
 * Returns the number of bytes consumed. On Error, 0 is returned and no data is
 * consumed.
 */
size_t fiobj_json2obj(FIOBJ *pobj, const void *data, size_t len) {
  fiobj_json_parser_s p = {.top = FIOBJ_INVALID};
  size_t consumed = fio_json_parse(&p.p, data, len);
  if (!consumed || p.p.depth) {
    fiobj_free(fio_json_stack_get(&p.stack, 0));
    p.top = FIOBJ_INVALID;
  }
  fio_json_stack_free(&p.stack);
  fiobj_free(p.key);
  *pobj = p.top;
  return consumed;
}

/**
 * Updates a Hash using JSON data.
 *
 * Parsing errors and non-dictionar object JSON data are silently ignored,
 * attempting to update the Hash as much as possible before any errors
 * encountered.
 *
 * Conflicting Hash data is overwritten (prefering the new over the old).
 *
 * Returns the number of bytes consumed. On Error, 0 is returned and no data is
 * consumed.
 */
size_t fiobj_hash_update_json(FIOBJ hash, const void *data, size_t len) {
  if (!hash)
    return 0;
  fiobj_json_parser_s p = {.top = FIOBJ_INVALID, .target = hash};
  size_t consumed = fio_json_parse(&p.p, data, len);
  fio_json_stack_free(&p.stack);
  fiobj_free(p.key);
  if (p.top != hash)
    fiobj_free(p.top);
  return consumed;
}

/**
 * Formats an object into a JSON string, appending the JSON string to an
 * existing String. Remember to `fiobj_free`.
 */
FIOBJ fiobj_obj2json2(FIOBJ dest, FIOBJ o, uint8_t pretty) {
  assert(dest && FIOBJ_TYPE_IS(dest, FIOBJ_T_STRING));
  if (!o) {
    fiobj_str_write(dest, "null", 4);
    return 0;
  }
  fio_json_stack_s stack = FIO_ARY_INIT;
  obj2json_data_s data = {
      .dest = dest,
      .stack = &stack,
      .pretty = pretty,
      .count = 1,
  };
  if (!o || !FIOBJ_IS_ALLOCATED(o) || !FIOBJECT2VTBL(o)->each) {
    fiobj_obj2json_task(o, &data);
    return dest;
  }
  fiobj_each2(o, fiobj_obj2json_task, &data);
  fio_json_stack_free(&stack);
  return dest;
}

/* Formats an object into a JSON string. Remember to `fiobj_free`. */
FIOBJ fiobj_obj2json(FIOBJ obj, uint8_t pretty) {
  return fiobj_obj2json2(fiobj_str_buf(128), obj, pretty);
}

/* *****************************************************************************
Test
***************************************************************************** */

#if DEBUG
void fiobj_test_json(void) {
  fprintf(stderr, "=== Testing JSON parser (simple test)\n");
#define TEST_ASSERT(cond, ...)                                                 \
  if (!(cond)) {                                                               \
    fprintf(stderr, "* " __VA_ARGS__);                                         \
    fprintf(stderr, "\n !!! Testing failed !!!\n");                            \
    exit(-1);                                                                  \
  }
  char json_str[] = "{\"array\":[1,2,3,\"boom\"],\"my\":{\"secret\":42},"
                    "\"true\":true,\"false\":false,\"null\":null,\"float\":-2."
                    "2,\"string\":\"I \\\"wrote\\\" this.\"}";
  char json_str_update[] = "{\"array\":[1,2,3]}";
  char json_str2[] =
      "[\n    \"JSON Test Pattern pass1\",\n    {\"object with 1 "
      "member\":[\"array with 1 element\"]},\n    {},\n    [],\n    -42,\n    "
      "true,\n    false,\n    null,\n    {\n        \"integer\": 1234567890,\n "
      "       \"real\": -9876.543210,\n        \"e\": 0.123456789e-12,\n       "
      " \"E\": 1.234567890E+34,\n        \"\":  23456789012E66,\n        "
      "\"zero\": 0,\n        \"one\": 1,\n        \"space\": \" \",\n        "
      "\"quote\": \"\\\"\",\n        \"backslash\": \"\\\\\",\n        "
      "\"controls\": \"\\b\\f\\n\\r\\t\",\n        \"slash\": \"/ & \\/\",\n   "
      "     \"alpha\": \"abcdefghijklmnopqrstuvwyz\",\n        \"ALPHA\": "
      "\"ABCDEFGHIJKLMNOPQRSTUVWYZ\",\n        \"digit\": \"0123456789\",\n    "
      "    \"0123456789\": \"digit\",\n        \"special\": "
      "\"`1~!@#$%^&*()_+-={':[,]}|;.</>?\",\n        \"hex\": "
      "\"\\u0123\\u4567\\u89AB\\uCDEF\\uabcd\\uef4A\",\n        \"true\": "
      "true,\n        \"false\": false,\n        \"null\": null,\n        "
      "\"array\":[  ],\n        \"object\":{  },\n        \"address\": \"50 "
      "St. James Street\",\n        \"url\": \"http://www.JSON.org/\",\n       "
      " \"comment\": \"// /* <!-- --\",\n        \"# -- --> */\": \" \",\n     "
      "   \" s p a c e d \" :[1,2 , 3\n\n,\n\n4 , 5        ,          6        "
      "   ,7        ],\"compact\":[1,2,3,4,5,6,7],\n        \"jsontext\": "
      "\"{\\\"object with 1 member\\\":[\\\"array with 1 element\\\"]}\",\n    "
      "    \"quotes\": \"&#34; \\u0022 %22 0x22 034 &#x22;\",\n        "
      "\"\\/"
      "\\\\\\\"\\uCAFE\\uBABE\\uAB98\\uFCDE\\ubcda\\uef4A\\b\\f\\n\\r\\t`1~!@#$"
      "%^&*()_+-=[]{}|;:',./<>?\"\n: \"A key can be any string\"\n    },\n    "
      "0.5 "
      ",98.6\n,\n99.44\n,\n\n1066,\n1e1,\n0.1e1,\n1e-1,\n1e00,2e+00,2e-00\n,"
      "\"rosebud\"]";

  FIOBJ o = 0;
  TEST_ASSERT(fiobj_json2obj(&o, "1", 2) == 1,
              "JSON number parsing failed to run!\n");
  TEST_ASSERT(o, "JSON (single) object missing!\n");
  TEST_ASSERT(FIOBJ_TYPE_IS(o, FIOBJ_T_NUMBER),
              "JSON (single) not a number!\n");
  TEST_ASSERT(fiobj_obj2num(o) == 1, "JSON (single) not == 1!\n");
  fiobj_free(o);

  TEST_ASSERT(fiobj_json2obj(&o, "2.0", 5) == 3,
              "JSON float parsing failed to run!\n");
  TEST_ASSERT(o, "JSON (float) object missing!\n");
  TEST_ASSERT(FIOBJ_TYPE_IS(o, FIOBJ_T_FLOAT), "JSON (float) not a float!\n");
  TEST_ASSERT(fiobj_obj2float(o) == 2, "JSON (float) not == 2!\n");
  fiobj_free(o);

  TEST_ASSERT(fiobj_json2obj(&o, json_str, sizeof(json_str)) ==
                  (sizeof(json_str) - 1),
              "JSON parsing failed to run!\n");
  TEST_ASSERT(o, "JSON object missing!\n");
  TEST_ASSERT(FIOBJ_TYPE_IS(o, FIOBJ_T_HASH),
              "JSON root not a dictionary (not a hash)!\n");
  FIOBJ tmp = fiobj_hash_get2(o, fiobj_hash_string("array", 5));
  TEST_ASSERT(FIOBJ_TYPE_IS(tmp, FIOBJ_T_ARRAY),
              "JSON 'array' not an Array!\n");
  TEST_ASSERT(fiobj_obj2num(fiobj_ary_index(tmp, 0)) == 1,
              "JSON 'array' index 0 error!\n");
  TEST_ASSERT(fiobj_obj2num(fiobj_ary_index(tmp, 1)) == 2,
              "JSON 'array' index 1 error!\n");
  TEST_ASSERT(fiobj_obj2num(fiobj_ary_index(tmp, 2)) == 3,
              "JSON 'array' index 2 error!\n");
  TEST_ASSERT(FIOBJ_TYPE_IS(fiobj_ary_index(tmp, 3), FIOBJ_T_STRING),
              "JSON 'array' index 3 type error!\n");
  TEST_ASSERT(!memcmp("boom", fiobj_obj2cstr(fiobj_ary_index(tmp, 3)).data, 4),
              "JSON 'array' index 3 error!\n");
  tmp = fiobj_hash_get2(o, fiobj_hash_string("my", 2));
  TEST_ASSERT(FIOBJ_TYPE_IS(tmp, FIOBJ_T_HASH),
              "JSON 'my:secret' not a Hash!\n");
  TEST_ASSERT(
      FIOBJ_TYPE_IS(fiobj_hash_get2(tmp, fiobj_hash_string("secret", 6)),
                    FIOBJ_T_NUMBER),
      "JSON 'my:secret' doesn't hold a number!\n");
  TEST_ASSERT(
      fiobj_obj2num(fiobj_hash_get2(tmp, fiobj_hash_string("secret", 6))) == 42,
      "JSON 'my:secret' not 42!\n");
  TEST_ASSERT(fiobj_hash_get2(o, fiobj_hash_string("true", 4)) == fiobj_true(),
              "JSON 'true' not true!\n");
  TEST_ASSERT(fiobj_hash_get2(o, fiobj_hash_string("false", 5)) ==
                  fiobj_false(),
              "JSON 'false' not false!\n");
  TEST_ASSERT(fiobj_hash_get2(o, fiobj_hash_string("null", 4)) == fiobj_null(),
              "JSON 'null' not null!\n");
  tmp = fiobj_hash_get2(o, fiobj_hash_string("float", 5));
  TEST_ASSERT(FIOBJ_TYPE_IS(tmp, FIOBJ_T_FLOAT), "JSON 'float' not a float!\n");
  tmp = fiobj_hash_get2(o, fiobj_hash_string("string", 6));
  TEST_ASSERT(FIOBJ_TYPE_IS(tmp, FIOBJ_T_STRING),
              "JSON 'string' not a string!\n");
  TEST_ASSERT(!strcmp(fiobj_obj2cstr(tmp).data, "I \"wrote\" this."),
              "JSON 'string' incorrect!\n");
  fprintf(stderr, "* passed.\n");
  fprintf(stderr, "=== Testing JSON formatting (simple test)\n");
  tmp = fiobj_obj2json(o, 0);
  fprintf(stderr, "* data (%p):\n%.*s\n", (void *)fiobj_obj2cstr(tmp).data,
          (int)fiobj_obj2cstr(tmp).len, fiobj_obj2cstr(tmp).data);
  if (!strcmp(fiobj_obj2cstr(tmp).data, json_str))
    fprintf(stderr, "* Stringify == Original.\n");
  TEST_ASSERT(
      fiobj_hash_update_json(o, json_str_update, strlen(json_str_update)),
      "JSON update failed to parse data.");
  fiobj_free(tmp);

  tmp = fiobj_hash_get2(o, fiobj_hash_string("array", 5));
  TEST_ASSERT(FIOBJ_TYPE_IS(tmp, FIOBJ_T_ARRAY),
              "JSON updated 'array' not an Array!\n");
  TEST_ASSERT(fiobj_ary_count(tmp) == 3, "JSON updated 'array' not updated?");
  tmp = fiobj_hash_get2(o, fiobj_hash_string("float", 5));
  TEST_ASSERT(FIOBJ_TYPE_IS(tmp, FIOBJ_T_FLOAT),
              "JSON updated (old) 'float' missing!\n");
  fiobj_free(o);
  fprintf(stderr, "* passed.\n");

  fprintf(stderr, "=== Testing JSON parsing (UTF-8 and special cases)\n");
  fiobj_json2obj(&o, "[\"\\uD834\\uDD1E\"]", 16);
  TEST_ASSERT(o, "JSON G clef String failed to parse!\n");
  TEST_ASSERT(FIOBJ_TYPE_IS(o, FIOBJ_T_ARRAY),
              "JSON G clef container has an incorrect type! (%s)\n",
              fiobj_type_name(o));
  tmp = o;
  o = fiobj_ary_pop(o);
  fiobj_free(tmp);
  TEST_ASSERT(FIOBJ_TYPE_IS(o, FIOBJ_T_STRING),
              "JSON G clef String incorrect type! %p => %s\n", (void *)o,
              fiobj_type_name(o));
  TEST_ASSERT((!strcmp(fiobj_obj2cstr(o).data, "\xF0\x9D\x84\x9E")),
              "JSON G clef String incorrect %s !\n", fiobj_obj2cstr(o).data);
  fiobj_free(o);

  fiobj_json2obj(&o, "\"\\uD834\\uDD1E\"", 14);
  TEST_ASSERT(FIOBJ_TYPE_IS(o, FIOBJ_T_STRING),
              "JSON direct G clef String incorrect type! %p => %s\n", (void *)o,
              fiobj_type_name(o));
  TEST_ASSERT((!strcmp(fiobj_obj2cstr(o).data, "\xF0\x9D\x84\x9E")),
              "JSON direct G clef String incorrect %s !\n",
              fiobj_obj2cstr(o).data);
  fiobj_free(o);
  fiobj_json2obj(&o, "\"Hello\\u0000World\"", 19);
  TEST_ASSERT(FIOBJ_TYPE_IS(o, FIOBJ_T_STRING),
              "JSON NUL containing String incorrect type! %p => %s\n",
              (void *)o, fiobj_type_name(o));
  TEST_ASSERT(
      (!memcmp(fiobj_obj2cstr(o).data, "Hello\0World", fiobj_obj2cstr(o).len)),
      "JSON NUL containing String incorrect! (%u): %s . %s\n",
      (int)fiobj_obj2cstr(o).len, fiobj_obj2cstr(o).data,
      fiobj_obj2cstr(o).data + 3);
  fiobj_free(o);
  size_t consumed = fiobj_json2obj(&o, json_str2, sizeof(json_str2));
  TEST_ASSERT(
      consumed == (sizeof(json_str2) - 1),
      "JSON messy string failed to parse (consumed %lu instead of %lu\n",
      (unsigned long)consumed, (unsigned long)(sizeof(json_str2) - 1));
  TEST_ASSERT(FIOBJ_TYPE_IS(o, FIOBJ_T_ARRAY),
              "JSON messy string object error\n");
  tmp = fiobj_obj2json(o, 1);
  TEST_ASSERT(FIOBJ_TYPE_IS(tmp, FIOBJ_T_STRING),
              "JSON messy string isn't a string\n");
  fprintf(stderr, "Messy JSON:\n%s\n", fiobj_obj2cstr(tmp).data);
  fiobj_free(o);
  fiobj_free(tmp);
  fprintf(stderr, "* passed.\n");
}

#endif
