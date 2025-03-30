//!
//! Part of the Zap examples.
//!
//! Build me with `zig build     hello`.
//! Run   me with `zig build run-hello`.
//!
const std = @import("std");
const zap = @import("zap");

fn on_request_verbose(r: zap.Request) !void {
    if (r.path) |the_path| {
        std.debug.print("PATH: {s}\n", .{the_path});
    }

    if (r.query) |the_query| {
        std.debug.print("QUERY: {s}\n", .{the_query});
    }
    try r.sendBody("<html><body><h1>Hello from ZAP!!!</h1></body></html>");
}

fn on_request_minimal(r: zap.Request) !void {
    try r.sendBody("<html><body><h1>Hello from ZAP!!!</h1></body></html>");
}

pub fn main() !void {
    var listener = zap.HttpListener.init(.{
        .port = 3000,
        .on_request = on_request_verbose,
        .log = true,
        .max_clients = 100000,
    });
    try listener.listen();

    std.debug.print("Listening on 0.0.0.0:3000\n", .{});

    // start worker threads
    zap.start(.{
        .threads = 2,
        .workers = 2,
    });
}
