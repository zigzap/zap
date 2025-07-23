const std = @import("std");
const zap = @import("zap");

/// A simple endpoint listening on the /error route that causes an error on GET
/// requests, which gets logged to the response (=browser) by default
pub const ErrorEndpoint = @This();

path: []const u8 = "/error",
error_strategy: zap.Endpoint.ErrorStrategy = .log_to_response,

pub fn get(_: *ErrorEndpoint, _: zap.Request) !void {
    // error_strategy is set to .log_to_response
    // --> this error will be shown in the browser, with a nice error trace
    return error.@"Oh-no!";
}
