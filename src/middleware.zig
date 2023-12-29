const std = @import("std");
const zap = @import("zap.zig");

pub const ContextDescriptor = struct {
    name: []const u8,
    type: type,
};

/// Provide a tuple of structs of type like ContextDescriptor
/// a name starting with '?', such as "?user" will be treated as Optional with default `null`.
pub fn MixContexts(comptime context_tuple: anytype) type {
    var fields: [context_tuple.len]std.builtin.Type.StructField = undefined;
    for (context_tuple, 0..) |t, i| {
        var fieldType: type = t.type;
        var fieldName: []const u8 = t.name[0..];
        var isOptional: bool = false;
        if (fieldName[0] == '?') {
            fieldType = @Type(.{ .Optional = .{ .child = fieldType } });
            fieldName = fieldName[1..];
            isOptional = true;
        }
        fields[i] = .{
            .name = fieldName,
            .type = fieldType,
            .default_value = if (isOptional) &@as(fieldType, null) else null,
            .is_comptime = false,
            .alignment = 0,
        };
    }
    return @Type(.{
        .Struct = .{
            .layout = .Auto,
            .fields = fields[0..],
            .decls = &[_]std.builtin.Type.Declaration{},
            .is_tuple = false,
        },
    });
}

/// Your middleware components need to contain a handler
pub fn Handler(comptime ContextType: anytype) type {
    return struct {
        other_handler: ?*Self = null,
        on_request: ?RequestFn = null,

        // will be set
        allocator: ?std.mem.Allocator = null,

        pub const RequestFn = *const fn (*Self, zap.SimpleRequest, *ContextType) bool;
        const Self = @This();

        pub fn init(on_request: RequestFn, other: ?*Self) Self {
            return .{
                .other_handler = other,
                .on_request = on_request,
            };
        }

        // example for handling request
        // which you can use in your components, e.g.:
        // return self.handler.handleOther(r, context);
        pub fn handleOther(self: *Self, r: zap.SimpleRequest, context: *ContextType) bool {
            // in structs embedding a handler, we'd @fieldParentPtr the first
            // param to get to the real self

            // First, do our pre-other stuff
            // ..

            // then call the wrapped thing
            var other_handler_finished = false;
            if (self.other_handler) |other_handler| {
                if (other_handler.on_request) |on_request| {
                    other_handler_finished = on_request(other_handler, r, context);
                }
            }

            // now do our post stuff
            return other_handler_finished;
        }
    };
}

/// A convenience handler for artibrary zap.SimpleEndpoint
pub fn EndpointHandler(comptime HandlerType: anytype, comptime ContextType: anytype) type {
    return struct {
        handler: HandlerType,
        endpoint: *zap.SimpleEndpoint,
        breakOnFinish: bool,

        const Self = @This();

        pub fn init(endpoint: *zap.SimpleEndpoint, other: ?*HandlerType, breakOnFinish: bool) Self {
            return .{
                .handler = HandlerType.init(onRequest, other),
                .endpoint = endpoint,
                .breakOnFinish = breakOnFinish,
            };
        }

        // we need the handler as a common interface to chain stuff
        pub fn getHandler(self: *Self) *HandlerType {
            return &self.handler;
        }

        pub fn onRequest(handler: *HandlerType, r: zap.SimpleRequest, context: *ContextType) bool {
            var self = @fieldParentPtr(Self, "handler", handler);
            r.setUserContext(context);
            self.endpoint.onRequest(r);

            // if the request was handled by the endpoint, we may break the chain here
            if (r.isFinished() and self.breakOnFinish) {
                return true;
            }
            return self.handler.handleOther(r, context);
        }
    };
}

pub const Error = error{
    InitOnRequestIsNotNull,
};

pub const RequestAllocatorFn = *const fn () std.mem.Allocator;

pub fn Listener(comptime ContextType: anytype) type {
    return struct {
        listener: zap.SimpleHttpListener = undefined,
        settings: zap.SimpleHttpListenerSettings,

        // static initial handler
        var handler: ?*Handler(ContextType) = undefined;
        // static allocator getter
        var requestAllocator: ?RequestAllocatorFn = null;

        const Self = @This();

        /// initialize the middleware handler
        /// the passed in settings must have on_request set to null
        pub fn init(settings: zap.SimpleHttpListenerSettings, initial_handler: *Handler(ContextType), request_alloc: ?RequestAllocatorFn) Error!Self {
            // override on_request with ourselves
            if (settings.on_request != null) {
                return Error.InitOnRequestIsNotNull;
            }
            requestAllocator = request_alloc;
            std.debug.assert(requestAllocator != null);

            var ret: Self = .{
                .settings = settings,
            };
            ret.settings.on_request = onRequest;
            ret.listener = zap.SimpleHttpListener.init(ret.settings);
            handler = initial_handler;
            return ret;
        }

        pub fn listen(self: *Self) !void {
            try self.listener.listen();
        }

        // this is just a reference implementation
        // but it's actually used obviously. Create your own listener if you
        // want different behavior.
        // Didn't want to make this a callback
        pub fn onRequest(r: zap.SimpleRequest) void {
            // we are the 1st handler in the chain, so we create a context
            var context: ContextType = .{};

            // handlers might need an allocator
            // we CAN provide an allocator getter
            var allocator: ?std.mem.Allocator = null;
            if (requestAllocator) |foo| {
                allocator = foo();
            }

            if (handler) |initial_handler| {
                initial_handler.allocator = allocator;
                if (initial_handler.on_request) |on_request| {
                    // we don't care about the return value at the top level
                    _ = on_request(initial_handler, r, &context);
                }
            }
        }
    };
}

test "it" {

    // just some made-up struct
    const User = struct {
        name: []const u8,
        email: []const u8,
    };

    // just some made-up struct
    const Session = struct {
        sessionType: []const u8,
        token: []const u8,
        valid: bool,
    };

    const Mixed = MixContexts(
        .{
            .{ .name = "?user", .type = *User },
            .{ .name = "?session", .type = *Session },
        },
    );

    std.debug.print("{any}\n", .{Mixed});
    inline for (@typeInfo(Mixed).Struct.fields, 0..) |f, i| {
        std.debug.print("field {} : name = {s} : type = {any}\n", .{ i, f.name, f.type });
    }

    const mixed: Mixed = .{
        // it's all optionals which we made default to null in MixContexts
    };
    std.debug.print("mixed = {any}\n", .{mixed});

    const NonOpts = MixContexts(
        .{
            .{ .name = "user", .type = *User },
            .{ .name = "session", .type = *Session },
        },
    );

    var user: User = .{
        .name = "renerocksai",
        .email = "secret",
    };
    var session: Session = .{
        .sessionType = "bearerToken",
        .token = "ABCDEFG",
        .valid = false,
    };

    // this will fail if we don't specify
    const nonOpts: NonOpts = .{
        .user = &user,
        .session = &session,
    };
    std.debug.print("nonOpts = {any}\n", .{nonOpts});
}
