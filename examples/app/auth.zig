//!
//! Part of the Zap examples.
//!
//! Build me with `zig build     app_auth`.
//! Run   me with `zig build run-app_auth`.
//!
const std = @import("std");
const zap = @import("zap");

const Allocator = std.mem.Allocator;

// The "Application Context"
const MyContext = struct {
    bearer_token: []const u8,
};

// We reply with this
const HTTP_RESPONSE_TEMPLATE: []const u8 =
    \\ <html><body>
    \\   {s} from ZAP on {s} (token {s} == {s} : {s})!!!
    \\ </body></html>
    \\
;

// Our simple endpoint that will be wrapped by the authenticator
const MyEndpoint = struct {
    // the slug
    path: []const u8,
    error_strategy: zap.Endpoint.ErrorStrategy = .log_to_response,

    fn get_bearer_token(r: zap.Request) []const u8 {
        const auth_header = zap.Auth.extractAuthHeader(.Bearer, &r) orelse "Bearer (no token)";
        return auth_header[zap.Auth.AuthScheme.Bearer.str().len..];
    }

    // authenticated GET requests go here
    // we use the endpoint, the context, the arena, and try
    pub fn get(ep: *MyEndpoint, arena: Allocator, context: *MyContext, r: zap.Request) !void {
        const used_token = get_bearer_token(r);
        const response = try std.fmt.allocPrint(
            arena,
            HTTP_RESPONSE_TEMPLATE,
            .{ "Hello", ep.path, used_token, context.bearer_token, "OK" },
        );
        r.setStatus(.ok);
        try r.sendBody(response);
    }

    // we also catch the unauthorized callback
    // we use the endpoint, the context, the arena, and try
    pub fn unauthorized(ep: *MyEndpoint, arena: Allocator, context: *MyContext, r: zap.Request) !void {
        r.setStatus(.unauthorized);
        const used_token = get_bearer_token(r);
        const response = try std.fmt.allocPrint(
            arena,
            HTTP_RESPONSE_TEMPLATE,
            .{ "UNAUTHORIZED", ep.path, used_token, context.bearer_token, "NOT OK" },
        );
        try r.sendBody(response);
    }

    // not implemented, don't care
    pub fn post(_: *MyEndpoint, _: Allocator, _: *MyContext, _: zap.Request) !void {}
    pub fn put(_: *MyEndpoint, _: Allocator, _: *MyContext, _: zap.Request) !void {}
    pub fn delete(_: *MyEndpoint, _: Allocator, _: *MyContext, _: zap.Request) !void {}
    pub fn patch(_: *MyEndpoint, _: Allocator, _: *MyContext, _: zap.Request) !void {}
    pub fn options(_: *MyEndpoint, _: Allocator, _: *MyContext, _: zap.Request) !void {}
    pub fn head(_: *MyEndpoint, _: Allocator, _: *MyContext, _: zap.Request) !void {}
};

pub fn main() !void {
    var gpa: std.heap.GeneralPurposeAllocator(.{
        // just to be explicit
        .thread_safe = true,
    }) = .{};
    defer std.debug.print("\n\nLeaks detected: {}\n\n", .{gpa.deinit() != .ok});
    const allocator = gpa.allocator();

    // our global app context
    var my_context: MyContext = .{ .bearer_token = "ABCDEFG" }; // ABCDEFG is our Bearer token

    // our global app that holds the context
    // App is the type
    // app is the instance
    const App = zap.App.Create(MyContext);
    var app = try App.init(allocator, &my_context, .{});
    defer app.deinit();

    // create mini endpoint
    var ep: MyEndpoint = .{
        .path = "/test",
    };

    // create authenticator, use token from context
    const Authenticator = zap.Auth.BearerSingle; // Simple Authenticator that uses a single bearer token
    var authenticator = try Authenticator.init(allocator, my_context.bearer_token, null);
    defer authenticator.deinit();

    // create authenticating endpoint by combining endpoint and authenticator
    const BearerAuthEndpoint = App.Endpoint.Authenticating(MyEndpoint, Authenticator);
    var auth_ep = BearerAuthEndpoint.init(&ep, &authenticator);

    // make the authenticating endpoint known to the app
    try app.register(&auth_ep);

    // listen
    try app.listen(.{
        .interface = "0.0.0.0",
        .port = 3000,
    });
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
        .threads = 2,
        .workers = 1,
    });
}
