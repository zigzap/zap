const std = @import("std");
const zap = @import("zap");

fn on_request_minimal(r: zap.SimpleRequest) void {
    _ = r.sendBody("Hello from ZAP!!!");
}

pub fn main() !void {
    var listener = zap.SimpleHttpListener.init(.{
        .port = 3000,
        .on_request = on_request_minimal,
        .log = false,
        .max_clients = 100000,
    });
    try listener.listen();

    std.debug.print("Listening on 0.0.0.0:3000\n", .{});

    // start worker threads
    zap.start(.{
        .threads = 4,
        .workers = 4,
    });
}
