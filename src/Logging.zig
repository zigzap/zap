//! Access to facil.io's logging facilities
//!
//! Zap uses Zig's standard logging facilities, which you can control like this:
//!
//! ```zig
//! pub const std_options: std.Options = .{
//!     // general log level
//!     .log_level = .info,
//!     .log_scope_levels = &[_]std.log.ScopeLevel{
//!         // log level specific to zap
//!         .{ .scope = .zap, .level = .debug },
//!     },
//! };
//! ```
const Logging = @This();

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

/// Error reporting of last resort
pub fn on_uncaught_error(comptime domain: []const u8, err: anyerror) void {
    const std = @import("std");
    const log = std.log.scoped(.zap);
    log.err(domain ++ " : {}", .{err});
}
