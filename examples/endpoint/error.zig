const std = @import("std");
const zap = @import("zap");

/// A simple endpoint listening on the /error route that causes an error on GET requests, which gets logged to the response (=browser) by default
pub const ErrorEndpoint = @This();

path: []const u8 = "/error",
error_strategy: zap.Endpoint.ErrorStrategy = .log_to_response,

pub fn get(e: *ErrorEndpoint, r: zap.Request) !void {
    _ = e;
    _ = r;
    return error.@"Oh-no!";
}

pub fn post(_: *ErrorEndpoint, _: zap.Request) !void {}
// pub fn post(_: *ErrorEndpoint, _: zap.Request) !void {}

pub fn put(_: *ErrorEndpoint, _: zap.Request) !void {}
pub fn delete(_: *ErrorEndpoint, _: zap.Request) !void {}
pub fn patch(_: *ErrorEndpoint, _: zap.Request) !void {}
pub fn options(_: *ErrorEndpoint, _: zap.Request) !void {}
