#include <fio.h>
#include <redis_engine.h>

static fio_lock_i global_lock = FIO_LOCK_INIT;

static void ask4data_callback(fio_pubsub_engine_s *e, FIOBJ reply,
                              void *udata) {

  if (udata != (void *)0x01)
    fprintf(stderr, "CRITICAL ERROR: redis callback udata mismatch (got %p)\n",
            udata);
  if (!FIOBJ_TYPE_IS(reply, FIOBJ_T_ARRAY)) {
    fprintf(stderr,
            "CRITICAL ERROR: redis callback return type mismatch (got %s)\n",
            fiobj_type_name(reply));
    return;
  }
  size_t count = fiobj_ary_count(reply);
  fprintf(stderr, "Redis command results (%zu):\n", count);
  for (size_t i = 0; i < count; ++i) {
    fprintf(stderr, "* %s\n", fiobj_obj2cstr(fiobj_ary_index(reply, i)).data);
  }
  kill(SIGINT, 0);
  (void)e;
}
static void ask4data(void *ignr) {
  FIOBJ data = fiobj_ary_new();
  fiobj_ary_push(data, fiobj_str_new("LRANGE", 6));
  fiobj_ary_push(data, fiobj_str_new("pids", 4));
  fiobj_ary_push(data, fiobj_num_new(0));
  fiobj_ary_push(data, fiobj_num_new(-1));

  (void)ignr;
  redis_engine_send(FIO_PUBSUB_DEFAULT, data, ask4data_callback, (void *)0x01);
  fiobj_free(data);
  fprintf(stderr, "* (%d) Asked redis for info.\n", getpid());
  (void)ignr;
}

static void after_fork(void *ignr) {
  if (fio_is_master()) {
    fio_trylock(&global_lock);
    return;
  }
  if (fio_trylock(&global_lock) == 0) {
    /* runs only once */
    fio_run_every(2000, 1, ask4data, NULL, NULL);
  }
  FIOBJ data = fiobj_ary_new();
  fiobj_ary_push(data, fiobj_str_new("LPUSH", 5));
  fiobj_ary_push(data, fiobj_str_new("pids", 4));
  if (0) {
    /* nested arrays... but lists can't contain them */
    FIOBJ tmp = fiobj_ary_new2(2);
    fiobj_ary_push(tmp, fiobj_str_new("worker pid", 10));
    fiobj_ary_push(tmp, fiobj_str_copy(fiobj_num_tmp(getpid())));
    fiobj_ary_push(data, tmp);
  } else {
    /* lists contain only Strings, so we need a string */
    fiobj_ary_push(data, fiobj_str_copy(fiobj_num_tmp(getpid())));
  }
  redis_engine_send(FIO_PUBSUB_DEFAULT, data, NULL, NULL);
  fiobj_free(data);
  fprintf(stderr, "* (%d) Sent info to redis.\n", getpid());
  (void)ignr;
}

static void start_shutdown(void *ignr) {
  if (fio_is_master())
    fio_stop();
  (void)ignr;
}

int main(void) {
  fio_pubsub_engine_s *r = redis_engine_create(.ping_interval = 1);
  FIO_PUBSUB_DEFAULT = r;
  fio_run_every(10000, 1, start_shutdown, NULL, NULL);
  fio_state_callback_add(FIO_CALL_AFTER_FORK, after_fork, NULL);
  fio_start(.workers = 4);
  redis_engine_destroy(r);
  return 0;
}
