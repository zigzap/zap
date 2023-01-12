const std = @import("std");
const zap = @import("zap");

fn on_request(r: [*c]zap.C.http_s) callconv(.C) void {
    _ = zap.sendBody(r, "<html><body><h1>Hello from ZAP!!!</h1></body></html>");
}

pub fn main() !void {
    // listen
    try zap.listen("3000", null, .{
        .on_request = on_request,
        .log = true,
    });
    std.debug.print("Listening on 0.0.0.0:3000\n", .{});

    // start working
    zap.start(.{
        .threads = 4,
        .workers = 4,
    });
}
