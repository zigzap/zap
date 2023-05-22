const std = @import("std");
const zap = @import("zap");

fn MAKE_MEGA_ERROR() !void {
    return error.MEGA_ERROR;
}

fn MY_REQUEST_HANDLER(r: zap.SimpleRequest) void {
    MAKE_MEGA_ERROR() catch |err| {
        r.sendError(err, 505);
    };
}

pub fn main() !void {
    var listener = zap.SimpleHttpListener.init(.{
        .port = 3000,
        .on_request = MY_REQUEST_HANDLER,
        .log = true,
    });
    try listener.listen();

    std.debug.print("Listening on 0.0.0.0:3000\n", .{});

    zap.start(.{
        .threads = 2,
        .workers = 2,
    });
}
