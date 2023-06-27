const std = @import("std");
const fio = @import("fio.zig");

/// note: since this is called from within request functions, we don't make
/// copies. Also, we return temp memory from fio. -> don't hold on to it outside
/// of a request function
pub fn fio2str(o: fio.FIOBJ) ?[]const u8 {
    if (o == 0) return null;
    const x: fio.fio_str_info_s = fio.fiobj_obj2cstr(o);
    if (x.data == 0)
        return null; // TODO: should we return an error? Actually, looking at fiobj_obj2cstr, this is unreachable
    return std.mem.span(x.data);
}

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

pub fn fio2strAllocOrNot(o: fio.FIOBJ, a: std.mem.Allocator, always_alloc: bool) !FreeOrNot {
    if (o == 0) return .{ .str = "null", .freeme = false };
    if (o == fio.FIOBJ_INVALID) return .{ .str = "null", .freeme = false };
    return switch (fio.fiobj_type(o)) {
        fio.FIOBJ_T_TRUE => .{ .str = "true", .freeme = false },
        fio.FIOBJ_T_FALSE => .{ .str = "false", .freeme = false },
        // according to fio2str above, the orelse should never happen
        fio.FIOBJ_T_NUMBER => .{ .str = try a.dupe(u8, fio2str(o) orelse "null"), .freeme = true, .allocator = a },
        fio.FIOBJ_T_FLOAT => .{ .str = try a.dupe(u8, fio2str(o) orelse "null"), .freeme = true, .allocator = a },
        // the string comes out of the request, so it is safe to not make a copy
        fio.FIOBJ_T_STRING => .{ .str = if (always_alloc) try a.dupe(u8, fio2str(o) orelse "") else fio2str(o) orelse "", .freeme = if (always_alloc) true else false, .allocator = a },
        else => .{ .str = "null", .freeme = false },
    };
}
pub fn str2fio(s: []const u8) fio.fio_str_info_s {
    return .{
        .data = toCharPtr(s),
        .len = s.len,
        .capa = s.len,
    };
}

pub fn toCharPtr(s: []const u8) [*c]u8 {
    return @as([*c]u8, @ptrFromInt(@intFromPtr(s.ptr)));
}

//
// JSON helpers
//

/// provide your own buf, NOT mutex-protected!
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

// deprecated:

// 1MB JSON buffer
// var jsonbuf: [1024 * 1024]u8 = undefined;
// var mutex: std.Thread.Mutex = .{};

// use default 1MB buffer, mutex-protected
// pub fn stringify(
//     value: anytype,
//     options: std.json.StringifyOptions,
// ) ?[]const u8 {
//     mutex.lock();
//     defer mutex.unlock();
//     var fba = std.heap.FixedBufferAllocator.init(&jsonbuf);
//     var string = std.ArrayList(u8).init(fba.allocator());
//     if (std.json.stringify(value, options, string.writer())) {
//         return string.items;
//     } else |_| { // error
//         return null;
//     }
// }

// use default 1MB buffer, mutex-protected
// pub fn stringifyArrayList(
//     comptime T: anytype,
//     list: *std.ArrayList(T),
//     options: std.json.StringifyOptions,
// ) !?[]const u8 {
//     mutex.lock();
//     defer mutex.unlock();
//     var fba = std.heap.FixedBufferAllocator.init(&jsonbuf);
//     var string = std.ArrayList(u8).init(fba.allocator());
//     var writer = string.writer();
//     try writer.writeByte('[');
//     var first: bool = true;
//     for (list.items) |user| {
//         if (!first) try writer.writeByte(',');
//         first = false;
//         try std.json.stringify(user, options, string.writer());
//     }
//     try writer.writeByte(']');
//     return string.items;
// }
