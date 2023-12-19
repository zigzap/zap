/*
Copyright: Boaz segev, 2017
License: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#include <fio.h>

#include <fio_cli.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* *****************************************************************************
CLI Data Stores
***************************************************************************** */

typedef struct {
  size_t len;
  const char *data;
} cstr_s;

#define FIO_SET_OBJ_TYPE const char *
#define FIO_SET_KEY_TYPE cstr_s
#define FIO_SET_KEY_COMPARE(o1, o2)                                            \
  (o1.len == o2.len &&                                                         \
   (o1.data == o2.data || !memcmp(o1.data, o2.data, o1.len)))
#define FIO_SET_NAME fio_cli_hash
#include <fio.h>

static fio_cli_hash_s fio_aliases = FIO_SET_INIT;
static fio_cli_hash_s fio_values = FIO_SET_INIT;
static size_t fio_unnamed_count = 0;

typedef struct {
  int unnamed_min;
  int unnamed_max;
  int pos;
  int unnamed_count;
  int argc;
  char const **argv;
  char const *description;
  char const **names;
} fio_cli_parser_data_s;

/** this will allow the function definition fio_cli_start to avoid the MACRO */
#define AVOID_MACRO

#define FIO_CLI_HASH_VAL(s)                                                    \
  fio_risky_hash((s).data, (s).len, (uint64_t)fio_cli_start)

/* *****************************************************************************
CLI Parsing
***************************************************************************** */

/* *****************************************************************************
CLI Parsing
***************************************************************************** */

static void fio_cli_map_line2alias(char const *line) {
  cstr_s n = {.data = line};
  while (n.data[0] == '-') {
    while (n.data[n.len] && n.data[n.len] != ' ' && n.data[n.len] != ',') {
      ++n.len;
    }
    const char *old = NULL;
    fio_cli_hash_insert(&fio_aliases, FIO_CLI_HASH_VAL(n), n, (void *)line,
                        &old);
    if (old) {
      FIO_LOG_WARNING("CLI argument name conflict detected\n"
                      "         The following two directives conflict:\n"
                      "\t%s\n\t%s\n",
                      old, line);
    }

    while (n.data[n.len] && (n.data[n.len] == ' ' || n.data[n.len] == ',')) {
      ++n.len;
    }
    n.data += n.len;
    n.len = 0;
  }
}

static char const *fio_cli_get_line_type(fio_cli_parser_data_s *parser,
                                         const char *line) {
  if (!line) {
    return NULL;
  }
  char const **pos = parser->names;
  while (*pos) {
    switch ((intptr_t)*pos) {
    case FIO_CLI_STRING__TYPE_I:       /* fallthrough */
    case FIO_CLI_BOOL__TYPE_I:         /* fallthrough */
    case FIO_CLI_INT__TYPE_I:          /* fallthrough */
    case FIO_CLI_PRINT__TYPE_I:        /* fallthrough */
    case FIO_CLI_PRINT_HEADER__TYPE_I: /* fallthrough */
      ++pos;
      continue;
    }
    if (line == *pos) {
      goto found;
    }
    ++pos;
  }
  return NULL;
found:
  switch ((size_t)pos[1]) {
  case FIO_CLI_STRING__TYPE_I:       /* fallthrough */
  case FIO_CLI_BOOL__TYPE_I:         /* fallthrough */
  case FIO_CLI_INT__TYPE_I:          /* fallthrough */
  case FIO_CLI_PRINT__TYPE_I:        /* fallthrough */
  case FIO_CLI_PRINT_HEADER__TYPE_I: /* fallthrough */
    return pos[1];
  }
  return NULL;
}

static void fio_cli_set_arg(cstr_s arg, char const *value, char const *line,
                            fio_cli_parser_data_s *parser) {
  /* handle unnamed argument */
  if (!line || !arg.len) {
    if (!value) {
      goto print_help;
    }
    if (!strcmp(value, "-?") || !strcasecmp(value, "-h") ||
        !strcasecmp(value, "-help") || !strcasecmp(value, "--help")) {
      goto print_help;
    }
    cstr_s n = {.len = ++parser->unnamed_count};
    fio_cli_hash_insert(&fio_values, n.len, n, value, NULL);
    if (parser->unnamed_max >= 0 &&
        parser->unnamed_count > parser->unnamed_max) {
      arg.len = 0;
      goto error;
    }
    return;
  }

  /* validate data types */
  char const *type = fio_cli_get_line_type(parser, line);
  switch ((size_t)type) {
  case FIO_CLI_BOOL__TYPE_I:
    if (value && value != parser->argv[parser->pos + 1]) {
      goto error;
    }
    value = "1";
    break;
  case FIO_CLI_INT__TYPE_I:
    if (value) {
      char const *tmp = value;
      fio_atol((char **)&tmp);
      if (*tmp) {
        goto error;
      }
    }
    /* fallthrough */
  case FIO_CLI_STRING__TYPE_I:
    if (!value)
      goto error;
    if (!value[0])
      goto finish;
    break;
  }

  /* add values using all aliases possible */
  {
    cstr_s n = {.data = line};
    while (n.data[0] == '-') {
      while (n.data[n.len] && n.data[n.len] != ' ' && n.data[n.len] != ',') {
        ++n.len;
      }
      fio_cli_hash_insert(&fio_values, FIO_CLI_HASH_VAL(n), n, value, NULL);
      while (n.data[n.len] && (n.data[n.len] == ' ' || n.data[n.len] == ',')) {
        ++n.len;
      }
      n.data += n.len;
      n.len = 0;
    }
  }

finish:

  /* handle additional argv progress (if value is on separate argv) */
  if (value && parser->pos < parser->argc &&
      value == parser->argv[parser->pos + 1])
    ++parser->pos;
  return;

error: /* handle errors*/
  /* TODO! */
  fprintf(stderr, "\n\r\x1B[31mError:\x1B[0m unknown argument %.*s %s %s\n\n",
          (int)arg.len, arg.data, arg.len ? "with value" : "",
          value ? (value[0] ? value : "(empty)") : "(null)");
print_help:
  fprintf(stderr, "\n%s\n",
          parser->description ? parser->description
                              : "This application accepts any of the following "
                                "possible arguments:");
  /* print out each line's arguments */
  char const **pos = parser->names;
  while (*pos) {
    switch ((intptr_t)*pos) {
    case FIO_CLI_STRING__TYPE_I: /* fallthrough */
    case FIO_CLI_BOOL__TYPE_I:   /* fallthrough */
    case FIO_CLI_INT__TYPE_I:    /* fallthrough */
    case FIO_CLI_PRINT__TYPE_I:  /* fallthrough */
    case FIO_CLI_PRINT_HEADER__TYPE_I:
      ++pos;
      continue;
    }
    type = (char *)FIO_CLI_STRING__TYPE_I;
    switch ((intptr_t)pos[1]) {
    case FIO_CLI_PRINT__TYPE_I:
      fprintf(stderr, "%s\n", pos[0]);
      pos += 2;
      continue;
    case FIO_CLI_PRINT_HEADER__TYPE_I:
      fprintf(stderr, "\n\x1B[4m%s\x1B[0m\n", pos[0]);
      pos += 2;
      continue;

    case FIO_CLI_STRING__TYPE_I: /* fallthrough */
    case FIO_CLI_BOOL__TYPE_I:   /* fallthrough */
    case FIO_CLI_INT__TYPE_I:    /* fallthrough */
      type = pos[1];
    }
    /* print line @ pos, starting with main argument name */
    int alias_count = 0;
    int first_len = 0;
    size_t tmp = 0;
    char const *const p = *pos;
    while (p[tmp] == '-') {
      while (p[tmp] && p[tmp] != ' ' && p[tmp] != ',') {
        if (!alias_count)
          ++first_len;
        ++tmp;
      }
      ++alias_count;
      while (p[tmp] && (p[tmp] == ' ' || p[tmp] == ',')) {
        ++tmp;
      }
    }
    switch ((size_t)type) {
    case FIO_CLI_STRING__TYPE_I:
      fprintf(stderr, " \x1B[1m%.*s\x1B[0m\x1B[2m <>\x1B[0m\t%s\n", first_len,
              p, p + tmp);
      break;
    case FIO_CLI_BOOL__TYPE_I:
      fprintf(stderr, " \x1B[1m%.*s\x1B[0m   \t%s\n", first_len, p, p + tmp);
      break;
    case FIO_CLI_INT__TYPE_I:
      fprintf(stderr, " \x1B[1m%.*s\x1B[0m\x1B[2m ##\x1B[0m\t%s\n", first_len,
              p, p + tmp);
      break;
    }
    /* print aliase information */
    tmp = first_len;
    while (p[tmp] && (p[tmp] == ' ' || p[tmp] == ',')) {
      ++tmp;
    }
    while (p[tmp] == '-') {
      const size_t start = tmp;
      while (p[tmp] && p[tmp] != ' ' && p[tmp] != ',') {
        ++tmp;
      }
      int padding = first_len - (tmp - start);
      if (padding < 0)
        padding = 0;
      switch ((size_t)type) {
      case FIO_CLI_STRING__TYPE_I:
        fprintf(stderr,
                " \x1B[1m%.*s\x1B[0m\x1B[2m <>\x1B[0m%*s\t(same as "
                "\x1B[1m%.*s\x1B[0m)\n",
                (int)(tmp - start), p + start, padding, "", first_len, p);
        break;
      case FIO_CLI_BOOL__TYPE_I:
        fprintf(stderr,
                " \x1B[1m%.*s\x1B[0m   %*s\t(same as \x1B[1m%.*s\x1B[0m)\n",
                (int)(tmp - start), p + start, padding, "", first_len, p);
        break;
      case FIO_CLI_INT__TYPE_I:
        fprintf(stderr,
                " \x1B[1m%.*s\x1B[0m\x1B[2m ##\x1B[0m%*s\t(same as "
                "\x1B[1m%.*s\x1B[0m)\n",
                (int)(tmp - start), p + start, padding, "", first_len, p);
        break;
      }
    }

    ++pos;
  }
  fprintf(stderr, "\nUse any of the following input formats:\n"
                  "\t-arg <value>\t-arg=<value>\t-arg<value>\n"
                  "\n"
                  "Use the -h, -help or -? to get this information again.\n"
                  "\n");
  fio_cli_end();
  exit(0);
}

static void fio_cli_end_promise(void *ignr_) {
  /* make sure fio_cli_end is called before facil.io exists. */
  fio_cli_end();
  (void)ignr_;
}

void fio_cli_start AVOID_MACRO(int argc, char const *argv[], int unnamed_min,
                               int unnamed_max, char const *description,
                               char const **names) {
  static fio_lock_i run_once = FIO_LOCK_INIT;
  if (!fio_trylock(&run_once))
    fio_state_callback_add(FIO_CALL_AT_EXIT, fio_cli_end_promise, NULL);
  if (unnamed_max >= 0 && unnamed_max < unnamed_min)
    unnamed_max = unnamed_min;
  fio_cli_parser_data_s parser = {
      .unnamed_min = unnamed_min,
      .unnamed_max = unnamed_max,
      .description = description,
      .argc = argc,
      .argv = argv,
      .names = names,
      .pos = 0,
  };

  if (fio_cli_hash_count(&fio_values)) {
    fio_cli_end();
  }

  /* prepare aliases hash map */

  char const **line = names;
  while (*line) {
    switch ((intptr_t)*line) {
    case FIO_CLI_STRING__TYPE_I:       /* fallthrough */
    case FIO_CLI_BOOL__TYPE_I:         /* fallthrough */
    case FIO_CLI_INT__TYPE_I:          /* fallthrough */
    case FIO_CLI_PRINT__TYPE_I:        /* fallthrough */
    case FIO_CLI_PRINT_HEADER__TYPE_I: /* fallthrough */
      ++line;
      continue;
    }
    if (line[1] != (char *)FIO_CLI_PRINT__TYPE_I &&
        line[1] != (char *)FIO_CLI_PRINT_HEADER__TYPE_I)
      fio_cli_map_line2alias(*line);
    ++line;
  }

  /* parse existing arguments */

  while ((++parser.pos) < argc) {
    char const *value = NULL;
    cstr_s n = {.data = argv[parser.pos], .len = strlen(argv[parser.pos])};
    if (parser.pos + 1 < argc) {
      value = argv[parser.pos + 1];
    }
    const char *l = NULL;
    while (n.len &&
           !(l = fio_cli_hash_find(&fio_aliases, FIO_CLI_HASH_VAL(n), n))) {
      --n.len;
      value = n.data + n.len;
    }
    if (n.len && value && value[0] == '=') {
      ++value;
    }
    // fprintf(stderr, "Setting %.*s to %s\n", (int)n.len, n.data, value);
    fio_cli_set_arg(n, value, l, &parser);
  }

  /* Cleanup and save state for API */
  fio_cli_hash_free(&fio_aliases);
  fio_unnamed_count = parser.unnamed_count;
  /* test for required unnamed arguments */
  if (parser.unnamed_count < parser.unnamed_min)
    fio_cli_set_arg((cstr_s){.len = 0}, NULL, NULL, &parser);
}

void fio_cli_end(void) {
  fio_cli_hash_free(&fio_values);
  fio_cli_hash_free(&fio_aliases);
  fio_unnamed_count = 0;
}
/* *****************************************************************************
CLI Data Access
***************************************************************************** */

/** Returns the argument's value as a NUL terminated C String. */
char const *fio_cli_get(char const *name) {
  cstr_s n = {.data = name, .len = strlen(name)};
  if (!fio_cli_hash_count(&fio_values)) {
    return NULL;
  }
  char const *val = fio_cli_hash_find(&fio_values, FIO_CLI_HASH_VAL(n), n);
  return val;
}

/** Returns the argument's value as an integer. */
int fio_cli_get_i(char const *name) {
  char const *val = fio_cli_get(name);
  if (!val)
    return 0;
  int i = (int)fio_atol((char **)&val);
  return i;
}

/** Returns the number of unrecognized argument. */
unsigned int fio_cli_unnamed_count(void) {
  return (unsigned int)fio_unnamed_count;
}

/** Returns the unrecognized argument using a 0 based `index`. */
char const *fio_cli_unnamed(unsigned int index) {
  if (!fio_cli_hash_count(&fio_values) || !fio_unnamed_count) {
    return NULL;
  }
  cstr_s n = {.data = NULL, .len = index + 1};
  return fio_cli_hash_find(&fio_values, index + 1, n);
}

/**
 * Sets the argument's value as a NUL terminated C String (no copy!).
 *
 * Note: this does NOT copy the C strings to memory. Memory should be kept
 * alive until `fio_cli_end` is called.
 */
void fio_cli_set(char const *name, char const *value) {
  cstr_s n = (cstr_s){.data = name, .len = strlen(name)};
  fio_cli_hash_insert(&fio_values, FIO_CLI_HASH_VAL(n), n, value, NULL);
}
