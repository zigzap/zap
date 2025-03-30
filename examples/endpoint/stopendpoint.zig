const std = @import("std");
const zap = @import("zap");

/// A simple endpoint listening on the /stop route that shuts down zap
/// the main thread usually continues at the instructions after the call to zap.start().
pub const StopEndpoint = @This();

path: []const u8,
error_strategy: zap.Endpoint.ErrorStrategy = .log_to_response,

pub fn init(path: []const u8) StopEndpoint {
    return .{
        .path = path,
    };
}

pub fn get(e: *StopEndpoint, r: zap.Request) anyerror!void {
    _ = e;
    _ = r;
    zap.stop();
}

pub fn post(_: *StopEndpoint, _: zap.Request) anyerror!void {}
pub fn put(_: *StopEndpoint, _: zap.Request) anyerror!void {}
pub fn delete(_: *StopEndpoint, _: zap.Request) anyerror!void {}
pub fn patch(_: *StopEndpoint, _: zap.Request) anyerror!void {}
pub fn options(_: *StopEndpoint, _: zap.Request) anyerror!void {}
