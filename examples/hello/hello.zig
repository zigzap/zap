const std = @import("std");
const facilio = @import("facilio");

fn on_request(request: [*c]facilio.C.http_s) callconv(.C) void {
    std.debug.print("REQUEST!\n", .{});
    var msg: []const u8 = "Hello from ZAP!";
    _ = facilio.sendBody(request, msg);
}

pub fn main() void {
    if (facilio.C.http_listen("3000", null, .{
        .on_request = on_request,
        .log = 1,
        .on_upgrade = null,
        .on_response = null,
        .on_finish = null,
        .udata = null,
        .public_folder = null,
        .public_folder_length = 0,
        .max_header_size = 4096,
        .max_body_size = 4096,
        .max_clients = 42,
        .tls = null,
        .reserved1 = 0,
        .reserved2 = 0,
        .reserved3 = 0,
        .ws_max_msg_size = 250 * 1024,
        .timeout = 0,
        .ws_timeout = 0,
        .is_client = 0,
    }) == -1) {
        // listen failed
        std.debug.print("Listening failed\n", .{});
        return;
    }
    facilio.start(.{
        .threads = 4,
        .workers = 4,
    });
}
