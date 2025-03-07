const std = @import("std");
const zap = @import("zap");
const Authenticators = zap.Auth;
const Endpoint = zap.Endpoint;
const fio = zap;
const util = zap;

test "BearerAuthSingle authenticate" {
    const a = std.testing.allocator;
    const token = "hello, world";

    var auth = try Authenticators.BearerSingle.init(a, token, null);
    defer auth.deinit();

    // invalid auth header
    try std.testing.expectEqual(auth.authenticate("wrong header"), .AuthFailed);
    try std.testing.expectEqual(auth.authenticate("Bearer wrong-token"), .AuthFailed);
    // ok
    try std.testing.expectEqual(auth.authenticate("Bearer " ++ token), .AuthOK);
}

test "BearerAuthMulti authenticate" {
    const a = std.testing.allocator;
    const token = "hello, world";

    const Set = std.StringHashMap(void);
    var set = Set.init(a); // set
    defer set.deinit();

    try set.put(token, {});

    var auth = try Authenticators.BearerMulti(Set).init(a, &set, null);
    defer auth.deinit();

    // invalid auth header
    try std.testing.expectEqual(auth.authenticate("wrong header"), .AuthFailed);
    try std.testing.expectEqual(auth.authenticate("Bearer wrong-token"), .AuthFailed);
    // ok
    try std.testing.expectEqual(auth.authenticate("Bearer " ++ token), .AuthOK);
}

test "BasicAuth Token68" {
    const a = std.testing.allocator;
    const token = "QWxhZGRpbjpvcGVuIHNlc2FtZQ==";

    // create a set of Token68 entries
    const Set = std.StringHashMap(void);
    var set = Set.init(a); // set
    defer set.deinit();
    try set.put(token, {});

    // create authenticator
    const Authenticator = Authenticators.Basic(Set, .Token68);
    var auth = try Authenticator.init(a, &set, null);
    defer auth.deinit();

    // invalid auth header
    try std.testing.expectEqual(auth.authenticate("wrong header"), .AuthFailed);
    try std.testing.expectEqual(auth.authenticate("Basic wrong-token"), .AuthFailed);
    // ok
    try std.testing.expectEqual(auth.authenticate("Basic " ++ token), .AuthOK);
}

test "BasicAuth UserPass" {
    const a = std.testing.allocator;

    // create a set of User -> Pass entries
    const Map = std.StringHashMap([]const u8);
    var map = Map.init(a);
    defer map.deinit();

    // create user / pass entry
    const user = "Alladdin";
    const pass = "opensesame";
    try map.put(user, pass);

    // now, do the encoding for the Basic auth
    const token = user ++ ":" ++ pass;
    var encoder = std.base64.url_safe.Encoder;
    var buffer: [256]u8 = undefined;
    const encoded = encoder.encode(&buffer, token);

    // create authenticator
    const Authenticator = Authenticators.Basic(Map, .UserPass);
    var auth = try Authenticator.init(a, &map, null);
    defer auth.deinit();

    // invalid auth header
    try std.testing.expectEqual(auth.authenticate("wrong header"), .AuthFailed);
    try std.testing.expectEqual(auth.authenticate("Basic wrong-token"), .AuthFailed);
    // ok
    const expected = try std.fmt.allocPrint(a, "Basic {s}", .{encoded});

    defer a.free(expected);
    try std.testing.expectEqual(auth.authenticate(expected), .AuthOK);
}

const HTTP_RESPONSE: []const u8 =
    \\ <html><body>
    \\   Hello from ZAP!!!
    \\ </body></html>
;
var received_response: []const u8 = "null";

fn endpoint_http_get(e: *Endpoint, r: zap.Request) void {
    _ = e;
    r.sendBody(HTTP_RESPONSE) catch return;
    received_response = HTTP_RESPONSE;
    std.time.sleep(1 * std.time.ns_per_s);
    zap.stop();
}

fn endpoint_http_unauthorized(e: *Endpoint, r: zap.Request) void {
    _ = e;
    r.setStatus(.unauthorized);
    r.sendBody("UNAUTHORIZED ACCESS") catch return;
    received_response = "UNAUTHORIZED";
    std.time.sleep(1 * std.time.ns_per_s);
    zap.stop();
}

//
// http client code for in-process sending of http request
//

const ClientAuthReqHeaderFields = struct {
    auth: Authenticators.AuthScheme,
    token: []const u8,
};

fn makeRequest(a: std.mem.Allocator, url: []const u8, auth: ?ClientAuthReqHeaderFields) !void {
    var http_client: std.http.Client = .{ .allocator = a };
    defer http_client.deinit();

    var auth_buf: [256]u8 = undefined;
    const auth_string: []const u8 = blk: {
        if (auth) |auth_fields| {
            const authstring = try std.fmt.bufPrint(&auth_buf, "{s}{s}", .{ auth_fields.auth.str(), auth_fields.token });
            break :blk authstring;
        } else {
            break :blk "";
        }
    };
    _ = try http_client.fetch(.{
        .location = .{ .url = url },
        .headers = .{
            .authorization = .omit,
        },
        .extra_headers = blk: {
            if (auth) |auth_fields| {
                break :blk &.{.{
                    .name = auth_fields.auth.headerFieldStrHeader(),
                    .value = auth_string,
                }};
            } else {
                break :blk &.{};
            }
        },
    });

    zap.stop();
}

fn makeRequestThread(a: std.mem.Allocator, url: []const u8, auth: ?ClientAuthReqHeaderFields) !std.Thread {
    return try std.Thread.spawn(.{}, makeRequest, .{ a, url, auth });
}

//
// end of http client code
//

test "BearerAuthSingle authenticateRequest OK" {
    const a = std.testing.allocator;
    const token = "ABCDEFG";

    // setup listener
    var listener = zap.Endpoint.Listener.init(
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
    var ep = Endpoint.init(.{
        .path = "/test",
        .get = endpoint_http_get,
        .unauthorized = endpoint_http_unauthorized,
        .unset = Endpoint.dummy_handler,
    });

    // create authenticator
    const Authenticator = Authenticators.BearerSingle;
    var authenticator = try Authenticator.init(a, token, null);
    defer authenticator.deinit();

    // create authenticating endpoint
    const BearerAuthEndpoint = Endpoint.Authenticating(Authenticator);
    var auth_ep = BearerAuthEndpoint.init(&ep, &authenticator);

    try listener.register(auth_ep.endpoint());

    listener.listen() catch {};
    // std.debug.print("\n\n*******************************************\n", .{});
    // std.debug.print("\n\nPlease run the following:\n", .{});
    // std.debug.print("./zig-out/bin/http_client_runner\n", .{});
    // std.debug.print("\n\n*******************************************\n", .{});

    const thread = try makeRequestThread(a, "http://127.0.0.1:3000/test", .{ .auth = .Bearer, .token = token });
    defer thread.join();

    // start worker threads
    zap.start(.{
        .threads = 1,
        .workers = 1,
    });

    try std.testing.expectEqualStrings(HTTP_RESPONSE, received_response);
}

test "BearerAuthSingle authenticateRequest test-unauthorized" {
    const a = std.testing.allocator;
    const token = "ABCDEFG";

    // setup listener
    var listener = zap.Endpoint.Listener.init(
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
    var ep = Endpoint.init(.{
        .path = "/test",
        .get = endpoint_http_get,
        .unauthorized = endpoint_http_unauthorized,
        .unset = Endpoint.dummy_handler,
    });

    const Set = std.StringHashMap(void);
    var set = Set.init(a); // set
    defer set.deinit();

    // insert auth tokens
    try set.put(token, {});

    const Authenticator = Authenticators.BearerMulti(Set);
    var authenticator = try Authenticator.init(a, &set, null);
    defer authenticator.deinit();

    // create authenticating endpoint
    const BearerAuthEndpoint = Endpoint.Authenticating(Authenticator);
    var auth_ep = BearerAuthEndpoint.init(&ep, &authenticator);

    try listener.register(auth_ep.endpoint());

    listener.listen() catch {};
    // std.debug.print("Waiting for the following:\n", .{});
    // std.debug.print("./zig-out/bin/http_client http://127.0.0.1:3000/test Bearer invalid\r", .{});

    const thread = try makeRequestThread(a, "http://127.0.0.1:3000/test", .{ .auth = .Bearer, .token = "invalid" });
    defer thread.join();

    // start worker threads
    zap.start(.{
        .threads = 1,
        .workers = 1,
    });

    try std.testing.expectEqualStrings("UNAUTHORIZED", received_response);
}

test "BearerAuthMulti authenticateRequest OK" {
    const a = std.testing.allocator;
    const token = "ABCDEFG";

    // setup listener
    var listener = zap.Endpoint.Listener.init(
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
    var ep = Endpoint.init(.{
        .path = "/test",
        .get = endpoint_http_get,
        .unauthorized = endpoint_http_unauthorized,
        .unset = Endpoint.dummy_handler,
    });

    // create authenticator
    const Authenticator = Authenticators.BearerSingle;
    var authenticator = try Authenticator.init(a, token, null);
    defer authenticator.deinit();

    // create authenticating endpoint
    const BearerAuthEndpoint = Endpoint.Authenticating(Authenticator);
    var auth_ep = BearerAuthEndpoint.init(&ep, &authenticator);

    try listener.register(auth_ep.endpoint());

    listener.listen() catch {};
    // std.debug.print("Waiting for the following:\n", .{});
    // std.debug.print("./zig-out/bin/http_client_runner http://127.0.0.1:3000/test Bearer " ++ token ++ "\r", .{});

    const thread = try makeRequestThread(a, "http://127.0.0.1:3000/test", .{ .auth = .Bearer, .token = token });
    defer thread.join();

    // start worker threads
    zap.start(.{
        .threads = 1,
        .workers = 1,
    });

    try std.testing.expectEqualStrings(HTTP_RESPONSE, received_response);
}

test "BearerAuthMulti authenticateRequest test-unauthorized" {
    const a = std.testing.allocator;
    const token = "invalid";

    // setup listener
    var listener = zap.Endpoint.Listener.init(
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
    var ep = Endpoint.init(.{
        .path = "/test",
        .get = endpoint_http_get,
        .unauthorized = endpoint_http_unauthorized,
        .unset = Endpoint.dummy_handler,
    });

    // create authenticator
    const Authenticator = Authenticators.BearerSingle;
    var authenticator = try Authenticator.init(a, token, null);
    defer authenticator.deinit();

    // create authenticating endpoint
    const BearerAuthEndpoint = Endpoint.Authenticating(Authenticator);
    var auth_ep = BearerAuthEndpoint.init(&ep, &authenticator);

    try listener.register(auth_ep.endpoint());

    listener.listen() catch {};
    // std.debug.print("Waiting for the following:\n", .{});
    // std.debug.print("./zig-out/bin/http_client_runner http://127.0.0.1:3000/test Bearer invalid\r", .{});

    const thread = try makeRequestThread(a, "http://127.0.0.1:3000/test", .{ .auth = .Bearer, .token = "invalid" });
    defer thread.join();

    // start worker threads
    zap.start(.{
        .threads = 1,
        .workers = 1,
    });

    try std.testing.expectEqualStrings(HTTP_RESPONSE, received_response);
}

test "BasicAuth Token68 authenticateRequest" {
    const a = std.testing.allocator;
    const token = "QWxhZGRpbjpvcGVuIHNlc2FtZQ==";

    // setup listener
    var listener = zap.Endpoint.Listener.init(
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
    var ep = Endpoint.init(.{
        .path = "/test",
        .get = endpoint_http_get,
        .unauthorized = endpoint_http_unauthorized,
        .unset = Endpoint.dummy_handler,
    });
    // create a set of Token68 entries
    const Set = std.StringHashMap(void);
    var set = Set.init(a); // set
    defer set.deinit();
    try set.put(token, {});

    // create authenticator
    const Authenticator = Authenticators.Basic(Set, .Token68);
    var authenticator = try Authenticator.init(a, &set, null);
    defer authenticator.deinit();

    // create authenticating endpoint
    const BearerAuthEndpoint = Endpoint.Authenticating(Authenticator);
    var auth_ep = BearerAuthEndpoint.init(&ep, &authenticator);

    try listener.register(auth_ep.endpoint());

    listener.listen() catch {};
    // std.debug.print("Waiting for the following:\n", .{});
    // std.debug.print("./zig-out/bin/http_client http://127.0.0.1:3000/test Basic " ++ token ++ "\r", .{});

    const thread = try makeRequestThread(a, "http://127.0.0.1:3000/test", .{ .auth = .Basic, .token = token });
    defer thread.join();

    // start worker threads
    zap.start(.{
        .threads = 1,
        .workers = 1,
    });

    try std.testing.expectEqualStrings(HTTP_RESPONSE, received_response);
}

test "BasicAuth Token68 authenticateRequest test-unauthorized" {
    const a = std.testing.allocator;
    const token = "QWxhZGRpbjpvcGVuIHNlc2FtZQ==";

    // setup listener
    var listener = zap.Endpoint.Listener.init(
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
    var ep = Endpoint.init(.{
        .path = "/test",
        .get = endpoint_http_get,
        .unauthorized = endpoint_http_unauthorized,
        .unset = Endpoint.dummy_handler,
    });
    // create a set of Token68 entries
    const Set = std.StringHashMap(void);
    var set = Set.init(a); // set
    defer set.deinit();
    try set.put(token, {});

    // create authenticator
    const Authenticator = Authenticators.Basic(Set, .Token68);
    var authenticator = try Authenticator.init(a, &set, null);
    defer authenticator.deinit();

    // create authenticating endpoint
    const BearerAuthEndpoint = Endpoint.Authenticating(Authenticator);
    var auth_ep = BearerAuthEndpoint.init(&ep, &authenticator);

    try listener.register(auth_ep.endpoint());

    listener.listen() catch {};
    // std.debug.print("Waiting for the following:\n", .{});
    // std.debug.print("./zig-out/bin/http_client http://127.0.0.1:3000/test Basic " ++ "invalid\r", .{});

    const thread = try makeRequestThread(a, "http://127.0.0.1:3000/test", .{ .auth = .Basic, .token = "invalid" });
    defer thread.join();

    // start worker threads
    zap.start(.{
        .threads = 1,
        .workers = 1,
    });

    try std.testing.expectEqualStrings("UNAUTHORIZED", received_response);
}

test "BasicAuth UserPass authenticateRequest" {
    const a = std.testing.allocator;

    // setup listener
    var listener = zap.Endpoint.Listener.init(
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
    var ep = Endpoint.init(.{
        .path = "/test",
        .get = endpoint_http_get,
        .unauthorized = endpoint_http_unauthorized,
        .unset = Endpoint.dummy_handler,
    });

    // create a set of User -> Pass entries
    const Map = std.StringHashMap([]const u8);
    var map = Map.init(a);
    defer map.deinit();

    // create user / pass entry
    const user = "Alladdin";
    const pass = "opensesame";
    try map.put(user, pass);

    // now, do the encoding for the Basic auth
    const token = user ++ ":" ++ pass;
    var encoder = std.base64.url_safe.Encoder;
    var buffer: [256]u8 = undefined;
    const encoded = encoder.encode(&buffer, token);

    // create authenticator
    const Authenticator = Authenticators.Basic(Map, .UserPass);
    var authenticator = try Authenticator.init(a, &map, null);
    defer authenticator.deinit();

    // create authenticating endpoint
    const BearerAuthEndpoint = Endpoint.Authenticating(Authenticator);
    var auth_ep = BearerAuthEndpoint.init(&ep, &authenticator);

    try listener.register(auth_ep.endpoint());

    listener.listen() catch {};
    // std.debug.print("Waiting for the following:\n", .{});
    // std.debug.print("./zig-out/bin/http_client http://127.0.0.1:3000/test Basic {s}\r", .{encoded});

    const thread = try makeRequestThread(a, "http://127.0.0.1:3000/test", .{ .auth = .Basic, .token = encoded });
    defer thread.join();

    // start worker threads
    zap.start(.{
        .threads = 1,
        .workers = 1,
    });

    try std.testing.expectEqualStrings(HTTP_RESPONSE, received_response);
}

test "BasicAuth UserPass authenticateRequest test-unauthorized" {
    const a = std.testing.allocator;

    // setup listener
    var listener = zap.Endpoint.Listener.init(
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
    var ep = Endpoint.init(.{
        .path = "/test",
        .get = endpoint_http_get,
        .unauthorized = endpoint_http_unauthorized,
        .unset = Endpoint.dummy_handler,
    });

    // create a set of User -> Pass entries
    const Map = std.StringHashMap([]const u8);
    var map = Map.init(a);
    defer map.deinit();

    // create user / pass entry
    const user = "Alladdin";
    const pass = "opensesame";
    try map.put(user, pass);

    // now, do the encoding for the Basic auth
    const token = user ++ ":" ++ pass;
    var encoder = std.base64.url_safe.Encoder;
    var buffer: [256]u8 = undefined;
    const encoded = encoder.encode(&buffer, token);
    _ = encoded;

    // create authenticator
    const Authenticator = Authenticators.Basic(Map, .UserPass);
    var authenticator = try Authenticator.init(a, &map, null);
    defer authenticator.deinit();

    // create authenticating endpoint
    const BearerAuthEndpoint = Endpoint.Authenticating(Authenticator);
    var auth_ep = BearerAuthEndpoint.init(&ep, &authenticator);

    try listener.register(auth_ep.endpoint());

    listener.listen() catch {};
    // std.debug.print("Waiting for the following:\n", .{});
    // std.debug.print("./zig-out/bin/http_client http://127.0.0.1:3000/test Basic invalid\r", .{});

    const thread = try makeRequestThread(a, "http://127.0.0.1:3000/test", .{ .auth = .Basic, .token = "invalid" });
    defer thread.join();

    // start worker threads
    zap.start(.{
        .threads = 1,
        .workers = 1,
    });

    try std.testing.expectEqualStrings("UNAUTHORIZED", received_response);
}
