const std = @import("std");
const zap = @import("zap");

/// A simple endpoint listening on the /stop route that shuts down zap
/// the main thread usually continues at the instructions after the call to zap.start().
pub const Self = @This();

ep: zap.Endpoint = undefined,

pub fn init(
    path: []const u8,
) Self {
    return .{
        .ep = zap.Endpoint.init(.{
            .path = path,
            .get = get,
        }),
    };
}

pub fn endpoint(self: *Self) *zap.Endpoint {
    return &self.ep;
}

fn get(e: *zap.Endpoint, r: zap.Request) void {
    _ = e;
    _ = r;
    zap.stop();
}
