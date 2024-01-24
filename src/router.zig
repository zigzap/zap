const std = @import("std");
const zap = @import("zap.zig");

const Allocator = std.mem.Allocator;
const RouterError = error{
    AlreadyExists,
    EmptyPath,
};

const Self = @This();

pub const Options = struct {
    not_found: ?zap.HttpRequestFn = null,
};

routes: std.StringHashMap(zap.HttpRequestFn),
not_found: ?zap.HttpRequestFn,

pub fn init(allocator: Allocator, options: Options) Self {
    return .{
        .routes = std.StringHashMap(zap.HttpRequestFn).init(allocator),

        .not_found = options.not_found,
    };
}

pub fn deinit(self: *Self) void {
    self.routes.deinit();
}

pub fn handle_func(self: *Self, path: []const u8, h: zap.HttpRequestFn) !void {
    if (path.len == 0) {
        return RouterError.EmptyPath;
    }

    const route = self.routes.get(path);

    if (route != null) {
        return RouterError.AlreadyExists;
    }

    try self.routes.put(path, h);
}

pub fn serve(self: *Self, r: zap.Request) void {
    const path = if (r.path) |p| p else "/";

    const route = self.routes.get(path);

    if (route) |handler| {
        handler(r);
    } else if (self.not_found) |handler| {
        // not found handler
        handler(r);
    } else {
        // default 404 output
        r.setStatus(.not_found);
        r.sendBody("404 Not Found") catch return;
    }
}
