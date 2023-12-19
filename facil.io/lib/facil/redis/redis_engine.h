/*
Copyright: Boaz segev, 2016-2019
License: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#ifndef H_REDIS_ENGINE_H
#define H_REDIS_ENGINE_H

#include <fio.h>
#include <fiobj.h>

/* support C++ */
#ifdef __cplusplus
extern "C" {
#endif

/** possible arguments for the `redis_engine_create` function call */
struct redis_engine_create_args {
  /** Redis server's address, defaults to localhost. */
  fio_str_info_s address;
  /** Redis server's port, defaults to 6379. */
  fio_str_info_s port;
  /** Redis server's password, if any. */
  fio_str_info_s auth;
  /** A `ping` will be sent every `ping_interval` interval or inactivity. */
  uint8_t ping_interval;
};

/**
 * See the {fio.h} file for documentation about engines.
 *
 * The engine is active only after facil.io starts running.
 *
 * A `ping` will be sent every `ping_interval` interval or inactivity. The
 * default value (0) will fallback to facil.io's maximum time of inactivity (5
 * minutes) before polling on the connection's protocol.
 *
 * function names speak for themselves ;-)
 *
 * Note: The Redis engine assumes it will stay alive until all the messages and
 * callbacks have been called (or facil.io exits)... If the engine is destroyed
 * midway, memory leaks might occur (and little puppies might cry).
 */
fio_pubsub_engine_s *redis_engine_create(struct redis_engine_create_args);
#define redis_engine_create(...)                                               \
  redis_engine_create((struct redis_engine_create_args){__VA_ARGS__})

/**
 * Sends a Redis command through the engine's connection.
 *
 * The response will be sent back using the optional callback. `udata` is passed
 * along untouched.
 *
 * The message will be resent on network failures, until a response validates
 * the fact that the command was sent (or the engine is destroyed).
 *
 * Note: NEVER call Pub/Sub commands using this function, as it will violate the
 * Redis connection's protocol (best case scenario, a disconnection will occur
 * before and messages are lost).
 */
intptr_t redis_engine_send(fio_pubsub_engine_s *engine, FIOBJ command,
                           void (*callback)(fio_pubsub_engine_s *e, FIOBJ reply,
                                            void *udata),
                           void *udata);

/**
 * See the {pubsub.h} file for documentation about engines.
 *
 * function names speak for themselves ;-)
 */
void redis_engine_destroy(fio_pubsub_engine_s *engine);

/* support C++ */
#ifdef __cplusplus
}
#endif

#endif /* H_REDIS_ENGINE_H */
