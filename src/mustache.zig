// supporting code to make using facilio's mustache stuff
// (see http://facil.io/0.7.x/fiobj_mustache)
// easier / possible / more zig-like

// const C = @cImport({
//     @cInclude("mustache_parser.h");
//     @cInclude("fiobj_mustache.h");
// });

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

// implement these: fiobj_mustache.c
// pub extern fn fiobj_mustache_new(args: mustache_load_args_s) ?*mustache_s;
// pub extern fn fiobj_mustache_free(mustache: ?*mustache_s) void;
// pub extern fn fiobj_mustache_build(mustache: ?*mustache_s, data: FIOBJ) FIOBJ;
// pub extern fn fiobj_mustache_build2(dest: FIOBJ, mustache: ?*mustache_s, data: FIOBJ) FIOBJ;

pub fn MustacheNew(data: []const u8) MustacheError!*Mustache {
    // pub fn MustacheNew(data: []const u8) !*Mustache {
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
pub extern fn fiobj_free(arg_o: FIOBJ) callconv(.C) void;
pub extern fn fiobj_hash_set(hash: FIOBJ, key: FIOBJ, obj: FIOBJ) c_int;
pub extern fn fiobj_ary_push(ary: FIOBJ, obj: FIOBJ) void;
pub extern fn fiobj_float_new(num: f64) FIOBJ;
pub extern fn fiobj_num_new_bignum(num: isize) FIOBJ;

// pub extern fn fiobj_num_new(num: isize) callconv(.C) FIOBJ;

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

// this build is slow because it needs to translate to a FIOBJ data
// object FIOBJ_T_HASH
pub fn MustacheBuild(mustache: *Mustache, data: anytype) ?[]const u8 {

    // FIOBJ data = fiobj_hash_new();
    // FIOBJ key = fiobj_str_new("users", 5);
    // FIOBJ ary = fiobj_ary_new2(4);
    // fiobj_hash_set(data, key, ary);
    // fiobj_free(key);
    // for (int i = 0; i < 4; ++i) {
    //   FIOBJ id = fiobj_str_buf(4);
    //   fiobj_str_write_i(id, i);
    //   FIOBJ name = fiobj_str_buf(4);
    //   fiobj_str_write(name, "User ", 5);
    //   fiobj_str_write_i(name, i);
    //   FIOBJ usr = fiobj_hash_new2(2);
    //   key = fiobj_str_new("id", 2);
    //   fiobj_hash_set(usr, key, id);
    //   fiobj_free(key);
    //   key = fiobj_str_new("name", 4);
    //   fiobj_hash_set(usr, key, name);
    //   fiobj_free(key);
    //   fiobj_ary_push(ary, usr);
    // }
    // key = fiobj_str_new("nested", 6);
    // ary = fiobj_hash_new2(2);
    // fiobj_hash_set(data, key, ary);
    // fiobj_free(key);
    // key = fiobj_str_new("item", 4);
    // fiobj_hash_set(ary, key, fiobj_str_new("dot notation success", 20));
    // fiobj_free(key);

    const T = @TypeOf(data);
    if (@typeInfo(T) != .Struct) {
        @compileError("No struct: '" ++ @typeName(T) ++ "'");
    }

    // std.debug.print("data: ", .{});
    const fiobj_data = fiobjectify(data);
    // std.debug.print("{any}\n", .{fiobj_data});

    // TODO: fiobj_free everything
    var ret = fiobj_mustache_build(mustache, fiobj_data);
    return util.fio2str(ret);
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

// pub extern fn fiobj_mustache_free(mustache: ?*mustache_s) void;
pub fn MustacheFree(m: ?*Mustache) void {
    fiobj_mustache_free(m);
}
