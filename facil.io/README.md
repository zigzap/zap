<p align="center"><img src="https://s33.postimg.cc/5d7j1nacf/Readme_1.jpg"></p>

[![GitHub](https://img.shields.io/badge/Open%20Source-MIT-blue.svg)](https://github.com/boazsegev/facil.io)
[![Build Status](https://travis-ci.org/boazsegev/facil.io.svg?branch=master)](https://travis-ci.org/boazsegev/facil.io)
[![Codacy Badge](https://api.codacy.com/project/badge/Grade/2abeba588afb444ca6d92e68ccfbe36b)](https://www.codacy.com/app/boazsegev/facil.io?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=boazsegev/facil.io&amp;utm_campaign=Badge_Grade)
[![codecov](https://codecov.io/gh/boazsegev/facil.io/branch/master/graph/badge.svg)](https://codecov.io/gh/boazsegev/facil.io)

[facil.io](http://facil.io) is a C micro-framework for web applications. facil.io includes:

* A fast HTTP/1.1 and Websocket static file + application server.
* Support for custom network protocols for both server and client connections.
* Dynamic types designed with web applications in mind (Strings, Hashes, Arrays etc').
* Performant JSON parsing and formatting for easy network communication.
* A pub/sub process cluster engine for local and Websocket pub/sub.
* Optional connectivity with Redis.

[facil.io](http://facil.io) provides high performance TCP/IP network services to Linux / BSD (and macOS) by using an evented design (as well as thread pool and forking support) and provides an easy solution to [the C10K problem](http://www.kegel.com/c10k.html).

You can read more about [facil.io](http://facil.io) on the [facil.io](http://facil.io) website.

### Important to Note

The master branch on the `git` repo is the development branch and is likely to be broken at any given time (especially when working on major revisions, as I am at the moment).

Please select a release version for any production needs.

### Who's running on `facil.io`

* [Iodine, a Ruby HTTP/Websockets Ruby application server](https://github.com/boazsegev/iodine) is powered by `facil.io` - so everyone using the iodine server is running on facil.io.

* Are you using `facil.io`? Let me know!

### An HTTP example

```c
#include "http.h" /* the HTTP facil.io extension */

// We'll use this callback in `http_listen`, to handles HTTP requests
void on_request(http_s *request);

// These will contain pre-allocated values that we will use often
FIOBJ HTTP_X_DATA;

// Listen to HTTP requests and start facil.io
int main(int argc, char const **argv) {
  // allocating values we use often
  HTTP_X_DATA = fiobj_str_new("X-Data", 6);
  // listen on port 3000 and any available network binding (NULL == 0.0.0.0)
  http_listen("3000", NULL, .on_request = on_request, .log = 1);
  // start the server
  facil_run(.threads = 1);
  // deallocating the common values
  fiobj_free(HTTP_X_DATA);
}

// Easy HTTP handling
void on_request(http_s *request) {
  http_set_cookie(request, .name = "my_cookie", .name_len = 9, .value = "data",
                  .value_len = 4);
  http_set_header(request, HTTP_HEADER_CONTENT_TYPE,
                  http_mimetype_find("txt", 3));
  http_set_header(request, HTTP_X_DATA, fiobj_str_new("my data", 7));
  http_send_body(request, "Hello World!\r\n", 14);
}
```

## Using `facil.io` in your project

It's possible to either start a new project with `facil.io` or simply add it to an existing one. GNU `make` is the default build system and CMake is also supported.

`facil.io` should be C99 compatible.

### Starting a new project with `facil.io`

To start a new project using the `facil.io` framework, run the following command in the terminal (change `appname` to whatever you want):

     $ bash <(curl -s https://raw.githubusercontent.com/boazsegev/facil.io/master/scripts/new/app) appname

You can [review the script here](scripts/new/app). In short, it will create a new folder, download a copy of the stable branch, add some demo boiler plate code and run `make clean` (which is required to build the `tmp` folder structure).

Next, edit the `makefile` to remove any generic features you don't need, such as the `DUMP_LIB` feature, the `DEBUG` flag or the `DISAMS` disassembler and start development.

Credit to @benjcal for suggesting the script.

**Notice: The *master* branch is the development branch. Please select the latest release tag for the latest stable release version.**

### Adding facil.io to an existing project

[facil.io](http://facil.io) is a source code library, so it's easy to copy the source code into an existing project and start using the library right away.

The `make libdump` command will dump all the relevant files in a single folder called `libdump`, and you can copy them all or divide them into header ands source files.

It's also possible to compile the facil.io library separately using the `make lib` command.

### Using `facil.io` as a CMake submodule

[facil.io](http://facil.io) also supports both `git` and CMake submodules. Credit to @OwenDelahoy (PR#8).

First, add the repository as a submodule using `git`:

    git submodule add https://github.com/boazsegev/facil.io.git

Then add the following line the project's `CMakeLists.txt`

    add_subdirectory(facil.io)

## More Examples

The examples folder includes code examples for a [telnet echo protocol](examples/raw-echo.c), a [Simple Hello World server](examples/raw-http.c), an example for [Websocket pub/sub with (optional) Redis](examples/http-chat.c), etc'.

You can find more information on the [facil.io](http://facil.io) website

---

## Forking, Contributing and all that Jazz

[The contribution guide can be found here](CONTRIBUTING.md).

Sure, why not. If you can add Solaris or Windows support to `evio` and `sock`, that could mean `facil` would become available for use on these platforms as well.

If you encounter any issues, open an issue (or, even better, a pull request with a fix) - that would be great :-)

Hit me up if you want to:

* Write tests... I always need more tests...

* Help me write HPACK / HTTP2 protocol support.

* Help me design / write a generic HTTP routing helper library for the `http_s` struct.

* If you want to help me write a new SSL/TLS library or have an SSL/TLS solution we can fit into `facil` (as source code)... Note: SSL/TLS solutions should fit both client and server modes.

* If you want to help promote the library, that would be great as well. Perhaps publish [benchmarks](https://github.com/TechEmpower/FrameworkBenchmarks) or share your story.

* Writing documentation into the `facil.io` website would be great. I keep the source code documentation fairly updated, but the documentation should be copied to the `docs` folder to get the documentation website up and running.

<p align="center"><img src="https://s33.postimg.cc/seo47ha0v/Readme_2.jpg"></p>
