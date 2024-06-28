# ⚡zap⚡ - blazingly fast backends in zig

![](https://github.com/zigzap/zap/actions/workflows/build-current-zig.yml/badge.svg) ![](https://github.com/zigzap/zap/actions/workflows/mastercheck.yml/badge.svg) [![Discord](https://img.shields.io/discord/1107835896356675706?label=chat&logo=discord&style=plastic)](https://discord.gg/jQAAN6Ubyj)

Zap is the [zig](https://ziglang.org) replacement for the REST APIs I used to
write in [python](https://python.org) with
[Flask](https://flask.palletsprojects.com) and
[mongodb](https://www.mongodb.com), etc. It can be considered to be a
microframework for web applications.

What I needed as a replacement was a blazingly fast and robust HTTP server that
I could use with Zig, and I chose to wrap the superb evented networking C
library [facil.io](https://facil.io). Zap wraps and patches [facil.io - the C
web application framework](https://facil.io).

## **⚡ZAP⚡ IS FAST, ROBUST, AND STABLE**


After having used ZAP in production for a year, I can confidently assert that it
proved to be:

- ⚡ **blazingly fast** ⚡
- 💪 **extremely robust** 💪

Exactly the goals I set out to achieve!

## FAQ:

- Q: **What version of Zig does Zap support?**
    - Zap uses the latest stable zig release (0.13.0), so you don't have to keep
      up with frequent breaking changes. It's an "LTS feature".
- Q: **Can Zap build with Zig's master branch?**
    - See the `zig-master` branch. An example of how to use it is
      [here](https://github.com/zigzap/hello-master). Please note that the
      zig-master branch is not the official master branch of ZAP. Be aware that
      I don't provide `build.zig.zon` snippets or tagged releases for it for
      the time being. If you know what you are doing, that shouldn't stop you
      from using it with zig master though.
- Q: **Where is the API documentation?**
    - Docs are a work in progress. You can check them out
      [here](https://zigzap.org/zap).
    - Run `zig build run-docserver` to serve them locally.
- Q: **Does ZAP work on Windows?**
    - No. This is due to the underlying facil.io C library. Future versions
      of facil.io might support Windows but there is no timeline yet. Your best
      options on Windows are WSL2 or a docker container.
- Q: **Does ZAP support TLS / HTTPS?**
    - Yes, ZAP supports using the system's openssl. See the
      [https](./examples/https/https.zig) example and make sure to build with
      the `-Dopenssl` flag or the environment variable `ZAP_USE_OPENSSL=true`:
      - `.openssl = true,` (in dependent projects' build.zig,
        `b.dependency("zap" .{...})`)
      - `ZAP_USE_OPENSSL=true zig build https`
      - `zig build -Dopenssl=true https`

## Here's what works

I recommend checking out **Endpoint-based examples for more realistic
use cases**. Most of the examples are super stripped down to only include
what's necessary to show a feature.

**NOTE: To see API docs, run `zig build run-docserver`.** To specify a custom
port and docs dir: `zig build docserver && zig-out/bin/docserver --port=8989
--docs=path/to/docs`.

- **Super easy build process**: Zap's `build.zig` now uses the new Zig package
  manager for its C-dependencies, no git submodules anymore.
  - _tested on Linux and macOS (arm, M1)_
- **[hello](examples/hello/hello.zig)**: welcomes you with some static HTML
- **[routes](examples/routes/routes.zig)**: a super easy example dispatching on
  the HTTP path. **NOTE**: The dispatch in the example is a super-basic
  DIY-style dispatch. See endpoint-based examples for more realistic use cases.
- **[serve](examples/serve/serve.zig)**: the traditional static web server with
  optional dynamic request handling
- **[sendfile](examples/sendfile/sendfile.zig)**: simple example of how to send
  a file, honoring compression headers, etc.
- **[bindataformpost](examples/bindataformpost/bindataformpost.zig)**: example
  to receive binary files via form post.
- **[hello_json](examples/hello_json/hello_json.zig)**: serves you json
  dependent on HTTP path
- **[endpoint](examples/endpoint/)**: a simple JSON REST API example featuring a
  `/users` endpoint for performing PUT/DELETE/GET/POST operations and listing
  users, together with a simple frontend to play with. **It also introduces a
  `/stop` endpoint** that shuts down Zap, so **memory leak detection** can be
  performed in main().
    - Check out how [main.zig](examples/endpoint/main.zig) uses ZIG's awesome
      `GeneralPurposeAllocator` to report memory leaks when ZAP is shut down.
      The [StopEndpoint](examples/endpoint/stopendpoint.zig) just stops ZAP when
      receiving a request on the `/stop` route.
- **[mustache](examples/mustache/mustache.zig)**: a simple example using
  [mustache](https://mustache.github.io/) templating.
- **[endpoint authentication](examples/endpoint_auth/endpoint_auth.zig)**: a
  simple authenticated endpoint. Read more about authentication
  [here](./doc/authentication.md).
- **[http parameters](examples/http_params/http_params.zig)**: a simple example
  sending itself query parameters of all supported types.
- **[cookies](examples/cookies/cookies.zig)**: a simple example sending itself a
  cookie and responding with a session cookie.
- **[websockets](examples/websockets/)**: a simple websockets chat for the
  browser.
- **[Username/Password Session
  Authentication](./examples/userpass_session_auth/)**: A convenience
  authenticator that redirects un-authenticated requests to a login page and
  sends cookies containing session tokens based on username/password pairs
  received via POST request.
- **[MIDDLEWARE support](examples/middleware/middleware.zig)**: chain together
  request handlers in middleware style. Provide custom context structs, totally
  type-safe, using **[ZIG-CEPTION](doc/zig-ception.md)**. If you come from GO
  this might appeal to you.
- **[MIDDLEWARE with endpoint
  support](examples/middleware_with_endpoint/middleware_with_endpoint.zig)**:
  Same as the example above, but this time we use an endpoint at the end of the
  chain, by wrapping it via `zap.Middleware.EndpointHandler`. Mixing endpoints
  in your middleware chain allows for usage of Zap's authenticated endpoints and
  your custom endpoints. Since Endpoints use a simpler API, you have to use
  `r.setUserContext()` and `r.getUserContext()` with the request if you want to
  access the middleware context from a wrapped endpoint. Since this mechanism
  uses an `*anyopaque` pointer underneath (to not break the Endpoint API), it is
  less type-safe than `zap.Middleware`'s use of contexts.
- [**Per Request Contexts**](./src/zap.zig#L102) : With the introduction of
  `setUserContext()` and `getUserContext()`, you can, of course use those two in
  projects that don't use `zap.Endpoint` or `zap.Middleware`, too, if you
  really, really, absolutely don't find another way to solve your context
  problem. **We recommend using a `zap.Endpoint`** inside of a struct that
  can provide all the context you need **instead**. You get access to your
  struct in the callbacks via the `@fieldParentPtr()` trick that is used
  extensively in Zap's examples, like the [endpoint
  example](examples/endpoint/endpoint.zig).
- [**Error Trace Responses**](./examples/senderror/senderror.zig): You can now
  call `r.sendError(err, status_code)` when you catch an error and a stack trace
  will be returned to the client / browser.
- [**HTTPS**](examples/https/https.zig): Shows how easy it is to use facil.io's
  openssl support. Must be compiled with `-Dopenssl=true` or the environment
  variable `ZAP_USE_OPENSSL` set to `true` and requires openssl dev dependencies
  (headers, lib) to be installed on the system.
  - run it like this: `ZAP_USE_OPENSSL=true zig build run-https`
    OR like this: `zig build -Dopenssl=true run-https`
  - it will tell you how to generate certificates
- [**simple_router**](examples/simple_router/simple_router.zig): See how you
  can use `zap.Router` to dispatch to handlers by HTTP path.

I'll continue wrapping more of facil.io's functionality and adding stuff to zap
to a point where I can use it as the JSON REST API backend for real research
projects, serving thousands of concurrent clients.


## ⚡blazingly fast⚡

Claiming to be blazingly fast is the new black. At least, Zap doesn't slow you
down and if your server performs poorly, it's probably not exactly Zap's fault.
Zap relies on the [facil.io](https://facil.io) framework and so it can't really
claim any performance fame for itself. In this initial implementation of Zap,
I didn't care about optimizations at all.

But, how fast is it? Being blazingly fast is relative. When compared with a
simple GO HTTP server, a simple Zig Zap HTTP server performed really good on my
machine (x86_64-linux):

- Zig Zap was nearly 30% faster than GO
- Zig Zap had over 50% more throughput than GO

**Update**: Thanks to @felipetrz, I got to test against more realistic Python
and Rust examples. Both python `sanic` and rust `axum` were easy enough to
integrate.

**Update**: I have automated the benchmarks. See
[blazingly-fast.md](./blazingly-fast.md) for more information. Also, thanks to
@alexpyattaev, the benchmarks are fairer now, pinning server and client to
specific CPU cores.

**Update**: I have consolidated the benchmarks to one good representative per
language. See more details in [blazingly-fast.md](./blazingly-fast.md). It
contains rust implementations that come pretty close to Zap's performance in the
simplistic testing scenario.

![](./wrk/samples/README_req_per_sec.png)

![](./wrk/samples/README_xfer_per_sec.png)


So, being somewhere in the ballpark of basic GO performance, zig zap seems to be
... of reasonable performance 😎.

I can rest my case that developing ZAP was a good idea because it's faster than
both alternatives: a) staying with Python, and b) creating a GO + Zig hybrid.

See more details in [blazingly-fast.md](blazingly-fast.md).

## 💪 Robust

ZAP is **very robust**. In fact, it is so robust that I was confidently able to
only work with in-memory data (RAM) in all my ZAP projects so far: over 5 large
online research experiments. No database, no file persistence, until I hit
"save" at the end 😊.

So I was able to postpone my cunning data persistence strategy that's similar to
a mark-and-sweep garbage collector and would only persist "dirty" data when
traffic is low, in favor of getting stuff online more quickly. But even if
implemented, such a persistence strategy is risky because when traffic is not
low, it means the system is under (heavy) load. Would you confidently NOT save
data when load is high and the data changes most frequently -> the potential
data loss is maximized?

To answer that question, I just skipped it. I skipped saving any data until
receiving a "save" signal via API. And it worked. ZAP just kept on zapping. When
traffic calmed down or all experiment participants had finished, I hit "save"
and went on analyzing the data.

Handling all errors does pay off after all. No hidden control flow, no hidden
errors or exceptions is one of Zig's strengths.

To be honest: There are still pitfalls. E.g. if you request large stack sizes
for worker threads, Zig won't like that and panic. So make sure you don't have
local variables that require tens of megabytes of stack space.


### 🛡️ Memory-safe

See the [StopEndpoint](examples/endpoint/stopendpoint.zig) in the
[endpoint](examples/endpoint) example. That example uses ZIG's awesome
`GeneralPurposeAllocator` to report memory leaks when ZAP is shut down. The
`StopEndpoint` just stops ZAP when receiving a request on the `/stop` route.

You can use the same strategy in your debug builds and tests to check if your
code leaks memory.



## Getting started

Make sure you have **zig 0.13.0** installed. Fetch it from
[here](https://ziglang.org/download).

```shell
$ git clone https://github.com/zigzap/zap.git
$ cd zap
$ zig build run-hello
$ # open http://localhost:3000 in your browser
```

... and open [http://localhost:3000](http://localhost:3000) in your browser.

## Using ⚡zap⚡ in your own projects

Make sure you have **the latest zig release (0.13.0)** installed. Fetch it from
[here](https://ziglang.org/download).

If you don't have an existing zig project, create one like this:

```shell
$ mkdir zaptest && cd zaptest
$ zig init
$ git init      ## (optional)
```
**Note**: Nix/NixOS users are lucky; you can use the existing `flake.nix` and run
`nix develop` to get a development shell providing zig and all
dependencies to build and run the GO, python, and rust examples for the
`wrk` performance tests. For the mere building of zap projects,
`nix develop .#build` will only fetch zig 0.11.0. TODO: upgrade to latest zig.

With an existing Zig project, adding Zap to it is easy:

1. Add zap to your `build.zig.zon`
2. Add zap to your `build.zig`

In your zig project folder (where `build.zig` is located), run:

<!-- INSERT_DEP_BEGIN -->
```
zig fetch --save "git+https://github.com/zigzap/zap#v0.9.1"
```
<!-- INSERT_DEP_END -->

Then, in your `build.zig`'s `build` function, add the following before
`b.installArtifact(exe)`:

```zig
    const zap = b.dependency("zap", .{
        .target = target,
        .optimize = optimize,
        .openssl = false, // set to true to enable TLS support
    });

    exe.root_module.addImport("zap", zap.module("zap"));
```

From then on, you can use the Zap package in your project. Check out the
examples to see how to use Zap.

## Updating your project to the latest version of zap

You can change the URL to Zap in your `build.zig.zon`

- easiest: use a tagged release
- or to one of the tagged versions, e.g. `0.0.9`
- or to the latest commit of `zap`

### Using a tagged release

Go to the [release page](https://github.com/zigzap/zap/releases). Every release
will state its version number and also provide instructions for changing
`build.zig.zon` and `build.zig`.

### Using other versions

See [here](./doc/other-versions.md).

## Contribute to ⚡zap⚡ - blazingly fast

At the current time, I can only add to zap what I need for my personal and
professional projects. While this happens **blazingly fast**, some if not all
nice-to-have additions will have to wait. You are very welcome to help make the
world a blazingly fast place by providing patches or pull requests, add
documentation or examples, or interesting issues and bug reports - you'll know
what to do when you receive your calling 👼.

Check out [CONTRIBUTING.md](CONTRIBUTING.md) for more details.

See also [introducing.md](introducing.md) for more on the state and progress of
this project.

**We now have our own [ZAP discord](https://discord.gg/jQAAN6Ubyj) server!!!**

You can also reach me on [the zig showtime discord
server](https://discord.gg/CBzE3VMb) under the handle renerocksai
(renerocksai#1894).

## Support ⚡zap⚡

Being blazingly fast requires a constant feed of caffeine. I usually manage to
provide that to myself for myself. However, to support keeping the juices
flowing and putting a smile on my face and that warm and cozy feeling into my
heart, you can always [buy me a coffee](https://buymeacoffee.com/renerocksai)
☕. All donations are welcomed 🙏 blazingly fast! That being said, just saying
"hi" also works wonders with the smiles, warmth, and coziness 😊.

## Examples

You build and run the examples via:

```shell
$ zig build [EXAMPLE]
$ ./zig-out/bin/[EXAMPLE]
```

... where `[EXAMPLE]` is one of `hello`, `routes`, `serve`, ... see the [list of
examples above](#heres-what-works).

Example: building and running the hello example:

```shell
$ zig build hello
$ ./zig-out/bin/hello
```

To just run an example, like `routes`, without generating an executable, run:

```shell
$ zig build run-[EXAMPLE]
```

Example: building and running the routes example:

```shell
$ zig build run-routes
```

### [hello](examples/hello/hello.zig)

```zig
const std = @import("std");
const zap = @import("zap");

fn on_request(r: zap.Request) void {
    if (r.path) |the_path| {
        std.debug.print("PATH: {s}\n", .{the_path});
    }

    if (r.query) |the_query| {
        std.debug.print("QUERY: {s}\n", .{the_query});
    }
    r.sendBody("<html><body><h1>Hello from ZAP!!!</h1></body></html>") catch return;
}

pub fn main() !void {
    var listener = zap.HttpListener.init(.{
        .port = 3000,
        .on_request = on_request,
        .log = true,
    });
    try listener.listen();

    std.debug.print("Listening on 0.0.0.0:3000\n", .{});

    // start worker threads
    zap.start(.{
        .threads = 2,
        .workers = 2,
    });
}
```




