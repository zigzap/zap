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

pub extern const fio_log_level_none: c_int;
pub extern const fio_log_level_fatal: c_int;
pub extern const fio_log_level_error: c_int;
pub extern const fio_log_level_warning: c_int;
pub extern const fio_log_level_info: c_int;
pub extern const fio_log_level_debug: c_int;
pub extern fn fio_set_log_level(level: c_int) void;
pub extern fn fio_get_log_level() c_int;
pub extern fn fio_log_print(level: c_int, msg: [*c]const u8) void;
pub extern fn fio_log_info(msg: [*c]const u8) void;
pub extern fn fio_log_warning(msg: [*c]const u8) void;
pub extern fn fio_log_error(msg: [*c]const u8) void;
pub extern fn fio_log_fatal(msg: [*c]const u8) void;
pub extern fn fio_log_debug(msg: [*c]const u8) void;

// pub fn getDebugLogger(comptime debug: bool) type {
//     return struct {
//         pub fn log(comptime fmt: []const u8, args: anytype) void {
//             if (debug) {
//                 std.debug.print("[zap] - " ++ fmt, args);
//             }
//         }
//     };
// }
