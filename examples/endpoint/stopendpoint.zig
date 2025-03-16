const std = @import("std");
const zap = @import("zap");

/// A simple endpoint listening on the /stop route that shuts down zap
/// the main thread usually continues at the instructions after the call to zap.start().
pub const Self = @This();

path: []const u8,
error_strategy: zap.Endpoint.ErrorStrategy = .log_to_response,

pub fn init(path: []const u8) Self {
    return .{
        .path = path,
    };
}

pub fn get(e: *Self, r: zap.Request) anyerror!void {
    _ = e;
    _ = r;
    zap.stop();
}

pub fn post(_: *Self, _: zap.Request) anyerror!void {}
pub fn put(_: *Self, _: zap.Request) anyerror!void {}
pub fn delete(_: *Self, _: zap.Request) anyerror!void {}
pub fn patch(_: *Self, _: zap.Request) anyerror!void {}
pub fn options(_: *Self, _: zap.Request) anyerror!void {}
