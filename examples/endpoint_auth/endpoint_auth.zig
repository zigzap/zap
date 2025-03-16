const std = @import("std");
const zap = @import("zap");

const a = std.heap.page_allocator;
const token = "ABCDEFG";

const HTTP_RESPONSE: []const u8 =
    \\ <html><body>
    \\   Hello from ZAP!!!
    \\ </body></html>
;

const Endpoint = struct {
    // the slug
    path: []const u8,

    // authenticated requests go here
    pub fn get(_: *Endpoint, r: zap.Request) void {
        r.sendBody(HTTP_RESPONSE) catch return;
    }

    // just for fun, we also catch the unauthorized callback
    pub fn unauthorized(_: *Endpoint, r: zap.Request) void {
        r.setStatus(.unauthorized);
        r.sendBody("UNAUTHORIZED ACCESS") catch return;
    }

    // not implemented, don't care
    pub fn post(_: *Endpoint, _: zap.Request) void {}
    pub fn put(_: *Endpoint, _: zap.Request) void {}
    pub fn delete(_: *Endpoint, _: zap.Request) void {}
    pub fn patch(_: *Endpoint, _: zap.Request) void {}
    pub fn options(_: *Endpoint, _: zap.Request) void {}
};

pub fn main() !void {
    // setup listener
    var listener = zap.Endpoint.Listener.init(
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
    var ep: Endpoint = .{
        .path = "/test",
    };

    // create authenticator
    const Authenticator = zap.Auth.BearerSingle;
    var authenticator = try Authenticator.init(a, token, null);
    defer authenticator.deinit();

    // create authenticating endpoint
    const BearerAuthEndpoint = zap.Endpoint.Authenticating(Endpoint, Authenticator);
    var auth_ep = BearerAuthEndpoint.init(&ep, &authenticator);

    try listener.register(&auth_ep);

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
