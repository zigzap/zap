const std = @import("std");
const zap = @import("zap.zig");

const Allocator = std.mem.Allocator;

/// Errors returnable by init()
const RouterError = error{
    AlreadyExists,
    EmptyPath,
};

const Self = @This();

/// This is a singleton
var _instance: *Self = undefined;

/// Options to pass to init()
pub const Options = struct {
    /// an optional zap request function for 404 not found case
    not_found: ?zap.HttpRequestFn = null,
};

const CallbackTag = enum { bound, unbound };
const BoundHandler = *fn (*const anyopaque, zap.Request) void;
const Callback = union(CallbackTag) {
    bound: struct { instance: usize, handler: usize },
    unbound: zap.HttpRequestFn,
};

routes: std.StringHashMap(Callback),
not_found: ?zap.HttpRequestFn,

/// Create a new Router
pub fn init(allocator: Allocator, options: Options) Self {
    return .{
        .routes = std.StringHashMap(Callback).init(allocator),

        .not_found = options.not_found,
    };
}

/// Deinit the router
pub fn deinit(self: *Self) void {
    self.routes.deinit();
}

/// Call this to add a route with an unbound handler: a handler that is not member of a struct.
pub fn handle_func_unbound(self: *Self, path: []const u8, h: zap.HttpRequestFn) !void {
    if (path.len == 0) {
        return RouterError.EmptyPath;
    }

    if (self.routes.contains(path)) {
        return RouterError.AlreadyExists;
    }

    try self.routes.put(path, Callback{ .unbound = h });
}

/// Call this to add a route with a handler that is bound to an instance of a struct.
/// Example:
///
/// ```zig
/// const HandlerType = struct {
///     pub fn getA(self: *HandlerType, r: zap.Request) void {
///         _ = self;
///         r.sendBody("hello\n\n") catch return;
///     }
/// }
/// var handler_instance = HandlerType{};
///
/// my_router.handle_func("/getA", &handler_instance, HandlerType.getA);
/// ```
pub fn handle_func(self: *Self, path: []const u8, instance: *anyopaque, handler: anytype) !void {
    // TODO: assert type of instance has handler

    // Introspection checks on handler type
    comptime {
        const hand_info = @typeInfo(@TypeOf(handler));

        // Need to check:
        // 1) handler is function pointer
        const f = blk: {
            if (hand_info == .Pointer) {
                const inner = @typeInfo(hand_info.Pointer.child);
                if (inner == .Fn) {
                    break :blk inner.Fn;
                }
            }
            @compileError("Expected handler to be a function pointer. Found " ++
                @typeName(@TypeOf(handler)));
        };

        // 2) snd arg is zap.Request
        if (f.params.len != 2) {
            @compileError("Expected handler to have two paramters");
        }
        const arg_type = f.params[1].type.?;
        if (arg_type != zap.Request) {
            @compileError("Expected handler's second argument to be of type zap.Request. Found " ++
                @typeName(arg_type));
        }

        // 3) handler returns void
        const ret_info = @typeInfo(f.return_type.?);
        if (ret_info != .Void) {
            @compileError("Expected handler's return type to be void. Found " ++
                @typeName(f.return_type.?));
        }
    }

    if (path.len == 0) {
        return RouterError.EmptyPath;
    }

    if (self.routes.contains(path)) {
        return RouterError.AlreadyExists;
    }

    try self.routes.put(path, Callback{ .bound = .{
        .instance = @intFromPtr(instance),
        .handler = @intFromPtr(handler),
    } });
}

/// Get the zap request handler function needed for a listener
pub fn on_request_handler(self: *Self) zap.HttpRequestFn {
    _instance = self;
    return zap_on_request;
}

fn zap_on_request(r: zap.Request) void {
    return serve(_instance, r);
}

fn serve(self: *Self, r: zap.Request) void {
    const path = r.path orelse "/";

    if (self.routes.get(path)) |routeInfo| {
        switch (routeInfo) {
            .bound => |b| @call(.auto, @as(BoundHandler, @ptrFromInt(b.handler)), .{ @as(*anyopaque, @ptrFromInt(b.instance)), r }),
            .unbound => |h| h(r),
        }
    } else if (self.not_found) |handler| {
        // not found handler
        handler(r);
    } else {
        // default 404 output
        r.setStatus(.not_found);
        r.sendBody("404 Not Found") catch return;
    }
}
