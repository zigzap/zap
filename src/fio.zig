pub const FIOBJ = usize;
pub extern fn is_invalid(o: FIOBJ) c_int;
pub const fio_url_s = extern struct {
    scheme: fio_str_info_s,
    user: fio_str_info_s,
    password: fio_str_info_s,
    host: fio_str_info_s,
    port: fio_str_info_s,
    path: fio_str_info_s,
    query: fio_str_info_s,
    target: fio_str_info_s,
};
pub extern fn fio_url_parse(url: [*c]const u8, length: usize) fio_url_s;
pub const struct_fio_start_args = extern struct {
    threads: i16,
    workers: i16,
};
pub const fio_start_args = struct_fio_start_args;
pub extern fn fio_start(args: struct_fio_start_args) void;
pub extern fn fio_stop() void;
const struct_unnamed_37 = extern struct {
    vtbl: ?*anyopaque,
    flag: usize,
    out_headers: FIOBJ,
};
pub const __time_t = c_long;
pub const time_t = __time_t;
pub const __syscall_slong_t = c_long;
pub const struct_timespec = extern struct {
    tv_sec: __time_t,
    tv_nsec: __syscall_slong_t,
};
pub const struct_http_settings_s = extern struct {
    on_request: ?*const fn ([*c]http_s) callconv(.C) void,
    on_upgrade: ?*const fn ([*c]http_s, [*c]u8, usize) callconv(.C) void,
    on_response: ?*const fn ([*c]http_s) callconv(.C) void,
    on_finish: ?*const fn ([*c]struct_http_settings_s) callconv(.C) void,
    udata: ?*anyopaque,
    public_folder: [*c]const u8,
    public_folder_length: usize,
    max_header_size: usize,
    max_body_size: usize,
    max_clients: isize,
    tls: ?*anyopaque,
    reserved1: isize,
    reserved2: isize,
    reserved3: isize,
    ws_max_msg_size: usize,
    timeout: u8,
    ws_timeout: u8,
    log: u8,
    is_client: u8,
};
pub const http_settings_s = struct_http_settings_s;
pub const http_s = extern struct {
    private_data: struct_unnamed_37,
    received_at: struct_timespec,
    method: FIOBJ,
    status_str: FIOBJ,
    version: FIOBJ,
    status: usize,
    path: FIOBJ,
    query: FIOBJ,
    headers: FIOBJ,
    cookies: FIOBJ,
    params: FIOBJ,
    body: FIOBJ,
    udata: ?*anyopaque,
}; // zig-cache/i/e0c8a6e617497ade13de512cbe191f23/include/http.h:153:12: warning: struct demoted to opaque type - has bitfield
pub const struct_fio_str_info_s = extern struct {
    capa: usize,
    len: usize,
    data: [*c]u8,
};
pub const fio_str_info_s = struct_fio_str_info_s;
pub extern fn http_send_body(h: [*c]http_s, data: ?*anyopaque, length: usize) c_int;

pub extern fn fiobj_hash_new() FIOBJ;
pub extern fn fiobj_hash_set(hash: FIOBJ, key: FIOBJ, obj: FIOBJ) c_int;
pub extern fn fiobj_hash_get(hash: FIOBJ, key: FIOBJ) FIOBJ;
pub extern fn fiobj_ary_push(ary: FIOBJ, obj: FIOBJ) void;
pub extern fn fiobj_float_new(num: f64) FIOBJ;
pub extern fn fiobj_num_new_bignum(num: isize) FIOBJ;
pub extern fn fiobj_free_wrapped(o: FIOBJ) callconv(.C) void;
pub fn fiobj_null() callconv(.C) FIOBJ {
    return @bitCast(FIOBJ, @as(c_long, FIOBJ_T_NULL));
}
pub fn fiobj_true() callconv(.C) FIOBJ {
    return @bitCast(FIOBJ, @as(c_long, FIOBJ_T_TRUE));
}
pub fn fiobj_false() callconv(.C) FIOBJ {
    return @bitCast(FIOBJ, @as(c_long, FIOBJ_T_FALSE));
}
pub extern fn fiobj_ary_new2(capa: usize) FIOBJ;
pub extern fn fiobj_str_new(str: [*c]const u8, len: usize) FIOBJ;
pub extern fn fiobj_str_buf(capa: usize) FIOBJ;

pub inline fn FIOBJ_TYPE(obj: anytype) @TypeOf(fiobj_type(obj)) {
    return fiobj_type(obj);
}
pub inline fn FIOBJ_TYPE_IS(obj: anytype, @"type": anytype) @TypeOf(fiobj_type_is(obj, @"type")) {
    return fiobj_type_is(obj, @"type");
}
pub inline fn FIOBJ_IS_NULL(obj: anytype) @TypeOf(!(obj != 0) or (obj == @import("std").zig.c_translation.cast(FIOBJ, FIOBJ_T_NULL))) {
    return !(obj != 0) or (obj == @import("std").zig.c_translation.cast(FIOBJ, FIOBJ_T_NULL));
}
pub const FIOBJ_INVALID = @as(c_int, 0);
pub const FIOBJECT_NUMBER_FLAG = @as(c_int, 1);
pub const FIOBJECT_PRIMITIVE_FLAG = @as(c_int, 6);
pub const FIOBJECT_STRING_FLAG = @as(c_int, 2);
pub const FIOBJECT_HASH_FLAG = @as(c_int, 4);
pub const FIOBJECT_TYPE_MASK = ~@import("std").zig.c_translation.cast(usize, @as(c_int, 7));
pub const FIOBJ_NUMBER_SIGN_MASK = ~@import("std").zig.c_translation.cast(usize, @as(c_int, 0)) >> @as(c_int, 1);
pub const FIOBJ_NUMBER_SIGN_BIT = ~FIOBJ_NUMBER_SIGN_MASK;
pub const FIOBJ_NUMBER_SIGN_EXCLUDE_BIT = FIOBJ_NUMBER_SIGN_BIT >> @as(c_int, 1);
pub inline fn FIOBJ_IS_ALLOCATED(o: anytype) @TypeOf(((o != 0) and ((o & FIOBJECT_NUMBER_FLAG) == @as(c_int, 0))) and ((o & FIOBJECT_PRIMITIVE_FLAG) != FIOBJECT_PRIMITIVE_FLAG)) {
    return ((o != 0) and ((o & FIOBJECT_NUMBER_FLAG) == @as(c_int, 0))) and ((o & FIOBJECT_PRIMITIVE_FLAG) != FIOBJECT_PRIMITIVE_FLAG);
}
pub inline fn FIOBJ2PTR(o: anytype) ?*anyopaque {
    return @import("std").zig.c_translation.cast(?*anyopaque, o & FIOBJECT_TYPE_MASK);
}
pub inline fn FIOBJECT2VTBL(o: anytype) @TypeOf(fiobj_type_vtable(o)) {
    return fiobj_type_vtable(o);
}
pub inline fn FIOBJECT2HEAD(o: anytype) [*c]fiobj_object_header_s {
    return @import("std").zig.c_translation.cast([*c]fiobj_object_header_s, FIOBJ2PTR(o));
}
pub inline fn fiobj_ary_entry(a: anytype, p: anytype) @TypeOf(fiobj_ary_index(a, p)) {
    return fiobj_ary_index(a, p);
}
pub extern fn fiobj_ary_index(ary: FIOBJ, pos: i64) FIOBJ;
pub const FIOBJ_T_NUMBER: c_int = 1;
pub const FIOBJ_T_NULL: c_int = 6;
pub const FIOBJ_T_TRUE: c_int = 22;
pub const FIOBJ_T_FALSE: c_int = 38;
pub const FIOBJ_T_FLOAT: c_int = 39;
pub const FIOBJ_T_STRING: c_int = 40;
pub const FIOBJ_T_ARRAY: c_int = 41;
pub const FIOBJ_T_HASH: c_int = 42;
pub const FIOBJ_T_DATA: c_int = 43;
pub const FIOBJ_T_UNKNOWN: c_int = 44;
pub const fiobj_type_enum = u8;
pub const fiobj_object_vtable_s = extern struct {
    class_name: [*c]const u8,
    dealloc: ?*const fn (FIOBJ, ?*const fn (FIOBJ, ?*anyopaque) callconv(.C) void, ?*anyopaque) callconv(.C) void,
    count: ?*const fn (FIOBJ) callconv(.C) usize,
    is_true: ?*const fn (FIOBJ) callconv(.C) usize,
    is_eq: ?*const fn (FIOBJ, FIOBJ) callconv(.C) usize,
    each: ?*const fn (FIOBJ, usize, ?*const fn (FIOBJ, ?*anyopaque) callconv(.C) c_int, ?*anyopaque) callconv(.C) usize,
    to_str: ?*const fn (FIOBJ) callconv(.C) fio_str_info_s,
    to_i: ?*const fn (FIOBJ) callconv(.C) isize,
    to_f: ?*const fn (FIOBJ) callconv(.C) f64,
};
pub const fiobj_object_header_s = extern struct {
    type: fiobj_type_enum,
    ref: u32,
};
pub fn fiobj_type_is(arg_o: FIOBJ, arg_type: fiobj_type_enum) callconv(.C) usize {
    var o = arg_o;
    var @"type" = arg_type;
    while (true) {
        switch (@bitCast(c_int, @as(c_uint, @"type"))) {
            @as(c_int, 1) => return @bitCast(usize, @as(c_long, @boolToInt(((o & @bitCast(c_ulong, @as(c_long, @as(c_int, 1)))) != 0) or (@bitCast(c_int, @as(c_uint, @intToPtr([*c]fiobj_type_enum, o)[@intCast(c_uint, @as(c_int, 0))])) == FIOBJ_T_NUMBER)))),
            @as(c_int, 6) => return @bitCast(usize, @as(c_long, @boolToInt(!(o != 0) or (o == fiobj_null())))),
            @as(c_int, 22) => return @bitCast(usize, @as(c_long, @boolToInt(o == fiobj_true()))),
            @as(c_int, 38) => return @bitCast(usize, @as(c_long, @boolToInt(o == fiobj_false()))),
            @as(c_int, 40) => return @bitCast(usize, @as(c_long, @boolToInt(((true and ((o & @bitCast(c_ulong, @as(c_long, @as(c_int, 1)))) == @bitCast(c_ulong, @as(c_long, @as(c_int, 0))))) and ((o & @bitCast(c_ulong, @as(c_long, @as(c_int, 6)))) == @bitCast(c_ulong, @as(c_long, @as(c_int, 2))))) or (((@as(c_int, 2) == @as(c_int, 0)) and (((o != 0) and ((o & @bitCast(c_ulong, @as(c_long, @as(c_int, 1)))) == @bitCast(c_ulong, @as(c_long, @as(c_int, 0))))) and ((o & @bitCast(c_ulong, @as(c_long, @as(c_int, 6)))) != @bitCast(c_ulong, @as(c_long, @as(c_int, 6)))))) and (@bitCast(c_int, @as(c_uint, @ptrCast([*c]fiobj_type_enum, @alignCast(@import("std").meta.alignment([*c]fiobj_type_enum), @intToPtr(?*anyopaque, o & ~@bitCast(usize, @as(c_long, @as(c_int, 7))))))[@intCast(c_uint, @as(c_int, 0))])) == FIOBJ_T_STRING))))),
            @as(c_int, 42) => {
                if (true) {
                    return @bitCast(usize, @as(c_long, @boolToInt(((o & @bitCast(c_ulong, @as(c_long, @as(c_int, 1)))) == @bitCast(c_ulong, @as(c_long, @as(c_int, 0)))) and ((o & @bitCast(c_ulong, @as(c_long, @as(c_int, 6)))) == @bitCast(c_ulong, @as(c_long, @as(c_int, 4)))))));
                }
                return @bitCast(usize, @as(c_long, @boolToInt((((o != 0) and ((o & @bitCast(c_ulong, @as(c_long, @as(c_int, 1)))) == @bitCast(c_ulong, @as(c_long, @as(c_int, 0))))) and ((o & @bitCast(c_ulong, @as(c_long, @as(c_int, 6)))) != @bitCast(c_ulong, @as(c_long, @as(c_int, 6))))) and (@bitCast(c_int, @as(c_uint, @ptrCast([*c]fiobj_type_enum, @alignCast(@import("std").meta.alignment([*c]fiobj_type_enum), @intToPtr(?*anyopaque, o & ~@bitCast(usize, @as(c_long, @as(c_int, 7))))))[@intCast(c_uint, @as(c_int, 0))])) == @bitCast(c_int, @as(c_uint, @"type"))))));
            },
            @as(c_int, 39), @as(c_int, 41), @as(c_int, 43), @as(c_int, 44) => return @bitCast(usize, @as(c_long, @boolToInt((((o != 0) and ((o & @bitCast(c_ulong, @as(c_long, @as(c_int, 1)))) == @bitCast(c_ulong, @as(c_long, @as(c_int, 0))))) and ((o & @bitCast(c_ulong, @as(c_long, @as(c_int, 6)))) != @bitCast(c_ulong, @as(c_long, @as(c_int, 6))))) and (@bitCast(c_int, @as(c_uint, @ptrCast([*c]fiobj_type_enum, @alignCast(@import("std").meta.alignment([*c]fiobj_type_enum), @intToPtr(?*anyopaque, o & ~@bitCast(usize, @as(c_long, @as(c_int, 7))))))[@intCast(c_uint, @as(c_int, 0))])) == @bitCast(c_int, @as(c_uint, @"type")))))),
            else => {},
        }
        break;
    }
    return @bitCast(usize, @as(c_long, @boolToInt((((o != 0) and ((o & @bitCast(c_ulong, @as(c_long, @as(c_int, 1)))) == @bitCast(c_ulong, @as(c_long, @as(c_int, 0))))) and ((o & @bitCast(c_ulong, @as(c_long, @as(c_int, 6)))) != @bitCast(c_ulong, @as(c_long, @as(c_int, 6))))) and (@bitCast(c_int, @as(c_uint, @ptrCast([*c]fiobj_type_enum, @alignCast(@import("std").meta.alignment([*c]fiobj_type_enum), @intToPtr(?*anyopaque, o & ~@bitCast(usize, @as(c_long, @as(c_int, 7))))))[@intCast(c_uint, @as(c_int, 0))])) == @bitCast(c_int, @as(c_uint, @"type"))))));
}
pub fn fiobj_type(arg_o: FIOBJ) callconv(.C) fiobj_type_enum {
    var o = arg_o;
    if (!(o != 0)) return @bitCast(u8, @truncate(i8, FIOBJ_T_NULL));
    if ((o & @bitCast(c_ulong, @as(c_long, @as(c_int, 1)))) != 0) return @bitCast(u8, @truncate(i8, FIOBJ_T_NUMBER));
    if ((o & @bitCast(c_ulong, @as(c_long, @as(c_int, 6)))) == @bitCast(c_ulong, @as(c_long, @as(c_int, 6)))) return @bitCast(u8, @truncate(u8, o));
    if (true and ((o & @bitCast(c_ulong, @as(c_long, @as(c_int, 6)))) == @bitCast(c_ulong, @as(c_long, @as(c_int, 2))))) return @bitCast(u8, @truncate(i8, FIOBJ_T_STRING));
    if (true and ((o & @bitCast(c_ulong, @as(c_long, @as(c_int, 6)))) == @bitCast(c_ulong, @as(c_long, @as(c_int, 4))))) return @bitCast(u8, @truncate(i8, FIOBJ_T_HASH));
    return @ptrCast([*c]fiobj_type_enum, @alignCast(@import("std").meta.alignment([*c]fiobj_type_enum), @intToPtr(?*anyopaque, o & ~@bitCast(usize, @as(c_long, @as(c_int, 7))))))[@intCast(c_uint, @as(c_int, 0))];
}
pub extern const FIOBJECT_VTABLE_NUMBER: fiobj_object_vtable_s;
pub extern const FIOBJECT_VTABLE_FLOAT: fiobj_object_vtable_s;
pub extern const FIOBJECT_VTABLE_STRING: fiobj_object_vtable_s;
pub extern const FIOBJECT_VTABLE_ARRAY: fiobj_object_vtable_s;
pub extern const FIOBJECT_VTABLE_HASH: fiobj_object_vtable_s;
pub extern const FIOBJECT_VTABLE_DATA: fiobj_object_vtable_s;
pub fn fiobj_type_vtable(arg_o: FIOBJ) callconv(.C) [*c]const fiobj_object_vtable_s {
    var o = arg_o;
    while (true) {
        switch (@bitCast(c_int, @as(c_uint, fiobj_type(o)))) {
            @as(c_int, 1) => return &FIOBJECT_VTABLE_NUMBER,
            @as(c_int, 39) => return &FIOBJECT_VTABLE_FLOAT,
            @as(c_int, 40) => return &FIOBJECT_VTABLE_STRING,
            @as(c_int, 41) => return &FIOBJECT_VTABLE_ARRAY,
            @as(c_int, 42) => return &FIOBJECT_VTABLE_HASH,
            @as(c_int, 43) => return &FIOBJECT_VTABLE_DATA,
            @as(c_int, 6), @as(c_int, 22), @as(c_int, 38), @as(c_int, 44) => return null,
            else => {},
        }
        break;
    }
    return null;
}
pub extern fn fio_ltocstr(c_long) fio_str_info_s;
pub fn fiobj_obj2cstr(o: FIOBJ) callconv(.C) fio_str_info_s {
    if (!(o != 0)) {
        var ret: fio_str_info_s = fio_str_info_s{
            .capa = @bitCast(usize, @as(c_long, @as(c_int, 0))),
            .len = @bitCast(usize, @as(c_long, @as(c_int, 4))),
            .data = @intToPtr([*c]u8, @ptrToInt("null")),
        };
        return ret;
    }
    if ((o & @bitCast(c_ulong, @as(c_long, @as(c_int, 1)))) != 0) return fio_ltocstr(@bitCast(isize, o) >> @intCast(@import("std").math.Log2Int(isize), 1));
    if ((o & @bitCast(c_ulong, @as(c_long, @as(c_int, 6)))) == @bitCast(c_ulong, @as(c_long, @as(c_int, 6)))) {
        while (true) {
            switch (@bitCast(c_int, @as(c_uint, @bitCast(u8, @truncate(u8, o))))) {
                @as(c_int, 6) => {
                    {
                        var ret: fio_str_info_s = fio_str_info_s{
                            .capa = @bitCast(usize, @as(c_long, @as(c_int, 0))),
                            .len = @bitCast(usize, @as(c_long, @as(c_int, 4))),
                            .data = @intToPtr([*c]u8, @ptrToInt("null")),
                        };
                        return ret;
                    }
                },
                @as(c_int, 38) => {
                    {
                        var ret: fio_str_info_s = fio_str_info_s{
                            .capa = @bitCast(usize, @as(c_long, @as(c_int, 0))),
                            .len = @bitCast(usize, @as(c_long, @as(c_int, 5))),
                            .data = @intToPtr([*c]u8, @ptrToInt("false")),
                        };
                        return ret;
                    }
                },
                @as(c_int, 22) => {
                    {
                        var ret: fio_str_info_s = fio_str_info_s{
                            .capa = @bitCast(usize, @as(c_long, @as(c_int, 0))),
                            .len = @bitCast(usize, @as(c_long, @as(c_int, 4))),
                            .data = @intToPtr([*c]u8, @ptrToInt("true")),
                        };
                        return ret;
                    }
                },
                else => break,
            }
            break;
        }
    }
    return fiobj_type_vtable(o).*.to_str.?(o);
}
pub const http_cookie_args_s = opaque {};
pub extern fn http_set_header(h: [*c]http_s, name: FIOBJ, value: FIOBJ) c_int;
pub extern fn http_set_header2(h: [*c]http_s, name: fio_str_info_s, value: fio_str_info_s) c_int;
pub extern fn http_set_cookie(h: [*c]http_s, http_cookie_args_s) c_int;
pub extern fn http_sendfile(h: [*c]http_s, fd: c_int, length: usize, offset: usize) c_int;
pub extern fn http_sendfile2(h: [*c]http_s, prefix: [*c]const u8, prefix_len: usize, encoded: [*c]const u8, encoded_len: usize) c_int;
pub extern fn http_send_error(h: [*c]http_s, error_code: usize) c_int;
pub extern fn http_finish(h: [*c]http_s) void;
pub extern fn http_push_data(h: [*c]http_s, data: ?*anyopaque, length: usize, mime_type: FIOBJ) c_int;
pub extern fn http_push_file(h: [*c]http_s, filename: FIOBJ, mime_type: FIOBJ) c_int;
pub const struct_http_pause_handle_s = opaque {};
pub const http_pause_handle_s = struct_http_pause_handle_s;
pub extern fn http_pause(h: [*c]http_s, task: ?*const fn (?*http_pause_handle_s) callconv(.C) void) void;
pub extern fn http_resume(http: ?*http_pause_handle_s, task: ?*const fn ([*c]http_s) callconv(.C) void, fallback: ?*const fn (?*anyopaque) callconv(.C) void) void;
pub extern fn http_paused_udata_get(http: ?*http_pause_handle_s) ?*anyopaque;
pub extern fn http_paused_udata_set(http: ?*http_pause_handle_s, udata: ?*anyopaque) ?*anyopaque;
pub extern fn http_listen(port: [*c]const u8, binding: [*c]const u8, struct_http_settings_s) isize;
pub extern fn http_connect(url: [*c]const u8, unix_address: [*c]const u8, struct_http_settings_s) isize;
pub extern fn http_settings(h: [*c]http_s) [*c]struct_http_settings_s;
pub extern fn http_peer_addr(h: [*c]http_s) fio_str_info_s;
pub extern fn http_hijack(h: [*c]http_s, leftover: [*c]fio_str_info_s) isize;
pub const struct_ws_s = opaque {};
pub const ws_s = struct_ws_s;
pub const websocket_settings_s = extern struct {
    on_message: ?*const fn (?*ws_s, fio_str_info_s, u8) callconv(.C) void,
    on_open: ?*const fn (?*ws_s) callconv(.C) void,
    on_ready: ?*const fn (?*ws_s) callconv(.C) void,
    on_shutdown: ?*const fn (?*ws_s) callconv(.C) void,
    on_close: ?*const fn (isize, ?*anyopaque) callconv(.C) void,
    udata: ?*anyopaque,
};
pub extern fn http_upgrade2ws(http: [*c]http_s, websocket_settings_s) c_int;
pub extern fn websocket_connect(url: [*c]const u8, settings: websocket_settings_s) c_int;
pub extern fn websocket_attach(uuid: isize, http_settings: [*c]http_settings_s, args: [*c]websocket_settings_s, data: ?*anyopaque, length: usize) void;
pub extern fn websocket_udata_get(ws: ?*ws_s) ?*anyopaque;
pub extern fn websocket_udata_set(ws: ?*ws_s, udata: ?*anyopaque) ?*anyopaque;
pub extern fn websocket_uuid(ws: ?*ws_s) isize;
pub extern fn websocket_is_client(ws: ?*ws_s) u8;
pub extern fn websocket_write(ws: ?*ws_s, msg: fio_str_info_s, is_text: u8) c_int;
pub extern fn websocket_close(ws: ?*ws_s) void; // zig-cache/i/e0c8a6e617497ade13de512cbe191f23/include/websockets.h:104:12: warning: struct demoted to opaque type - has bitfield
pub const struct_websocket_subscribe_s = opaque {};
pub extern fn websocket_subscribe(args: struct_websocket_subscribe_s) usize;
pub extern fn websocket_unsubscribe(ws: ?*ws_s, subscription_id: usize) void;
pub extern fn websocket_optimize4broadcasts(@"type": isize, enable: c_int) void;
pub const http_sse_s = struct_http_sse_s;
pub const struct_http_sse_s = extern struct {
    on_open: ?*const fn ([*c]http_sse_s) callconv(.C) void,
    on_ready: ?*const fn ([*c]http_sse_s) callconv(.C) void,
    on_shutdown: ?*const fn ([*c]http_sse_s) callconv(.C) void,
    on_close: ?*const fn ([*c]http_sse_s) callconv(.C) void,
    udata: ?*anyopaque,
};
pub extern fn http_upgrade2sse(h: [*c]http_s, http_sse_s) c_int;
pub extern fn http_sse_set_timout(sse: [*c]http_sse_s, timeout: u8) void;
pub const fio_match_fn = ?*const fn (fio_str_info_s, fio_str_info_s) callconv(.C) c_int;
pub const struct_http_sse_subscribe_args = extern struct {
    channel: fio_str_info_s,
    on_message: ?*const fn ([*c]http_sse_s, fio_str_info_s, fio_str_info_s, ?*anyopaque) callconv(.C) void,
    on_unsubscribe: ?*const fn (?*anyopaque) callconv(.C) void,
    udata: ?*anyopaque,
    match: fio_match_fn,
};
pub extern fn http_sse_subscribe(sse: [*c]http_sse_s, args: struct_http_sse_subscribe_args) usize;
pub extern fn http_sse_unsubscribe(sse: [*c]http_sse_s, subscription: usize) void;
pub const struct_http_sse_write_args = extern struct {
    id: fio_str_info_s,
    event: fio_str_info_s,
    data: fio_str_info_s,
    retry: isize,
};
pub extern fn http_sse_write(sse: [*c]http_sse_s, struct_http_sse_write_args) c_int;
pub extern fn http_sse2uuid(sse: [*c]http_sse_s) isize;
pub extern fn http_sse_close(sse: [*c]http_sse_s) c_int;
pub extern fn http_sse_dup(sse: [*c]http_sse_s) [*c]http_sse_s;
pub extern fn http_sse_free(sse: [*c]http_sse_s) void;
pub extern fn http_parse_body(h: [*c]http_s) c_int;
pub extern fn http_parse_query(h: [*c]http_s) void;
pub extern fn http_parse_cookies(h: [*c]http_s, is_url_encoded: u8) void;
pub extern fn http_add2hash(dest: FIOBJ, name: [*c]u8, name_len: usize, value: [*c]u8, value_len: usize, encoded: u8) c_int;
pub extern fn http_add2hash2(dest: FIOBJ, name: [*c]u8, name_len: usize, value: FIOBJ, encoded: u8) c_int;
pub extern fn http_status2str(status: usize) fio_str_info_s;
pub extern fn http_mimetype_register(file_ext: [*c]u8, file_ext_len: usize, mime_type_str: FIOBJ) void;
pub extern fn http_mimetype_find(file_ext: [*c]u8, file_ext_len: usize) FIOBJ;
pub extern fn http_mimetype_find2(url: FIOBJ) FIOBJ;
pub extern fn http_mimetype_clear() void;
pub extern var HTTP_HEADER_ACCEPT: FIOBJ;
pub extern var HTTP_HEADER_CACHE_CONTROL: FIOBJ;
pub extern var HTTP_HEADER_CONNECTION: FIOBJ;
pub extern var HTTP_HEADER_CONTENT_ENCODING: FIOBJ;
pub extern var HTTP_HEADER_CONTENT_LENGTH: FIOBJ;
pub extern var HTTP_HEADER_CONTENT_RANGE: FIOBJ;
pub extern var HTTP_HEADER_CONTENT_TYPE: FIOBJ;
pub extern var HTTP_HEADER_COOKIE: FIOBJ;
pub extern var HTTP_HEADER_DATE: FIOBJ;
pub extern var HTTP_HEADER_ETAG: FIOBJ;
pub extern var HTTP_HEADER_HOST: FIOBJ;
pub extern var HTTP_HEADER_LAST_MODIFIED: FIOBJ;
pub extern var HTTP_HEADER_ORIGIN: FIOBJ;
pub extern var HTTP_HEADER_SET_COOKIE: FIOBJ;
pub extern var HTTP_HEADER_UPGRADE: FIOBJ;
pub extern fn http_req2str(h: [*c]http_s) FIOBJ;
pub extern fn http_write_log(h: [*c]http_s) void;
pub extern fn http_gmtime(timer: time_t, tmbuf: [*c]struct_tm) [*c]struct_tm;
pub extern fn http_date2rfc7231(target: [*c]u8, tmbuf: [*c]struct_tm) usize;
pub extern fn http_date2rfc2109(target: [*c]u8, tmbuf: [*c]struct_tm) usize;
pub extern fn http_date2rfc2822(target: [*c]u8, tmbuf: [*c]struct_tm) usize;
pub fn http_date2str(arg_target: [*c]u8, arg_tmbuf: [*c]struct_tm) callconv(.C) usize {
    var target = arg_target;
    var tmbuf = arg_tmbuf;
    return http_date2rfc7231(target, tmbuf);
}
pub extern fn http_time2str(target: [*c]u8, t: time_t) usize;
pub extern fn http_decode_url_unsafe(dest: [*c]u8, url_data: [*c]const u8) isize;
pub extern fn http_decode_url(dest: [*c]u8, url_data: [*c]const u8, length: usize) isize;
pub extern fn http_decode_path_unsafe(dest: [*c]u8, url_data: [*c]const u8) isize;
pub extern fn http_decode_path(dest: [*c]u8, url_data: [*c]const u8, length: usize) isize;
pub const http_url_s = fio_url_s;

pub const struct_tm = extern struct {
    tm_sec: c_int,
    tm_min: c_int,
    tm_hour: c_int,
    tm_mday: c_int,
    tm_mon: c_int,
    tm_year: c_int,
    tm_wday: c_int,
    tm_yday: c_int,
    tm_isdst: c_int,
    tm_gmtoff: c_long,
    tm_zone: [*c]const u8,
};
