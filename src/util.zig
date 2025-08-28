const std = @import("std");
const fio = @import("fio.zig");
const zap = @import("zap.zig");

/// Used internally: convert a FIO object into its string representation.
/// note: since this is called from within request functions, we don't make
/// copies. Also, we return temp memory from fio. -> don't hold on to it outside
/// of a request function. FIO temp memory strings do not need to be freed.
///
/// IMPORTANT!!! The "temp" memory can refer to a shared buffer that subsequent
///              calls to this function will **overwrite**!!!
pub fn fio2str(o: fio.FIOBJ) ?[]const u8 {
    if (o == 0) return null;
    const x: fio.fio_str_info_s = fio.fiobj_obj2cstr(o);
    if (x.data == 0)
        return null; // TODO: should we return an error? Actually, looking at fiobj_obj2cstr, this is unreachable
    return x.data[0..x.len];
}

pub fn fiobj_type_as_string(o: zap.fio.FIOBJ) []const u8 {
    const value_type = switch (zap.fio.fiobj_type(o)) {
        zap.fio.FIOBJ_T_NULL => "null",
        zap.fio.FIOBJ_T_TRUE => "true",
        zap.fio.FIOBJ_T_FALSE => "false",
        zap.fio.FIOBJ_T_NUMBER => "number",
        zap.fio.FIOBJ_T_FLOAT => "float",
        zap.fio.FIOBJ_T_STRING => "string",
        zap.fio.FIOBJ_T_ARRAY => "array",
        zap.fio.FIOBJ_T_HASH => "hash",
        zap.fio.FIOBJ_T_DATA => "data",
        zap.fio.FIOBJ_T_UNKNOWN => "unknown",
        else => "unreachable",
    };
    return value_type;
}

/// Used internally: convert a FIO object into its string representation.
/// This always allocates, so choose your allocator wisely.
/// Let's never use that
pub fn fio2strAlloc(a: std.mem.Allocator, o: fio.FIOBJ) ![]const u8 {
    if (o == 0) return try a.dupe(u8, "null");
    if (o == fio.FIOBJ_INVALID) return try a.dupe(u8, "invalid");
    return switch (fio.fiobj_type(o)) {
        fio.FIOBJ_T_TRUE => try a.dupe(u8, "true"),
        fio.FIOBJ_T_FALSE => try a.dupe(u8, "false"),
        // according to fio2str above, the orelse should never happen
        fio.FIOBJ_T_NUMBER => try a.dupe(u8, fio2str(o) orelse "null"),
        fio.FIOBJ_T_FLOAT => try a.dupe(u8, fio2str(o) orelse "null"),
        // if the string comes out of the request, it is safe to not make a copy
        fio.FIOBJ_T_STRING => try a.dupe(u8, fio2str(o) orelse ""),
        else => try a.dupe(u8, "unknown_type"),
    };
}

/// Used internally: convert a zig slice into a FIO string.
pub fn str2fio(s: []const u8) fio.fio_str_info_s {
    return .{
        .data = toCharPtr(s),
        .len = s.len,
        .capa = s.len,
    };
}

/// Used internally: convert a zig slice into a C pointer
pub fn toCharPtr(s: []const u8) [*c]u8 {
    return @as([*c]u8, @ptrFromInt(@intFromPtr(s.ptr)));
}

//
// JSON helpers
//

/// Concenience: format an arbitrary value into a JSON string buffer.
/// Provide your own buf; this function is NOT mutex-protected!
pub fn stringifyBuf(
    buffer: []u8,
    value: anytype,
    options: std.json.Stringify.Options,
) ![]const u8 {
    var w: std.io.Writer = .fixed(buffer);
    if (std.json.Stringify.value(value, options, &w)) {
        return w.buffered();
    } else |err| { // error
        return err;
    }
}
