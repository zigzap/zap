const std = @import("std");
const zap = @import("zap");

fn on_request(request: [*c]zap.C.http_s) callconv(.C) void {
    std.debug.print("GOT A REQUEST!\n", .{});
    // std.time.sleep(5 * 1000 * 1000);
    _ = zap.sendBody(request, "Hello from ZAP!!!");
}

pub fn main() !void {
    // std.debug.print("debug1\n", .{});
    // configure
    var listen_settings: zap.ListenSettings = .{};
    // std.debug.print("debug2\n", .{});
    listen_settings.on_request = on_request;
    listen_settings.log = true;

    // std.debug.print("debug3\n", .{});

    // listen
    try zap.listen("3000", null, listen_settings);
    std.debug.print("Listening on port 3000\n", .{});

    // std.debug.print("debug4\n", .{});

    // start working
    zap.start(.{
        .threads = 4,
        .workers = 4,
    });
}
