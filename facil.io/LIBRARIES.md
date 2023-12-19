# Independent libraries offered by facil.io 

The facil.io framework is based on a modular design, which means many of the modules can be extracted and used independently as separate libraries.

## Single file libraries

The following libraries consist of a single header file that can be used independently.

Simply copy the header file to your project and enjoy.

Please note that this isn't a comprehensive list (for example, the Base64 library and SHA256 libraries aren't mentioned).

### Types:

These type libraries are designed to make many common tasks easy while offering an easy to use API.

They are all designed to use a data container (that can be allocated either on the stack or on the heap) as well as dynamic memory management, for maximum flexibility.

And although they often prefer ease of use over performance, they are very libraries.

* [Dynamic String Library](lib/facil/core/types/fiobj/fio_str.h): this library is easy to use and helps with authoring binary and C Strings.

    For example:

    ```c
    // container on the stack
    fio_str_s str = FIO_STR_INIT;
    fio_str_write(&str, "Hello", 5);
    fio_str_printf(&str, " world, %d", 42);
    printf("%s\n", fio_str_data(&str)); // "Hello world, 42"
    fio_str_free(&str);

    // container on the heap
    fio_str_s *str = malloc(sozeof(*str));
    *str = FIO_STR_INIT;
    // use ... and ... free when done:
    fio_str_free(str);
    free(str);
    ```

    It should be noted that short Strings (up to 30 bytes on 64bit machines) will be stored within the container without additional memory allocations, improving performance for many common use cases.

* [Dynamic Array Library](lib/facil/core/types/fiobj/fio_ary.h): was designed to make dynamic arrays easy to handle.

    For example:

    ```c
    // container on the stack (can also be placed on the heap).
    fio_ary_s ary = FIO_ARY_INIT;
    fio_ary_push(&ary, (void *)1);
    printf("Array pop value: %zd", (size_t)fio_ary_pop(&ary));
    fio_ary_free(&ary);
    ```

* [Dynamic Hash Map Library](lib/facil/core/types/fiobj/fio_hashmap.h): was designed to make Hash maps a breeze.

    The following example uses `void *` types for values and `uint64_t` types for keys, but it's easy enough to use Strings or any other data type as keys:

    ```c
    // container on the stack (can also be placed on the heap).
    fio_hash_s hash = FIO_HASH_INIT;
    fio_hash_insert(&hash, 1, (void *)1);
    printf("Hash seek key %u => value: %zd", 1, fio_hash_find(&hash, 1));
    printf("Hash seek key %u => value: %zd", 2, fio_hash_find(&hash, 2));
    fio_hash_free(&ary); // use FIO_HASH_FOR_FREE to free object data or custom keys.
    ```


* [Linked Lists Library](lib/facil/core/types/fiobj/fio_llist.h): was designed to make linked lists a breeze.

    The library supports both flavors of linked lists, external (node contains a pointer) and embedded (nodes contain actual data).

### Parsers:

The single header parser libraries often declare callbacks that should be defined (implemented) by the file that includes the library.

For example, the JSON parser expects the client to implement the `fio_json_on_null`. The callbacks should be used to build the data structure that contains the results.

These single-file parsers include:

* [JSON parser](lib/facil/core/types/fiobj/fio_json_parser.h): JSON stands for JavaScript Object Notation and is commonly used for data exchange. More details about JSON at [json.org](http://json.org).

* [Mustache template parser](lib/facil/core/types/fiobj/mustache_parser.h): Mustache is a templating scheme. More details about mustache at [mustache.github.io](http://mustache.github.io).

* [RESP parser](lib/facil/redis/resp_parser.h): RESP (REdis Serialization Protocol) is the protocol used by Redis for data transfer and communications. More details about RESP at [redis.io](https://redis.io/topics/protocol).

* [WebSockets parser](lib/facil/http/parsers/mustache_parser.h): WebSockets are used for bi-directional communication across the web. Unlike raw TCP/IP, this added layer converts the communication scheme from streaming based communication to message based communication, preserving message boundaries. More details about WebSockets at [websocket.org](https://www.websocket.org/aboutwebsocket.html).

* [MIME Multipart parser](ib/facil/http/parsers/http_mime_parser.h): This parser decodes HTTP multipart data used in form data submissions (i.e., when uploading a file or submitting a form using POST). More details about MIME at [wikipedia.org](https://en.wikipedia.org/wiki/MIME).

An honorary mention goes out to the HTTP/1.1 parser. It is a 2 file parser library ([header file](lib/facil/http/parsers/http1_parser.h) and [source file](lib/facil/http/parsers/http1_parser.c)) that is totally independent from the IO layer or the rest of facil.io.

## Two File Libraries

The following libraries consist of both a header and a source file that can be used independently.

Simply copy both files to your project and enjoy.

### Memory Allocator

The `fio_mem` library is a custom memory allocator that was designed for network use, minimizing lock contention and offering minimal allocation overhead.

I wrote it to solve an issue I had with memory fragmentation and race conditions during multi-threaded allocations.

The allocator doesn't return all the memory to the system. Instead, there's a memory pool that is retained, improving concurrency even across process borders by minimizing system calls.

More details can be found in [the header file `fio_mem.h`](lib/facil/core/types/fiobj/fio_mem.h) and [the implementation file `fio_mem.c`](lib/facil/core/types/fiobj/fio_mem.c).
