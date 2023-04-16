const std = @import("std");
const zap = @import("zap.zig");
const fio = @import("fio.zig");
const util = @import("util.zig");
const auth = @import("http_auth.zig");

var http_header_field: [:0]const u8 = undefined;
var http_header_value: [:0]const u8 = undefined;

pub fn main() !void {
    var allocator = std.heap.page_allocator;
    var args_it = std.process.args();
    _ = args_it.skip(); // skip process name
    //
    const url = args_it.next() orelse "http://127.0.0.1:3000/test";
    const method = args_it.next() orelse "Bearer";
    const token = args_it.next() orelse "ABCDEFG";

    const scheme: auth.AuthScheme = if (std.mem.eql(u8, method, "Bearer")) .Bearer else .Basic;
    http_header_field = scheme.headerFieldStrHeader();

    http_header_value = switch (scheme) {
        .Basic => try std.fmt.allocPrintZ(allocator, "Basic {s}", .{token}),
        .Bearer => try std.fmt.allocPrintZ(allocator, "Bearer {s}", .{token}),
    };

    std.debug.print("Connecting to: {s}\n", .{url});
    std.debug.print("   Header: '{s}:{s}'\n", .{ http_header_field, http_header_value });

    const ret = zap.http_connect(url, null, .{
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
    std.debug.print("\nHTTP CONNECT ret = {d}\n", .{ret});
    zap.fio_start(.{ .threads = 1, .workers = 1 });
}

pub fn setHeader(h: [*c]fio.http_s, name: [:0]const u8, value: [:0]const u8) !void {
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
    // the first time around, we need to complete the request. E.g. set headers.
    if (r.*.status_str == zap.FIOBJ_INVALID) {
        setHeader(r, http_header_field, http_header_value) catch return;
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
