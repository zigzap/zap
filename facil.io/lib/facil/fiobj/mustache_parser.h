/*
Copyright: Boaz Segev, 2018-2019
License: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#ifndef H_MUSTACHE_LOADR_H
/**
 * A mustache parser using a callback systems that allows this implementation to
 * be framework agnostic (i.e., can be used with any JSON library).
 */
#define H_MUSTACHE_LOADR_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#if !defined(__GNUC__) && !defined(__clang__) && !defined(FIO_GNUC_BYPASS)
#define __attribute__(...)
#define __has_include(...) 0
#define __has_builtin(...) 0
#define FIO_GNUC_BYPASS 1
#elif !defined(__clang__) && !defined(__has_builtin)
#define __has_builtin(...) 0
#define FIO_GNUC_BYPASS 1
#endif

#ifndef MUSTACHE_FUNC
#define MUSTACHE_FUNC static __attribute__((unused))
#endif

/* *****************************************************************************
Compile Time Behavior Flags
***************************************************************************** */

#ifndef MUSTACHE_USE_DYNAMIC_PADDING
#define MUSTACHE_USE_DYNAMIC_PADDING 1
#endif

#ifndef MUSTACHE_FAIL_ON_MISSING_TEMPLATE
#define MUSTACHE_FAIL_ON_MISSING_TEMPLATE 1
#endif

#if !defined(MUSTACHE_NESTING_LIMIT) || !MUSTACHE_NESTING_LIMIT
#undef MUSTACHE_NESTING_LIMIT
#define MUSTACHE_NESTING_LIMIT 82
#endif

/* *****************************************************************************
Mustache API Argument types
***************************************************************************** */

/** an opaque type for mustache template data (when caching). */
typedef struct mustache_s mustache_s;

/** Error reporting type. */
typedef enum mustache_error_en {
  MUSTACHE_OK,
  MUSTACHE_ERR_TOO_DEEP,
  MUSTACHE_ERR_CLOSURE_MISMATCH,
  MUSTACHE_ERR_FILE_NOT_FOUND,
  MUSTACHE_ERR_FILE_TOO_BIG,
  MUSTACHE_ERR_FILE_NAME_TOO_LONG,
  MUSTACHE_ERR_FILE_NAME_TOO_SHORT,
  MUSTACHE_ERR_EMPTY_TEMPLATE,
  MUSTACHE_ERR_DELIMITER_TOO_LONG,
  MUSTACHE_ERR_NAME_TOO_LONG,
  MUSTACHE_ERR_UNKNOWN,
  MUSTACHE_ERR_USER_ERROR,
} mustache_error_en;

/** Arguments for the `mustache_load` function, used by the mustache parser. */
typedef struct {
  /** The root template's file name. */
  char const *filename;
  /** The file name's length. */
  size_t filename_len;
  /** If data and data_len are set, they will be used as the file's contents. */
  char const *data;
  /** If data and data_len are set, they will be used as the file's contents. */
  size_t data_len;
  /** Parsing error reporting (can be NULL). */
  mustache_error_en *err;
} mustache_load_args_s;

/* *****************************************************************************
REQUIRED: Define INCLUDE_MUSTACHE_IMPLEMENTATION only in the implementation file
***************************************************************************** */

/**
 * In non-implementation files, don't define the INCLUDE_MUSTACHE_IMPLEMENTATION
 * macro.
 *
 * Before including the header within an implementation faile, define
 * INCLUDE_MUSTACHE_IMPLEMENTATION as 1.
 */
#if INCLUDE_MUSTACHE_IMPLEMENTATION

/* *****************************************************************************
Mustache API Functions and Arguments
***************************************************************************** */

MUSTACHE_FUNC mustache_s *mustache_load(mustache_load_args_s args);

#define mustache_load(...) mustache_load((mustache_load_args_s){__VA_ARGS__})

/** free the mustache template */
inline MUSTACHE_FUNC void mustache_free(mustache_s *mustache) {
  free(mustache);
}

/** Arguments for the `mustache_build` function. */
typedef struct {
  /** The parsed template (an instruction collection). */
  mustache_s *mustache;
  /** Opaque user data (recommended for input review) - children will inherit
   * the parent's udata value. Updated values will propegate to child sections
   * but won't effect parent sections.
   */
  void *udata1;
  /** Opaque user data (recommended for output handling)- children will inherit
   * the parent's udata value. Updated values will propegate to child sections
   * but won't effect parent sections.
   */
  void *udata2;
  /** Formatting error reporting (can be NULL). */
  mustache_error_en *err;
} mustache_build_args_s;
MUSTACHE_FUNC int mustache_build(mustache_build_args_s args);

#define mustache_build(mustache_s_ptr, ...)                                    \
  mustache_build(                                                              \
      (mustache_build_args_s){.mustache = (mustache_s_ptr), __VA_ARGS__})

/* *****************************************************************************
Callbacks Types - types used by the template builder callbacks
***************************************************************************** */

/**
 * A mustache section allows the callbacks to "walk" backwards towards the root
 * in search of argument data.
 *
 * Note that every section is allowed a separate udata value.
 */
typedef struct mustache_section_s {
  /** Opaque user data (recommended for input review) - children will inherit
   * the parent's udata value. Updated values will propegate to child sections
   * but won't effect parent sections.
   */
  void *udata1;
  /** Opaque user data (recommended for output handling)- children will inherit
   * the parent's udata value. Updated values will propegate to child sections
   * but won't effect parent sections.
   */
  void *udata2;
} mustache_section_s;

/* *****************************************************************************
Callbacks Helpers - These functions can be called from within callbacks
***************************************************************************** */

/**
 * Returns the section's parent for nested sections, or NULL (for root section).
 *
 * This allows the `udata` values in the parent to be accessed and can be used,
 * for example, when seeking a value (argument / keyword) within a nested data
 * structure such as a Hash.
 */
static inline mustache_section_s *
mustache_section_parent(mustache_section_s *section);

/**
 * This helper function should be used to write text to the output
 * stream form within `mustache_on_arg` or `mustache_on_section_test`.
 *
 * This function will call the `mustache_on_text` callback for each slice of
 * text that requires padding and for escaped data.
 *
 * `mustache_on_text` must NEVER call this function.
 */
static inline int mustache_write_text(mustache_section_s *section, char *text,
                                      uint32_t len, uint8_t escape);

/**
 * Returns the section's unparsed content as a non-NUL terminated byte array.
 *
 * The length of the data will be placed in the `size_t` variable pointed to by
 * `p_len`. Do NOT use `strlen`, since the data isn't NUL terminated.
 *
 * This allows text to be accessed when a section's content is, in fact, meant
 * to be passed along to a function / lambda.
 */
static inline const char *mustache_section_text(mustache_section_s *section,
                                                size_t *p_len);

/* *****************************************************************************
Client Callbacks - MUST be implemented by the including file
***************************************************************************** */

/**
 * Called when an argument name was detected in the current section.
 *
 * A conforming implementation will search for the named argument both in the
 * existing section and all of it's parents (walking backwards towards the root)
 * until a value is detected.
 *
 * A missing value should be treated the same as an empty string.
 *
 * A conforming implementation will output the named argument's value (either
 * HTML escaped or not, depending on the `escape` flag) as a string.
 *
 * A conforming implementation will test for dot notation in the name.
 *
 * NOTE: the `name` data is **not** NUL terminated. Use the `name_len` data to
 * determine the actual string length.
 */
static int mustache_on_arg(mustache_section_s *section, const char *name,
                           uint32_t name_len, unsigned char escape);

/**
 * Called when simple template text (string) is detected.
 *
 * A conforming implementation will output data as a string (no escaping).
 *
 * NOTE: the `data` is **not** NUL terminated. Use the `data_len` data to
 * determine the actual data length.
 */
static int mustache_on_text(mustache_section_s *section, const char *data,
                            uint32_t data_len);

/**
 * Called for nested sections, must return the number of objects in the new
 * subsection (depending on the argument's name).
 *
 * Arrays should return the number of objects in the array.
 *
 * `true` values should return 1.
 *
 * `false` values should return 0.
 *
 * A return value of -1 will stop processing with an error.
 *
 * Please note, this will handle both normal and inverted sections.
 *
 * The `callable` value is true if the section is allowed to be a function /
 * callback. If the section object represent a function / callback (lambda), the
 * lambda should be called and the `mustache_on_section_test` callback should
 * return 0.
 *
 * NOTE: the `name` data is **not** NUL terminated. Use the `name_len` data to
 * determine the actual string length.
 *
 * A conforming implementation will test for dot notation in the name.
 */
static int32_t mustache_on_section_test(mustache_section_s *section,
                                        const char *name, uint32_t name_len,
                                        uint8_t callable);

/**
 * Called when entering a nested section.
 *
 * `index` is a zero based index indicating the number of repetitions that
 * occurred so far (same as the array index for arrays).
 *
 * A return value of -1 will stop processing with an error.
 *
 * Note: this is a good time to update the subsection's `udata` with the value
 * of the array index. The `udata` will always contain the value or the parent's
 * `udata`.
 *
 * NOTE: the `name` data is **not** NUL terminated. Use the `name_len` data to
 * determine the actual string length.
 *
 * A conforming implementation will test for dot notation in the name.
 */
static int mustache_on_section_start(mustache_section_s *section,
                                     char const *name, uint32_t name_len,
                                     uint32_t index);

/**
 * Called for cleanup in case of error.
 */
static void mustache_on_formatting_error(void *udata1, void *udata2);

/* *****************************************************************************

IMPLEMENTATION (beware: monolithic functions ahead)

***************************************************************************** */

/* *****************************************************************************
Internal types
***************************************************************************** */

struct mustache_s {
  /* The number of instructions in the engine */
  union {
    void *read_only_pt; /* ensure pointer wide padding */
    struct {
      uint32_t intruction_count;
      uint32_t data_length;
    } read_only;
  } u;
};

typedef struct mustache__instruction_s {
  enum {
    MUSTACHE_WRITE_TEXT,
    MUSTACHE_WRITE_ARG,
    MUSTACHE_WRITE_ARG_UNESCAPED,
    MUSTACHE_SECTION_START,
    MUSTACHE_SECTION_START_INV,
    MUSTACHE_SECTION_END,
    MUSTACHE_SECTION_GOTO,
    MUSTACHE_PADDING_PUSH,
    MUSTACHE_PADDING_POP,
    MUSTACHE_PADDING_WRITE,
  } instruction;
  /** the data the instruction acts upon */
  struct {
    /** The length instruction block in the instruction array (for sections). */
    uint32_t end;
    /** The length of the (string) data. */
    uint32_t len;
    /** The offset from the beginning of the data segment. */
    uint32_t name_pos;
    /** The offset between the name (start) and content (for sections). */
    uint16_t name_len;
    /** The offset between the name and the content (left / right by type). */
    uint16_t offset;
  } data;
} mustache__instruction_s;

typedef struct {
  mustache_section_s sec; /* client visible section data */
  uint32_t start;         /* section start instruction position */
  uint32_t end;           /* instruction to jump to after completion */
  uint32_t index;         /* zero based index for section loops */
  uint32_t count; /* the number of times the section should be performed */
  uint16_t frame; /* the stack frame's index (zero based) */
} mustache__section_stack_frame_s;

/*
 * The stack memory is placed in a structure to allow stack unrolling and
 * avoiding recursion with it's stack overflow risks.
 */
typedef struct {
  mustache_s *data; /* the mustache template being built */
  uint32_t pos;     /* the instruction postision index */
  uint32_t padding; /* padding instruction position */
  uint16_t index;   /* the stack postision index */
  mustache__section_stack_frame_s stack[MUSTACHE_NESTING_LIMIT];
} mustache__builder_stack_s;

#define MUSTACHE_DELIMITER_LENGTH_LIMIT 5

/*
 * The stack memory is placed in a structure to allow stack unrolling and
 * avoiding recursion with it's stack overflow risks.
 */
typedef struct {
  mustache_s *m;
  mustache__instruction_s *i;
  mustache_error_en *err;
  char *data;
  char *path;
  uint32_t i_capa;
  uint32_t data_len;
  uint32_t padding;
  uint16_t path_len;
  uint16_t path_capa;
  uint16_t index; /* stack index */
  struct {
    uint32_t data_start;    /* template starting position (with header) */
    uint32_t data_pos;      /* data reading position (how much was consumed) */
    uint32_t data_end;      /* data ending position (for this template) */
    uint16_t open_sections; /* counts open sections waiting for closure */
    char del_start[MUSTACHE_DELIMITER_LENGTH_LIMIT]; /* currunt instruction
                                                        start delimiter */
    char del_end[MUSTACHE_DELIMITER_LENGTH_LIMIT];   /* currunt instruction end
                                                        delimiter */
    uint8_t del_start_len; /* currunt start delimiter length */
    uint8_t del_end_len;   /* currunt end delimiter length */
  } stack[MUSTACHE_NESTING_LIMIT];
} mustache__loader_stack_s;

/* *****************************************************************************
Callbacks Helper Implementation
***************************************************************************** */

#define MUSTACH2INSTRUCTIONS(mustache)                                         \
  ((mustache__instruction_s *)((mustache) + 1))
#define MUSTACH2DATA(mustache)                                                 \
  (char *)(MUSTACH2INSTRUCTIONS((mustache)) +                                  \
           (mustache)->u.read_only.intruction_count)
#define MUSTACHE_OBJECT_OFFSET(type, member, ptr)                              \
  ((type *)((uintptr_t)(ptr) - (uintptr_t)(&(((type *)0)->member))))

#define MUSTACHE_ASSERT(cond, msg)                                             \
  if (!(cond)) {                                                               \
    perror("FATAL ERROR: " msg);                                               \
    exit(errno);                                                               \
  }
#define MUSTACHE_IGNORE_WHITESPACE(str, step)                                  \
  while (isspace(*(str))) {                                                    \
    (str) += (step);                                                           \
  }

/*
 * used internally by some of the helpers to find convert a section pointer to
 * the full builder stack data.
 */
static inline mustache__builder_stack_s *
mustache___section2stack(mustache_section_s *section) {
  mustache__section_stack_frame_s *f =
      (mustache__section_stack_frame_s *)section;
  return MUSTACHE_OBJECT_OFFSET(mustache__builder_stack_s, stack,
                                (f - f->frame));
}

/**
 * Returns the section's parent for nested sections, or NULL (for root section).
 *
 * This allows the `udata` values in the parent to be accessed and can be used,
 * for example, when seeking a value (argument / keyword) within a nested data
 * structure such as a Hash.
 */
static inline mustache_section_s *
mustache_section_parent(mustache_section_s *section) {
  mustache_section_s tmp = *section;
  mustache__section_stack_frame_s *f =
      (mustache__section_stack_frame_s *)section;
  while (f->frame) {
    --f;
    if (tmp.udata1 != f->sec.udata1 || tmp.udata2 != f->sec.udata2)
      return &f->sec;
  }
  return NULL;
}

/**
 * Returns the section's unparsed content as a non-NUL terminated byte array.
 *
 * The length of the data will be placed in the `size_t` variable pointed to by
 * `p_len`. Do NOT use `strlen`, since the data isn't NUL terminated.
 *
 * This allows text to be accessed when a section's content is, in fact, meant
 * to be passed along to a function / lambda.
 */
static inline const char *mustache_section_text(mustache_section_s *section,
                                                size_t *p_len) {
  if (!section || !p_len)
    goto error;
  mustache__builder_stack_s *s = mustache___section2stack(section);
  mustache__instruction_s *inst =
      MUSTACH2INSTRUCTIONS(s->data) + s->pos; /* current instruction*/
  if (inst->instruction != MUSTACHE_SECTION_START)
    goto error;
  const char *start =
      MUSTACH2DATA(s->data) + inst->data.name_pos + inst->data.offset;
  *p_len = inst->data.len;
  return start;
error:
  *p_len = 0;
  return NULL;
}

/**
 * used internally to write escaped text rather than clear text.
 */
static inline int mustache__write_padding(mustache__builder_stack_s *s) {
  mustache__instruction_s *const inst = MUSTACH2INSTRUCTIONS(s->data);
  char *const data = MUSTACH2DATA(s->data);
  for (uint32_t i = s->padding; i; i = inst[i].data.end) {
    if (mustache_on_text(&s->stack[s->index].sec, data + inst[i].data.name_pos,
                         inst[i].data.name_len) == -1)
      return -1;
  }
  return 0;
}

/**
 * used internally to write escaped text rather than clear text.
 */
static int mustache__write_escaped(mustache__builder_stack_s *s, char *text,
                                   uint32_t len) {
#define MUSTACHE_ESCAPE_BUFFER_SIZE 4096
  /** HTML ecape table, created using the following Ruby Script:

        a = (0..255).to_a.map {|i| i.chr }
        100.times {|i| a[i] = "&\##{i.to_s(10)};"}
        ('a'.ord..'z'.ord).each {|i| a[i] = i.chr }
        ('A'.ord..'Z'.ord).each {|i| a[i] = i.chr }
        ('0'.ord..'9'.ord).each {|i| a[i] = i.chr }
        a['<'.ord] = "&lt;"
        a['>'.ord] = "&gt;"
        a['&'.ord] = "&amp;"
        a['"'.ord] = "&quot;"
        a["\'".ord] = "&apos;"
        a['|'.ord] = "&\##{'|'.ord.to_s(10)};"

        b = a.map {|s| s.length }
        puts "static char *html_escape_strs[] = {",
             a.to_s.slice(1..-2) ,"};",
             "static uint8_t html_escape_len[] = {",
             b.to_s.slice(1..-2),"};"
*/
  static const char *html_escape_strs[] = {
      "&#0;",  "&#1;",  "&#2;",   "&#3;",  "&#4;",   "&#5;",  "&#6;",  "&#7;",
      "&#8;",  "&#9;",  "&#10;",  "&#11;", "&#12;",  "&#13;", "&#14;", "&#15;",
      "&#16;", "&#17;", "&#18;",  "&#19;", "&#20;",  "&#21;", "&#22;", "&#23;",
      "&#24;", "&#25;", "&#26;",  "&#27;", "&#28;",  "&#29;", "&#30;", "&#31;",
      "&#32;", "&#33;", "&quot;", "&#35;", "&#36;",  "&#37;", "&amp;", "&apos;",
      "&#40;", "&#41;", "&#42;",  "&#43;", "&#44;",  "&#45;", "&#46;", "&#47;",
      "0",     "1",     "2",      "3",     "4",      "5",     "6",     "7",
      "8",     "9",     "&#58;",  "&#59;", "&lt;",   "&#61;", "&gt;",  "&#63;",
      "&#64;", "A",     "B",      "C",     "D",      "E",     "F",     "G",
      "H",     "I",     "J",      "K",     "L",      "M",     "N",     "O",
      "P",     "Q",     "R",      "S",     "T",      "U",     "V",     "W",
      "X",     "Y",     "Z",      "&#91;", "&#92;",  "&#93;", "&#94;", "&#95;",
      "&#96;", "a",     "b",      "c",     "d",      "e",     "f",     "g",
      "h",     "i",     "j",      "k",     "l",      "m",     "n",     "o",
      "p",     "q",     "r",      "s",     "t",      "u",     "v",     "w",
      "x",     "y",     "z",      "{",     "&#124;", "}",     "~",     "\x7F",
      "\x80",  "\x81",  "\x82",   "\x83",  "\x84",   "\x85",  "\x86",  "\x87",
      "\x88",  "\x89",  "\x8A",   "\x8B",  "\x8C",   "\x8D",  "\x8E",  "\x8F",
      "\x90",  "\x91",  "\x92",   "\x93",  "\x94",   "\x95",  "\x96",  "\x97",
      "\x98",  "\x99",  "\x9A",   "\x9B",  "\x9C",   "\x9D",  "\x9E",  "\x9F",
      "\xA0",  "\xA1",  "\xA2",   "\xA3",  "\xA4",   "\xA5",  "\xA6",  "\xA7",
      "\xA8",  "\xA9",  "\xAA",   "\xAB",  "\xAC",   "\xAD",  "\xAE",  "\xAF",
      "\xB0",  "\xB1",  "\xB2",   "\xB3",  "\xB4",   "\xB5",  "\xB6",  "\xB7",
      "\xB8",  "\xB9",  "\xBA",   "\xBB",  "\xBC",   "\xBD",  "\xBE",  "\xBF",
      "\xC0",  "\xC1",  "\xC2",   "\xC3",  "\xC4",   "\xC5",  "\xC6",  "\xC7",
      "\xC8",  "\xC9",  "\xCA",   "\xCB",  "\xCC",   "\xCD",  "\xCE",  "\xCF",
      "\xD0",  "\xD1",  "\xD2",   "\xD3",  "\xD4",   "\xD5",  "\xD6",  "\xD7",
      "\xD8",  "\xD9",  "\xDA",   "\xDB",  "\xDC",   "\xDD",  "\xDE",  "\xDF",
      "\xE0",  "\xE1",  "\xE2",   "\xE3",  "\xE4",   "\xE5",  "\xE6",  "\xE7",
      "\xE8",  "\xE9",  "\xEA",   "\xEB",  "\xEC",   "\xED",  "\xEE",  "\xEF",
      "\xF0",  "\xF1",  "\xF2",   "\xF3",  "\xF4",   "\xF5",  "\xF6",  "\xF7",
      "\xF8",  "\xF9",  "\xFA",   "\xFB",  "\xFC",   "\xFD",  "\xFE",  "\xFF"};
  static const uint8_t html_escape_len[] = {
      4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
      5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6, 5, 5, 5, 5, 6, 5, 5, 5, 5, 5, 5, 5, 5,
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 5, 5, 4, 5, 4, 5, 5, 1, 1, 1, 1, 1, 1, 1,
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 5, 5, 5, 5, 5,
      5, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
      1, 1, 1, 1, 6, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
  char buffer[MUSTACHE_ESCAPE_BUFFER_SIZE];
  size_t pos = 0;
  const char *end = text + len;
  while (text < end) {
    if (MUSTACHE_USE_DYNAMIC_PADDING && *text == '\n' && s->padding) {
      buffer[pos++] = '\n';
      buffer[pos] = 0;
      if (mustache_on_text(&s->stack[s->index].sec, buffer, pos) == -1)
        return -1;
      pos = 0;
      if (mustache__write_padding(s) == -1)
        return -1;
    } else {
      memcpy(buffer + pos, html_escape_strs[(uint8_t)text[0]],
             html_escape_len[(uint8_t)text[0]]);
      pos += html_escape_len[(uint8_t)text[0]];
      if (pos >= (MUSTACHE_ESCAPE_BUFFER_SIZE - 6)) {
        buffer[pos] = 0;
        if (mustache_on_text(&s->stack[s->index].sec, buffer, pos) == -1)
          return -1;
        pos = 0;
      }
    }
    ++text;
  }
  if (pos) {
    buffer[pos] = 0;
    if (mustache_on_text(&s->stack[s->index].sec, buffer, pos) == -1)
      return -1;
  }
  return 0;
#undef MUSTACHE_ESCAPE_BUFFER_SIZE
}
/**
 * This helper function should be used to write text to the output
 * stream (often used within the `mustache_on_arg` callback).
 */
static inline int mustache_write_text(mustache_section_s *section, char *text,
                                      uint32_t len, uint8_t escape) {
  mustache__builder_stack_s *s = mustache___section2stack(section);
  if (escape)
    return mustache__write_escaped(s, text, len);
    /* TODO */
#if MUSTACHE_USE_DYNAMIC_PADDING
  char *end = memchr(text, '\n', len);
  while (len && end) {
    ++end;
    const uint32_t slice_len = end - text;
    if (mustache_on_text(&s->stack[s->index].sec, text, slice_len) == -1)
      return -1;
    text = end;
    len -= slice_len;
    end = memchr(text, '\n', len);
    if (mustache__write_padding(s) == -1)
      return -1;
  }
  if (len && mustache_on_text(&s->stack[s->index].sec, text, len) == -1)
    return -1;
#else
  if (mustache_on_text(&s->stack[s->index].sec, text, len) == -1)
    return -1;

#endif
  return 0;
}

/* *****************************************************************************
Internal Helpers
***************************************************************************** */

/* the data segment's new length after loading the the template */
/* The data segments includes a template header: */
/*  | 4 bytes template start instruction position | */
/*  | 2 bytes template name | 4 bytes next template position | */
/*  | template name (filename) | ...[template data]... */
/* this allows template data to be reused when repeating a template */

/** a template file data segment header */
typedef struct {
  const char *filename;  /* template file name */
  uint32_t inst_start;   /* start position for instructions */
  uint32_t next;         /* next template position (absolute) */
  uint16_t filename_len; /* file name length */
  uint16_t path_len;     /* if the file is in a folder, this marks the '/' */
} mustache__data_segment_s;

/* data segment serialization, returns the number of bytes written. */
static inline size_t
mustache__data_segment_write(uint8_t *dest, mustache__data_segment_s data) {
  dest[0] = 0xFF & data.inst_start;
  dest[1] = 0xFF & (data.inst_start >> 1);
  dest[2] = 0xFF & (data.inst_start >> 2);
  dest[3] = 0xFF & (data.inst_start >> 3);
  dest[4] = 0xFF & data.next;
  dest[5] = 0xFF & (data.next >> 1);
  dest[6] = 0xFF & (data.next >> 2);
  dest[7] = 0xFF & (data.next >> 3);
  dest[8] = 0xFF & data.filename_len;
  dest[9] = 0xFF & (data.filename_len >> 1);
  dest[10] = 0xFF & data.path_len;
  dest[11] = 0xFF & (data.path_len >> 1);
  if (data.filename_len)
    memcpy(dest + 12, data.filename, data.filename_len);
  (dest + 12)[data.filename_len] = 0;
  return 13 + data.filename_len;
}

static inline size_t mustache__data_segment_length(size_t filename_len) {
  return 13 + filename_len;
}

/* data segment serialization, reads data from raw stream. */
static inline mustache__data_segment_s
mustache__data_segment_read(uint8_t *data) {
  mustache__data_segment_s s = {
      .filename = (char *)(data + 12),
      .inst_start = ((uint32_t)data[0] | ((uint32_t)data[1] << 1) |
                     ((uint32_t)data[2] << 2) | ((uint32_t)data[3] << 3)),
      .next = ((uint32_t)data[4] | ((uint32_t)data[5] << 1) |
               ((uint32_t)data[6] << 2) | ((uint32_t)data[7] << 3)),
      .filename_len = ((uint16_t)data[8] | ((uint16_t)data[9] << 1)),
      .path_len = ((uint16_t)data[10] | ((uint16_t)data[11] << 1)),
  };
  return s;
}

static inline void mustache__stand_alone_adjust(mustache__loader_stack_s *s,
                                                uint32_t stand_alone) {
  if (!stand_alone)
    return;
  /* adjust reading position */
  s->stack[s->index].data_pos +=
      1 + (s->data[s->stack[s->index].data_pos] == '\r');
  /* remove any padding from the beginning of the line */
  if (s->m->u.read_only.intruction_count &&
      s->i[s->m->u.read_only.intruction_count - 1].instruction ==
          MUSTACHE_WRITE_TEXT) {
    mustache__instruction_s *ins =
        s->i + s->m->u.read_only.intruction_count - 1;
    if (ins->data.name_len <= (stand_alone >> 1))
      --s->m->u.read_only.intruction_count;
    else
      ins->data.name_len -= (stand_alone >> 1);
  }
}

/* pushes an instruction to the instruction array */
static inline int mustache__instruction_push(mustache__loader_stack_s *s,
                                             mustache__instruction_s inst) {
  if (s->m->u.read_only.intruction_count >= INT32_MAX)
    goto instructions_too_long;
  if (s->i_capa <= s->m->u.read_only.intruction_count) {
    s->m = realloc(s->m, sizeof(*s->m) + (sizeof(*s->i) * (s->i_capa + 32)));
    MUSTACHE_ASSERT(s->m, "Mustache memory allocation failed");
    s->i_capa += 32;
    s->i = MUSTACH2INSTRUCTIONS(s->m);
  }
  s->i[s->m->u.read_only.intruction_count] = inst;
  ++s->m->u.read_only.intruction_count;
  return 0;
instructions_too_long:
  *s->err = MUSTACHE_ERR_TOO_DEEP;
  return -1;
}

/* pushes text and padding instruction to the instruction array */
static inline int mustache__push_text_instruction(mustache__loader_stack_s *s,
                                                  uint32_t pos, uint32_t len) {
  /* always push padding instructions, in case or recursion with padding */
  // if (!s->padding) {
  //   /* no padding, push text, as is */
  //   return mustache__instruction_push(
  //       s, (mustache__instruction_s){
  //              .instruction = MUSTACHE_WRITE_TEXT,
  //              .data = {.name_pos = pos, .name_len = len},
  //          });
  // }
  /* insert padding instruction after each new line */
  for (;;) {
    /* seek new line markers */
    char *start = s->data + pos;
    char *end = memchr(s->data + pos, '\n', len);
    if (!end)
      break;
    /* offset marks the line's length */
    const size_t offset = (end - start) + 1;
    /* push text and padding instructions */
    if (mustache__instruction_push(
            s,
            (mustache__instruction_s){
                .instruction = MUSTACHE_WRITE_TEXT,
                .data = {.name_pos = pos, .name_len = offset},
            }) == -1 ||
        mustache__instruction_push(s, (mustache__instruction_s){
                                          .instruction = MUSTACHE_PADDING_WRITE,
                                      }) == -1)
      return -1;
    pos += offset;
    len -= offset;
  }
  /* done? */
  if (!len)
    return 0;
  /* write any text that doesn't terminate in the EOL marker */
  return mustache__instruction_push(
      s, (mustache__instruction_s){
             .instruction = MUSTACHE_WRITE_TEXT,
             .data = {.name_pos = pos, .name_len = len},
         });
}

/*
 * Returns the instruction's position if the template is already existing.
 *
 * Returns `(uint32_t)-1` if the template is missing (not loaded, yet).
 */
static inline uint32_t mustache__file_is_loaded(mustache__loader_stack_s *s,
                                                char *name, size_t name_len) {
  char *data = s->data;
  const char *end = data + s->m->u.read_only.data_length;
  while (data < end) {
    mustache__data_segment_s seg = mustache__data_segment_read((uint8_t *)data);
    if (seg.filename_len == name_len && !memcmp(seg.filename, name, name_len))
      return seg.inst_start;
    data += seg.next;
  }
  return (uint32_t)-1;
}

static inline ssize_t mustache__load_data(mustache__loader_stack_s *s,
                                          const char *name, size_t name_len,
                                          const char *data, size_t data_len) {
  const size_t old_len = s->data_len;
  if (old_len + data_len > UINT32_MAX)
    goto too_long;
  s->data = realloc(s->data, old_len + data_len +
                                 mustache__data_segment_length(name_len) + 1);
  MUSTACHE_ASSERT(s->data,
                  "failed to allocate memory for mustache template data");
  size_t path_len = name_len;
  while (path_len) {
    --path_len;
    if (name[path_len] == '/' || name[path_len] == '\\') {
      ++path_len;
      break;
    }
  }
  mustache__data_segment_write(
      (uint8_t *)s->data + old_len,
      (mustache__data_segment_s){
          .filename = name,
          .filename_len = name_len,
          .inst_start = s->m->u.read_only.intruction_count,
          .next =
              s->data_len + data_len + mustache__data_segment_length(name_len),
          .path_len = path_len,
      });
  if (data) {
    memcpy(s->data + old_len + mustache__data_segment_length(name_len), data,
           data_len);
  }
  s->data_len += data_len + mustache__data_segment_length(name_len);
  s->data[s->data_len] = 0;
  s->m->u.read_only.data_length = s->data_len;

  mustache__instruction_push(
      s, (mustache__instruction_s){.instruction = MUSTACHE_SECTION_START});
  /* advance stack frame */
  ++s->index;
  if (s->index >= MUSTACHE_NESTING_LIMIT)
    goto too_long;
  s->stack[s->index].data_pos =
      old_len + mustache__data_segment_length(name_len);
  s->stack[s->index].data_start = old_len;
  s->stack[s->index].data_end = s->data_len;
  /* reset delimiters */
  s->stack[s->index].del_start_len = 2;
  s->stack[s->index].del_end_len = 2;
  s->stack[s->index].del_start[0] = s->stack[s->index].del_start[1] = '{';
  s->stack[s->index].del_start[2] = 0;
  s->stack[s->index].del_end[0] = s->stack[s->index].del_end[1] = '}';
  s->stack[s->index].del_end[2] = 0;
  s->stack[s->index].open_sections = 0;
  return data_len;
too_long:
  *s->err = MUSTACHE_ERR_TOO_DEEP;
  return -1;
}

static inline ssize_t mustache__load_file(mustache__loader_stack_s *s,
                                          const char *name, size_t name_len) {
  struct stat f_data;
  uint16_t i = s->index;
  uint32_t old_path_len = 0;
  if (!name_len) {
    goto name_missing_error;
  }
  if (name_len >= 8192)
    goto name_length_error;
  /* test file names by walking the stack backwards and matching paths */
  do {
    mustache__data_segment_s seg;
    if (s->data)
      seg = mustache__data_segment_read((uint8_t *)s->data +
                                        s->stack[i].data_start);
    else
      seg = (mustache__data_segment_s){
          .path_len = 0,
      };
    /* did we test this path for the file? */
    if (old_path_len && (old_path_len == seg.path_len &&
                         !memcmp(s->path, seg.filename, old_path_len))) {
      continue;
    }
    old_path_len = seg.path_len;
    /* make sure s->path capacity is enough. */
    if (s->path_capa < seg.path_len + name_len + 10) {
      s->path = realloc(s->path, seg.path_len + name_len + 10);
      MUSTACHE_ASSERT(s->path,
                      "failed to allocate memory for mustache template data");
      s->path_capa = seg.path_len + name_len + 10;
    }
    /* if testing local folder, there's no need to keep looping */
    if (!seg.path_len) {
      i = 1;
    } else {
      memcpy(s->path, seg.filename, seg.path_len);
    }
    /* copy name to path */
    memcpy(s->path + seg.path_len, name, name_len);
    s->path[name_len + seg.path_len] = 0;
    /* test if file exists */
    if (!stat(s->path, &f_data) && S_ISREG(f_data.st_mode)) {
      old_path_len = name_len + seg.path_len;
      goto file_found;
    }
    /* add default extension  */
    memcpy(s->path + seg.path_len + name_len, ".mustache", 9);
    s->path[name_len + seg.path_len + 9] = 0;
    /* test if new filename file exists */
    if (!stat(s->path, &f_data) && S_ISREG(f_data.st_mode)) {
      old_path_len = name_len + seg.path_len + 9;
      goto file_found;
    }
  } while (--i);

  /* test if the file is "virtual" (only true for the first template loaded) */
  if (s->data) {
    mustache__data_segment_s seg =
        mustache__data_segment_read((uint8_t *)s->data);
    if (seg.filename_len == name_len && !memcmp(seg.filename, name, name_len)) {
      /* this name points to the original (root) template, and it's virtual */
      if (mustache__instruction_push(
              s, (mustache__instruction_s){
                     .instruction = MUSTACHE_SECTION_GOTO,
                     .data =
                         {
                             .len = 0,
                             .end = s->m->u.read_only.intruction_count,
                         },
                 }))
        goto unknown_error;
      return 0;
    }
  }

#if MUSTACHE_FAIL_ON_MISSING_TEMPLATE
  *s->err = MUSTACHE_ERR_FILE_NOT_FOUND;
  return -1;
#else
  return 0;
#endif

file_found:
  if (f_data.st_size >= INT32_MAX) {
    goto file_too_big;
  } else if (f_data.st_size == 0) {
    /* empty, do nothing */
    return 0;
  } else {
    /* test if the file was previously loaded */
    uint32_t pre_existing = mustache__file_is_loaded(s, s->path, old_path_len);
    if (pre_existing != (uint32_t)-1) {
      if (mustache__instruction_push(
              s, (mustache__instruction_s){
                     .instruction = MUSTACHE_SECTION_GOTO,
                     .data =
                         {
                             .len = pre_existing,
                             .end = s->m->u.read_only.intruction_count,
                         },
                 })) {
        goto unknown_error;
      }
      return 0;
    }
  }
  if (mustache__load_data(s, s->path, old_path_len, NULL, f_data.st_size) == -1)
    goto unknown_error;
  int fd = open(s->path, O_RDONLY);
  if (fd == -1)
    goto file_err;
  if (pread(fd, s->data + s->data_len - f_data.st_size, f_data.st_size, 0) !=
      f_data.st_size)
    goto file_err;
  close(fd);
  return f_data.st_size;

name_missing_error:
  *s->err = MUSTACHE_ERR_FILE_NAME_TOO_SHORT;
  return -1;

name_length_error:
  *s->err = MUSTACHE_ERR_FILE_NAME_TOO_LONG;
  return -1;

file_too_big:
  *s->err = MUSTACHE_ERR_FILE_TOO_BIG;
  return -1;

file_err:
  *s->err = MUSTACHE_ERR_UNKNOWN;
  return -1;

unknown_error:
  return -1;
}

/* *****************************************************************************
Calling the instrustion list (using the template engine)
***************************************************************************** */

/*
 * This function reviews the instructions listed at the end of the mustache_s
 * and performs any callbacks necessary.
 *
 * The `mustache_s` data is looks like this:
 *
 *  - header (the `mustache_s` struct): lists the length of the instruction
 *    array and data segments.
 *  - Instruction array: lists all the instructions extracted from the
 *    template(s) (an array of `mustache__instruction_s`).
 *  - Data segment: text and data related to the instructions.
 *
 * The instructions, much like machine code, might loop or jump. This is why the
 * function keeps a stack of sorts. This allows the code to avoid recursion and
 * minimize any risk of stack overflow caused by recursive templates.
 */
MUSTACHE_FUNC int(mustache_build)(mustache_build_args_s args) {
  mustache_error_en err_if_missing;
  if (!args.err)
    args.err = &err_if_missing;
  if (!args.mustache) {
    goto user_error;
  }
  /* extract the instruction array and data segment from the mustache_s */
  mustache__instruction_s *instructions = MUSTACH2INSTRUCTIONS(args.mustache);
  char *const data = MUSTACH2DATA(args.mustache);
  mustache__builder_stack_s s;
  s.data = args.mustache;
  s.pos = 0;
  s.index = 0;
  s.padding = 0;
  s.stack[0] = (mustache__section_stack_frame_s){
      .sec =
          {
              .udata1 = args.udata1,
              .udata2 = args.udata2,
          },
      .start = 0,
      .end = instructions[0].data.end,
      .index = 0,
      .count = 0,
      .frame = 0,
  };
  while ((uintptr_t)(instructions + s.pos) < (uintptr_t)data) {
    switch (instructions[s.pos].instruction) {
    case MUSTACHE_WRITE_TEXT:
      if (mustache_on_text(&s.stack[s.index].sec,
                           data + instructions[s.pos].data.name_pos,
                           instructions[s.pos].data.name_len))
        goto user_error;
      break;
      /* fallthrough */
    case MUSTACHE_WRITE_ARG:
      if (mustache_on_arg(&s.stack[s.index].sec,
                          data + instructions[s.pos].data.name_pos,
                          instructions[s.pos].data.name_len, 1))
        goto user_error;
      break;
    case MUSTACHE_WRITE_ARG_UNESCAPED:
      if (mustache_on_arg(&s.stack[s.index].sec,
                          data + instructions[s.pos].data.name_pos,
                          instructions[s.pos].data.name_len, 0))
        goto user_error;
      break;
      /* fallthrough */
    case MUSTACHE_SECTION_GOTO:
    /* fallthrough */
    case MUSTACHE_SECTION_START:
    case MUSTACHE_SECTION_START_INV:
      /* advance stack*/
      if (s.index + 1 >= MUSTACHE_NESTING_LIMIT) {
        if (args.err)
          *args.err = MUSTACHE_ERR_TOO_DEEP;
        goto error;
      }
      s.stack[s.index + 1].sec = s.stack[s.index].sec;
      ++s.index;
      s.stack[s.index].start =
          (instructions[s.pos].instruction == MUSTACHE_SECTION_GOTO
               ? instructions[s.pos].data.len
               : s.pos);
      s.stack[s.index].end = instructions[s.pos].data.end;
      s.stack[s.index].frame = s.index;
      s.stack[s.index].index = 0;
      s.stack[s.index].count = 1;

      /* test section count */
      if (instructions[s.pos].data.name_pos) {
        /* this is a named section, it should be tested against user data */
        int32_t val = mustache_on_section_test(
            &s.stack[s.index].sec, data + instructions[s.pos].data.name_pos,
            instructions[s.pos].data.name_len,
            instructions[s.pos].instruction == MUSTACHE_SECTION_START);
        if (val == -1) {
          goto user_error;
        }
        if (instructions[s.pos].instruction == MUSTACHE_SECTION_START_INV) {
          /* invert test */
          val = (val == 0);
        }
        s.stack[s.index].count = (uint32_t)val;
      }
      /* fallthrough  */
    case MUSTACHE_SECTION_END:
      /* loop section or continue */
      if (s.stack[s.index].index < s.stack[s.index].count) {
        /* repeat / start section */
        s.pos = s.stack[s.index].start;
        s.stack[s.index].sec = s.stack[s.index - 1].sec;
        /* review user callback (if it's a named section) */
        if (instructions[s.pos].data.name_pos &&
            mustache_on_section_start(&s.stack[s.index].sec,
                                      data + instructions[s.pos].data.name_pos,
                                      instructions[s.pos].data.name_len,
                                      s.stack[s.index].index) == -1)
          goto user_error;
        /* skip padding instructions in GOTO tags (recursive partials) */
        if (instructions[s.pos].instruction == MUSTACHE_SECTION_GOTO)
          ++s.pos;
        ++s.stack[s.index].index;
        break;
      }
      s.pos = s.stack[s.index].end;
      --s.index;
      break;
    case MUSTACHE_PADDING_PUSH:
      s.padding = s.pos;
      break;
    case MUSTACHE_PADDING_POP:
      s.padding = instructions[s.padding].data.end;
      break;
    case MUSTACHE_PADDING_WRITE:
      for (uint32_t i = s.padding; i; i = instructions[i].data.end) {
        if (mustache_on_text(&s.stack[s.index].sec,
                             data + instructions[i].data.name_pos,
                             instructions[i].data.name_len))
          goto user_error;
      }
      break;
    default:
      /* not a valid engine */
      fprintf(stderr, "ERROR: invalid mustache instruction set detected (wrong "
                      "`mustache_s`?)\n");
      if (args.err) {
        *args.err = MUSTACHE_ERR_UNKNOWN;
      }
      goto error;
    }
    ++s.pos;
  }
  *args.err = MUSTACHE_OK;
  return 0;
user_error:
  *args.err = MUSTACHE_ERR_USER_ERROR;
error:
  mustache_on_formatting_error(args.udata1, args.udata2);
  return -1;
}

/* *****************************************************************************
Building the instrustion list (parsing the template)
***************************************************************************** */

/* The parsing implementation, converts a template to an instruction array */
MUSTACHE_FUNC mustache_s *(mustache_load)(mustache_load_args_s args) {
  mustache_error_en err_if_missing;
  mustache__loader_stack_s s;
  uint8_t flag = 0;

  if (!args.err)
    args.err = &err_if_missing;
  s.path_capa = 0;
  s.path = NULL;
  s.data = NULL;
  s.data_len = 0;
  s.i = NULL;
  s.i_capa = 32;
  s.index = 0;
  s.padding = 0;
  s.m = malloc(sizeof(*s.m) + (sizeof(*s.i) * 32));
  MUSTACHE_ASSERT(s.m, "failed to allocate memory for mustache data");
  s.m->u.read_only_pt = 0;
  s.m->u.read_only.data_length = 0;
  s.m->u.read_only.intruction_count = 0;
  s.i = MUSTACH2INSTRUCTIONS(s.m);
  s.err = args.err;

  if (!args.filename_len && args.filename)
    args.filename_len = strlen(args.filename);

  if (args.data) {
    if (mustache__load_data(&s, args.filename, args.filename_len, args.data,
                            args.data_len) == -1) {
      goto error;
    }
  } else {
    if (mustache__load_file(&s, args.filename, args.filename_len) == -1) {
      goto error;
    }
  }

  /* loop while there are templates to be parsed on the stack */
  while (s.index) {
    /* parsing loop */
    while (s.stack[s.index].data_pos < s.stack[s.index].data_end) {
      /* stand-alone tag flag, also containes padding length after bit 1 */
      uint32_t stand_alone = 0;
      uint32_t stand_alone_pos = 0;
      /* start parsing at current position */
      const char *start = s.data + s.stack[s.index].data_pos;
      /* find the next instruction (beg == beginning) */
      char *beg = strstr(start, s.stack[s.index].del_start);
      const char *org_beg = beg;
      if (!beg || beg >= s.data + s.stack[s.index].data_end) {
        /* no instructions left, only text */
        mustache__push_text_instruction(&s, s.stack[s.index].data_pos,
                                        s.stack[s.index].data_end -
                                            s.stack[s.index].data_pos);
        s.stack[s.index].data_pos = s.stack[s.index].data_end;
        continue;
      }
      if (beg != start) {
        /* there's text before the instruction */
        mustache__push_text_instruction(&s, s.stack[s.index].data_pos,
                                        (uint32_t)(uintptr_t)(beg - start));
      }
      /* move beg (reading position) after the delimiter */
      beg += s.stack[s.index].del_start_len;
      /* seek the end of the instruction delimiter */
      char *end = strstr(beg, s.stack[s.index].del_end);
      if (!end || end >= s.data + s.stack[s.index].data_end) {
        /* delimiter not closed */
        *args.err = MUSTACHE_ERR_CLOSURE_MISMATCH;
        goto error;
      }

      /* update reading position in the stack */
      s.stack[s.index].data_pos = (end - s.data) + s.stack[s.index].del_end_len;

      /* Test for stand-alone tags */
      if (!end[s.stack[s.index].del_end_len] ||
          end[s.stack[s.index].del_end_len] == '\n' ||
          (end[s.stack[s.index].del_end_len] == '\r' &&
           end[1 + s.stack[s.index].del_end_len] == '\n')) {
        char *pad = beg - (s.stack[s.index].del_start_len + 1);
        while (pad >= start && (pad[0] == ' ' || pad[0] == '\t'))
          --pad;
        if (pad[0] == '\n' || pad[0] == 0) {
          /* Yes, this a stand-alone tag, store padding length + flag  */
          ++pad;
          stand_alone_pos = pad - s.data;
          stand_alone =
              ((beg - (pad + s.stack[s.index].del_start_len)) << 1) | 1;
        }
      }

      /* parse instruction content */
      flag = 1;

      switch (beg[0]) {
      case '!':
        /* comment, do nothing... almost */
        mustache__stand_alone_adjust(&s, stand_alone);
        break;

      case '=':
        /* define new seperators */
        mustache__stand_alone_adjust(&s, stand_alone);
        ++beg;
        --end;
        if (end[0] != '=') {
          *args.err = MUSTACHE_ERR_CLOSURE_MISMATCH;
          goto error;
        }
        --end;
        MUSTACHE_IGNORE_WHITESPACE(beg, 1);
        MUSTACHE_IGNORE_WHITESPACE(end, -1);
        ++end;
        {
          char *div = beg;
          while (div < end && !isspace(*div)) {
            ++div;
          }
          if (div == end || div == beg) {
            *args.err = MUSTACHE_ERR_CLOSURE_MISMATCH;
            goto error;
          }
          if (div - beg >= MUSTACHE_DELIMITER_LENGTH_LIMIT) {
            *args.err = MUSTACHE_ERR_DELIMITER_TOO_LONG;
            goto error;
          }
          /* copy starting delimiter */
          s.stack[s.index].del_start_len = div - beg;
          for (size_t i = 0; i < s.stack[s.index].del_start_len; ++i) {
            s.stack[s.index].del_start[i] = beg[i];
          }
          s.stack[s.index].del_start[s.stack[s.index].del_start_len] = 0;

          ++div;
          MUSTACHE_IGNORE_WHITESPACE(div, 1);
          if (div == end) {
            *args.err = MUSTACHE_ERR_CLOSURE_MISMATCH;
            goto error;
          }
          if (end - div >= MUSTACHE_DELIMITER_LENGTH_LIMIT) {
            *args.err = MUSTACHE_ERR_DELIMITER_TOO_LONG;
            goto error;
          }
          /* copy ending delimiter */
          s.stack[s.index].del_end_len = end - div;
          for (size_t i = 0; i < s.stack[s.index].del_end_len; ++i) {
            s.stack[s.index].del_end[i] = div[i];
          }
          s.stack[s.index].del_end[s.stack[s.index].del_end_len] = 0;
        }
        break;

      case '^':
        /* start inverted section */
        flag = 0;
        /* fallthrough */
      case '#':
        /* start section (or inverted section) */
        mustache__stand_alone_adjust(&s, stand_alone);
        ++beg;
        --end;
        MUSTACHE_IGNORE_WHITESPACE(beg, 1);
        MUSTACHE_IGNORE_WHITESPACE(end, -1);
        ++end;

        ++s.stack[s.index].open_sections;
        if (s.stack[s.index].open_sections >= MUSTACHE_NESTING_LIMIT) {
          *args.err = MUSTACHE_ERR_TOO_DEEP;
          goto error;
        }
        if (beg - s.data >= UINT16_MAX) {
          *args.err = MUSTACHE_ERR_NAME_TOO_LONG;
        }
        if (mustache__instruction_push(
                &s,
                (mustache__instruction_s){
                    .instruction = (flag ? MUSTACHE_SECTION_START
                                         : MUSTACHE_SECTION_START_INV),
                    .data = {
                        .name_pos = beg - s.data,
                        .name_len = end - beg,
                        .offset = s.stack[s.index].data_pos - (beg - s.data),
                    }})) {
          goto error;
        }
        break;

      case '>':
        /* partial template - search data for loaded template or load new */
        mustache__stand_alone_adjust(&s, stand_alone);
        if ((stand_alone >> 1)) {
          /* TODO: add padding markers */
          if (mustache__instruction_push(
                  &s, (mustache__instruction_s){
                          .instruction = MUSTACHE_PADDING_PUSH,
                          .data = {
                              .name_pos = stand_alone_pos,
                              .name_len = (stand_alone >> 1),
                              .end = s.padding,
                          }})) {
            goto error;
          }
          s.padding = s.m->u.read_only.intruction_count - 1;
        }
        ++beg;
        --end;
        MUSTACHE_IGNORE_WHITESPACE(beg, 1);
        MUSTACHE_IGNORE_WHITESPACE(end, -1);
        ++end;
        ssize_t loaded = mustache__load_file(&s, beg, end - beg);
        if (loaded == -1)
          goto error;
        /* Add latest padding section to text */
        if ((stand_alone >> 1)) {
          if (loaded)
            mustache__instruction_push(
                &s, (mustache__instruction_s){
                        .instruction = MUSTACHE_WRITE_TEXT,
                        .data =
                            {
                                .name_pos = stand_alone_pos,
                                .name_len = (stand_alone >> 1),
                            },
                    });
          else if (mustache__instruction_push(
                       &s, (mustache__instruction_s){.instruction =
                                                         MUSTACHE_PADDING_POP}))
            goto error;
        }
        break;

      case '/':
        /* section end */
        mustache__stand_alone_adjust(&s, stand_alone);
        ++beg;
        --end;
        MUSTACHE_IGNORE_WHITESPACE(beg, 1);
        MUSTACHE_IGNORE_WHITESPACE(end, -1);
        ++end;
        if (!s.stack[s.index].open_sections) {
          *args.err = MUSTACHE_ERR_CLOSURE_MISMATCH;
          goto error;
        } else {
          uint32_t pos = s.m->u.read_only.intruction_count;
          uint32_t nested = 0;
          do {
            --pos;
            if (s.i[pos].instruction == MUSTACHE_SECTION_END)
              ++nested;
            else if (s.i[pos].instruction == MUSTACHE_SECTION_START ||
                     s.i[pos].instruction == MUSTACHE_SECTION_START_INV) {
              if (nested) {
                --nested;
              } else {
                /* test instruction closure */
                if (s.i[pos].data.name_len != end - beg ||
                    memcmp(beg, s.data + s.i[pos].data.name_pos,
                           s.i[pos].data.name_len)) {
                  *args.err = MUSTACHE_ERR_CLOSURE_MISMATCH;
                  goto error;
                }
                /* update initial instruction (do this before adding closure) */
                s.i[pos].data.end = s.m->u.read_only.intruction_count;
                s.i[pos].data.len = org_beg - (s.data + s.i[pos].data.name_pos +
                                               s.i[pos].data.offset);
                /* add closure instruction */
                mustache__instruction_push(
                    &s, (mustache__instruction_s){.instruction =
                                                      MUSTACHE_SECTION_END,
                                                  .data = s.i[pos].data});
                /* update closure count */
                --s.stack[s.index].open_sections;
                /* stop loop */
                pos = 0;
                beg = NULL;
              }
            }
          } while (pos);
          if (beg) {
            *args.err = MUSTACHE_ERR_CLOSURE_MISMATCH;
            goto error;
          }
        }
        break;

      case '{':
        /* step the read position forward if the ending was '}}}' */
        if (s.data[s.stack[s.index].data_pos] == '}' &&
            s.stack[s.index].del_end[0] == '}' &&
            s.stack[s.index].del_end[s.stack[s.index].del_end_len - 1] == '}') {
          ++s.stack[s.index].data_pos;
        }
        /* fallthrough */
      case '&':
        /* unescaped variable data */
        flag = 0;
        /* fallthrough */
      case ':': /*fallthrough*/
      case '<': /*fallthrough*/
        ++beg;  /*fallthrough*/
      default:
        --end;
        MUSTACHE_IGNORE_WHITESPACE(beg, 1);
        MUSTACHE_IGNORE_WHITESPACE(end, -1);
        ++end;
        mustache__instruction_push(
            &s, (mustache__instruction_s){
                    .instruction = (flag ? MUSTACHE_WRITE_ARG
                                         : MUSTACHE_WRITE_ARG_UNESCAPED),
                    .data = {.name_pos = beg - s.data, .name_len = end - beg}});
        break;
      }
    }
    /* make sure all sections were closed */
    if (s.stack[s.index].open_sections) {
      *args.err = MUSTACHE_ERR_CLOSURE_MISMATCH;
      goto error;
    }
    /* move padding from section tail to post closure (adjust for padding
     * changes) */
    flag = 0;
    if (s.m->u.read_only.intruction_count &&
        s.i[s.m->u.read_only.intruction_count - 1].instruction ==
            MUSTACHE_PADDING_WRITE) {
      --s.m->u.read_only.intruction_count;
      flag = 1;
    }
    /* mark section length */
    mustache__data_segment_s seg = mustache__data_segment_read(
        (uint8_t *)s.data + s.stack[s.index].data_start);
    s.i[seg.inst_start].data.end = s.m->u.read_only.intruction_count;
    /* add instruction closure */
    mustache__instruction_push(
        &s, (mustache__instruction_s){.instruction = MUSTACHE_SECTION_END});
    /* TODO: pop any padding (if exists) */
    if (s.padding && s.padding + 1 == seg.inst_start) {
      s.padding = s.i[s.padding].data.end;
      mustache__instruction_push(&s, (mustache__instruction_s){
                                         .instruction = MUSTACHE_PADDING_POP,
                                     });
    }
    /* complete padding switch*/
    if (flag) {
      mustache__instruction_push(&s, (mustache__instruction_s){
                                         .instruction = MUSTACHE_PADDING_WRITE,
                                     });
      flag = 0;
    }
    /* pop stack */
    --s.index;
  }

  s.m = realloc(s.m, sizeof(*s.m) +
                         (sizeof(*s.i) * s.m->u.read_only.intruction_count) +
                         s.data_len);
  MUSTACHE_ASSERT(s.m,
                  "failed to allocate memory for consolidated mustache data");
  memcpy(MUSTACH2DATA(s.m), s.data, s.data_len);
  free(s.data);
  free(s.path);

  *args.err = MUSTACHE_OK;
  return s.m;

error:
  free(s.data);
  free(s.path);
  free(s.m);
  return NULL;
}

#endif /* INCLUDE_MUSTACHE_IMPLEMENTATION */

#undef MUSTACHE_FUNC
#undef MUSTACH2INSTRUCTIONS
#undef MUSTACH2DATA
#undef MUSTACHE_OBJECT_OFFSET
#undef MUSTACHE_IGNORE_WHITESPACE

#endif /* H_MUSTACHE_LOADR_H */
