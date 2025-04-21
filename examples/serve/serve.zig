//!
//! Part of the Zap examples.
//!
//! Build me with `zig build     serve`.
//! Run   me with `zig build run-serve`.
//!
const std = @import("std");
const zap = @import("zap");

fn on_request(r: zap.Request) !void {
    r.setStatus(.not_found);
    r.sendBody("<html><body><h1>404 - File not found</h1></body></html>") catch return;
}

pub fn main() !void {
    var listener = zap.HttpListener.init(.{
        .port = 3000,
        .on_request = on_request,
        .public_folder = "examples/serve",
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
