# Authentication

Zap supports both Basic and Bearer authentication which are based on HTTP
headers.

For a cookie-based ("session token", not to mistake for "session cookie")
authentication, see the [UserPassSession](../src/http_auth.zig#L319) and its
[example](../examples/userpass_session_auth/).

For convenience, Authenticator types exist that can authenticate requests.

Zap also provides an `Endpoint.Authenticating` endpoint-wrapper. Have a look at the [example](../examples/endpoint_auth) and the [tests](../src/tests/test_auth.zig).

The following describes the Authenticator types. All of them provide the
`authenticateRequest()` function, which takes a `zap.Request` and returns
a bool value whether it could be authenticated or not.

Further down, we show how to use the Authenticators, and also the
`Endpoint.Authenticating`.

## Basic Authentication

The `zap.Auth.Basic` Authenticator accepts 2 comptime values:

- `Lookup`: either a map to look up passwords for users or a set to lookup
  base64 encoded tokens (user:pass -> base64-encode = token)
- `kind` : 
    - `UserPass` : decode the authentication header, split into user and
      password, then lookup the password in the provided map and compare it.
    - `Token68` : don't bother decoding, the 'lookup' set is filled with
      base64-encoded tokens, so a fast lookup is enough.

Maps passed in as `Lookup` type must support `get([]const u8)`, and sets must
support `contains([]const u8)`.

## Bearer Authentication

The `zap.Auth.BearerSingle` Authenticator is a convenience-authenticator that
takes a single auth token. If all you need is to protect your prototype with a
token, this is the one you want to use.

`zap.BearerMulti` accepts a map (`Lookup`) that needs to support
`contains([]const u8)`.

## Request Authentication

Here we describe how to authenticate requests from within your `on_request`
callback.

### Single-Token Bearer Authentication

```zig
const std = @import("std");
const zap = @import("zap");

const allocator = std.heap.page_allocator;
const token = "hello, world";

var auth = try zap.Auth.BearerSingle.init(allocator, token, null);
defer auth.deinit();


fn on_request(r: zap.Request) void {
    if(authenticator.authenticateRequest(r)) {
        r.sendBody(
            \\ <html><body>
            \\   <h1>Hello from ZAP!!!</h1>
            \\ </body></html>
        ) catch return;
    } else {
        r.setStatus(.unauthorized);
        r.sendBody("UNAUTHORIZED") catch return;
    }
}
```

### Multi-Token Bearer Authentication

```zig
const std = @import("std");
const zap = @import("zap");

const allocator = std.heap.page_allocator;
const token = "hello, world";

const Set = std.StringHashMap(void);
var set = Set.init(allocator); // set
defer set.deinit();

// insert auth tokens
try set.put(token, {});

var auth = try zap.Auth.BearerMulti(Set).init(allocator, &set, null);
defer auth.deinit();


fn on_request(r: zap.Request) void {
    if(authenticator.authenticateRequest(r)) {
        r.sendBody(
            \\ <html><body>
            \\   <h1>Hello from ZAP!!!</h1>
            \\ </body></html>
        ) catch return;
    } else {
        r.setStatus(.unauthorized);
        r.sendBody("UNAUTHORIZED") catch return;
    }
}
```

### UserPass Basic Authentication

```zig
const std = @import("std");
const zap = @import("zap");

const allocator = std.heap.page_allocator;

// create a set of User -> Pass entries
const Map = std.StringHashMap([]const u8);
var map = Map.init(allocator);
defer map.deinit();

// create user / pass entry
const user = "Alladdin";
const pass = "opensesame";
try map.put(user, pass);

// create authenticator
const Authenticator = zap.Auth.Basic(Map, .UserPass);
var auth = try Authenticator.init(a, &map, null);
defer auth.deinit();


fn on_request(r: zap.Request) void {
    if(authenticator.authenticateRequest(r)) {
        r.sendBody(
            \\ <html><body>
            \\   <h1>Hello from ZAP!!!</h1>
            \\ </body></html>
        ) catch return;
    } else {
        r.setStatus(.unauthorized);
        r.sendBody("UNAUTHORIZED") catch return;
    }
}
```


### Token68 Basic Authentication

```zig
const std = @import("std");
const zap = @import("zap");

const allocator = std.heap.page_allocator;
const token = "QWxhZGRpbjpvcGVuIHNlc2FtZQ==";

// create a set of Token68 entries
const Set = std.StringHashMap(void);
var set = Set.init(allocator); // set
defer set.deinit();
try set.put(token, {});

// create authenticator
const Authenticator = zap.Auth.Basic(Set, .Token68);
var auth = try Authenticator.init(allocator, &set, null);
defer auth.deinit();


fn on_request(r: zap.Request) void {
    if(authenticator.authenticateRequest(r)) {
        r.sendBody(
            \\ <html><body>
            \\   <h1>Hello from ZAP!!!</h1>
            \\ </body></html>
        ) catch return;
    } else {
        r.setStatus(.unauthorized);
        r.sendBody("UNAUTHORIZED") catch return;
    }
}
```

## Endpoint.Authenticating

Here, we only show using one of the Authenticator types. See the tests for more
examples.

The `Endpoint.Authenticating` honors `.unauthorized` in the endpoint settings, where you can pass in a callback to deal with unauthorized requests. If you leave it to `null`, the endpoint will automatically reply with a `401 - Unauthorized` response.

The example below should make clear how to wrap an endpoint into an
`Endpoint.Authenticating`:

```zig
const std = @import("std");
const zap = @import("zap");

const a = std.heap.page_allocator;
const token = "ABCDEFG";

const HTTP_RESPONSE: []const u8 =
    \\ <html><body>
    \\   Hello from ZAP!!!
    \\ </body></html>
;

// authenticated requests go here
fn endpoint_http_get(e: *zap.Endpoint, r: zap.Request) void {
    _ = e;
    r.sendBody(HTTP_RESPONSE) catch return;
}

// just for fun, we also catch the unauthorized callback
fn endpoint_http_unauthorized(e: *zap.Endpoint, r: zap.Request) void {
    _ = e;
    r.setStatus(.unauthorized);
    r.sendBody("UNAUTHORIZED ACCESS") catch return;
}

pub fn main() !void {
    // setup listener
    var listener = zap.EndpointListener.init(
        a,
        .{
            .port = 3000,
            .on_request = null,
            .log = true,
            .max_clients = 10,
            .max_body_size = 1 * 1024,
        },
    );
    defer listener.deinit();

    // create mini endpoint
    var ep = zap.Endpoint.init(.{
        .path = "/test",
        .get = endpoint_http_get,
        .unauthorized = endpoint_http_unauthorized,
        .unset = Endpoint.dummy_handler,
    });

    // create authenticator
    const Authenticator = zap.Auth.BearerSingle;
    var authenticator = try Authenticator.init(a, token, null);
    defer authenticator.deinit();

    // create authenticating endpoint
    const BearerAuthEndpoint = zap.Endpoint.Authenticating(Authenticator);
    var auth_ep = BearerAuthEndpoint.init(&ep, &authenticator);

    try listener.register(auth_ep.endpoint());

    listener.listen() catch {};
    std.debug.print(
        \\ Run the following:
        \\ 
        \\ curl http://localhost:3000/test -i -H "Authorization: Bearer ABCDEFG" -v
        \\ curl http://localhost:3000/test -i -H "Authorization: Bearer invalid" -v
        \\
        \\ and see what happens
        \\
    , .{});

    // start worker threads
    zap.start(.{
        .threads = 1,
        .workers = 1,
    });
}
```


