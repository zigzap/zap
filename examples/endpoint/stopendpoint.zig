const std = @import("std");
const zap = @import("zap");

/// A simple endpoint listening on the /stop route that shuts down zap
/// the main thread usually continues at the instructions after the call to zap.start().
pub const Self = @This();

path: []const u8,

pub fn init(path: []const u8) Self {
    return .{
        .path = path,
    };
}

pub fn get(e: *Self, r: zap.Request) void {
    _ = e;
    _ = r;
    zap.stop();
}

pub fn post(_: *Self, _: zap.Request) void {}
pub fn put(_: *Self, _: zap.Request) void {}
pub fn delete(_: *Self, _: zap.Request) void {}
pub fn patch(_: *Self, _: zap.Request) void {}
pub fn options(_: *Self, _: zap.Request) void {}
