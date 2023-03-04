const std = @import("std");

debugOn: bool,

const Self = @This();

pub fn init(comptime debug: bool) Self {
    return .{
        .debugOn = debug,
    };
}

pub fn log(self: *const Self, comptime fmt: []const u8, args: anytype) void {
    if (self.debugOn) {
        std.debug.print("[zap] - " ++ fmt, args);
    }
}
// pub fn getDebugLogger(comptime debug: bool) type {
//     return struct {
//         pub fn log(comptime fmt: []const u8, args: anytype) void {
//             if (debug) {
//                 std.debug.print("[zap] - " ++ fmt, args);
//             }
//         }
//     };
// }
