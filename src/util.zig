const std = @import("std");

//
// JSON helpers
//

// 1MB JSON buffer
var jsonbuf: [1024 * 1024]u8 = undefined;
var mutex: std.Thread.Mutex = .{};

/// use default 1MB buffer, mutex-protected
pub fn stringify(
    value: anytype,
    options: std.json.StringifyOptions,
) ?[]const u8 {
    mutex.lock();
    defer mutex.unlock();
    var fba = std.heap.FixedBufferAllocator.init(&jsonbuf);
    var string = std.ArrayList(u8).init(fba.allocator());
    if (std.json.stringify(value, options, string.writer())) {
        return string.items;
    } else |_| { // error
        return null;
    }
}

/// use default 1MB buffer, mutex-protected
pub fn stringifyArrayList(
    comptime T: anytype,
    list: *std.ArrayList(T),
    options: std.json.StringifyOptions,
) !?[]const u8 {
    mutex.lock();
    defer mutex.unlock();
    var fba = std.heap.FixedBufferAllocator.init(&jsonbuf);
    var string = std.ArrayList(u8).init(fba.allocator());
    var writer = string.writer();
    try writer.writeByte('[');
    var first: bool = true;
    for (list.items) |user| {
        if (!first) try writer.writeByte(',');
        first = false;
        try std.json.stringify(user, options, string.writer());
    }
    try writer.writeByte(']');
    return string.items;
}

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
