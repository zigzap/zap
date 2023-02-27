// supporting code to make using facilio's mustache stuff
// (see http://facil.io/0.7.x/fiobj_mustache)
// easier / possible / more zig-like

const std = @import("std");
const util = @import("util.zig");

pub const FIOBJ = usize;
pub const struct_mustache_s = opaque {};
pub const enum_mustache_error_en = c_uint;
pub const mustache_error_en = enum_mustache_error_en;

pub const mustache_s = struct_mustache_s;
pub extern fn fiobj_mustache_new(args: MustacheLoadArgs) ?*mustache_s;
pub extern fn fiobj_mustache_build(mustache: ?*mustache_s, data: FIOBJ) FIOBJ;
pub extern fn fiobj_mustache_build2(dest: FIOBJ, mustache: ?*mustache_s, data: FIOBJ) FIOBJ;
pub extern fn fiobj_mustache_free(mustache: ?*mustache_s) void;

pub const MustacheLoadArgs = extern struct {
    filename: [*c]const u8,
    filename_len: usize,
    data: [*c]const u8,
    data_len: usize,
    err: [*c]mustache_error_en,
};

pub const Mustache = mustache_s;

pub const MustacheStatus = enum(c_int) {};

pub const MustacheError = error{
    MUSTACHE_ERR_TOO_DEEP,
    MUSTACHE_ERR_CLOSURE_MISMATCH,
    MUSTACHE_ERR_FILE_NOT_FOUND,
    MUSTACHE_ERR_FILE_TOO_BIG,
    MUSTACHE_ERR_FILE_NAME_TOO_LONG,
    MUSTACHE_ERR_FILE_NAME_TOO_SHORT,
    MUSTACHE_ERR_EMPTY_TEMPLATE,
    MUSTACHE_ERR_DELIMITER_TOO_LONG,
    MUSTACHE_ERR_NAME_TOO_LONG,
    MUSTACHE_ERR_UNKNOWN,
    MUSTACHE_ERR_USER_ERROR,
};

// pub extern fn fiobj_mustache_load(filename: fio_str_info_s) ?*mustache_s;
// pub extern fn fiobj_mustache_build2(dest: FIOBJ, mustache: ?*mustache_s, data: FIOBJ) FIOBJ;

pub fn MustacheNew(data: []const u8) MustacheError!*Mustache {
    var err: mustache_error_en = undefined;
    var args: MustacheLoadArgs = .{
        .filename = null,
        .filename_len = 0,
        .data = data.ptr,
        .data_len = data.len,
        .err = &err,
    };
    var ret = fiobj_mustache_new(args);
    switch (err) {
        0 => return ret.?,
        1 => return MustacheError.MUSTACHE_ERR_TOO_DEEP,
        2 => return MustacheError.MUSTACHE_ERR_CLOSURE_MISMATCH,
        3 => return MustacheError.MUSTACHE_ERR_FILE_NOT_FOUND,
        4 => return MustacheError.MUSTACHE_ERR_FILE_TOO_BIG,
        5 => return MustacheError.MUSTACHE_ERR_FILE_NAME_TOO_LONG,
        6 => return MustacheError.MUSTACHE_ERR_FILE_NAME_TOO_SHORT,
        7 => return MustacheError.MUSTACHE_ERR_EMPTY_TEMPLATE,
        8 => return MustacheError.MUSTACHE_ERR_DELIMITER_TOO_LONG,
        9 => return MustacheError.MUSTACHE_ERR_NAME_TOO_LONG,
        10 => return MustacheError.MUSTACHE_ERR_UNKNOWN,
        11 => return MustacheError.MUSTACHE_ERR_USER_ERROR,
        else => return MustacheError.MUSTACHE_ERR_UNKNOWN,
    }
}

// implement these: fiobj_mustache.c
// pub extern fn fiobj_mustache_build(mustache: ?*mustache_s, data: FIOBJ) FIOBJ;
// pub extern fn fiobj_mustache_build2(dest: FIOBJ, mustache: ?*mustache_s, data: FIOBJ) FIOBJ;

pub extern fn fiobj_hash_new() FIOBJ;
pub extern fn fiobj_hash_set(hash: FIOBJ, key: FIOBJ, obj: FIOBJ) c_int;
pub extern fn fiobj_ary_push(ary: FIOBJ, obj: FIOBJ) void;
pub extern fn fiobj_float_new(num: f64) FIOBJ;
pub extern fn fiobj_num_new_bignum(num: isize) FIOBJ;
pub extern fn fiobj_free_wrapped(o: FIOBJ) callconv(.C) void;
pub const FIOBJ_T_TRUE: c_int = 22;
pub const FIOBJ_T_FALSE: c_int = 38;
pub fn fiobj_true() callconv(.C) FIOBJ {
    return @bitCast(FIOBJ, @as(c_long, FIOBJ_T_TRUE));
}
pub fn fiobj_false() callconv(.C) FIOBJ {
    return @bitCast(FIOBJ, @as(c_long, FIOBJ_T_FALSE));
}
pub extern fn fiobj_ary_new2(capa: usize) FIOBJ;
pub extern fn fiobj_str_new(str: [*c]const u8, len: usize) FIOBJ;
pub extern fn fiobj_str_buf(capa: usize) FIOBJ;

const MustacheBuildResult = struct {
    fiobj_result: FIOBJ = 0,

    pub fn deinit(m: *const MustacheBuildResult) void {
        fiobj_free_wrapped(m.fiobj_result);
    }

    pub fn str(m: *const MustacheBuildResult) ?[]const u8 {
        return util.fio2str(m.fiobj_result);
    }
};

// this build is slow because it needs to translate to a FIOBJ data
// object FIOBJ_T_HASH
pub fn MustacheBuild(mustache: *Mustache, data: anytype) MustacheBuildResult {
    const T = @TypeOf(data);
    if (@typeInfo(T) != .Struct) {
        @compileError("No struct: '" ++ @typeName(T) ++ "'");
    }

    const fiobj_data = fiobjectify(data);

    return .{ .fiobj_result = fiobj_mustache_build(mustache, fiobj_data) };
}

pub fn fiobjectify(
    value: anytype,
) FIOBJ {
    const T = @TypeOf(value);
    switch (@typeInfo(T)) {
        .Float, .ComptimeFloat => {
            return fiobj_float_new(value);
        },
        .Int, .ComptimeInt => {
            return fiobj_num_new_bignum(value);
        },
        .Bool => {
            return if (value) fiobj_true() else fiobj_false();
        },
        .Null => {
            return 0;
        },
        .Optional => {
            if (value) |payload| {
                return fiobjectify(payload);
            } else {
                return fiobjectify(null);
            }
        },
        .Enum => {
            return fiobj_num_new_bignum(@enumToInt(value));
        },
        .Union => {
            const info = @typeInfo(T).Union;
            if (info.tag_type) |UnionTagType| {
                inline for (info.fields) |u_field| {
                    if (value == @field(UnionTagType, u_field.name)) {
                        return fiobjectify(@field(value, u_field.name));
                    }
                }
            } else {
                @compileError("Unable to fiobjectify untagged union '" ++ @typeName(T) ++ "'");
            }
        },
        .Struct => |S| {
            // create a new fio hashmap
            var m = fiobj_hash_new();
            // std.debug.print("new struct\n", .{});
            inline for (S.fields) |Field| {
                // don't include void fields
                if (Field.type == void) continue;

                // std.debug.print("    new field: {s}\n", .{Field.name});
                const fname = fiobj_str_new(util.toCharPtr(Field.name), Field.name.len);
                // std.debug.print("    fiobj name : {any}\n", .{fname});
                const v = @field(value, Field.name);
                // std.debug.print("    value: {any}\n", .{v});
                const fvalue = fiobjectify(v);
                // std.debug.print("    fiobj value: {any}\n", .{fvalue});
                _ = fiobj_hash_set(m, fname, fvalue);
                fiobj_free_wrapped(fname);
            }
            return m;
        },
        .ErrorSet => return fiobjectify(@as([]const u8, @errorName(value))),
        .Pointer => |ptr_info| switch (ptr_info.size) {
            .One => switch (@typeInfo(ptr_info.child)) {
                .Array => {
                    const Slice = []const std.meta.Elem(ptr_info.child);
                    return fiobjectify(@as(Slice, value));
                },
                else => {
                    // TODO: avoid loops?
                    return fiobjectify(value.*);
                },
            },
            // TODO: .Many when there is a sentinel (waiting for https://github.com/ziglang/zig/pull/3972)
            .Slice => {
                // std.debug.print("new slice\n", .{});
                if (ptr_info.child == u8 and std.unicode.utf8ValidateSlice(value)) {
                    return fiobj_str_new(util.toCharPtr(value), value.len);
                }

                var arr = fiobj_ary_new2(value.len);
                for (value) |x| {
                    const v = fiobjectify(x);
                    fiobj_ary_push(arr, v);
                }
                return arr;
            },
            else => @compileError("Unable to fiobjectify type '" ++ @typeName(T) ++ "'"),
        },
        .Array => return fiobjectify(&value),
        .Vector => |info| {
            const array: [info.len]info.child = value;
            return fiobjectify(&array);
        },
        else => @compileError("Unable to fiobjectify type '" ++ @typeName(T) ++ "'"),
    }
    unreachable;
}

pub fn MustacheFree(m: ?*Mustache) void {
    fiobj_mustache_free(m);
}
