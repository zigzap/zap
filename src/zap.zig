// zig type definitions for facilio lib
// or maybe let's just make it zap directly...

const std = @import("std");
pub const C = @cImport({
    @cInclude("http.h");
    @cInclude("fio.h");
});

pub usingnamespace @import("endpoint.zig");
pub usingnamespace @import("util.zig");
pub usingnamespace @import("http.zig");
pub usingnamespace @import("mustache.zig");

const _module = @This();

pub fn fio2str(o: C.FIOBJ) ?[]const u8 {
    if (o == 0) return null;
    const x: C.fio_str_info_s = C.fiobj_obj2cstr(o);
    return std.mem.span(x.data);
}

pub fn str2fio(s: []const u8) C.fio_str_info_s {
    return .{
        .data = toCharPtr(s),
        .len = s.len,
        .capa = s.len,
    };
}

fn toCharPtr(s: []const u8) [*c]u8 {
    return @intToPtr([*c]u8, @ptrToInt(s.ptr));
}

pub fn start(args: C.fio_start_args) void {
    C.fio_start(args);
}

pub const ListenError = error{
    AlreadyListening,
    ListenError,
};

pub const HttpParam = struct {
    key: []const u8,
    value: []const u8,
};

pub const ContentType = enum {
    TEXT,
    HTML,
    JSON,
};

pub const SimpleRequest = struct {
    path: ?[]const u8,
    query: ?[]const u8,
    body: ?[]const u8,
    method: ?[]const u8,
    h: [*c]C.http_s,

    const Self = @This();

    pub fn sendBody(self: *const Self, body: []const u8) c_int {
        return C.http_send_body(self.h, @intToPtr(
            *anyopaque,
            @ptrToInt(body.ptr),
        ), body.len);
    }

    pub fn sendJson(self: *const Self, json: []const u8) c_int {
        self.setContentType(.JSON);
        return C.http_send_body(self.h, @intToPtr(
            *anyopaque,
            @ptrToInt(json.ptr),
        ), json.len);
    }

    pub fn setContentType(self: *const Self, c: ContentType) void {
        self.setHeader("content-type", switch (c) {
            .TEXT => "text/plain",
            .JSON => "application/json",
            else => "text/html",
        });
    }

    pub fn setContentTypeFromPath(self: *const Self) void {
        _ = C.fiobj_hash_set(
            self.h.*.private_data.out_headers,
            C.HTTP_HEADER_CONTENT_TYPE,
            C.http_mimetype_find2(self.h.*.path),
        );
    }

    pub fn setHeader(self: *const Self, name: []const u8, value: []const u8) void {
        const hname: C.fio_str_info_s = .{
            .data = toCharPtr(name),
            .len = name.len,
            .capa = name.len,
        };
        const vname: C.fio_str_info_s = .{
            .data = toCharPtr(value),
            .len = value.len,
            .capa = value.len,
        };
        _ = C.http_set_header2(self.h, hname, vname);

        // Note to self:
        // const new_fiobj_str = C.fiobj_str_new(name.ptr, name.len);
        // C.fiobj_free(new_fiobj_str);
    }

    pub fn setStatusNumeric(self: *const Self, status: usize) void {
        self.h.*.status = status;
    }

    pub fn setStatus(self: *const Self, status: _module.StatusCode) void {
        self.h.*.status = @intCast(usize, @enumToInt(status));
    }

    pub fn nextParam(self: *const Self) ?HttpParam {
        if (self.h.*.params == 0) return null;
        var key: C.FIOBJ = undefined;
        const value = C.fiobj_hash_pop(self.h.*.params, &key);
        if (value == C.FIOBJ_INVALID) {
            return null;
        }
        return HttpParam{
            .key = fio2str(key).?,
            .value = fio2str(value).?,
        };
    }
};

pub const HttpRequestFn = *const fn (r: [*c]C.http_s) callconv(.C) void;
pub const SimpleHttpRequestFn = *const fn (SimpleRequest) void;

pub const SimpleHttpListenerSettings = struct {
    port: usize,
    interface: [*c]const u8 = null,
    on_request: ?SimpleHttpRequestFn,
    public_folder: ?[]const u8 = null,
    max_clients: ?isize = null,
    max_body_size: ?usize = null,
    timeout: ?u8 = null,
    log: bool = false,
};

pub const SimpleHttpListener = struct {
    settings: SimpleHttpListenerSettings,

    const Self = @This();
    var the_one_and_only_listener: ?*SimpleHttpListener = null;

    pub fn init(settings: SimpleHttpListenerSettings) Self {
        return .{
            .settings = settings,
        };
    }

    // we could make it dynamic by passing a SimpleHttpListener via udata
    pub fn theOneAndOnlyRequestCallBack(r: [*c]C.http_s) callconv(.C) void {
        if (the_one_and_only_listener) |l| {
            var req: SimpleRequest = .{
                .path = fio2str(r.*.path),
                .query = fio2str(r.*.query),
                .body = fio2str(r.*.body),
                .method = fio2str(r.*.method),
                .h = r,
            };
            l.settings.on_request.?(req);
        }
    }

    pub fn listen(self: *Self) !void {
        var pfolder: [*c]const u8 = null;
        var pfolder_len: usize = 0;

        if (self.settings.public_folder) |pf| {
            pfolder_len = pf.len;
            pfolder = pf.ptr;
        }

        var x: C.http_settings_s = .{
            .on_request = if (self.settings.on_request) |_| Self.theOneAndOnlyRequestCallBack else null,
            .on_upgrade = null,
            .on_response = null,
            .on_finish = null,
            .udata = null,
            .public_folder = pfolder,
            .public_folder_length = pfolder_len,
            .max_header_size = 32 * 1024,
            .max_body_size = self.settings.max_body_size orelse 50 * 1024 * 1024,
            .max_clients = self.settings.max_clients orelse 100,
            .tls = null,
            .reserved1 = 0,
            .reserved2 = 0,
            .reserved3 = 0,
            .ws_max_msg_size = 0,
            .timeout = self.settings.timeout orelse 5,
            .ws_timeout = 0,
            .log = if (self.settings.log) 1 else 0,
            .is_client = 0,
        };
        // TODO: BUG: without this print/sleep statement, -Drelease* loop forever
        // in debug2 and debug3 of hello example
        // std.debug.print("X\n", .{});
        std.time.sleep(500 * 1000 * 1000);

        var portbuf: [100]u8 = undefined;
        const printed_port = try std.fmt.bufPrintZ(&portbuf, "{d}", .{self.settings.port});

        // pub fn bufPrintZ(buf: []u8, comptime fmt: []const u8, args: anytype) BufPrintError![:0]u8 {
        //     const result = try bufPrint(buf, fmt ++ "\x00", args);
        //     return result[0 .. result.len - 1 :0];
        // }
        if (C.http_listen(printed_port.ptr, self.settings.interface, x) == -1) {
            return error.ListenError;
        }

        // set ourselves up to handle requests:
        // TODO: do we mind the race condition?
        // the SimpleHttpRequestFn will check if this is null and not process
        // the request if it isn't set. hence, if started under full load, the
        // first request(s) might not be serviced, as long as it takes from
        // C.http_listen() to here
        Self.the_one_and_only_listener = self;
    }
};

//
// lower level listening
//
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

pub fn listen(port: [*c]const u8, interface: [*c]const u8, settings: ListenSettings) ListenError!void {
    var pfolder: [*c]const u8 = null;
    var pfolder_len: usize = 0;

    if (settings.public_folder) |pf| {
        pfolder_len = pf.len;
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
    // TODO: BUG: without this print/sleep statement, -Drelease* loop forever
    // in debug2 and debug3 of hello example
    // std.debug.print("X\n", .{});
    std.time.sleep(500 * 1000 * 1000);

    if (C.http_listen(port, interface, x) == -1) {
        return error.ListenError;
    }
}

// lower level sendBody
pub fn sendBody(request: [*c]C.http_s, body: []const u8) void {
    _ = C.http_send_body(request, @intToPtr(
        *anyopaque,
        @ptrToInt(body.ptr),
    ), body.len);
}
