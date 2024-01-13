const std = @import("std");
const zap = @import("zap.zig");

const Allocator = std.mem.Allocator;
const RouterError = error{
    AlreadyExists,
    EmptyPath,
};

// inline closure for RequestFn with self argument
pub inline fn RequestHandler(self: anytype, func: *const fn (@TypeOf(self), zap.Request) void) *const fn (zap.Request) void {
    return (opaque {
        var hidden_self: @TypeOf(self) = undefined;
        var hidden_func: *const fn (@TypeOf(self), zap.Request) void = undefined;
        pub fn init(h_self: @TypeOf(self), h_func: *const fn (@TypeOf(self), zap.Request) void) *const @TypeOf(run) {
            hidden_self = h_self;
            hidden_func = h_func;
            return &run;
        }

        fn run(req: zap.Request) void {
            hidden_func(hidden_self, req);
        }
    }).init(self, func);
}

pub const RouterOptions = struct {
    not_found: ?zap.HttpRequestFn = null,
};

pub const Router = struct {
    const Self = @This();

    routes: std.StringHashMap(zap.HttpRequestFn),
    not_found: ?zap.HttpRequestFn,

    pub fn init(allocator: Allocator, options: RouterOptions) Self {
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
        var route = self.routes.get(r.path.?);

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
};
