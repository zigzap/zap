const std = @import("std");
const zap = @import("zap.zig");
const fio = @import("fio.zig");
const util = @import("util.zig");

pub fn setHeader(h: [*c]fio.http_s, name: []const u8, value: []const u8) !void {
    const hname: fio.fio_str_info_s = .{
        .data = util.toCharPtr(name),
        .len = name.len,
        .capa = name.len,
    };

    const vname: fio.fio_str_info_s = .{
        .data = util.toCharPtr(value),
        .len = value.len,
        .capa = value.len,
    };
    const ret = fio.http_set_header2(h, hname, vname);

    if (ret == 0) return;
    return zap.HttpError.HttpSetHeader;
}

fn on_response(r: [*c]fio.http_s) callconv(.C) void {
    if (r.*.status_str == zap.FIOBJ_INVALID) {
        setHeader(r, "Authorization", "Bearer ABCDEFG") catch return;
        zap.http_finish(r);
        return;
    }

    const response = zap.http_req2str(r);
    if (zap.fio2str(response)) |body| {
        std.debug.print("{s}\n", .{body});
    } else {
        std.debug.print("Oops\n", .{});
    }
    zap.fio_stop();
}

pub fn main() void {
    const ret = zap.http_connect("http://127.0.0.1:3000/test", null, .{
        .on_response = on_response,
        .on_request = null,
        .on_upgrade = null,
        .on_finish = null,
        .udata = null,
        .public_folder = null,
        .public_folder_length = 0,
        .max_header_size = 32 * 1024,
        .max_body_size = 500 * 1024,
        .max_clients = 1,
        .tls = null,
        .reserved1 = 0,
        .reserved2 = 0,
        .reserved3 = 0,
        .ws_max_msg_size = 0,
        .timeout = 5,
        .ws_timeout = 0,
        .log = 1,
        .is_client = 1,
    });
    // _ = ret;
    std.debug.print("\nHTTP CONNECT ret = {d}\n", .{ret});
    zap.fio_start(.{ .threads = 1, .workers = 1 });
}
