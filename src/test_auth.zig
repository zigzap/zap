const std = @import("std");
const Authenticators = @import("http_auth.zig");
const zap = @import("zap.zig");
const Endpoints = @import("endpoint.zig");
const fio = @import("fio.zig");
const util = @import("util.zig");

test "BearerAuthSingle authenticate" {
    const a = std.testing.allocator;
    const token = "hello, world";

    var auth = try Authenticators.BearerAuthSingle.init(a, token, null);
    defer auth.deinit();

    // invalid auth header
    try std.testing.expectEqual(auth.authenticate("wrong header"), false);
    try std.testing.expectEqual(auth.authenticate("Bearer wrong-token"), false);
    try std.testing.expectEqual(auth.authenticate("Bearer " ++ token), true);
}

test "BearerAuthMulti authenticate" {
    const a = std.testing.allocator;
    const token = "hello, world";

    var map = std.StringHashMap(void).init(a); // set
    defer map.deinit();

    try map.put(token, {});

    var auth = try Authenticators.BearerAuthMulti(@TypeOf(map)).init(a, &map, null);
    defer auth.deinit();

    // invalid auth header
    try std.testing.expectEqual(auth.authenticate("wrong header"), false);
    try std.testing.expectEqual(auth.authenticate("Bearer wrong-token"), false);
    try std.testing.expectEqual(auth.authenticate("Bearer " ++ token), true);
}

const HTTP_RESPONSE: []const u8 =
    \\ <html><body>
    \\   Hello from ZAP!!!
    \\ </body></html>
;
var received_response: []const u8 = "null";

fn endpoint_http_get(e: *Endpoints.SimpleEndpoint, r: zap.SimpleRequest) void {
    _ = e;
    r.sendBody(HTTP_RESPONSE) catch return;
    received_response = HTTP_RESPONSE;
    zap.fio_stop();
}

//
// http client code for in-process sending of http request
//
fn setHeader(h: [*c]fio.http_s, name: []const u8, value: []const u8) !void {
    const hname: fio.fio_str_info_s = .{
        .data = util.toCharPtr(name),
        .len = name.len,
        .capa = name.len,
    };

    const vname: fio.fio_str_info_s = .{
        .data = util.toCharPtr(value),
        .len = value.len,
        .capa = value.len,
    };
    const ret = fio.http_set_header2(h, hname, vname);

    if (ret == 0) return;
    return zap.HttpError.HttpSetHeader;
}

fn sendRequest() void {
    const ret = zap.http_connect("http://127.0.0.1:3000/test", null, .{
        .on_response = on_response,
        .on_request = null,
        .on_upgrade = null,
        .on_finish = null,
        .udata = null,
        .public_folder = null,
        .public_folder_length = 0,
        .max_header_size = 32 * 1024,
        .max_body_size = 500 * 1024,
        .max_clients = 1,
        .tls = null,
        .reserved1 = 0,
        .reserved2 = 0,
        .reserved3 = 0,
        .ws_max_msg_size = 0,
        .timeout = 5,
        .ws_timeout = 0,
        .log = 0,
        .is_client = 1,
    });
    // _ = ret;
    std.debug.print("\nret = {d}\n", .{ret});
    zap.fio_start(.{ .threads = 1, .workers = 1 });
}

fn on_response(r: [*c]fio.http_s) callconv(.C) void {
    // the first time around, we need to complete the request. E.g. set headers.
    if (r.*.status_str == zap.FIOBJ_INVALID) {
        setHeader(r, "Authorization", "Bearer ABCDEFG") catch return;
        zap.http_finish(r);
        return;
    }
    const response = zap.http_req2str(r);
    if (zap.fio2str(response)) |body| {
        std.debug.print("{s}\n", .{body});
    } else {
        std.debug.print("Oops\n", .{});
    }
    zap.fio_stop();
}
//
// end of http client code
//

test "BearerAuthSingle authenticateRequest" {
    const a = std.testing.allocator;
    const token = "ABCDEFG";

    //
    // Unfortunately, spawning a child process confuses facilio:
    //
    // 1. attempt: spawn curl process before we start facilio threads
    // this doesn't work: facilio doesn't start up if we spawn a child process
    // var p = std.ChildProcess.init(&.{
    //     "bash",
    //     "-c",
    //     "sleep 10; curl -H \"Authorization: Bearer\"" ++ token ++ " http://localhost:3000/test -v",
    // }, a);
    // try p.spawn();

    // 2. attempt:
    // our custom client doesn't work either
    // var p = std.ChildProcess.init(&.{
    //     "bash",
    //     "-c",
    //     "sleep 3; ./zig-out/bin/http_client &",
    // }, a);
    // try p.spawn();
    // std.debug.print("done spawning\n", .{});

    // 3. attempt: sending the request in-process
    // this doesn't work either because facilio wants to be either server or client, gets confused, at least when we're doing it this way
    // sendRequest();

    // setup listener
    var listener = zap.SimpleEndpointListener.init(
        a,
        .{
            .port = 3000,
            .on_request = null,
            .log = false,
            .max_clients = 10,
            .max_body_size = 1 * 1024,
        },
    );
    defer listener.deinit();

    // create mini endpoint
    var ep = Endpoints.SimpleEndpoint.init(.{ .path = "/test", .get = endpoint_http_get });

    // create authenticator
    var authenticator = try Authenticators.BearerAuthSingle.init(a, token, null);
    defer authenticator.deinit();

    // create authenticating endpoint
    var auth_ep = Endpoints.AuthenticatingEndpoint(@TypeOf(authenticator)).init(&ep, &authenticator);

    try listener.addEndpoint(auth_ep.getEndpoint());

    listener.listen() catch {};
    std.debug.print("Listening on 0.0.0.0:3000\n", .{});
    std.debug.print("Please run the following:\n", .{});
    std.debug.print("./zig-out/bin/http_client", .{});

    // start worker threads
    zap.start(.{
        .threads = 1,
        .workers = 0,
    });

    try std.testing.expectEqualStrings(HTTP_RESPONSE, received_response);
}
