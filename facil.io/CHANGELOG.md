# Change Log

### v. 0.7.5 (2020-05-18)

**Security**: backport the 0.8.x HTTP/1.1 parser and it's security updates to the 0.7.x version branch. This fixes a request smuggling attack vector and Transfer Encoding attack vector that were exposed by Sam Sanoop from [the Snyk Security team (snyk.io)](https://snyk.io). The parser was updated to deal with these potential issues.

**Fix**: (`http`) fixes an issue with date calculation by backporting code from the 0.8.x branch.

**Fix**: (`fio`) call less signal handlers during shutdown.

### v. 0.7.4

**Fix**: (`http`) fixes an issue and improves support for `chunked` encoded payloads. Credit to Ian Ker-Seymer ( @ianks ) for exposing this, writing tests  (for the Ruby wrapper) and opening both the issue boazsegev/iodine#87 and the PR boazsegev/iodine#88.

**Fix**: (`http`) requests will fail when the path contains a dangling `?` (empty query). Credit to @adam12 for exposing this and opening issue boazsegev/iodine#86.

### v. 0.7.3

**Fix**: (`http`) fixes a security issue in the static file name resolution logic, where a maliciously encoded request could invoke an arbitrary response.

**Fix**: (`fio`) fixes an issue where setting a different value to `FIO_SLOWLORIS_LIMIT` was being ignored.

**Fix**: (`fio`, `fiobj`) improved C++ compatibility. Credit to Joey (@joeyhoek) for PR #76.

**Fix**: (`fio`) fixes an issue where timer cleanup wasn't performed after `fio_stop` (or SIGINT/SIGTERM). No a "clean slate" will be provided if `fio_start` is called more then once. Note: this may **break previous behavior**, which should be considered undocumented and unexpected behavior. (this fax **may** be deferred to version 0.8.x, still undecided). Credit to @fbrausse for opening issue #72. 

**Fix**: (`fio`) fixes an issue where timer cleanup would be performed after the `AT_EXIT` state callbacks. Now the timer cleanup callbacks will be performed **before** the `AT_EXIT` callback (as they should). (See issue #72).

**Fix**: (`fio`) fixes signal handler (re)establishment test to prevent recursive signal calling.

### v. 0.7.2

**Fix**: (`fio_tls`) fixes a memory leak in the trusted certificate chain. Credit to @fbrausse for opening PR #71.

**Fix**: (`fio_tls`) fixes compilation / linking flags (including a bug caused by the `gcc` optimizer `-fipa-icf`) and improves support for OpenSSL using `pkg-config`. Credit to @fbrausse for PR #71.

**Fix**: (`http1`) fixes a race-condition between the `on_ready` and `on_data` events, that could result in the `on_data` event being called twice instead of once (only possible with some clients). On multi-threaded workers, this could result in the CPU spinning while the task lock remains busy. Credit to Néstor Coppi (@Shelvak) for exposing the issue and providing an example application with detailed reports. Issue #75.

### v. 0.7.1

**Security**: a heap-overflow vulnerability was fixed in the WebSocket parser, which could have been triggered by a maliciously crafted message-header. Credit to Dane (4cad@silvertoque) for exposing this issue and providing a Python script demonstrating the attack. 

### v. 0.7.0

Stable API release. Future API updates will be wait for the 0.8.x release.

**Fix**: (`fio`, `fiobj`) fixed some `gcc` and `clang` compatibility issues and warnings.

**Fix**: (`http`) fixed HTTP date format to force the day of the month to use two digits. Credit to @ianks (Ian Ker-Seymer) for exposing this issue (iodine#64).

**Compatibility**: (`http`) updated time-zone compile-time tests with a safer fall-back.

### v. 0.7.0.beta8

**Security**: (`fio`) Slowloris mitigation is now part of the core library, where `FIO_SLOWLORIS_LIMIT` pending calls to `write` (currently 1,024 backlogged calls) will flag the connection as an attacker and either close the connection or ignore it. This protocol independent approach improves security.

**Security**: (`http`) HTTP/1.1 client throttling - new requests will not be consumed until pending responses were sent. Since HTTP/1.1 is a response-request protocol, this protocol specific approach should protect the HTTP application against slow clients.

**Fix**: (`fio`) fixed fallback implementation for `fio_atomic_xchange` when missing atomic primitives in compiler (older compilers). Credit to @Low-power for identifying and fixing the issue (PR #55).

**Fix**: (`fio`) fixed a possible unreleased lock when a memory allocation failed (no memory in the system). Credit to @Low-power for identifying and fixing the issue (PR #54).

**Fix**: (`fio`) fixed the `fio_sock_sendfile_from_fd` fall-back for a missing `sendfile`. Credit to @Low-power for identifying and fixing the typo (PR #49).

**Fix**: (`fio`) fixed `fio_pending` not decrementing packet count before reaching zero.

**Fix**: (`fio`) fixed logging message for overflowing log messages. Credit to @weskerfoot (Wesley Kerfoot) and @adam12 (Adam Daniels) for exposing the issue (issue iodine/#56).

**Fix**: (`fio`, `fio_risky_hash`) Florian Weber (@Florianjw) [exposed a byte ordering error (last 7 byte reading order) and took time challenge the algorithm](https://www.reddit.com/r/crypto/comments/9kk5gl/break_my_ciphercollectionpost/eekxw2f/?context=3). The exposed errors were fixed and the exposed a possible attack on RiskyHash using a variation on a Meet-In-The-Middle attack, written by Hening Makholm (@hmakholm). This prompted an update and fixes to the function.

**Fix**: (`fio`) fixed `fio_str_resize` where data might be lost if data was written beyond the current size and the requested size is larger then the String's capacity (i.e., when `fio_str_resize` is (mis)used as an alternative to `fio_str_capa_assert`).

**Fix**: (`json` / `redis`) fixed JSON formatting error caused by buffer reallocation when multiple (more then 48) escape sequences were detected. This issue also effected the Redis command callback handler (which was using JSON for IPC).

**Fix**: (`redis`) fixed a potential double `free` call.

**Fix**: (`redis`) fixed a recursive endless loop when converting nested Hash Tables to Redis objects (which normally wouldn't happen anyway, since they would be processed as JSON).

**Fix**: (`redis`) fixed Redis reconnection. Address and port data was mistakingly written at the wrong address, causing it to be overwritten by incoming (non-pub/sub) data.

**Fix**: (`redis`) fixed a race condition in the Redis reconnection logic which might have caused more then a single pub/sub connection to be established and the first pending command to be sent again.

**Fix**: (`fio`) fix capacity maximization log to accommodate issues where `getrlimit` would return a `rlim_max` that's too high for `rlim_cur` (macOS).

**Fix**: (`fio`) fix uninitialized `kqueue` message in `fio_poll_remove_fd`.

**Fix**: (`http`) possible fix for `http_connect`, where `host` header length might have been left uninitialized, resulting in possible errors.

**Fix**: (`fio`) fixed logging error message for long error messages.

**Update**: (`fio` / `makefile`) improved detection for polling system call, `sendfile`, etc'.

**Update**: (`fio`) improved signal handling. Signal handling now propagates to pre-existing signal handlers. In addition, the `fio_signal_handler_reset` function was made public, allowing facil.io signal handlers to be removed immediately following startup (using `fio_state_callback_add` with `FIO_CALL_PRE_START` to call `fio_signal_handler_reset`).

**Update**: (`fio`) improved pub/sub memory usage to minimize message copying in cluster mode (same memory is used for IPC and local-process message publishing).

**Update**: (`fio`) updated the non-cryptographic PRG algorithm for performance and speed. Now the `fio_rand` functions are modeled after the `xoroshiro128+` algorithm, with an automated re-seeding counter based on RiskyHash. This should improve performance for non cryptographic random requirements.

**Compatibility**: (`fio`) mitigate undefined MAP_ANONYMOUS on MacOS <= 10.10. Credit to @xicreative (Evan Pavlica) for iodine/PR#61.

**Compatibility**: (`fio`) various Solaris OS compatibility patches, courtesy of @Low-power (PR #52, #53).

### v. 0.7.0.beta7

**BREAK**: (`fio_tls`) breaking API changes to the SSL/TLS API... I know, I'm sorry, especially since there's a small and misleading change in argument ordering for `fio_tls_cert_add` and `fio_tls_new`... but if we don't fix the API now, before the 0.7.0 release, bad design might ruin our Wednesday meditation for all eternity.

**BREAK**: (`http`) breaking API changes to `http_connect` were required in order to support Unix Socket connections in client mode.

**Deprecation**: (`http`) deprecating the `http_url_parse` in favor of `fio_url_parse` (moved the function to the core library and rewrote it in part).

**Security**: facil.io hash maps now limit the number of full-collisions allowed in a hash map. This mitigates the effects of hash flooding attacks. As a side effect, hash maps that are under attack might return false results for collision objects.

**Fix**: (`websocket`) fixed an issue with the WebSocket parser where network byte order for long message lengths wasn't always respected and integer bit size was wrong for larger payloads. Credit to Marouane Elmidaoui (@moxgeek) for exposing the issue.

**Fix**: (`http`) fixed `udata` in WebSocket client callback for failed WebSocket client connections.

**Fix**: (`fio`) logging message when listening to a Unix Socket.

**Fix**: (`fio`) numerous minor design fixes, such as Big-Endian string memory access, allowing `fio.h` to be used as a header only library (requires `FIO_FORCE_MALLOC`) and other adjustments.

**Fix**: (`fio`) fixed unaligned memory access in SipHash implementation and added secret randomization for each application restart.

**Fix**: (`redis`) fixed an issue where destroying the Redis engine and exiting pre-maturely, before running facio.io (`fio_start`), will cause a segmentation fault during cleanup.

**Update**: (`fio`) added Risky Hash, for fast hashing of safe data. This is a fast hashing function (about twice as fast as the default SipHash1-3 secure function) that wasn't tested for security. For this reason it should be limited to internal / safe data, such as CLI argument names.

### v. 0.7.0.beta6

**BREAK**: (`fio_tls`) breaking API changes to the SSL/TLS API, adding support for password protected private key files. *Note*: The TLS API is still fragile and should only be considered stable once version 0.7.0 is released with SSL/TLS support.

**Security** / **Fix**: (`http`) fixed an issue with the HTTP/1.1 parser, where maliciously crafted white-space data could cause a segmentation fault, resulting in a potential DoS.

**Fix**: (`fio`) fixed an issue exposed by implementing the TLS layer, where the highet `fd` for a connection that wasn't assigned a `protocol_s` object immediately after the connection was opened, might avoid timeout review or avoid cleanup during shutdown (which will be marked as a memory leak).

**Update**: (`fio_tls`) added experimental support for OpenSSL. This was only partially tested and should be considered experimental.

**Update**: (`fio`) added, the `fio_rw_hook_replace_unsafe` to allow r/w hook switching from within a r/w hook callback.

**Update**: (`fio_cli`) a common user-error is a a missing `fio_cli_end`, resulting in a memory leak notification. Now facil.io protects against this common error by automatically calling  `fio_cli_end` during the exit stage, if `fio_cli_start` was called.

### v. 0.7.0.beta5

**Fix**: (`fio_cli`) fixed an issue introduced in version 0.7.0.beta4, where `fio_cli_get_i` would dereference NULL if the value wasn't set. Now `fio_cli_get_i` returns zero (0) for missing values, as expected. Note: this related to the new hex and binary base support in command line numerals.

### v. 0.7.0.beta4

**BREAK**: (`fio_cli`) breaking API changes make this extension easier than ever to use... *I do apologize for this, but part of the reason 0.7.0 is still in beta is to test the API itself for ease of use and readability*.

**Fix**: (`fio`) fixed a minor memory leak in cluster mode, caused by the root process not freeing the hash map used for child process subscription monitoring.

**Fix**: (`fio`) fixed superfluous and potentially erroneous pub/sub engine callback calls to `unsubscribe`, caused by (mistakingly) reporting filter channel closure.

**Fix**: (`mustache`, `FIOBJ`) added support for dot notation in mustache templates.

**Fix**: (`http/1.1`) avoid processing further requests if the connection was closed.

**Fix**: (`fio_test`) fixed some memory leaks in the testing functions.

**Update**: (`fio_cli`) stylize and beautify `FIO_CLI_PRINT_HEADER` lines.

**Update**: (`fio`) updated the automatic concurrency calculations to leave resources for the system when a negative value is provided (was only available for worker count calculations, now available for thread count as well).

### v. 0.7.0.beta3

**Fix**: (`fio`) fixed superfluous `ping` events that might occur after a `fio_write` (but before the scheduled write actually occurred).

**Fix**: (`mustache`) updated the mustache parser to fix an issue with template loading path names. The partial template path resolution logic was re-written, fixed and improved (I hope). This also adds support for text in mustache lambda, though not applicable when used with FIOBJ.

**Fix**: (`fio`) prevent Read/Write Hooks from delaying `fio_force_close` when an error occures while polling a connection.

**Fix**: (`fio`) deletes Unix sockets once done listening. Fixes an issue where the files would remain intact.

**Fix**: (`fio`) replaced `fio_malloc` existing memory allocation / free-list implementation. This also fixes an issue with large memory pools being retained on multi-core systems with many reported CPU cores.

**Fix**: (`fio`) the `FIO_FORCE_MALLOC` flag was fixed to accommodate for the fact that fio_malloc returns zeroed data (all bytes are set to zero) vs. the system's `malloc` which might return junk data.

**Fix**: (`http`) fixes a possible memory leak in `http_mimetype_register`, where clearing the registry wouldn't free the FIOBJ Strings.

**Update**: (`cli`) updated handling of empty strings in CLI values by ignoring the argument rather than printing an error or experiencing an undefined value.

**Optimization**: (`fio`) pub/sub channel names appear to be (often) objects with a long life-span. Hence, these objects now use `malloc` (instead of `fio_malloc`). Also, temporary allocations in `fio_subscribe` were removed.

**Optimization**: (`fio`) pub/sub meta-data information and callbacks now use an Array (instead of link lists and a hash map). This should improve cache locality when setting and retrieving pub/sub meta-data.

**Optimization**: (`fio`) added an urgent task queue for outbound IO, possibly improving protection against non-evented / blocking user code.

**Optimization**: (`http`) WebSocket broadcasting optimizations are now automated.

### v. 0.7.0.beta2

**Breaking Changes**!

A lot of the code was re-written and re-organized, minimizing the name space used by the core library and consolidating the core library into a two file library (`fio.h` and `fio.c`).

This translated directly to **breaking the API and ABI and bumping the version number**.

This should make the library easier to copy and use as well as minimize possible name collisions (at the price of maintaining a couple of monolithic files as the core library).

**These are the main changes**:

* Extracted the FIOBJ library from the core library, making it an add-on that could used by the extensions (such as the HTTP extension) rather than a core requirement.

* Condensed the core library and it's namespace to two files (`fio.h` and `fio.c`) - replaced all `facil_` function names with `fio_` to accommodate the new namespace.

    ...why?

    It's a choice to sacrifice ease of maintainability in favor of ease of use.

    Although two files are more difficult to maintain than 10 files (assuming it's the same amount of code)... it seems that two files are easier for other developers to copy and paste into their projects.

* Added poll support to allow for performance testing and CYGWIN compatibility. The system epoll/kqueue calls should perform better for higher loads, but now you can see for yourself.

* Timers are now in user space, allowing for more timers and less kernel dependencies.

* The `on_idle` and `on_finish` settings in `facil_run` (now `fio_start`) were removed, replaced by the more flexible `fio_state_callback_add` approach.

* The `fio_listen` and `http_listen` functions now return the listening socket's `uuid` (much like `fio_connect` and `http_connect` did).

* The Protocol's `on_shutdown` callback is now expected to return a `uint8_t`, hinting at a requested timeout before the socket is forcefully closed. A return value of 0 will indicate immediate socket closure with an 8 second timeout for outgoing buffer flushing.

* The cluster messaging system and the Pub/Sub system were both refactored, **the API changed** and the FIOBJ dependency was removed. This change cascades to effect all the Pub/Sub system elements.

* The Pub/Sub system's `use_pattern` was replaced with the optional callback argument `match` (a function pointer), allowing for custom pattern matching approaches (such as implementing NATs and RabbitMQ pattern matching). The previous glob matching approach (Redis compatible) is available using the provided `FIO_MATCH_GLOB` function pointer.

* The WebSocket upgrade (`http_upgrade2ws`) now matches the SSE upgrade function (starts with the handle `http_s *` and named arguments come later).

* The CLI API and implementation was completely rewritten. The new code is slightly more "monolithic" (one long function does most of the work), but should waste less memory with a simpler API (also got rid of some persistent data).

* The Read/Write socket hooks were redesigned.

* An SSL/TLS API stub was designed for SSL/TLS library abstraction (not implemented yet). This API is experimental and might change as I author the first SSL/TLS library wrappers (roadmap includes OpenSSL and BearSSL).

**Update**: (`fio_mem` => `fio.h`) updated the allocator defaults to lower the price of a longer life allocation. Reminder: the allocator was designed for short/medium allocation life-spans _or_ large allocations (as they directly map to `mmap`). Now 16Kb will be considered a larger allocation and the price of holding on to memory is lower (less fragmentation).

**Fix**: (`fio`) fixed a typo in the shutdown output. Credit to @bjeanes (Bo Jeanes) for the Iodine#39 PR.

**Feature**: (`FIOBJ`) added [mustache template support](http://mustache.github.io).

**Logo**: Logo created by @area55git ([Area55](https://github.com/area55git))

**Documentation**: A new website!

### v. 0.6.4

**Fix**: (`sock`) fixed an issue where calls to `sock_write` could potentially add data to the outgoing queue even after `sock_close` in cases where the outgoing queue isn't empty.

**Fix**: (`facil.io`) fixed a race condition between pre-scheduled tasks (`defer` calls) and the worker process initialization. The race condition might have resulted in some pre-scheduled tasks not running on all the workers.

**Fix**: (`http`) fixed an issue with the HTTP request logging, where the peer address wasn't shown.

**Fix**: (`websocket`) made sure that `on_ready` is never called before `on_open` returns.

**Fix**: (`fio_mem`, `facil`, `http`) fixed compatibility issues with Alpine Linux distro and older OS X versions (< 10.12).

**Fix**: (`http`) fixed the `http_date2rfc2109` method where a space wasn't written to the buffer after the month name (resulting in a junk byte).

**Fix**: (`pubsub`) made sure that newly registered engines get the full list of existing subscriptions (no need to call `pubsub_engine_resubscribe`).

**Fix**: (`facil`) possible fix for protocol attachment with `NULL` protocol.


### v. 0.7.0.beta1

**(yanked)**

### v. 0.6.3

**Fix**: (`fio_hashmap` / `fiobj_hash`) fixed a possible issue that could occur when compacting a severely fragmented Hash (where the length of the new storage requirements is shorter than the fragmented ordered array of data).

**Fix**: (`http` / `websocket`) fixed an issue where the WebSocket's `on_close` callback wouldn't be called if certain errors prevented the upgrade. Now the `on_close` callback is always called.

**Fix**: (`http`) fixed an issue where the Content-Type header might be missing when sending unrecognized files. Now an additional best attempt to detect the content type (this time using the URL instead of the file name) will be performed. If no content type is detected, the default RFC content type will be attached (`application/octet-stream`).

**Fix**: (`http1_parser`) fixed a possible unaligned memory access concern.

**Fix**: (`FIOBJ`) fixed compiler compatibility concerns with the `fiobj_num_new` logic, removing some possibly undefined behavior.

**Fix**: (`facil`) a missing `on_data` protocol callback (missing during `facil_attach`) will now call `facil_quite`, preventing the event from firing endlessly.

**Update**: (`http`) the `on_upgrade` callback now supports SSE connections with `sse` protocol identifier and the `http_upgrade2sse` function.

**Update**: (`sock`) initial support for TCP Fast Open (TFO) when listening for connections.

### v. 0.6.2

This version fixes a number of issues, including a serious issue that prevented sockets from fully flushing their buffer.

This version also improved the shutdown and hot restart logic and fixes numerous issues with cluster mode an pub/sub services.

It's recommended that all 0.6.0.beta, 0.6.0 and 0.6.1 upgrade to this version.

**Security**: (`http1`) added a hard-coded limit on the number of headers allowed per request (regardless of size). `HTTP_MAX_HEADER_COUNT` defaults to 128, which should be enough by all accounts.

**Fix**: (`pubsub`, `facil`, `redis-engine`) fixed numerous cluster and Pub/Sub issues, including support for new `on_startup` callback for `pubsub_engine_s` objects (to make handling `fork`s that much easier. This fixes a memory leak, a reference counting error that freed memory prematurely, message parsing errors on fragmented messages, an obsolete ping formatting error, and more.

**Fix**: (`sock`, `facil`) fixed an issue where socket buffers wouldn't be completely cleared (the `on_ready` event wouldn't be properly re-armed). This was discovered as a serious issue and upgrading to 0.6.2 is recommended.

**Fix**: (`http`) fixed an HTTP status string output error, where status codes above 410 would degrade to status 500 (internal error) instead of printing the correct status string to the response.

**Fix**: (`websockets`) fixed a missing "close" packet signal that should have been sent immediately after the user's `on_shutdown` callback.

**Fix**: (`FIOBJ`) fixed the `fiobj_str_tmp` function to add thread safety (temp string should be stored in the thread's local storage, not globally accessible).

**Fix**: (`redis`) fixed a race condition in the Redis engine that could prevent publishing connections from being established in worker processes.

**Fix**: (`facil`) fixed an issue where `facil_attach` wouldn't call the `on_close` callback if the failure was due to the file descriptor being equal to -1.

**Fix**: (`facil`) fixed a signaling issue where a `SIGUSR1` sent to a worker process might inadvertently shutdown the server instead or wind down the specific worker and re-spawn a new one.

**Fix**: (`facil`) fixed a signal handling logic to make it async-safe, protecting it against possible deadlocks or cluster stream corruption.

**Update/Fix**: (`facil`) the `on_data` event will no longer be fired for sockets that were flagged to be closed using `sock_close`.

**Update**: (`FIOBJ`) updated the `fiobj_str_readfile` to allow for a negative `stat_at` position (calculated from the end of file of the file backwards).

**Update**: (`facil`) strengthened the `on_shutdown` callback lock to prevent the `on_shutdown` callback from being called while the `on_data` callback (or other connection tasks) is running.

**Update**: (`facil`) shutdown logic provides more time for socket buffers to flush (only when required).

### v. 0.6.1

**Fix**: (`pubsub`) fixed a possible issue where a channel name might be freed before it's callback is handled. This was overlooked during the Hash caching logic update that prevented key hashing when the last item of the ordered Hash is removed.

**Fix**: (`pubsub`) pubsub will now compact the memory used for client and channel data if the storage becomes excessively fragmented.

**Fix**: (`hashmap`) fixed a possible memory reading issue that could occur when a Hash contains only one object and that object is removed (causing a memory read into the location just before the Hash map's address).

**Fix**: (`defer`) defer now prefers the statically allocated buffer over the dynamically allocated buffer when all tasks have completed, preventing the last allocated buffer from leaking during the shutdown stage.

**Fix**: (`websocket`) subscriptions created during the on_close callback (besides indicating that the API was being abused) are now properly removed.

### v. 0.6.0

Version 0.6.0 is a major release, changing much of the extension API (HTTP, pub/sub, CLI) and some of the core API (i.e., moving the evio polling from level-triggered to one-shot polling, a rewrite to the facil.io dynamic object types FIOBJ, and more).

The following updates are included in this release (in addition to the beta updates):

**Fix**: (`pubsub`) Fixed an issue where deferred pub/sub messages would have `udata2` set to `udata1` instead of the actual correct value.

**Fix**: (`facil`) Fixed the `facil_is_running()` function, which could crash if facil.io wasn't initialized before the function was called.

**Fix**: (`facil`) Fix CPU limit detection. Negative values are now meaningful (fraction of CPU cores, so -2 == cores/2). Zero values are replaced by facil.io.

**Update**: (`facil`) Hot restart is now available for cluster mode. By sending the `SIGUSR1` signal to the program, facil.io will shutdown any worker processes and re-spawn new workers, allowing for a hot restart feature. Disable using `FACIL_DISABLE_HOT_RESTART`

**Update**: (`facil`) Dedicated system mode can be toggled by setting the `FIO_DEDICATED_SYSTEM` macro during compile time. When `FIO_DEDICATED_SYSTEM` is set, facil.io will assume all the CPU cores are available and it will activate threads sooner. When `FIO_DEDICATED_SYSTEM` is defined as 0, facil.io will limit thread to protect against slow user code (rather than attempt concurrent IO).

**Update**: (`fio_mem`) replaced the double linked list logic with a single linked list to make the library more independent as well as reduce some operations.

As well as some refactoring and minor adjustments.

### v. 0.6.0.beta.8

**Fix**: (`defer`) the `defer_free_thread` symbol is now correctly marked as weak, allowing the function to be overridden.

**Fix**: (`http`) fixes an issue where cookies without an explicit age would be marked for immediate deletion (instead of the expected "session" lifetime).

**Fix**: (`http`) fixes a potential issue where a missing or corrupt `accept-encoding` header could cause a segmentation fault.

**Fix**: (`http`) fixes an issue where a cookie encoding errors would reported repeatedly.

**Fix**: (`fio_hash`) fixes an issue where resubmitting a removed object wouldn't increase the object count.

**Fix**: (`fiobj`) fixes an issue where testing the type of specific FIOBJ_T_NUMBER objects using `FIOBJ_TYPE_IS` would return a false positive for the types FIOBJ_T_HASH or FIOBJ_T_STRING.

**Update**: Added an experimental custom memory allocator (`fio_mem.h`) optimized for small concurrent short-lived allocations (anything over 16Kb and reallocations start to take a toll). It can replace the system's `malloc` function family when `FIO_OVERRIDE_MALLOC` is defined. To use `tcmalloc` or `jemalloc`, define `FIO_FORCE_MALLOC` to prevent `fio_mem` from compiling.

**Update**: (`http`) added cookie parsing.

**Update**: minor optimizations, `fio_malloc` incorporation and documentation updates.

### v. 0.6.0.beta.7

**Fix**: (`websockets`) fixed an issue with client pinging would break the protocol in a way that would result in either loss of data or disconnections.

**Fix**: (`websockets`) removed the debugging ping (3 seconds interval ping) from the Websocket client. Pings can be sent manually or by setting the connection's timeout using `facil_set_timeout`.

**Fix**: (`websockets`) made sure the client mask is never zero by setting a few non-random bits.

**Fix**: (`redis`) fixed an issue where the command queue (for busy pipelined Redis commands and for reconnection) would send the last message repeatedly instead of sending the messages in the queue.

**Fix**: (`facil`) Fixed a possible memory leak related to `facil_connect` and failed connections to localhost. Improved process exit cleanup.

**Fix**: (`pubsub`) improved process exit cleanup.

**Fix**: (`fio_cli`) fixed text coloring on terminal output.

**Fix**: (`fio_hash`) fixed looping logic to remove the need for the "data-end" marker optimizing allocations in a very small way.

**Update**: (`websockets`) added a client example using the terminal IO for Websocket text communication.

### v. 0.6.0.beta.6

This beta release is a performance oriented release and includes mostly performance related changes.

This release updates some default values to make them more reasonable for common use cases and to help minimize memory consumption.

These values, such as the `LIB_SOCK_MAX_CAPACITY`, `FIO_HASH_INITIAL_CAPACITY` and the `FIOBJ_ARRAY_DEFAULT_CAPA` values, can be updated during compile time.

Some of these default values can be bypassed during runtime by using specific function calls (such as `fio_hash_new2`).

Other notable performance changes include the short string hash cashing (shortening the FIOBJ short-string capacity in exchange for reducing `fio_siphash` calls).

These are lessons learned from the TechEmpower benchmarks... although they will not be reflected in the Round 15 results.

### v. 0.6.0.beta.5

Released fixes for issues related to the [TechEmpower Framework Benchmarks](https://github.com/TechEmpower/FrameworkBenchmarks) 80 core startup.

**Fix**: fixed error handling during cluster mode startup, making sure facil.io fails to start.

**Update**: capped maximum core detection value to 120 cores. Any value larger than 120 will raise a warning and the cap (120) will be used.

### v. 0.6.0.beta.4

Released after stress testing and memory leakage testing.

### v. 0.6.0.beta.3

**Breaking Change**: (`websockets`) the websocket `on_close` callback signature had changed to allow it to be called on connection/upgrade failures as well (easier `udata` cleanup).

**Fix** (`facil`): fixes an issue introduced in the beta.2 version, where deferred events that where scheduled before a call to `facil_run` would only be called for the parent process. Now these events would perform as intended (once in the root process and once in each worker process).

**Fix** (`facil`): updates the logical assumption about open connections, to make sure any open connections are closed when re-spawning a child worker. This shift the connection assumption from unsafe (forked connections should be closed by extensions) to safe (reconnection should be handled by extension). This change should effect internal extensions only, since active connections aren't handled by the root process in clustered mode.

**Change** (`websocket`): the protocol is now more forgiving in cases where clients don't mask empty messages.

**Feature** (`websockets`): A simple and easy Websocket client using `websocket_connect` as well as support for more complex clients (with authentication logic etc') using a combination of the `http_connect` and `http_upgrade2ws` functions.

**Minor**: some changes to the inner HTTP logic, fixed some error messages, and other minor updates.

### v. 0.6.0.beta.2

Version 0.6.0 is a major release, changing much of the extension API (HTTP, pub/sub, CLI) and some of the core API (i.e., moving the `evio` polling to One-Shot polling).

In this beta 2 release:

**Fix** (`facil`): fixes an issue that could occur when forking a large number of processes, where cluster connection locks would remain locked, causing the cluster connections to spin the CPU and prevent shutdown.

**Fix** (`redis`, `pubsub`, `evio`): fixes for the internal Redis engine. There was a connection re-establishing error related to updates in the new `evio` event logic.

**Update**: (`http`) Added experimental query parsing helpers that perform nested parameter name resolution (i.e. `named_hash[named_array][]=value`). Logic might change as performance considerations apply. I'd love to read your feedback on this feature.

**Update**: (`facil`) Simplified the child worker sentinel observation logic, to use threads instead of IPC.

### v. 0.6.0.beta

Version 0.6.0 is a major release, changing much of the extension API (HTTP, pub/sub, CLI) and some of the core API (i.e., moving the `evio` polling to One-Shot polling).

In this beta 1 release:

**Fix** (`http`): fixed an issue where receiving the same header name more than once would fail to convert the header value into an array of values.

**Minor fixes**: more error handling, more tests, fixed `fiobj_iseq` to test hash keys as well as objects. The `fio_hashmap.h` key caching for removed objects is cleared when hash is empty (i.e, if it's empty, it's really empty).

**Performance** minor improvements. For example, Header Hash Maps are now cleared and reused by HTTP/1.1 during keep-alive (instead of deallocated and reallocated).

### v. 0.6.0.dev

This is a major release, changing much of the extension API (HTTP, pub/sub, CLI) and some of the core API (i.e., moving the `evio` polling to One-Shot polling).

Migration isn't difficult, but is not transparent either.

**Fix** (backported): (`websocket_parser`) The websocket parser had a memory offset and alignment handling issue in it's unmasking (XOR) logic and the new memory alignment protection code. The issue would impact the parser in rare occasions when multiple messages where pipelined in the internal buffer and their length produced an odd alignment (the issue would occur with very fast clients, or a very stressed server).

**Note About Fixes**:

-  I simply rewrote much of the code to know if the issues I fixed were present in the 0.5.x version or not.

   I believe some things work better. Some of the locking concerns will have less contention and I think I fixed some issues in the `fiobj` core types as well as the `http` extension.

   However, experience tells me a new major release (0.6.0) is always more fragile than a patch release. I did my best to test the new code, but experience tells me my tests are often as buggy as the code they test.

   Anyway, I'm releasing 0.6.0 knowing it works better than the 0.5.8 version, but also knowing it wasn't battle tested just yet.

**Changes!**: (`fiobj` / facil.io objects):

- Major API changes.

    The facil.io dynamic type library moved closer to facil.io's core, integrating itself into the HTTP request/response handling, the Pub/Sub engine, the Websocket internal buffer and practically every aspect of the library.

    This required some simplification of the `fiobj` and making sure future changes would require less of a migration process.

- The Symbol and Couplet types were removed, along with nesting protection support (which nobody seemed to use anyway).

- We're back to static typing with `enum`, using macros and inline functions for type identification (better performance at the expense of making extendability somewhat harder).

- Hashes are now 100% collision resistant and have improved memory locality, at the expense of using more memory and performing calling `memcmp` (this can be avoided when seeking / removing / deleting items, but not when overwriting items).

**Changes!**: (`http`):

- The HTTP API and engine was completely re-written (except the HTTP/1.1 parser), both to support future client mode (including chunked encoding for trailing headers) and to make routing and request parsing easier.

- The updates to the HTTP API might result in decreased performance during the HTTP request reading due to the need to allocate resources and possibly copy some of the data into dynamic storage... For example, header Hash Tables replaced header Arrays, improving lookup times and increasing creation time. 

- The HTTP parser now breaks down long URI schemes into a short URI + `host` header (which might become an array if it's included anyway).

**Changes!**: (`websocket`):

- The Websocket API includes numerous breaking changes, not least is the pub/sub API rewrite that now leverages `FIOBJ` Strings / Symbols.

- `websocket_write_each` was deprecated (favoring a pub/sub only design).

**Changes!**: (`pubsub`):

- The `pubsub` API was redesigned after re-evaluating the function of a pub/sub engine and in order to take advantage of the `FIOBJ` type system.

- Channel names now use a hash map with collision protection (using `memcmp` to compare channel names). The means that the 4 but trie is no longer in use and will be deprecated.

**Changes!**: (`redis`):

- The `redis_engine` was rewritten, along with the RESP parser, to reflect the changes in the new `pubsub` service and to remove obsolete code.

**Changes!**: (`facil`):

- Slight API changes:

    - `facil_last_tick` now returns `struct timespec` instead of `time_t`, allowing for more accurate time stamping.

    - `facil_cluster_send` and `facil_cluster_set_handler` were redesigned to reflect the new cluster engine (now using Unix Sockets instead of pipes).

- Internal updates to accommodate changes to other libraries.

- Cluster mode now behaves as sentinel, re-spawning any crashed worker processes (except in DEBUG mode).

**Changes!**: (`evio`):

- the evented IO library was redesigned for **one-shot** notifications, requiring a call to `evio_add` or `evio_set_timer` in order to receive future notifications.


    This was a significant change in behavior and the changes to the API (causing backwards incompatibility) were intentional.

- the code was refactored to separate system specific logic into different files. making it easier to support more systems in the future.

**Changes!**: (`sock`):

- the socket library now supports automatic Unix detection (set `port` to NULL and provide a valid Unix socket path in the `address` field).

- the socket library's Read/Write hooks API was revised, separating the function pointers from the user data. At server loads over 25%, this decreases the memory footprint.

- the socket library's packet buffer API was deprecated and all `sock_write2(...)` operations take ownership of the memory/file (enforce `move`).

- The internal engine was updated, removing pre-allocated packet buffers altogether and enabling packet header allocation using `malloc`, which introduces a significant changes to the internal behavior, possibly effecting embedded systems.

**Changes!**: (`defer`):

- Removed forking from the defer library, moving the `fork` logic into the main `facil` source code.

- Defer thread pools and now include two weak functions that allow for customized thread scheduling (wakeup/wait). These are overwritten by facil.io (in `facil.c`).

    By default, defer will use `nanosleep`.

**Refactoring**: (`fiobj`) moved the underlying Dynamic Array and Hash Table logic into single file libraries that support `void *` pointers, allowing the same logic to be used for any C object collection (as well as the facil.io objects).

---

### Ver. 0.5.10 (backported)

**Fix** (backported): (`facil` / `pubsub`) fixed an issue where cluster messages would be corrupted when passed in high succession. Credit to Dmitry Davydov (@haukot) for exposing this issue through the Iodine server implementation (the Ruby port).

It should be noted that this doesn't fix the core weakness related to large cluster or pub/sub messages, which is caused by a design weakness in the `pipe` implementation (in some kernels).

The only solution for large message corruption is to use the new pub/sub engine introduced in the facil.io 0.6.x release branch, which utilizes Unix Sockets instead of pipes.

### Ver. 0.5.9 (backported)

**Fix** (backported from 0.6.0): (`websocket_parser`) The websocket parser had a memory offset and alignment handling issue in it's unmasking (XOR) logic and the new memory alignment protection code. The issue would impact the parser in rare occasions when multiple messages where pipelined in the internal buffer and their length produced an odd alignment (the issue would occur with very fast clients, or a very stressed server).

### Ver. 0.5.8

**Fix**: (`defer`, `fiobj`) fix Linux compatibility concerns (when using GCC). Credit goes to @kotocom for opening issue #23.

### Ver. 0.5.7

**Fix**: (`defer`) fixes the non-debug version of the new (v.0.5.6) defer, which didn't define some debug macros.

**Updates**: minor updates to the boilerplate documentation and the "new application" creation process.

### Ver. 0.5.6 (yanked)

**Fix**: Added `cmake_minimum_required` and related CMake fixes to the CMake file and generator. Credit to David Morán (@david-moran) for [PR #22](https://github.com/boazsegev/facil.io/pull/22) fixing the CMakelist.txt.

**Compatibility**: (`websocket_parser`) removed unaligned memory access from the XOR logic in the parser, making it more compatible with older CPU systems that don't support unaligned memory access or 64 bit word lengths.

**Optimization**: (`defer`) rewrote the data structure to use a hybrid cyclic buffer and linked list for the task queue (instead of a simple linked list), optimizing locality and minimizing memory allocations.

**Misc**: minor updates and tweaks, such as adding the `fiobj_ary2prt` function for operations such as quick sort, updating some documentation etc'.

### Ver. 0.5.5

**Fix**: (`fiobj`) fixed an issue #21, where `gcc` would complain about overwriting the `fio_cstr_s` struct due to `const` members. Credit to @vit1251 for exposing this issue.

**Fix**: (`fiobj`) fixed NULL pointer testing for `fiobj_free(NULL)`.

**Compatibility**: (`gcc-6`) Fix some compatibility concerns with `gcc` version 6, as well as some warnings that were exposed when testing with `gcc`.

**Optimization**: (`fiobj`) optimized the JSON parsing memory allocations as well as fixed some of the function declarations to add the `const` keyword where relevant.

### Ver. 0.5.4

I've been making so many changes, it took me a while.

This version includes all the updates in the unreleased version 0.5.3 as well as some extra work.

The new HTTP/1.1 and Websocket parsers have been debugged and tested in the Iodine Ruby server, the dynamic type system (`fiobj_s`) will be updated to revision 2 in this release...

**Change/Update**: (`fiobj`) The dynamic type library was redesigned to make it extendable. This means that code that used type testing using a `switch` statement needs to be rewritten.

**Fix**: (`websocket`) issues with the new websocket parser were fixed. Credit to Tom Lahti (@uidzip) for exposing the issues.

### Ver. 0.5.3 (unreleased, included in 0.5.4)

**Change**: minor changes to the versioning scheme removed some version MACROS... this isn't API related, so I don't consider it a breaking change, but it might effect code that relied too much on internal workings. The only valid version macros are the `FACIL_VERSION_*` macros, in the `facil.h` header.

**Change**: (`http`) the HTTP/1.x parser was re-written and replaced. It should perform the same, while being easier to maintain. Also, the new parser could potentially be used to author an HTTP client.

**Change**: (`websocket`) the Websocket parser was re-written and replaced, decoupling the parser and message wrapper from the IO layer. Performance might slightly improve, but should mostly remain the same. The new code is easier to maintain and easier to port to other implementations. Also, the new parser supports a client mode (message masking).

**Fix**: (`websocket`) fix #16, where a client's first message could have been lost due to long `on_open` processing times. This was fixed by fragmenting the `upgrade` event into two events, adding the `facil_attach_locked` feature and attaching the new protocol before sending the response. Credit to @madsheep and @nilclass for exposing the issue and tracking it down to the `on_open` callbacks.

**Fix**: (`sock`) sockets created using the TCP/IP `sock` library now use `TCP_NODELAY` as the new default. This shouldn't be considered a breaking change as much as it should be considered a fix.

**Fix**: (`http1`) HTTP/1.x is now more fragmented, avoiding a `read` loop to allow for mid-stream / mid-processing protocol upgrades. This also fixes #16 at it's root (besides the improved `websocket` callback handling).

**Fix**: (`http1`) HTTP/1.x now correctly initializes the `udata` pointer to NULL fore each new request.

**Fix**: (`facil`) the `facil_run_every` function now correctly calls the `on_finish` callback when a timer initialization fails. This fixes a leak that could have occurred due to inconsistent API expectations. Workarounds written due to this issue should be removed.

**Fix**: (`facil`) connection timeout is now correctly ignored for timers.

**Fix**: (`defer`) a shutdown issue in `defer_perform_in_fork` was detected by @cdkrot and his fix was implemented.

**Fix**: (`evio`) fixed an issue where the evented IO library failed to reset the state indicator after `evio_close` was called, causing some functions to believe that events are still processed. Now the `evio_isactive` will correctly indicate that the evented IO is inactive after `evio_close` was called.

**Fix**: (`evio`) fixes an issue where `evio_add_timer` would fail with `EEXIST` instead of reporting success (this might be related to timer consolidation concerns in the Linux kernel).

**Fix**: (`evio`) better timer `fd` creation compatibility with different Linux kernels.

**Fix**: (documentation) credit to @cdkrot for reporting an outdated demo in the README.

**Fix**: (linking) added the missing `-lm` linker flag for `gcc`/Linux (I was using `clang`, which automatically links to the math library, so I didn't notice this).

**Portability**: added `extern "C"` directive for untested C++ support.

**Feature**: 🎉 added a CLI helper service, allowing easy parsing and publishing of possible command line arguments.

**Feature**: 🎉🎉 added a dynamic type library to `facil.io`'s core, making some common web related tasks easier to manage.

**Feature**: 🎉🎉🎉 added native JSON support. JSON strings can be converted to `fiobj_s *` objects and `fiobj_s *` objects can be rendered as JSON! I'm hoping to get it [benchmarked publicly](https://github.com/miloyip/nativejson-benchmark/pull/92).

### Ver. 0.5.2

**Change**: non-breaking changes to the folder structure are also reflected in the updated `makefile` and `.clang_complete`.

**Fix**: (`defer`) fixed `SIGTERM` handling (signal was mistakingly filtered away).

**Fix**: (`http_response`) fixed `http_response_sendfile2` where path concatenation occurred without a folder separator (`/`) and exclusively safe file paths were being ignored (the function assumed an unsafe path to be used, at least in part).

**Fix**: minor fixes and documentation.

**Fix / Feature**: (`facil`) sibling processes will now detect a sibling's death (caused by a crashed process) and shutdown.

**Feature**: @benjcal suggested the script used to create new applications. The current version is a stand-in draft used for testing.

**Feature**: temporary boiler plate code for a simple "hello world" HTTP application using the new application script (see the README). This is a temporary design to allow us to test the script's functionality and decide on the final boiler plate's design.

### Ver. 0.5.1

**Fix**: (`sock`) Fixed an issue where `sock_flush` would always invoke `sock_touch`, even if no data was actually sent on the wire.

**Fix**: (`sock`) fixed a possible issue with `sock_flush_strong` which might have caused the facil.io to hang.

**Fix**: (`Websocket`) fixed an issue with fragmented pipelined Websocket messages.

**Feature**: (`facil`) easily force an IO event, even if it did not occur, using `facil_force_event`.

### Ver. 0.5.0

**Braking changes**: (`pubsub`) The API was changed / updated, making `pubsub_engine_s` objects easier to author and allowing allocations to be avoided by utilizing two `void * udata` fields... Since this is a breaking change, and following semantic versioning, the minor version is updated. I do wish I could have delayed the version bump, as the roadmap ahead is long, but it is what it is.

**Braking changes**: (`facil`) Since the API is already changing a bit, I thought I'd clean it up a bit and have all the `on_X` flow events (`on_close`, `on_fail`, `on_start`...) share the same function signature where possible.

**Changes**: (`facil`) Minor changes to the `fio_cluster_*` API now use signed message types. All negative `msg_type` values are reserved for internal use.

**Fix**: plugging memory leaks while testing the system under very high stress.

**Fix**: (`pubsub`, `fio_dict`) Fixed glob pattern matching... I hope. It seems to work fine, but I'm not sure it the algorithm matches the Redis implementation which is the de-facto standard for channel pattern matching.

**Security**: (`http`) the HTTP parser now breaks pipelined HTTP requests into fragmented events, preventing an attacker from monopolizing requests through endless pipelining of requests that have a long processing time.

**Fix**: (`http`) `http_listen` will now always *copy* the string for the `public_folder`, allowing dynamic strings to be safely used.

**Fix**: (`http`) default error files weren't located due to missing `/` in name. Fixed by adjusting the requirement for the `/` to be more adaptive.

**Fix**: (`http`) dates were off by 1 day. Now fixed.

**Fix**: (`http1`) a minor issue in the `on_data` callback could have caused the parser to crash in rare cases of fragmented pipelined requests on slower connections. This is now fixed.

**Fix**?: (`http`) When decoding the path or a request, the `+` sign is now left unencoded (correct behavior), trusting in better clients in this great jungle.

**Fix**: (`facil`) `facil_defer` would leak memory if a connection was disconnected while a task was scheduled.

**Fix**: (`facil`) `facil_connect` now correctly calls the `on_fail` callback even on immediate failures (i.e. when the function call was missing a target address and port).

**Fix**: (`facil`) `facil_connect` can now be called before other socket events (protected form library initialization conflicts).

**Fix**: (`facil`) `facil_listen` will now always *copy* the string for the `port`, allowing dynamic strings to be safely used when `FACIL_PRINT_STATE` is set.

**Fix**: (`facil`) `facil_last_tick` would crash if called before the library was initialized during socket operations (`facil_listen`, `facil_attach`, etc')...  now `facil_last_tick` falls back to `time()` if nothing happened yet.

**Fix**: (`facil`) `.on_idle` now correctly checks for non networks events as well before the callback is called.

**Fix**: (`defer`) A large enough (or fast enough) thread pool in a forked process would complete the existing tasks before the active flag was set, causing the facil.io reactor to be stranded in an unscheduled mode, as if expecting to exit. This is now fixed by setting a temporary flag pointer for the forked children, preventing a premature task cleanup.

**Changes**: Major folder structure updates make development and support for CMake submodules easier. These changes should also make it easier to push PRs for by offering the `dev` folder for any localized testing prior to submitting the PR.

**Feature**: (`websockets`) The websocket pub/sub support is here - supporting protocol tasks as well as direct client publishsing (and autu text/binary detection)! There are limits and high memory costs related to channel names, since `pubsub` uses a trie for absolute channel matching (i.e. channel name length should be short, definitely less than 1024Bytes).

**Feature**: (`redis`) The websocket pub/sub support features a shiny new Redis engine to synchronize pub/sub across machines! ... I tested it as much as I could, but I know my tests are as buggy as my code, so please test before using.

**Feature**: (`facil_listen`, `http_listen`) supports an optional `on_finish_rw` callback to clean-up the `rw_udata` object.

**Feature**: (`pubsub`) channels now use the available `fio_dict_s` (trie) data store. The potential price of the larger data-structure is elevated by it's absolute protection against hash collisions. Also, I hope that since channels are more often searched than created, this should improve performance when searching for channels by both pattern and perfect match. I hope this combination of hash tables (for client lookup) and tries (for channel traversal) will provide the best balance between string matching, pattern matching, iterations and subscription management.

**Feature**: (`http`) `http_listen` now supports an `on_finish` callback.

**Feature**: (`http1`) HTTP/1.1 will, in some cases, search for available error files (i.e. "400.html") in the `public_folder` root, allowing for custom error messages.

**Feature**: CMake inclusion. Credit to @OwenDelahoy (PR#8).

> To use facil.io in a CMake build you may add it as a submodule to the project's repository.
>
>       git submodule add https://github.com/boazsegev/facil.io.git

> Then add the following line the project's `CMakeLists.txt`
>
>       add_subdirectory(facil.io)

**Optimize**: (`fio_hash_table`) optimize `fio_ht_s` bin memory allocations.

### Ver. 0.4.4

**Fix**: (`pubsub`) Fixed collisions between equal channel names on different engines, so that channels are only considered equal if they share the same name as well as the same engine (rather than only the same name)... actually, if they share the same channel name SipHash value XOR'd with the engine's memory location.

**Fix**: (`facil`) Fixed compiling error on older `gcc` v.4.8.4, discovered on Ubuntu trusty/64.

**Fix**: Fix enhanced CPU cycles introduced in the v.0.4.3 update. Now CPU cycles are lower and thread throttling handles empty queues more effectively.

**Performance**: (`pubsub`) now uses a hash-table storage for the channels, clients and patterns, making the duplicate review in `pubsub_subscribe` much faster, as well as improving large channel collection performance (nothing I can do about pattern pub/sub, though, as they still need to be matched using iterations, channel by channel and for every channel match).

**Feature**: (`http`) The `http_response_sendfile2` function will now test for a `gzip` encoded alternative when the client indicated support for the encoding. To provide a `gzip` alternative file, simply `gzip` the original file and place the `.gz` file in the original file's location.

**Folder Structure**: Updated the folder structure to reflect source code relation to the library. `core` being required files, `http` relating to `http` etc'.

### Ver. 0.4.3

**Fix**: Some killer error handling should now signal all the process group to exit.

**Fix**: (`sock`, `websocket`) `sock_buffer_send` wouldn't automatically schedule a socket buffer flush. This caused some websocket messages to stay in the unsent buffer until a new event would push them along. Now flushing is scheduled and messages are send immediately, regardless of size.

**Fix**: (`facil`) `facil_attach` now correctly calls the `on_close` callback in case of error.

**Fix**: (`facil`) `facil_protocol_try_lock` would return false errors, preventing external access to the internal protocol data... this is now fixed.

**Feature**: (`facil`) Experimental cluster mode messaging, allowing messages to be sent to all the cluster workers. A classic use-case would be a localized pub/sub websocket service that doesn't require a backend database for syncing a single machine... Oh wait, we've added that one too...

**Feature**: (`facil`) Experimental cluster wide pub/sub API with expendable engine support (i.e., I plan to add Redis as a possible engine for websocket pub/sub).

**Update**: (`http`) Updated the `http_listen` to accept the new `sock_rw_hook_set` and `rw_udata` options.

**Update**: (`sock`) Rewrote some of the error handling code. Will it change anything? only if there were issues I didn't know about. It mostly effects errno value availability, I think.

### Ver. 0.4.2

**Fix**: (`sock`) Fixed an issue with the `sendfile` implementation on macOS and BSD, where medium to large files wouldn't be sent correctly.

**Fix**: (`sock`) Fixed the `sock_rw_hook_set` implementation (would lock the wrong `fd`).

**Design**: (`facil`) Separated the Read/Write hooks from the protocol's `on_open` callback by adding a `set_rw_hook` callback, allowing the same protocol to be used either with or without Read/Write hooks (i.e., both HTTP and HTTPS can share the same `on_open` function).

**Fix**: (`evio`, `facil`) Closes the `evio` once facil.io finished running, presumably allowing facil.io to be reinitialized and run again.

**Fix**: (`defer`) return an error if `defer_perform_in_fork` is called from within a running defer-forked process.

**Fix**: (`sock`, `facil`, bscrypt) Add missing `static` keywords.

**Compatibility**: (bscrypt) Add an alternative `HAS_UNIX_FEATURES` test that fits older \*nix compilers.

---

### Ver. 0.4.1

**Fix**: (HTTP/1.1) fixed the default response `date` (should have been "now", but was initialized to 0 instead).

**Fix**: fixed thread throttling for better energy conservation.

**Fix**: fixed stream response logging.

**Compatibility**: (HTTP/1.1) Automatic `should_close` now checks for HTTP/1.0 clients to determine connection persistence.

**Compatibility**: (HTTP/1.1) Added spaces after header names, since some parsers don't seem to read the RFC.

**Fix/Compatibility**: compiling under Linux had been improved.

---

### Ver. 0.4.0

Updated core and design. New API. Minor possible fixes for HTTP pipelining and parsing.

# Historic Change log

The following is a historic change log, from before the `facil_` API.

---

Note: This change log is incomplete. I started it quite late, as interest in the libraries started to warrant better documentation of any changes made.

Although the libraries in this repo are designed to work together, they are also designed to work separately. Hence, the change logs for each library are managed separately. Here are the different libraries and changes:

* [Lib-React (`libreact`)](#lib-react) - The Reactor library.

* [Lib-Sock (`libsock`)](#lib-sock) - The socket helper library (development incomplete).

* [Lib-Async (`libasync`)](#lib-async) - The thread pool and tasking library.

* [Lib-Server (`libserver`)](#lib-server) - The server writing library.

* [MiniCrypt (`minicrypt`)](#minicrypt) - Common simple crypto library (development incomplete).

* [HTTP Protocol (`http`)](#http_protocol) - including request and response helpers, etc'.

* [Websocket extension (`websockets`)](#websocket_extension) - Websocket Protocol for the basic HTTP implementation provided.

## General notes and _future_ plans

Changes I plan to make in future versions:

* Implement a `Server.connect` for client connections and a Websocket client implementation.

* Implement Websocket writing using `libsock` packets instead of `malloc`.

* Remove / fix server task container pooling (`FDTask` and `GroupTask` pools).

## A note about version numbers

I attempt to follow semantic versioning, except that the libraries are still under pre-release development, so version numbers get updated only when a significant change occurs or API breaks.

Libraries with versions less then 0.1.0 have missing features (i.e. `mini-crypt` is missing almost everything except what little published functions it offers).

Minor bug fixes, implementation optimizations etc' might not prompt a change in version numbers (not even the really minor ones).

API breaking changes always cause version bumps (could be a tiny version bump for tiny API changes).

Git commits aren't automatically tested yet and they might introduce new issues or break existing code (I use Git also for backup purposes)...

... In other words, since these libraries are still in early development, test before adopting any updates.

## Lib-React

### V. 0.3.0

* Rewrite from core. The code is (I think) better organized.

*  Different API.

* The reactor is now stateless instead of an object. All state data (except the reactor's ID, which remains static throughout during it's existence), is managed by the OS implementation (`kqueue`/`epoll`).

* Callbacks are statically linked instead of dynamically assigned.

* Better integration with `libsock`.

* (optional) Handles `libsock`'s UUID instead of direct file descriptors, preventing file descriptor collisions.

### V. 0.2.2

* Fixed support for `libsock`, where the `sock_flush` wasn't automatically called due to inline function optimizations used by the compiler (and my errant code).

### V. 0.2.1

Baseline (changes not logged before this point in time).

## Lib-Sock

### V. 0.2.3 (next version number)

* Apple's `getrlimit` is broken, causing server capacity limits to be less than they could / should be.

### V. 0.2.2

* Fixed an issue introduced in `libsock` 0.2.1, where `sock_close` wouldn't close the socket even when all the data was sent.

### V. 0.2.1

* Larger user level buffer - increased from ~4Mb to ~16Mb.

* The system call to `write` will be deferred (asynchronous) when using `libasync`. This can be changed by updating the `SOCK_DELAY_WRITE` value in the `libsock.c` file.

    This will not prevent `sock_write` from emulating a blocking state while the user level buffer is full.

### V. 0.2.0

* Almost the same API. Notice the following: no initialization required; rw_hooks callbacks aren't protected by a lock (use your own thread protection code).

   There was an unknown issue with version 0.1.0 that caused large data sending to hang... tracking it proved harder then re-writing the whole logic, which was both easier and allowed for simplifying some of the code for better maintenance.

* `sock_checkout_packet` will now hang until a packet becomes available. Don't check out more then a single packet at a time and don't hold on to checked out packets, or you might find your threads waiting.

### V. 0.1.0

* Huge rewrite. Different API.

* Uses connection UUIDs instead of direct file descriptors, preventing file descriptor collisions. Note that the UUIDs aren't random and cannot be used to identify the connections across machines or processes.

* No global lock, spin-lock oriented design.

* Better (optional) integration with `libreact`.

### V. 0.0.6

* `libsock` experienced minor API changes, specifically to the `init_socklib` function (which now accepts 0 arguments).

* The `rw_hooks` now support a `flush` callback for hooks that keep an internal buffer. Implementing the `flush` callback will allow these callbacks to prevent a pre-mature closure of the socket stream and ensure that all the data will be sent.

### V. 0.0.5

* Added the client implementation (`sock_connect`).

* Rewrote the whole library to allow for a fixed user-land buffer limit. This means that instead of having buffer packets automatically allocated when more memory is required, the `sock_write(2)` function will hang and flush any pending socket buffers until packets become available.

* File sending is now offset based, so `fseek` data is ignored. This means that it would be possible to cache open `fd` files and send the same file descriptor to multiple clients.

### V. 0.0.4

* Fixed issues with non-system `sendfile` and with underused packet pool memory.

* Added the `.metadata.keep_open` flag, to allow file caching... however, keep in mind that the file offset for read/write is the file's `lseek` position and sending the same file to different sockets will create race conditions related to the file `lseek` position.

* Fix for epoll's on_ready not being sent (sock flush must raise the EAGAIN error, or the on_ready event will not get called). Kqueue is better since the `on_ready` refers to the buffer being clear instead of available (less events to copy the same amount of data, as each data write is optimal when enough data is available to be written).

* optional implementation of sendfile for Apple, BSD and Linux (BSD **not** tested).

* Misc. optimizations. i.e. Buffer packet size now increased to 64Kb, to fit Linux buffer allocation.

* File sending now supports file descriptors.

* TLC support replaced with a simplified read/write hook.

* Changed `struct SockWriteOpt` to a typedef `sock_write_info_s`.

### V. 0.0.3

* Changed `struct Packet` to a typedef `sock_packet_s`.

* fixed and issue where using `sock_write(2)` for big data chunks would cause errors when copying the data to the user buffer.

    it should be noted, for performance reasons, that it is better to send big data using external pointers (especially if the data is cashed) using the `sock_send_packet` function - for cached data, do not set the `packet->external` flag, so the data isn't freed after it was sent.

### V. 0.0.2

* fixed situations in which the `send_packet` might not close a file (if the packet buffer references a `FILE`) before returning an error.

* The use of `sock_free_packet` is now required for any unused Packet object pointers checked out using `sock_checkout_packet`.

    This requirement allows the pool management to minimize memory fragmentation for long running processes.

* `libsock` memory requirements are now higher, as the user land buffer's Packet memory pool is pre-allocated to minimize memory fragmentation.

* Corrected documentation mistakes, such as the one stating that the `sock_send_packet` function will not handle the Packet object's memory on error (it does handle the memory, **always**).

### V. 0.0.1

Baseline (changes not logged before this point in time).

## Lib-Async

### V. 0.4.0

* I rewrote (almost) everything.

* `libasync` now behaves as a global state machine. No more `async_p` objects.

* Uses (by default) `nanosleep` instead of pipes (you can revert back to pipes by setting a simple flag). This, so far, seems to provide better performance (at the expense of a slightly increased CPU load).

### V. 0.3.0

* Fixed task pool initialization to zero-out data that might cause segmentation faults.

* `libasync`'s task pool optimizations and limits were added to minimize memory fragmentation issues for long running processes.

Baseline (changes not logged before this point in time).

## Lib-Server

### V. 0.4.2 (next version number)

* Limited the number of threads (1023) and processes (127) that can be invoked without changing the library's code.

* Minor performance oriented changes.

* Fixed an issue where Websocket upgrade would allow code execution in parallel with `on_open` (protocol locking was fixed while switching the protocol).

* Added `server_each_unsafe` to iterate over all client connections to perform a task. The `unsafe` part in the name is very important - for example, memory could be deallocated during execution.

### V. 0.4.1

* Minor performance oriented changes.

* Shutdown process should now allow single threaded asynchronous (evented) task scheduling.

* Updating a socket's timeout settings automatically "touches" the socket (resets the timeout count).

### V. 0.4.0

* Rewrite from core. The code is more concise with less duplications.

* Different API.

* The server is now a global state machine instead of an object.

* Better integration with `libsock`.

* Handles `libsock`'s UUID instead of direct file descriptors, preventing file descriptor collisions and preventing long running tasks from writing to the wrong client (i.e., if file descriptor 6 got disconnected and someone else connected and receive file descriptor 6 to identify the socket).

* Better concurrency protection and protocol cleanup `on_close`. Now, deferred tasks (`server_task` / `server_each`), the `on_data` callback and even the `on_close` callback all run within a connection's "lock" (busy flag), limiting concurrency for a single connection to the `on_ready` and `ping` callbacks. No it is safe to free the protocol's memory during an `on_close` callback, as it is (almost) guarantied that no running tasks are using that memory (this assumes that `ping` and `on_ready` don't use any data placed protocol's memory).

### V. 0.3.5

* Moved the global server lock (the one protecting global server data integrity) from a mutex to a spin-lock. Considering API design changes that might allow avoiding a lock.

* File sending is now offset based, so `lseek` data is ignored. This means that it should be possible to cache open `fd` files and send the same file descriptor to multiple clients.

### V. 0.3.4

* Updated `sendfile` to only accept file descriptors (not `FILE *`). This is an optimization requirement.

### V. 0.3.3

* fixed situations in which the `sendfile` might not close the file before returning an error.

* There was a chance that the `on_data` callback might return after the connection was disconnected and a new connection was established for the same `fd`. This could have caused the `busy` flag to be cleared even if the new connection was actually busy. This potential issue had been fixed by checking the connection against the UUID counter before clearing the `busy` flag.

* reminder: The `Server.rw_hooks` feature is deprecated. Use `libsock`'s TLC (Transport Layer Callbacks) features instead.

### V. 0.3.2

Baseline (changes not logged before this point in time).

## MiniCrypt (development incomplete)

### V. 0.1.1

* added a "dirty" (and somewhat faster then libc) `gmtime` implementation that ignores localization.

Baseline (changes not logged before this point in time).

## HTTP Protocol

* Sep. 13, 2016: `ETag` support for the static file server, responding with 304 on valid `If-None-Match`.

* Sep. 13, 2016: Updated `HEAD` request handling for static files.

* Fixed pipelining... I think.

* Jun 26, 2016: Fixed logging for static file range requests.

* Jun 26, 2016: Moved URL decoding logic to the `HttpRequest` object.

* Jun 20, 2016: Added basic logging support.

* Jun 20, 2016: Added automatic `Content-Length` header constraints when setting status code to 1xx, 204 or 304.

* Jun 20, 2016: Nicer messages on startup.

* Jun 20, 2016: Updated for new `lib-server` and `libsock`.

* Jun 16, 2016: HttpResponse date handling now utilizes a faster (and dirtier) solution then the standard libc `gmtime_r` and `strftime` solutions.

* Jun 12, 2016: HTTP protocol and HttpResponse `sendfile` and `HttpResponse.sendfile` fixes and optimizations. Now file sending uses file descriptors instead of `FILE *`, avoiding the memory allocations related to `FILE *` data.

* Jun 12, 2016: HttpResponse copy optimizes the first header buffer packet to copy as much of the body as possible into the buffer packet, right after the headers.

* Jun 12, 2016: Optimized mime type search for static file service.

* Jun 9, 2016: Rewrote the HttpResponse implementation to leverage `libsock`'s direct user-land buffer packet injection, minimizing user land data-copying.

* Jun 9, 2016: rewrote the HTTP `sendfile` handling for public folder settings.

* Jun 9, 2016: Fixed an issue related to the new pooling scheme, where old data would persist in some pooled request objects.

* Jun 8, 2016: The HttpRequest object is now being pooled within the request library (not the HTTP protocol implementation) using Atomics (less mutex locking) and minimizing memory fragmentation by pre-initializing the buffer on first request (preventing memory allocated after the first request from getting "stuck behind" any of the pool members).

Jun 7, 2016: Baseline (changes not logged before this point in time).

## Websocket extension

* Resolved issue #6, Credit to @Filly for exposing the issue.

* Memory pool removed. Might be reinstated after patching it up, but when more tests were added, the memory pool failed them.

* Jan 12, 2017: Memory Performance.

     The Websocket connection Protocol now utilizes both a C level memory pool and a local thread storage for temporary data. This helps mitigate possible memory fragmentation issues related to long running processes and long-lived objects.

     In addition, the socket `read` buffer was moved from the protocol object to a local thread storage (assumes pthreads and not green threads). This minimizes the memory footprint for each connection (at the expense of memory locality) and should allow Iodine to support more concurrent connections using less system resources.

     Last, but not least, the default message buffer per connection starts at 4Kb instead of 16Kb (grows as needed, up to `Iodine::Rack.max_msg_size`), assuming smaller messages are the norm.

### Date 20160607

Baseline (changes not logged before this point in time).
