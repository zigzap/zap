// zig type definitions for facilio lib
// or maybe let's just make it zap directly...

const std = @import("std");
pub const C = @cImport({
    @cInclude("http.h");
    @cInclude("fio.h");
});

pub fn sendBody(request: [*c]C.http_s, body: []const u8) void {
    _ = C.http_send_body(request, @intToPtr(
        *anyopaque,
        @ptrToInt(body.ptr),
    ), body.len);
}

pub fn start(args: C.fio_start_args) void {
    C.fio_start(args);
}

const ListenError = error{
    ValueNotZeroTerminated,
    ListenError,
};

pub fn listen(port: [*c]const u8, interface: [*c]const u8, settings: ListenSettings) ListenError!void {
    var pfolder: [*c]const u8 = null;
    var pfolder_len: usize = 0;

    if (settings.public_folder) |pf| {
        pfolder_len = pf.len;
        // TODO: make sure it's 0-terminated!!!
        if (pf[pf.len - 1] != 0) {
            return error.ValueNotZeroTerminated;
        }
        pfolder = pf.ptr;
    }
    var x: C.http_settings_s = .{
        .on_request = settings.on_request,
        .on_upgrade = settings.on_upgrade,
        .on_response = settings.on_response,
        .on_finish = settings.on_finish,
        .udata = null,
        .public_folder = pfolder,
        .public_folder_length = pfolder_len,
        .max_header_size = settings.max_header_size,
        .max_body_size = settings.max_body_size,
        .max_clients = settings.max_clients,
        .tls = null,
        .reserved1 = 0,
        .reserved2 = 0,
        .reserved3 = 0,
        .ws_max_msg_size = 0,
        .timeout = settings.keepalive_timeout_s,
        .ws_timeout = 0,
        .log = if (settings.log) 1 else 0,
        .is_client = 0,
    };
    // TODO: BUG: without this print statement, -Drelease* loop forever
    // in debug2 and debug3
    // std.debug.print("X\n", .{});
    std.time.sleep(500 * 1000 * 1000);

    if (C.http_listen(port, interface, x) == -1) {
        return error.ListenError;
    }
}

pub const ListenSettings = struct {
    on_request: ?*const fn ([*c]C.http_s) callconv(.C) void = null,
    on_upgrade: ?*const fn ([*c]C.http_s, [*c]u8, usize) callconv(.C) void = null,
    on_response: ?*const fn ([*c]C.http_s) callconv(.C) void = null,
    on_finish: ?*const fn ([*c]C.struct_http_settings_s) callconv(.C) void = null,
    public_folder: ?[]const u8 = null,
    max_header_size: usize = 32 * 1024,
    max_body_size: usize = 50 * 1024 * 1024,
    max_clients: isize = 100,
    keepalive_timeout_s: u8 = 5,
    log: bool = false,

    const Self = @This();

    pub fn init() Self {
        return .{};
    }
};
