/*
Copyright: Boaz Segev, 2018-2019
License: MIT
*/
#define INCLUDE_MUSTACHE_IMPLEMENTATION 1
#include <mustache_parser.h>

#include <fiobj_ary.h>
#include <fiobj_hash.h>
#include <fiobj_mustache.h>
#include <fiobj_str.h>

#ifndef FIO_IGNORE_MACRO
/**
 * This is used internally to ignore macros that shadow functions (avoiding
 * named arguments when required).
 */
#define FIO_IGNORE_MACRO
#endif

/**
 * Loads a mustache template, converting it into an opaque instruction array.
 *
 * Returns a pointer to the instruction array.
 *
 * The `folder` argument should contain the template's root folder which would
 * also be used to search for any required partial templates.
 *
 * The `filename` argument should contain the template's file name.
 */
mustache_s *fiobj_mustache_load(fio_str_info_s filename) {
  return mustache_load(.filename = filename.data, .filename_len = filename.len);
}

/**
 * Loads a mustache template, converting it into an opaque instruction array.
 *
 * Returns a pointer to the instruction array.
 *
 * The `folder` argument should contain the template's root folder which would
 * also be used to search for any required partial templates.
 *
 * The `filename` argument should contain the template's file name.
 */
mustache_s *fiobj_mustache_new FIO_IGNORE_MACRO(mustache_load_args_s args) {
  return mustache_load FIO_IGNORE_MACRO(args);
}

/** Free the mustache template */
void fiobj_mustache_free(mustache_s *mustache) { mustache_free(mustache); }

/**
 * Renders a template into an existing FIOBJ String (`dest`'s end), using the
 * information in the `data` object.
 *
 * Returns FIOBJ_INVALID if an error occured and a FIOBJ String on success.
 */
FIOBJ fiobj_mustache_build2(FIOBJ dest, mustache_s *mustache, FIOBJ data) {
  mustache_build(mustache, .udata1 = (void *)dest, .udata2 = (void *)data);
  return dest;
}

/**
 * Creates a FIOBJ String containing the rendered template using the information
 * in the `data` object.
 *
 * Returns FIOBJ_INVALID if an error occured and a FIOBJ String on success.
 */
FIOBJ fiobj_mustache_build(mustache_s *mustache, FIOBJ data) {
  if (!mustache)
    return FIOBJ_INVALID;
  return fiobj_mustache_build2(fiobj_str_buf(mustache->u.read_only.data_length),
                               mustache, data);
}

/* *****************************************************************************
Mustache Callbacks
***************************************************************************** */

static inline FIOBJ fiobj_mustache_find_obj_absolute(FIOBJ parent, FIOBJ key) {
  if (!FIOBJ_TYPE_IS(parent, FIOBJ_T_HASH))
    return FIOBJ_INVALID;
  FIOBJ o = FIOBJ_INVALID;
  o = fiobj_hash_get(parent, key);
  return o;
}

static inline FIOBJ fiobj_mustache_find_obj_tree(mustache_section_s *section,
                                                 const char *name,
                                                 uint32_t name_len) {
  FIOBJ key = fiobj_str_tmp();
  fiobj_str_write(key, name, name_len);
  do {
    FIOBJ tmp = fiobj_mustache_find_obj_absolute((FIOBJ)section->udata2, key);
    if (tmp != FIOBJ_INVALID) {
      return tmp;
    }
  } while ((section = mustache_section_parent(section)));
  return FIOBJ_INVALID;
}

static inline FIOBJ fiobj_mustache_find_obj(mustache_section_s *section,
                                            const char *name,
                                            uint32_t name_len) {
  FIOBJ tmp = fiobj_mustache_find_obj_tree(section, name, name_len);
  if (tmp != FIOBJ_INVALID)
    return tmp;
  /* interpolate sections... */
  uint32_t dot = 0;
  while (dot < name_len && name[dot] != '.')
    ++dot;
  if (dot == name_len)
    return FIOBJ_INVALID;
  tmp = fiobj_mustache_find_obj_tree(section, name, dot);
  if (!tmp) {
    return FIOBJ_INVALID;
  }
  ++dot;
  for (;;) {
    FIOBJ key = fiobj_str_tmp();
    fiobj_str_write(key, name + dot, name_len - dot);
    FIOBJ obj = fiobj_mustache_find_obj_absolute(tmp, key);
    if (obj != FIOBJ_INVALID)
      return obj;
    name += dot;
    name_len -= dot;
    dot = 0;
    while (dot < name_len && name[dot] != '.')
      ++dot;
    if (dot == name_len) {
      return FIOBJ_INVALID;
    }
    key = fiobj_str_tmp();
    fiobj_str_write(key, name, dot);
    tmp = fiobj_mustache_find_obj_absolute(tmp, key);
    if (tmp == FIOBJ_INVALID)
      return FIOBJ_INVALID;
    ++dot;
  }
}

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
 */
static int mustache_on_arg(mustache_section_s *section, const char *name,
                           uint32_t name_len, unsigned char escape) {
  FIOBJ o = fiobj_mustache_find_obj(section, name, name_len);
  if (!o)
    return 0;
  fio_str_info_s i = fiobj_obj2cstr(o);
  if (!i.len)
    return 0;
  return mustache_write_text(section, i.data, i.len, escape);
}

/**
 * Called when simple template text (string) is detected.
 *
 * A conforming implementation will output data as a string (no escaping).
 */
static int mustache_on_text(mustache_section_s *section, const char *data,
                            uint32_t data_len) {
  FIOBJ dest = (FIOBJ)section->udata1;
  fiobj_str_write(dest, data, data_len);
  return 0;
}

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
 */
static int32_t mustache_on_section_test(mustache_section_s *section,
                                        const char *name, uint32_t name_len,
                                        uint8_t callable) {
  FIOBJ o = fiobj_mustache_find_obj(section, name, name_len);
  if (!o || FIOBJ_TYPE_IS(o, FIOBJ_T_FALSE))
    return 0;
  if (FIOBJ_TYPE_IS(o, FIOBJ_T_ARRAY))
    return fiobj_ary_count(o);
  return 1;
  (void)callable; /* FIOBJ doesn't support lambdas */
}

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
 */
static int mustache_on_section_start(mustache_section_s *section,
                                     char const *name, uint32_t name_len,
                                     uint32_t index) {
  FIOBJ o = fiobj_mustache_find_obj(section, name, name_len);
  if (!o)
    return -1;
  if (FIOBJ_TYPE_IS(o, FIOBJ_T_ARRAY))
    section->udata2 = (void *)fiobj_ary_index(o, index);
  else
    section->udata2 = (void *)o;
  return 0;
}

/**
 * Called for cleanup in case of error.
 */
static void mustache_on_formatting_error(void *udata1, void *udata2) {
  (void)udata1;
  (void)udata2;
}

/* *****************************************************************************
Testing
***************************************************************************** */

#if DEBUG
static inline void mustache_save2file(char const *filename, char const *data,
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

void fiobj_mustache_test(void) {
#define TEST_ASSERT(cond, ...)                                                 \
  if (!(cond)) {                                                               \
    fprintf(stderr, "* " __VA_ARGS__);                                         \
    fprintf(stderr, "\n !!! Testing failed !!!\n");                            \
    exit(-1);                                                                  \
  }

  char const *template =
      "{{=<< >>=}}* Users:\r\n<<#users>><<id>>. <<& name>> "
      "(<<name>>)\r\n<</users>>\r\nNested: <<& nested.item >>.";
  char const *template_name = "mustache_test_template.mustache";
  mustache_save2file(template_name, template, strlen(template));
  mustache_s *m =
      fiobj_mustache_load((fio_str_info_s){.data = (char *)template_name});
  unlink(template_name);
  TEST_ASSERT(m, "fiobj_mustache_load failed.\n");
  FIOBJ data = fiobj_hash_new();
  FIOBJ key = fiobj_str_new("users", 5);
  FIOBJ ary = fiobj_ary_new2(4);
  fiobj_hash_set(data, key, ary);
  fiobj_free(key);
  for (int i = 0; i < 4; ++i) {
    FIOBJ id = fiobj_str_buf(4);
    fiobj_str_write_i(id, i);
    FIOBJ name = fiobj_str_buf(4);
    fiobj_str_write(name, "User ", 5);
    fiobj_str_write_i(name, i);
    FIOBJ usr = fiobj_hash_new2(2);
    key = fiobj_str_new("id", 2);
    fiobj_hash_set(usr, key, id);
    fiobj_free(key);
    key = fiobj_str_new("name", 4);
    fiobj_hash_set(usr, key, name);
    fiobj_free(key);
    fiobj_ary_push(ary, usr);
  }
  key = fiobj_str_new("nested", 6);
  ary = fiobj_hash_new2(2);
  fiobj_hash_set(data, key, ary);
  fiobj_free(key);
  key = fiobj_str_new("item", 4);
  fiobj_hash_set(ary, key, fiobj_str_new("dot notation success", 20));
  fiobj_free(key);
  key = fiobj_mustache_build(m, data);
  fiobj_free(data);
  TEST_ASSERT(key, "fiobj_mustache_build failed!\n");
  fprintf(stderr, "%s\n", fiobj_obj2cstr(key).data);
  fiobj_free(key);
  fiobj_mustache_free(m);
}

#endif
