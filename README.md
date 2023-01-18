# ⚡zap⚡ - blazingly fast backends in zig

Zap is intended to become the [zig](https://ziglang.org) replacement for the
kind of REST APIs I used to write in [python](https://python.org) with
[Flask](https://flask.palletsprojects.com) and
[mongodb](https://www.mongodb.com), etc. It can be considered to be a
microframework for web applications.

What I need for that is a blazingly fast, robust HTTP server that I can use with
zig. While facil.io supports TLS, I don't care about HTTPS support. In
production, I use [nginx](https://www.nginx.com) as a reverse proxy anyway.

Zap wraps and patches [facil.io - the C web application
framework](https://facil.io).

**⚡ZAP⚡ IS SUPER ALPHA**

_Under the hood, everything is super robust and fast. My zig wrappers are fresh,
juicy, and alpha._

Here's what works:

- **Super easy build process**: zap's `build.zig` fetches git sub-modules,
  applies a patch to facil.io's logging for microsecond precision, builds and
  optionally runs everything.
  - _tested on Linux and macOS (arm, M1)_
- **[hello](examples/hello/hello.zig)**: welcomes you with some static
  HTML
- **[routes](examples/routes/routes.zig)**: a super easy example
  dispatching on the HTTP path 
- **[serve](examples/serve/serve.zig)**: the traditional static web
  server with optional dynamic request handling
- **[hello_json](examples/hello_json/hello_json.zig)**: serves you json
  dependent on HTTP path
- **[endpoint](examples/endpoint/)**: a simple JSON REST API example featuring
  a `/users` endpoint for PUTting/DELETE-ing/GET-ting/POST-ing and listing
  users, together with a static HTML and JavaScript frontend to play with.

I'll continue wrapping more of facil.io's functionality and adding stuff to zap
to a point where I can use it as the JSON REST API backend for real research
projects, serving thousands of concurrent clients.

## ⚡blazingly fast⚡

Claiming to be blazingly fast is the new black. At least, zap doesn't slow you
down and if your server performs poorly, it's probably not exactly zap's fault.
Zap relies on the [facil.io](https://facil.io) framework and so it can't really
claim any performance fame for itself. In this initial implementation of zap,
I didn't care about optimizations at all.

But, how fast is it? Being blazingly fast is relative. When compared with a
simple GO HTTP server, a simple zig zap HTTP server performed really good on my
machine:

- zig zap was nearly 30% faster than GO
- zig zap had over 50% more throughput than GO

**Update**: I was intrigued comparing to a basic rust HTTP server.
Unfortunately, knowing nothing at all about rust, I couldn't find one and hence
tried to go for the one in [The Rust Programming
Language](https://doc.rust-lang.org/book/ch20-00-final-project-a-web-server.html).
Wanting it to be of a somewhat fair comparison, I opted for the multi-threaded
example. It didn't work out-of-the-box, but I got it to work and changed it to
not read files but outputting a static text just like in the other examples.
**maybe someone with rust experience** can have a look at my
[wrk/rust/hello](wrk/rust/hello) code and tell me why it's surprisingly slow, as
I expected it to be faster than the basic GO example. I'll enable the
GitHub discussions for this matter. My suspicion is bad performance of the
mutexes.

![](wrk_table_summary.png)

![](wrk_charts_summary.png)

So, being somewhere in the ballpark of basic GO performance, zig zap seems to be
... of reasonable performance 😎.

See more details in [blazingly-fast.md](blazingly-fast.md).

## Getting started

```shell
$ git clone https://github.com/renerocksai/zap.git
$ cd zap 
$ zig build run-hello
$ # open http://localhost:3000 in your browser
```

... and open [http://localhost:3000](http://locahhost:3000) in your browser.

## Using ⚡zap⚡ in your own projects 

If you don't have an existing zig project, create one like this:

```shell 
$ mkdir zaptest && cd zaptest
$ zig init-exe
$ git init 
```

With an existing zig project, adding zap to it is easy:

1. Add zap as a git submodule 
2. Add zap to your `build.zig` 

To add zap as a git submodule:

```shell 
$ mkdir libs
$ git submodule add https://github.com/renerocksai/zap.git libs/zap
```

Then, add the following at the top of your `build.zig`:

```zig
const zap_builder = @import("./libs/zap/build.zig");

const zap = std.build.Pkg{
    .name = "zap",
    .source = std.build.FileSource{ .path = "./libs/zap/src/zap.zig" },
};
```

In the `build` function, add the following before `exe.install()`:

```zig 
    exe.addPackage(zap);
    zap_builder.addZap(exe, "./libs/zap/") catch unreachable;
```

From then on, you can use the zap package in your project. Check out the
examples to see how to use zap.

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
    r.setStatus(404);
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

### [hello_json](examples/hello_json/hello_json.zig)

```zig 
const std = @import("std");
const zap = @import("zap");

const User = struct {
    first_name: ?[]const u8 = null,
    last_name: ?[]const u8 = null,
};

fn on_request(r: zap.SimpleRequest) void {
    if (!std.mem.eql(u8, r.method.?, "GET"))
        return;

    // /user/n
    if (r.path) |the_path| {
        if (the_path.len < 7 or !std.mem.startsWith(u8, the_path, "/user/"))
            return;

        const user_id: usize = @intCast(usize, the_path[6] - 0x30);
        const user = users.get(user_id);

        var json_to_send: []const u8 = undefined;
        if (stringify(user, .{})) |json| {
            json_to_send = json;
        } else {
            json_to_send = "null";
        }
        std.debug.print("<< json: {s}\n", .{json_to_send});
        r.setContentType(.JSON);
        _ = r.sendBody(json_to_send);
    }
}

const UserMap = std.AutoHashMap(usize, User);

var users: UserMap = undefined;
fn setupUserData(a: std.mem.Allocator) !void {
    users = UserMap.init(a);
    try users.put(1, .{ .first_name = "renerocksai" });
    try users.put(2, .{ .first_name = "Your", .last_name = "Mom" });
}

fn stringify(value: anytype, options: std.json.StringifyOptions) ?[]const u8 {
    var buf: [100]u8 = undefined;
    var fba = std.heap.FixedBufferAllocator.init(&buf);
    var string = std.ArrayList(u8).init(fba.allocator());
    if (std.json.stringify(value, options, string.writer())) {
        return string.items;
    } else |_| { // error
        return null;
    }
}

pub fn main() !void {
    var a = std.heap.page_allocator;
    try setupUserData(a);
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

### [endpoint](examples/endpoint/)

The following only shows the GET functionality implemented on the `/users/`
See [endpoint](examples/endpoint/) for a complete
example, including an HTML + JavaScript frontend.

[`main.zig`](examples/endpoint/main.zig):

```zig

const std = @import("std");
const zap = @import("zap");
const Endpoint = @import("endpoint.zig");

pub fn main() !void {
    const allocator = std.heap.page_allocator;
    // setup listener
    var listener = zap.SimpleEndpointListener.init(
        allocator,
        .{
            .port = 3000,
            .on_request = null,
            .log = true,
            .public_folder = "./examples/endpoint/html",
        },
    );

    Endpoint.init(allocator, "/users");

    // add endpoint
    try listener.addEndpoint(Endpoint.getUserEndpoint());

    // fake some users
    var uid: usize = undefined;
    uid = try Endpoint.getUsers().addByName("renerocksai", null);
    uid = try Endpoint.getUsers().addByName("renerocksai", "your mom");

    // listen
    try listener.listen();

    std.debug.print("Listening on 0.0.0.0:3000\n", .{});

    // and run
    zap.start(.{
        .threads = 2,
        .workers = 2,
    });
}
```


[`endpoint.zig`](examples/endpoint/endpoint.zig):

```zig
const std = @import("std");
const zap = @import("zap");
const Users = @import("users.zig");
const User = Users.User;

// the Endpoint

pub const Self = @This();

var alloc: std.mem.Allocator = undefined;
var endpoint: zap.SimpleEndpoint = undefined;
var users: Users = undefined;

pub fn init(
    a: std.mem.Allocator,
    user_path: []const u8,
) void {
    users = Users.init(a);
    alloc = a;
    endpoint = zap.SimpleEndpoint.init(.{
        .path = user_path,
        .get = getUser,
        .post = postUser,
        .put = putUser,
        .delete = deleteUser,
    });
}

pub fn getUsers() *Users {
    return &users;
}

pub fn getUserEndpoint() *zap.SimpleEndpoint {
    return &endpoint;
}

fn userIdFromPath(path: []const u8) ?usize {
    if (path.len >= endpoint.settings.path.len + 2) {
        if (path[endpoint.settings.path.len] != '/') {
            return null;
        }
        const idstr = path[endpoint.settings.path.len + 1 ..];
        return std.fmt.parseUnsigned(usize, idstr, 10) catch null;
    }
    return null;
}

fn getUser(e: *zap.SimpleEndpoint, r: zap.SimpleRequest) void {
    if (r.path) |path| {
        // /users
        if (path.len == e.settings.path.len) {
            return listUsers(e, r);
        }
        if (userIdFromPath(path)) |id| {
            if (users.get(id)) |user| {
                if (zap.stringify(user, .{})) |json| {
                    _ = r.sendJson(json);
                }
            }
        }
    }
}

fn listUsers(e: *zap.SimpleEndpoint, r: zap.SimpleRequest) void {
    _ = e;
    var l: std.ArrayList(User) = std.ArrayList(User).init(alloc);
    if (users.list(&l)) {} else |_| {
        return;
    }
    if (zap.stringifyArrayList(User, &l, .{})) |maybe_json| {
        if (maybe_json) |json| {
            _ = r.sendJson(json);
        }
    } else |_| {
        return;
    }
}

// ...
```

