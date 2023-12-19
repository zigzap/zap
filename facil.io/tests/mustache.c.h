#include "fio.h"

#define INCLUDE_MUSTACHE_IMPLEMENTATION 1
#include "mustache_parser.h"

static size_t callback_count = 0;

static struct {
  enum cb_type_e {
    CB_ERROR,
    CB_ON_TEXT,
    CB_ON_ARG,
    CB_ON_ARG_UNESCAPE,
    CB_ON_TEST,
    CB_ON_START,
  } cb_type;
  void *udata1;
} callback_expected[] = {
    {CB_ON_TEXT, (void *)0},         {CB_ON_TEST, (void *)0},
    {CB_ON_START, (void *)0},        {CB_ON_ARG, (void *)1},
    {CB_ON_START, (void *)0},        {CB_ON_ARG, (void *)1},
    {CB_ON_ARG_UNESCAPE, (void *)0}, {CB_ON_ARG_UNESCAPE, (void *)0},
    {CB_ON_TEST, (void *)0},         {CB_ON_START, (void *)0},
    {CB_ON_ARG_UNESCAPE, (void *)1}, {CB_ON_ARG_UNESCAPE, (void *)1},
    {CB_ON_TEST, (void *)1},         {CB_ERROR, NULL},
};

static const size_t callback_max =
    sizeof(callback_expected) / sizeof(callback_expected[0]);

static void mustache_test_callback(mustache_section_s *section,
                                   enum cb_type_e expected) {
  switch (expected) {
  case CB_ON_TEXT:
    fprintf(stderr, "* mustache callback for text detected.\n");
    break;
  case CB_ON_ARG:
    fprintf(stderr, "* mustache callback for argument detected.\n");
    break;
  case CB_ON_ARG_UNESCAPE:
    fprintf(stderr, "* mustache callback for unescaped argument detected.\n");
    break;
  case CB_ON_TEST: {
    size_t len;
    const char *txt = mustache_section_text(section, &len);
    if (txt)
      fprintf(stderr,
              "* mustache callback for section testing detected. section "
              "string:\n%.*s\n",
              (int)len, txt);
    else
      fprintf(stderr, "* mustache callback for section testing detected (no "
                      "string data available)\n");
  } break;
  case CB_ON_START:
    fprintf(stderr, "* mustache callback for section start detected.\n");
    break;
  case CB_ERROR:
    fprintf(stderr, "* mustache callback for ERROR detected.\n");
    break;
  }
  if (callback_expected[callback_count].cb_type == CB_ERROR) {
    fprintf(stderr, "FAILED: mustache callback count overflow\n");
    exit(-1);
  }

  if (callback_expected[callback_count].cb_type != expected) {
    fprintf(stderr,
            "FAILED: mustache callback type mismatch (count: %zu, expected %u, "
            "got %u)\n",
            callback_count, callback_expected[callback_count].cb_type,
            expected);
    exit(-1);
  }
  if (callback_expected[callback_count].udata1 != section->udata1) {
    fprintf(stderr,
            "FAILED: mustache callback udata1 mismatch (count: %zu, expected "
            "%p, got %p)\n",
            callback_count, callback_expected[callback_count].udata1,
            section->udata1);
    exit(-1);
  }
  ++callback_count;
}

static int mustache_on_arg(mustache_section_s *section, const char *name,
                           uint32_t name_len, unsigned char escape) {
  mustache_test_callback(section, escape ? CB_ON_ARG : CB_ON_ARG_UNESCAPE);
  (void)name;
  (void)name_len;
  return 0;
}

static int mustache_on_text(mustache_section_s *section, const char *data,
                            uint32_t data_len) {
  mustache_test_callback(section, CB_ON_TEXT);
  (void)data;
  (void)data_len;
  return 0;
}

static int32_t mustache_on_section_test(mustache_section_s *section,
                                        const char *name, uint32_t name_len,
                                        uint8_t callable) {
  fprintf(stderr, "* mustache testing section %.*s\n", (int)name_len, name);
  mustache_test_callback(section, CB_ON_TEST);
  (void)name;
  (void)name_len;
  static size_t ret = 0;
  switch (ret++) {
  case 0:
    return 2;
  case 1:
    return 0;
  default:
    return 1;
  }
  (void)callable;
}

static int mustache_on_section_start(mustache_section_s *section,
                                     char const *name, uint32_t name_len,
                                     uint32_t index) {
  fprintf(stderr, "* mustache entering section %.*s\n", (int)name_len, name);
  mustache_test_callback(section, CB_ON_START);
  section->udata1 = (void *)((uintptr_t)section->udata1 + 1);
  (void)index;
  (void)name;
  (void)name_len;
  return 0;
}

static void mustache_on_formatting_error(void *udata1, void *udata2) {
  FIO_LOG_ERROR("mustache formatting error.");
  (void)udata1;
  (void)udata2;
}

static inline void save2file(char const *filename, char const *data,
                             size_t length) {
  int fd = open(filename, O_CREAT | O_RDWR, 0);
  if (fd == -1) {
    perror("Couldn't open / create file for template testing");
    exit(-1);
  }
  fchmod(fd, 0777);
  if (pwrite(fd, data, length, 0) != (ssize_t)length) {
    perror("Mustache template write error");
    exit(-1);
  }
  close(fd);
}

static inline void mustache_print_instructions(mustache_s *m) {
  mustache__instruction_s *ary = (mustache__instruction_s *)(m + 1);
  for (uint32_t i = 0; i < m->u.read_only.intruction_count; ++i) {
    char *name = NULL;
    switch (ary[i].instruction) {
    case MUSTACHE_WRITE_TEXT:
      name = "MUSTACHE_WRITE_TEXT";
      break;
    case MUSTACHE_WRITE_ARG:
      name = "MUSTACHE_WRITE_ARG";
      break;
    case MUSTACHE_WRITE_ARG_UNESCAPED:
      name = "MUSTACHE_WRITE_ARG_UNESCAPED";
      break;
    case MUSTACHE_SECTION_START:
      name = "MUSTACHE_SECTION_START";
      break;
    case MUSTACHE_SECTION_START_INV:
      name = "MUSTACHE_SECTION_START_INV";
      break;
    case MUSTACHE_SECTION_END:
      name = "MUSTACHE_SECTION_END";
      break;
    case MUSTACHE_SECTION_GOTO:
      name = "MUSTACHE_SECTION_GOTO";
      break;
    case MUSTACHE_PADDING_PUSH:
      name = "MUSTACHE_PADDING_PUSH";
      break;
    case MUSTACHE_PADDING_POP:
      name = "MUSTACHE_PADDING_POP";
      break;
    case MUSTACHE_PADDING_WRITE:
      name = "MUSTACHE_PADDING_WRITE";
      break;
    default:
      name = "UNKNOWN!!!";
      break;
    }
    fprintf(stderr, "[%u] %s, start: %u, len %u\n", i, name,
            ary[i].data.name_pos, ary[i].data.name_len);
  }
}

void mustache_test(void) {
  fprintf(stderr, "=== Testing Mustache parser (mustache_parser.h)\n");
  char const *template =
      "Hi there{{#user}}{{name}}{{/user}}{{> mustache_test_partial }}";
  char const *partial = "{{& raw1}}{{{raw2}}}{{^negative}}"
                        "{{> mustache_test_partial }}{{=<< >>=}}<</negative>>";
  char const *partial2 = "{{& raw1}}{{{raw2}}}{{^negative}}"
                         "{{=<< >>=}}<</negative>>";
  char const *template_name = "mustache_test_template.mustache";
  char const *partial_name = "mustache_test_partial.mustache";
  save2file(template_name, template, strlen(template));
  mustache_error_en err = MUSTACHE_OK;
  mustache_s *m;
  m = mustache_load(.filename = template_name, .err = &err);
  if (m) {
    unlink(template_name);
    FIO_ASSERT(!m,
               "Mustache template loading should have failed without partial "
               "(err = %u)\n",
               err);
  }
  save2file(partial_name, partial, strlen(partial));

  m = mustache_load(.filename = template_name, .err = &err);
  if (!m) {
    unlink(template_name);
    unlink(partial_name);
    FIO_ASSERT(m, "Mustache template loading from file failed with error %u\n",
               err);
  }
  mustache_free(m);
  m = mustache_load(.data = partial2, .data_len = strlen(partial2),
                    .err = &err);
  if (!m) {
    unlink(template_name);
    unlink(partial_name);
    FIO_ASSERT(
        m, "Mustache template loading partial as data failed with error %u\n",
        err);
  }
  mustache_free(m);
  m = mustache_load(.filename = template_name, .data = template,
                    .data_len = strlen(template), .err = &err);
  unlink(template_name);
  unlink(partial_name);

  uint32_t expected[] = {
      MUSTACHE_SECTION_START,       MUSTACHE_WRITE_TEXT,
      MUSTACHE_SECTION_START,       MUSTACHE_WRITE_ARG,
      MUSTACHE_SECTION_END,         MUSTACHE_SECTION_START,
      MUSTACHE_WRITE_ARG_UNESCAPED, MUSTACHE_WRITE_ARG_UNESCAPED,
      MUSTACHE_SECTION_START_INV,   MUSTACHE_SECTION_GOTO,
      MUSTACHE_SECTION_END,         MUSTACHE_SECTION_END,
      MUSTACHE_SECTION_END,
  };

  FIO_ASSERT(m, "Mustache template loading failed with error %u\n", err);

  fprintf(stderr, "* template loaded, testing template instruction array.\n");
  mustache_print_instructions(m);
  mustache__instruction_s *ary = (mustache__instruction_s *)(m + 1);

  FIO_ASSERT(m->u.read_only.intruction_count == 13,
             "Mustache template instruction count error %u\n",
             m->u.read_only.intruction_count);

  for (uint16_t i = 0; i < 13; ++i) {
    FIO_ASSERT(ary[i].instruction == expected[i],
               "Mustache instraction[%u] error, type %u != %u\n", i,
               ary[0].instruction, expected[i]);
  }
  fprintf(stderr,
          "* template loaded, testing template build callbacks for data.\n");
  mustache_build(m, .udata1 = NULL, .err = &err);
  FIO_ASSERT(!err, "Mustache template building template failed with error %u\n",
             err);
  FIO_ASSERT(callback_count + 1 == callback_max,
             "Callback count error %zu != %zu", callback_count + 1,
             callback_max);
  FIO_ASSERT(callback_expected[callback_count].cb_type == CB_ERROR,
             "Callback type error on finish");
  /* cleanup */
  mustache_free(m);
  fprintf(stderr, "* passed.\n");
}
