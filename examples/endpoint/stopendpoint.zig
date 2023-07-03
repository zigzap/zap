const std = @import("std");
const zap = @import("zap");

/// A simple endpoint listening on the /stop route that shuts down zap
/// the main thread usually continues at the instructions after the call to zap.start().
pub const Self = @This();

endpoint: zap.SimpleEndpoint = undefined,

pub fn init(
    path: []const u8,
) Self {
    return .{
        .endpoint = zap.SimpleEndpoint.init(.{
            .path = path,
            .get = get,
        }),
    };
}

pub fn getEndpoint(self: *Self) *zap.SimpleEndpoint {
    return &self.endpoint;
}

fn get(e: *zap.SimpleEndpoint, r: zap.SimpleRequest) void {
    _ = e;
    _ = r;
    zap.stop();
}
