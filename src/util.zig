const std = @import("std");
const fio = @import("fio.zig");
const zap = @import("zap.zig");

/// Used internally: convert a FIO object into its string representation.
/// note: since this is called from within request functions, we don't make
/// copies. Also, we return temp memory from fio. -> don't hold on to it outside
/// of a request function. FIO temp memory strings do not need to be freed.
pub fn fio2str(o: fio.FIOBJ) ?[]const u8 {
    if (o == 0) return null;
    const x: fio.fio_str_info_s = fio.fiobj_obj2cstr(o);
    if (x.data == 0)
        return null; // TODO: should we return an error? Actually, looking at fiobj_obj2cstr, this is unreachable
    return x.data[0..x.len];
}

/// A "string" type used internally that carries a flag whether its buffer needs
/// to be freed or not - and honors it in `deinit()`. That way, it's always
/// safe to call deinit().
/// For instance, slices taken directly from the zap.Request need not be freed.
/// But the ad-hoc created string representation of a float parameter must be
/// freed after use.
pub const FreeOrNot = struct {
    str: []const u8,
    freeme: bool,
    allocator: ?std.mem.Allocator = null,

    pub fn deinit(self: *const @This()) void {
        if (self.freeme) {
            self.allocator.?.free(self.str);
        }
    }
};

/// Used internally: convert a FIO object into its string representation.
/// Depending on the type of the object, a buffer will be created. Hence a
/// FreeOrNot type is used as the return type.
pub fn fio2strAllocOrNot(a: std.mem.Allocator, o: fio.FIOBJ, always_alloc: bool) !FreeOrNot {
    if (o == 0) return .{ .str = "null", .freeme = false };
    if (o == fio.FIOBJ_INVALID) return .{ .str = "invalid", .freeme = false };
    return switch (fio.fiobj_type(o)) {
        fio.FIOBJ_T_TRUE => .{ .str = "true", .freeme = false },
        fio.FIOBJ_T_FALSE => .{ .str = "false", .freeme = false },
        // according to fio2str above, the orelse should never happen
        fio.FIOBJ_T_NUMBER => .{ .str = try a.dupe(u8, fio2str(o) orelse "null"), .freeme = true, .allocator = a },
        fio.FIOBJ_T_FLOAT => .{ .str = try a.dupe(u8, fio2str(o) orelse "null"), .freeme = true, .allocator = a },
        // the string comes out of the request, so it is safe to not make a copy
        fio.FIOBJ_T_STRING => .{ .str = if (always_alloc) try a.dupe(u8, fio2str(o) orelse "") else fio2str(o) orelse "", .freeme = if (always_alloc) true else false, .allocator = a },
        else => .{ .str = "unknown_type", .freeme = false },
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
    options: std.json.StringifyOptions,
) ?[]const u8 {
    var fba = std.heap.FixedBufferAllocator.init(buffer);
    var string = std.ArrayList(u8).init(fba.allocator());
    if (std.json.stringify(value, options, string.writer())) {
        return string.items;
    } else |_| { // error
        return null;
    }
}
