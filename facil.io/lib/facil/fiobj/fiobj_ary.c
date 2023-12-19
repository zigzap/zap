/*
Copyright: Boaz Segev, 2017-2019
License: MIT
*/
#include <fio.h>
#include <fiobject.h>

#define FIO_ARY_NAME fio_ary__
#define FIO_ARY_TYPE FIOBJ
#define FIO_ARY_TYPE_INVALID FIOBJ_INVALID
#define FIO_ARY_TYPE_COMPARE(a, b) (fiobj_iseq((a), (b)))
#define FIO_ARY_INVALID FIOBJ_INVALID
#include <fio.h>

#include <assert.h>

/* *****************************************************************************
Array Type
***************************************************************************** */

typedef struct {
  fiobj_object_header_s head;
  fio_ary___s ary;
} fiobj_ary_s;

#define obj2ary(o) ((fiobj_ary_s *)(o))

/* *****************************************************************************
VTable
***************************************************************************** */

static void fiobj_ary_dealloc(FIOBJ o, void (*task)(FIOBJ, void *), void *arg) {
  FIO_ARY_FOR((&obj2ary(o)->ary), i) { task(*i, arg); }
  fio_ary___free(&obj2ary(o)->ary);
  fio_free(FIOBJ2PTR(o));
}

static size_t fiobj_ary_each1(FIOBJ o, size_t start_at,
                              int (*task)(FIOBJ obj, void *arg), void *arg) {
  return fio_ary___each(&obj2ary(o)->ary, start_at, task, arg);
}

static size_t fiobj_ary_is_eq(const FIOBJ self, const FIOBJ other) {
  fio_ary___s *a = &obj2ary(self)->ary;
  fio_ary___s *b = &obj2ary(other)->ary;
  if (fio_ary___count(a) != fio_ary___count(b))
    return 0;
  return 1;
}

/** Returns the number of elements in the Array. */
size_t fiobj_ary_count(const FIOBJ ary) {
  assert(FIOBJ_TYPE_IS(ary, FIOBJ_T_ARRAY));
  return fio_ary___count(&obj2ary(ary)->ary);
}

static size_t fiobj_ary_is_true(const FIOBJ ary) {
  return fiobj_ary_count(ary) > 0;
}

fio_str_info_s fiobject___noop_to_str(const FIOBJ o);
intptr_t fiobject___noop_to_i(const FIOBJ o);
double fiobject___noop_to_f(const FIOBJ o);

const fiobj_object_vtable_s FIOBJECT_VTABLE_ARRAY = {
    .class_name = "Array",
    .dealloc = fiobj_ary_dealloc,
    .is_eq = fiobj_ary_is_eq,
    .is_true = fiobj_ary_is_true,
    .count = fiobj_ary_count,
    .each = fiobj_ary_each1,
    .to_i = fiobject___noop_to_i,
    .to_f = fiobject___noop_to_f,
    .to_str = fiobject___noop_to_str,
};

/* *****************************************************************************
Allocation
***************************************************************************** */

static inline FIOBJ fiobj_ary_alloc(size_t capa) {
  fiobj_ary_s *ary = fio_malloc(sizeof(*ary));
  if (!ary) {
    perror("ERROR: fiobj array couldn't allocate memory");
    exit(errno);
  }
  *ary = (fiobj_ary_s){
      .head =
          {
              .ref = 1,
              .type = FIOBJ_T_ARRAY,
          },
  };
  if (capa)
    fio_ary_____require_on_top(&ary->ary, capa);
  return (FIOBJ)ary;
}

/** Creates a mutable empty Array object. Use `fiobj_free` when done. */
FIOBJ fiobj_ary_new(void) { return fiobj_ary_alloc(0); }
/** Creates a mutable empty Array object with the requested capacity. */
FIOBJ fiobj_ary_new2(size_t capa) { return fiobj_ary_alloc(capa); }

/* *****************************************************************************
Array direct entry access API
***************************************************************************** */

/** Returns the current, temporary, array capacity (it's dynamic). */
size_t fiobj_ary_capa(FIOBJ ary) {
  assert(ary && FIOBJ_TYPE_IS(ary, FIOBJ_T_ARRAY));
  return fio_ary___capa(&obj2ary(ary)->ary);
}

/**
 * Returns a TEMPORARY pointer to the beginning of the array.
 *
 * This pointer can be used for sorting and other direct access operations as
 * long as no other actions (insertion/deletion) are performed on the array.
 */
FIOBJ *fiobj_ary2ptr(FIOBJ ary) {
  assert(ary && FIOBJ_TYPE_IS(ary, FIOBJ_T_ARRAY));
  return (FIOBJ *)(obj2ary(ary)->ary.arry + obj2ary(ary)->ary.start);
}

/**
 * Returns a temporary object owned by the Array.
 *
 * Negative values are retrieved from the end of the array. i.e., `-1`
 * is the last item.
 */
FIOBJ fiobj_ary_index(FIOBJ ary, int64_t pos) {
  assert(ary && FIOBJ_TYPE_IS(ary, FIOBJ_T_ARRAY));
  return fio_ary___get(&obj2ary(ary)->ary, pos);
}

/**
 * Sets an object at the requested position.
 */
void fiobj_ary_set(FIOBJ ary, FIOBJ obj, int64_t pos) {
  assert(ary && FIOBJ_TYPE_IS(ary, FIOBJ_T_ARRAY));
  FIOBJ old = FIOBJ_INVALID;
  fio_ary___set(&obj2ary(ary)->ary, pos, obj, &old);
  fiobj_free(old);
}

/* *****************************************************************************
Array push / shift API
***************************************************************************** */

/**
 * Pushes an object to the end of the Array.
 */
void fiobj_ary_push(FIOBJ ary, FIOBJ obj) {
  assert(ary && FIOBJ_TYPE_IS(ary, FIOBJ_T_ARRAY));
  fio_ary___push(&obj2ary(ary)->ary, obj);
}

/** Pops an object from the end of the Array. */
FIOBJ fiobj_ary_pop(FIOBJ ary) {
  assert(ary && FIOBJ_TYPE_IS(ary, FIOBJ_T_ARRAY));
  FIOBJ ret = FIOBJ_INVALID;
  fio_ary___pop(&obj2ary(ary)->ary, &ret);
  return ret;
}

/**
 * Unshifts an object to the beginning of the Array. This could be
 * expensive.
 */
void fiobj_ary_unshift(FIOBJ ary, FIOBJ obj) {
  assert(ary && FIOBJ_TYPE_IS(ary, FIOBJ_T_ARRAY));
  fio_ary___unshift(&obj2ary(ary)->ary, obj);
}

/** Shifts an object from the beginning of the Array. */
FIOBJ fiobj_ary_shift(FIOBJ ary) {
  assert(ary && FIOBJ_TYPE_IS(ary, FIOBJ_T_ARRAY));
  FIOBJ ret = FIOBJ_INVALID;
  fio_ary___shift(&obj2ary(ary)->ary, &ret);
  return ret;
}

/* *****************************************************************************
Array Find / Remove / Replace
***************************************************************************** */

/**
 * Replaces the object at a specific position, returning the old object -
 * remember to `fiobj_free` the old object.
 */
FIOBJ fiobj_ary_replace(FIOBJ ary, FIOBJ obj, int64_t pos) {
  assert(ary && FIOBJ_TYPE_IS(ary, FIOBJ_T_ARRAY));
  FIOBJ old = fiobj_ary_index(ary, pos);
  fiobj_ary_set(ary, obj, pos);
  return old;
}

/**
 * Finds the index of a specifide object (if any). Returns -1 if the object
 * isn't found.
 */
int64_t fiobj_ary_find(FIOBJ ary, FIOBJ data) {
  assert(ary && FIOBJ_TYPE_IS(ary, FIOBJ_T_ARRAY));
  return (int64_t)fio_ary___find(&obj2ary(ary)->ary, data);
}

/**
 * Removes the object at the index (if valid), changing the index of any
 * following objects.
 *
 * Returns 0 on success or -1 (if no object or out of bounds).
 */
int fiobj_ary_remove(FIOBJ ary, int64_t pos) {
  assert(ary && FIOBJ_TYPE_IS(ary, FIOBJ_T_ARRAY));
  FIOBJ old = FIOBJ_INVALID;
  if (fio_ary___remove(&obj2ary(ary)->ary, (intptr_t)pos, &old)) {
    return -1;
  }
  fiobj_free(old);
  return 0;
}

/**
 * Removes the first instance of an object from the Array (if any), changing the
 * index of any following objects.
 *
 * Returns 0 on success or -1 (if the object wasn't found).
 */
int fiobj_ary_remove2(FIOBJ ary, FIOBJ data) {
  assert(ary && FIOBJ_TYPE_IS(ary, FIOBJ_T_ARRAY));
  if (-1 == fio_ary___remove2(&obj2ary(ary)->ary, data, &data)) {
    return -1;
  }
  fiobj_free(data);
  return 0;
}

/* *****************************************************************************
Array compacting (untested)
***************************************************************************** */

/**
 * Removes any NULL *pointers* from an Array, keeping all Objects (including
 * explicit NULL objects) in the array.
 *
 * This action is O(n) where n in the length of the array.
 * It could get expensive.
 */
void fiobj_ary_compact(FIOBJ ary) {
  assert(ary && FIOBJ_TYPE_IS(ary, FIOBJ_T_ARRAY));
  fio_ary___compact(&obj2ary(ary)->ary);
}

/* *****************************************************************************
Simple Tests
***************************************************************************** */

#if DEBUG
void fiobj_test_array(void) {
  fprintf(stderr, "=== Testing Array\n");
#define TEST_ASSERT(cond, ...)                                                 \
  if (!(cond)) {                                                               \
    fprintf(stderr, "* " __VA_ARGS__);                                         \
    fprintf(stderr, "Testing failed.\n");                                      \
    exit(-1);                                                                  \
  }
  FIOBJ a = fiobj_ary_new2(4);
  TEST_ASSERT(FIOBJ_TYPE_IS(a, FIOBJ_T_ARRAY), "Array type isn't an array!\n");
  TEST_ASSERT(fiobj_ary_capa(a) > 4, "Array capacity ignored!\n");
  fiobj_ary_push(a, fiobj_null());
  TEST_ASSERT(fiobj_ary2ptr(a)[0] == fiobj_null(),
              "Array direct access failed!\n");
  fiobj_ary_push(a, fiobj_true());
  fiobj_ary_push(a, fiobj_false());
  TEST_ASSERT(fiobj_ary_count(a) == 3, "Array count isn't 3\n");
  fiobj_ary_set(a, fiobj_true(), 63);
  TEST_ASSERT(fiobj_ary_count(a) == 64, "Array count isn't 64 (%zu)\n",
              fiobj_ary_count(a));
  TEST_ASSERT(fiobj_ary_index(a, 0) == fiobj_null(),
              "Array index retrival error for fiobj_null\n");
  TEST_ASSERT(fiobj_ary_index(a, 1) == fiobj_true(),
              "Array index retrival error for fiobj_true\n");
  TEST_ASSERT(fiobj_ary_index(a, 2) == fiobj_false(),
              "Array index retrival error for fiobj_false\n");
  TEST_ASSERT(fiobj_ary_index(a, 3) == 0,
              "Array index retrival error for NULL\n");
  TEST_ASSERT(fiobj_ary_index(a, 63) == fiobj_true(),
              "Array index retrival error for index 63\n");
  TEST_ASSERT(fiobj_ary_index(a, -1) == fiobj_true(),
              "Array index retrival error for index -1\n");
  fiobj_ary_compact(a);
  TEST_ASSERT(fiobj_ary_index(a, -1) == fiobj_true(),
              "Array index retrival error for index -1\n");
  TEST_ASSERT(fiobj_ary_count(a) == 4, "Array compact error\n");
  fiobj_ary_unshift(a, fiobj_false());
  TEST_ASSERT(fiobj_ary_count(a) == 5, "Array unshift error\n");
  TEST_ASSERT(fiobj_ary_shift(a) == fiobj_false(), "Array shift value error\n");
  TEST_ASSERT(fiobj_ary_replace(a, fiobj_true(), -2) == fiobj_false(),
              "Array replace didn't return correct value\n");

  FIO_ARY_FOR(&obj2ary(a)->ary, pos) {
    if (*pos) {
      fprintf(stderr, "%lu) %s\n", pos - obj2ary(a)->ary.arry,
              fiobj_obj2cstr(*pos).data);
    }
  }

  TEST_ASSERT(fiobj_ary_index(a, -2) == fiobj_true(),
              "Array index retrival error for index -2 (should be true)\n");
  TEST_ASSERT(fiobj_ary_count(a) == 4, "Array size error\n");
  fiobj_ary_remove(a, -2);
  TEST_ASSERT(fiobj_ary_count(a) == 3, "Array remove error\n");

  FIO_ARY_FOR(&obj2ary(a)->ary, pos) {
    if (*pos) {
      fprintf(stderr, "%lu) %s\n", pos - obj2ary(a)->ary.arry,
              fiobj_obj2cstr(*pos).data);
    }
  }

  fiobj_ary_remove2(a, fiobj_true());
  TEST_ASSERT(fiobj_ary_count(a) == 2, "Array remove2 error\n");
  TEST_ASSERT(fiobj_ary_index(a, 0) == fiobj_null(),
              "Array index 0 should be null - %s\n",
              fiobj_obj2cstr(fiobj_ary_index(a, 0)).data);
  TEST_ASSERT(fiobj_ary_index(a, 1) == fiobj_true(),
              "Array index 0 should be true - %s\n",
              fiobj_obj2cstr(fiobj_ary_index(a, 0)).data);

  fiobj_free(a);
  fprintf(stderr, "* passed.\n");
}
#endif
