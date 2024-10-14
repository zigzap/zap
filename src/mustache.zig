const std = @import("std");
const fio = @import("fio.zig");
const util = @import("util.zig");

const Self = @This();

const struct_mustache_s = opaque {};
const mustache_s = struct_mustache_s;
const enum_mustache_error_en = c_uint;
const mustache_error_en = enum_mustache_error_en;

extern fn fiobj_mustache_new(args: MustacheLoadArgsFio) ?*mustache_s;
extern fn fiobj_mustache_build(mustache: ?*mustache_s, data: fio.FIOBJ) fio.FIOBJ;
extern fn fiobj_mustache_build2(dest: fio.FIOBJ, mustache: ?*mustache_s, data: fio.FIOBJ) fio.FIOBJ;
extern fn fiobj_mustache_free(mustache: ?*mustache_s) void;

/// Load arguments used when creating a new Mustache instance.
pub const LoadArgs = struct {
    /// Filename. This enables partial templates on filesystem.
    filename: ?[]const u8 = null,

    /// String data. Should be used if no filename is specified.
    data: ?[]const u8 = null,
};

/// Internal struct used for interfacing with fio.
const MustacheLoadArgsFio = extern struct {
    filename: [*c]const u8,
    filename_len: usize,
    data: [*c]const u8,
    data_len: usize,
    err: [*c]mustache_error_en,
};

/// Handle to the underlying fiobj mustache instance.
handle: *mustache_s,

pub const Error = error{
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

/// Create a new `Mustache` instance; `deinit()` should be called to free
/// the object after usage.
pub fn init(load_args: LoadArgs) Error!Self {
    var err: mustache_error_en = undefined;

    const args: MustacheLoadArgsFio = .{
        .filename = filn: {
            if (load_args.filename) |filn| break :filn filn.ptr else break :filn null;
        },
        .filename_len = filn_len: {
            if (load_args.filename) |filn| break :filn_len filn.len else break :filn_len 0;
        },
        .data = data: {
            if (load_args.data) |data| break :data data.ptr else break :data null;
        },
        .data_len = data_len: {
            if (load_args.data) |data| break :data_len data.len else break :data_len 0;
        },
        .err = &err,
    };

    const ret = fiobj_mustache_new(args);
    switch (err) {
        0 => return Self{
            .handle = ret.?,
        },
        1 => return Error.MUSTACHE_ERR_TOO_DEEP,
        2 => return Error.MUSTACHE_ERR_CLOSURE_MISMATCH,
        3 => return Error.MUSTACHE_ERR_FILE_NOT_FOUND,
        4 => return Error.MUSTACHE_ERR_FILE_TOO_BIG,
        5 => return Error.MUSTACHE_ERR_FILE_NAME_TOO_LONG,
        6 => return Error.MUSTACHE_ERR_FILE_NAME_TOO_SHORT,
        7 => return Error.MUSTACHE_ERR_EMPTY_TEMPLATE,
        8 => return Error.MUSTACHE_ERR_DELIMITER_TOO_LONG,
        9 => return Error.MUSTACHE_ERR_NAME_TOO_LONG,
        10 => return Error.MUSTACHE_ERR_UNKNOWN,
        11 => return Error.MUSTACHE_ERR_USER_ERROR,
        else => return Error.MUSTACHE_ERR_UNKNOWN,
    }
    unreachable;
}

/// Convenience function to create a new `Mustache` instance with in-memory data loaded;
/// `deinit()` should be called to free the object after usage..
pub fn fromData(data: []const u8) Error!Self {
    return Self.init(.{ .data = data });
}

/// Convenience function to create a new `Mustache` instance with file-based data loaded;
/// `deinit()` should be called to free the object after usage..
pub fn fromFile(filename: []const u8) Error!Self {
    return Self.init(.{ .filename = filename });
}

/// Free the data backing a `Mustache` instance.
pub fn deinit(self: *Self) void {
    fiobj_mustache_free(self.handle);
}

// TODO: implement these - fiobj_mustache.c
// pub extern fn fiobj_mustache_build(mustache: ?*mustache_s, data: FIOBJ) FIOBJ;
// pub extern fn fiobj_mustache_build2(dest: FIOBJ, mustache: ?*mustache_s, data: FIOBJ) FIOBJ;

/// The result from calling `build`.
pub const BuildResult = struct {
    fiobj_result: fio.FIOBJ = 0,

    /// Holds the context converted into a fiobj.
    /// This is used in `build`.
    fiobj_context: fio.FIOBJ = 0,

    /// Free the data backing a `MustacheBuildResult` instance.
    pub fn deinit(m: *const BuildResult) void {
        fio.fiobj_free_wrapped(m.fiobj_result);
        fio.fiobj_free_wrapped(m.fiobj_context);
    }

    /// Retrieve a string representation of the built template.
    pub fn str(m: *const BuildResult) ?[]const u8 {
        return util.fio2str(m.fiobj_result);
    }
};

/// Build the Mustache template; `deinit()` should be called on the build
/// result to free the data.
// TODO: The build may be slow because it needs to convert zig types to facil.io
// types. However, this needs to be investigated into.
// See `fiobjectify` for more information.
pub fn build(self: *Self, data: anytype) BuildResult {
    const T = @TypeOf(data);
    if (@typeInfo(T) != .@"struct") {
        @compileError("No struct: '" ++ @typeName(T) ++ "'");
    }

    var result: BuildResult = .{};

    result.fiobj_context = fiobjectify(data);
    result.fiobj_result = fiobj_mustache_build(self.handle, result.fiobj_context);
    return result;
}

/// Internal function used to convert zig types to facil.io types.
/// Used when providing the context to `fiobj_mustache_build`.
fn fiobjectify(
    value: anytype,
) fio.FIOBJ {
    const T = @TypeOf(value);
    switch (@typeInfo(T)) {
        .float, .comptime_float => {
            return fio.fiobj_float_new(value);
        },
        .int, .comptime_int => {
            return fio.fiobj_num_new_bignum(value);
        },
        .bool => {
            return if (value) fio.fiobj_true() else fio.fiobj_false();
        },
        .null => {
            return 0;
        },
        .optional => {
            if (value) |payload| {
                return fiobjectify(payload);
            } else {
                return fiobjectify(null);
            }
        },
        .@"enum" => {
            return fio.fiobj_num_new_bignum(@intFromEnum(value));
        },
        .@"union" => {
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
        .@"struct" => |S| {
            // create a new fio hashmap
            const m = fio.fiobj_hash_new();
            // std.debug.print("new struct\n", .{});
            inline for (S.fields) |Field| {
                // don't include void fields
                if (Field.type == void) continue;

                // std.debug.print("    new field: {s}\n", .{Field.name});
                const fname = fio.fiobj_str_new(util.toCharPtr(Field.name), Field.name.len);
                // std.debug.print("    fiobj name : {any}\n", .{fname});
                const v = @field(value, Field.name);
                // std.debug.print("    value: {any}\n", .{v});
                const fvalue = fiobjectify(v);
                // std.debug.print("    fiobj value: {any}\n", .{fvalue});
                _ = fio.fiobj_hash_set(m, fname, fvalue);
                fio.fiobj_free_wrapped(fname);
            }
            return m;
        },
        .error_set => return fiobjectify(@as([]const u8, @errorName(value))),
        .pointer => |ptr_info| switch (ptr_info.size) {
            .One => switch (@typeInfo(ptr_info.child)) {
                .array => {
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
                    return fio.fiobj_str_new(util.toCharPtr(value), value.len);
                }

                const arr = fio.fiobj_ary_new2(value.len);
                for (value) |x| {
                    const v = fiobjectify(x);
                    fio.fiobj_ary_push(arr, v);
                }
                return arr;
            },
            else => @compileError("Unable to fiobjectify type '" ++ @typeName(T) ++ "'"),
        },
        .array => return fiobjectify(&value),
        .vector => |info| {
            const array: [info.len]info.child = value;
            return fiobjectify(&array);
        },
        else => @compileError("Unable to fiobjectify type '" ++ @typeName(T) ++ "'"),
    }
    unreachable;
}
