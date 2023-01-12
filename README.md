# ⚡zap⚡ - blazingly fast backends

Zap is intended to become the [zig](https://ziglang.org) replacement for the
kind of REST APIs I used to write in [python](https://python.org) with
[Flask](https://flask.palletsprojects.com) and
[mongodb](https://www.mongodb.com), etc.

What I need for that is a blazingly fast, robust HTTP server that I can use with
zig. While facil.io supports TLS, I don't mind HTTPS. In production, I use
[nginx](https://www.nginx.com) as a reverse proxy anyway.

Zap wraps and patches [facil.io - the C web application
framework](https://facil.io).

**⚡ZAP⚡ IS SUPER ALPHA**

Here's what works:

- **Super easy build process**: zag's `build.zig` fetches git sub-modules,
  applies a patch to facil.io's logging for microsecond precision, builds and
  optionally runs everything.
  - _tested on Linux and macOS (arm, M1)_
- **[hello](examples/hello/hello.zig)**: welcomes you with some static
  HTML
- **[routes](examples/routes/routes.zig)**: a super easy example
  dispatching on the HTTP path 
- **[serve](examples/serve/serve.zig)**: the traditional static web
  server with optional dynamic request handling

I'll continue wrapping more of facil.io's functionality and adding stuff to zap
to a point where I can use it as the JSON REST API backend for my current
research project which is likely to need to go live in the summer of 2023.

## Getting started

```shell
$ git clone https://github.com/renerocksai/zap.git
$ cd zap 
$ zig build run-hello
$ # open http://localhost:3000 in your browser
```
... and open [http://localhost:3000](http://locahhost:3000) in your browser.

## Examples

You build and run the examples via:

```shell
$ zig build hello [EXAMPLE]
$ ./zig-out/bin/[EXAMPLE]
```

... where `[EXAMPLE]` is one of `hello`, `routes`, or `serve`.

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

fn on_request(r: zap.SimpleRequest) void {
    if (r.path) |the_path| {
        std.debug.print("PATH: {s}\n", .{the_path});
    }

    if (r.query) |the_query| {
        std.debug.print("QUERY: {s}\n", .{the_query});
    }
    _ = r.sendBody("<html><body><h1>Hello from ZAP!!!</h1></body></html>");
}

pub fn main() !void {
    var listener = zap.SimpleHttpListener.init(.{
        .port = 3000,
        .on_request = on_request,
        .log = false,
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

### [serve](examples/serve/serve.zig)

```zig
const std = @import("std");
const zap = @import("zap");

fn on_request(r: zap.SimpleRequest) void {
    // TODO: send 404 response
    _ = r.sendBody("<html><body><h1>404 - File not found</h1></body></html>");
}

pub fn main() !void {
    var listener = zap.SimpleHttpListener.init(.{
        .port = 3000,
        .on_request = on_request,
        .public_folder = std.mem.span("examples/serve"),
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

### [routes](examples/routes/routes.zig)


```zig
const std = @import("std");
const zap = @import("zap");

fn dispatch_routes(r: zap.SimpleRequest) void {
    // dispatch
    if (r.path) |the_path| {
        if (routes.get(the_path)) |foo| {
            foo(r);
        }
    }
    // or default: present menu
    _ = r.sendBody(
        \\ <html>
        \\   <body>
        \\     <p><a href="/static">static</a></p>
        \\     <p><a href="/dynamic">dynamic</a></p>
        \\   </body>
        \\ </html>
    );
}

fn static_site(r: zap.SimpleRequest) void {
    _ = r.sendBody("<html><body><h1>Hello from STATIC ZAP!</h1></body></html>");
}

var dynamic_counter: i32 = 0;
fn dynamic_site(r: zap.SimpleRequest) void {
    dynamic_counter += 1;
    var buf: [128]u8 = undefined;
    const filled_buf = std.fmt.bufPrintZ(
        &buf,
        "<html><body><h1>Hello # {d} from DYNAMIC ZAP!!!</h1></body></html>",
        .{dynamic_counter},
    ) catch "ERROR";
    _ = r.sendBody(std.mem.span(filled_buf));
}

fn setup_routes(a: std.mem.Allocator) !void {
    routes = std.StringHashMap(zap.SimpleHttpRequestFn).init(a);
    try routes.put("/static", static_site);
    try routes.put("/dynamic", dynamic_site);
}

var routes: std.StringHashMap(zap.SimpleHttpRequestFn) = undefined;

pub fn main() !void {
    try setup_routes(std.heap.page_allocator);
    var listener = zap.SimpleHttpListener.init(.{
        .port = 3000,
        .on_request = dispatch_routes,
        .log = true,
    });
    try listener.listen();

    std.debug.print("Listening on 0.0.0.0:3000\n", .{});

    zap.start(.{
        .threads = 2,
        .workers = 2,
    });
}
```
