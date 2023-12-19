/*
Copyright: Boaz Segev, 2018-2019
License: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#ifndef H_FIO_TLS

/**
 * This is an SSL/TLS extension for the facil.io library.
 */
#define H_FIO_TLS

#include <stdint.h>

#ifndef FIO_TLS_PRINT_SECRET
/* if true, the master key secret should be printed using FIO_LOG_DEBUG */
#define FIO_TLS_PRINT_SECRET 0
#endif

/** An opaque type used for the SSL/TLS functions. */
typedef struct fio_tls_s fio_tls_s;

/**
 * Creates a new SSL/TLS context / settings object with a default certificate
 * (if any).
 *
 * If no server name is provided and no private key and public certificate are
 * provided, an empty TLS object will be created, (maybe okay for clients).
 *
 *      fio_tls_s * tls = fio_tls_new("www.example.com",
 *                                    "public_key.pem",
 *                                    "private_key.pem", NULL );
 */
fio_tls_s *fio_tls_new(const char *server_name, const char *public_cert_file,
                       const char *private_key_file, const char *pk_password);

/**
 * Adds a certificate a new SSL/TLS context / settings object (SNI support).
 *
 *      fio_tls_cert_add(tls, "www.example.com",
 *                            "public_key.pem",
 *                            "private_key.pem", NULL );
 */
void fio_tls_cert_add(fio_tls_s *, const char *server_name,
                      const char *public_cert_file,
                      const char *private_key_file, const char *pk_password);

/**
 * Adds an ALPN protocol callback to the SSL/TLS context.
 *
 * The first protocol added will act as the default protocol to be selected.
 *
 * The `on_selected` callback should accept the `uuid`, the user data pointer
 * passed to either `fio_tls_accept` or `fio_tls_connect` (here:
 * `udata_connetcion`) and the user data pointer passed to the
 * `fio_tls_alpn_add` function (`udata_tls`).
 *
 * The `on_cleanup` callback will be called when the TLS object is destroyed (or
 * `fio_tls_alpn_add` is called again with the same protocol name). The
 * `udata_tls` argument will be passed along, as is, to the callback (if set).
 *
 * Except for the `tls` and `protocol_name` arguments, all arguments can be
 * NULL.
 */
void fio_tls_alpn_add(fio_tls_s *tls, const char *protocol_name,
                      void (*on_selected)(intptr_t uuid, void *udata_connection,
                                          void *udata_tls),
                      void *udata_tls, void (*on_cleanup)(void *udata_tls));

/**
 * Returns the number of registered ALPN protocol names.
 *
 * This could be used when deciding if protocol selection should be delegated to
 * the ALPN mechanism, or whether a protocol should be immediately assigned.
 *
 * If no ALPN protocols are registered, zero (0) is returned.
 */
uintptr_t fio_tls_alpn_count(fio_tls_s *tls);

/**
 * Adds a certificate to the "trust" list, which automatically adds a peer
 * verification requirement.
 *
 * Note, when the fio_tls_s object is used for server connections, this will
 * limit connections to clients that connect using a trusted certificate.
 *
 *      fio_tls_trust(tls, "google-ca.pem" );
 */
void fio_tls_trust(fio_tls_s *, const char *public_cert_file);

/**
 * Establishes an SSL/TLS connection as an SSL/TLS Server, using the specified
 * context / settings object.
 *
 * The `uuid` should be a socket UUID that is already connected to a peer (i.e.,
 * the result of `fio_accept`).
 *
 * The `udata` is an opaque user data pointer that is passed along to the
 * protocol selected (if any protocols were added using `fio_tls_alpn_add`).
 */
void fio_tls_accept(intptr_t uuid, fio_tls_s *tls, void *udata);

/**
 * Establishes an SSL/TLS connection as an SSL/TLS Client, using the specified
 * context / settings object.
 *
 * The `uuid` should be a socket UUID that is already connected to a peer (i.e.,
 * one received by a `fio_connect` specified callback `on_connect`).
 *
 * The `udata` is an opaque user data pointer that is passed along to the
 * protocol selected (if any protocols were added using `fio_tls_alpn_add`).
 */
void fio_tls_connect(intptr_t uuid, fio_tls_s *tls, void *udata);

/**
 * Increase the reference count for the TLS object.
 *
 * Decrease with `fio_tls_destroy`.
 */
void fio_tls_dup(fio_tls_s *tls);

/**
 * Destroys the SSL/TLS context / settings object and frees any related
 * resources / memory.
 */
void fio_tls_destroy(fio_tls_s *tls);

#endif
