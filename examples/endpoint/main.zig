//!
//! Part of the Zap examples.
//!
//! Build me with `zig build     endpoint`.
//! Run   me with `zig build run-endpoint`.
//!
const std = @import("std");
const zap = @import("zap");
const UserWeb = @import("userweb.zig");
const StopEndpoint = @import("stopendpoint.zig");
const ErrorEndpoint = @import("error.zig");

// this is just to demo that we can catch arbitrary slugs as fallback
fn on_request(r: zap.Request) !void {
    if (r.path) |the_path| {
        std.debug.print("REQUESTED PATH: {s}\n", .{the_path});
    }

    try r.sendBody("<html><body><h1>Hello from ZAP!!!</h1></body></html>");
}

// this is just to demo that we could catch arbitrary errors as fallback
fn on_error(_: zap.Request, err: anyerror) void {
    std.debug.print("\n\n\nOh no!!! We didn't chatch this error: {}\n\n\n", .{err});
}

pub fn main() !void {
    var gpa = std.heap.GeneralPurposeAllocator(.{
        .thread_safe = true,
    }){};
    const allocator = gpa.allocator();

    // we scope everything that can allocate within this block for leak detection
    {
        // setup listener
        var listener = zap.Endpoint.Listener.init(
            allocator,
            .{
                .port = 3000,
                .on_request = on_request,
                // optional
                .on_error = on_error,
                .log = true,
                .public_folder = "examples/endpoint/html",
                .max_clients = 100000,
                .max_body_size = 100 * 1024 * 1024,
            },
        );
        defer listener.deinit();

        // /users endpoint
        var userWeb = UserWeb.init(allocator, "/users");
        defer userWeb.deinit();

        var stopEp = StopEndpoint.init("/stop");
        var errorEp: ErrorEndpoint = .{};
        var unhandledErrorEp: ErrorEndpoint = .{ .error_strategy = .raise, .path = "/unhandled" };

        // register endpoints with the listener
        try listener.register(&userWeb);
        try listener.register(&stopEp);
        try listener.register(&errorEp);
        try listener.register(&unhandledErrorEp);

        // fake some users
        var uid: usize = undefined;
        uid = try userWeb.users().addByName("renerocksai", null);
        uid = try userWeb.users().addByName("renerocksai", "your mom");

        // listen
        try listener.listen();

        std.debug.print("Listening on 0.0.0.0:3000\n", .{});

        // and run
        zap.start(.{
            .threads = 2,
            // IMPORTANT! It is crucial to only have a single worker for this example to work!
            // Multiple workers would have multiple copies of the users hashmap.
            //
            // Since zap is quite fast, you can do A LOT with a single worker.
            // Try it with `zig build run-endpoint -Drelease-fast`
            .workers = 1,
        });
    }

    // show potential memory leaks when ZAP is shut down
    const has_leaked = gpa.detectLeaks();
    std.log.debug("Has leaked: {}", .{has_leaked});
}
