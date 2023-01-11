const std = @import("std");
const zap = @import("facilio");

fn on_request(request: [*c]zap.C.http_s) callconv(.C) void {
    std.debug.print("GOT A REQUEST!\n", .{});
    _ = zap.sendBody(request, "Hello from ZAP!!!");
}

pub fn main() !void {
    // configure
    var listen_settings = zap.ListenSettings.init();
    listen_settings.on_request = on_request;
    listen_settings.log = true;

    // listen
    try zap.listen("3000", null, listen_settings);
    std.debug.print("Listening on port 3000\n", .{});

    // start working
    zap.start(.{
        .threads = 4,
        .workers = 4,
    });
}
