//! WIP: zap.App.
//!
//! - Per Request Arena(s) thread-local?
//! - Custom "State" Context, type-safe
//! - route handlers
//! - automatic error catching & logging, optional report to HTML

const std = @import("std");
const Allocator = std.mem.Allocator;
const ArenaAllocator = std.heap.ArenaAllocator;
const RwLock = std.Thread.RwLock;

const zap = @import("zap.zig");
const Request = zap.Request;

pub const Opts = struct {
    /// ErrorStrategy for (optional) request handler if no endpoint matches
    default_error_strategy: zap.Endpoint.ErrorStrategy = .log_to_console,
    arena_retain_capacity: usize = 16 * 1024 * 1024,
};

threadlocal var _arena: ?ArenaAllocator = null;

/// creates an App with custom app context
pub fn Create(comptime Context: type) type {
    return struct {
        const App = @This();

        // we make the following fields static so we can access them from a
        // context-free, pure zap request handler
        const InstanceData = struct {
            context: *Context = undefined,
            gpa: Allocator = undefined,
            opts: Opts = undefined,
            endpoints: std.StringArrayHashMapUnmanaged(*Endpoint.Interface) = .empty,

            there_can_be_only_one: bool = false,
            track_arenas: std.ArrayListUnmanaged(*ArenaAllocator) = .empty,
            track_arena_lock: RwLock = .{},
        };
        var _static: InstanceData = .{};

        /// Internal, static request handler callback. Will be set to the optional,
        /// user-defined request callback that only gets called if no endpoints match
        /// a request.
        var on_request: ?*const fn (Allocator, *Context, Request) anyerror!void = null;

        pub const Endpoint = struct {
            pub const Interface = struct {
                call: *const fn (*Interface, zap.Request) anyerror!void = undefined,
                path: []const u8,
                destroy: *const fn (allocator: Allocator, *Interface) void = undefined,
            };
            pub fn Wrap(T: type) type {
                return struct {
                    wrapped: *T,
                    interface: Interface,
                    opts: Opts,
                    app_context: *Context,

                    const Wrapped = @This();

                    pub fn unwrap(interface: *Interface) *Wrapped {
                        const self: *Wrapped = @alignCast(@fieldParentPtr("interface", interface));
                        return self;
                    }

                    pub fn destroy(allocator: Allocator, wrapper: *Interface) void {
                        const self: *Wrapped = @alignCast(@fieldParentPtr("interface", wrapper));
                        allocator.destroy(self);
                    }

                    pub fn onRequestWrapped(interface: *Interface, r: zap.Request) !void {
                        var self: *Wrapped = Wrapped.unwrap(interface);
                        const arena = try get_arena();
                        try self.onRequest(arena.allocator(), self.app_context, r);
                        arena.reset(.{ .retain_capacity = self.opts.arena_retain_capacity });
                    }

                    pub fn onRequest(self: *Wrapped, arena: Allocator, app_context: *Context, r: zap.Request) !void {
                        const ret = switch (r.methodAsEnum()) {
                            .GET => self.wrapped.*.get(arena, app_context, r),
                            .POST => self.wrapped.*.post(arena, app_context, r),
                            .PUT => self.wrapped.*.put(arena, app_context, r),
                            .DELETE => self.wrapped.*.delete(arena, app_context, r),
                            .PATCH => self.wrapped.*.patch(arena, app_context, r),
                            .OPTIONS => self.wrapped.*.options(arena, app_context, r),
                            else => error.UnsupportedHtmlRequestMethod,
                        };
                        if (ret) {
                            // handled without error
                        } else |err| {
                            switch (self.wrapped.*.error_strategy) {
                                .raise => return err,
                                .log_to_response => return r.sendError(err, if (@errorReturnTrace()) |t| t.* else null, 505),
                                .log_to_console => zap.debug("Error in {} {s} : {}", .{ Wrapped, r.method orelse "(no method)", err }),
                            }
                        }
                    }
                };
            }

            pub fn init(T: type, value: *T, app_opts: Opts, app_context: *Context) Endpoint.Wrap(T) {
                checkEndpointType(T);
                var ret: Endpoint.Wrap(T) = .{
                    .wrapped = value,
                    .wrapper = .{ .path = value.path },
                    .opts = app_opts,
                    .app_context = app_context,
                };
                ret.wrapper.call = Endpoint.Wrap(T).onRequestWrapped;
                ret.wrapper.destroy = Endpoint.Wrap(T).destroy;
                return ret;
            }

            pub fn checkEndpointType(T: type) void {
                if (@hasField(T, "path")) {
                    if (@FieldType(T, "path") != []const u8) {
                        @compileError(@typeName(@FieldType(T, "path")) ++ " has wrong type, expected: []const u8");
                    }
                } else {
                    @compileError(@typeName(T) ++ " has no path field");
                }

                if (@hasField(T, "error_strategy")) {
                    if (@FieldType(T, "error_strategy") != zap.Endpoint.ErrorStrategy) {
                        @compileError(@typeName(@FieldType(T, "error_strategy")) ++ " has wrong type, expected: zap.Endpoint.ErrorStrategy");
                    }
                } else {
                    @compileError(@typeName(T) ++ " has no error_strategy field");
                }

                const methods_to_check = [_][]const u8{
                    "get",
                    "post",
                    "put",
                    "delete",
                    "patch",
                    "options",
                };
                inline for (methods_to_check) |method| {
                    if (@hasDecl(T, method)) {
                        if (@TypeOf(@field(T, method)) != fn (_: *T, _: Allocator, _: *Context, _: zap.Request) anyerror!void) {
                            @compileError(method ++ " method of " ++ @typeName(T) ++ " has wrong type:\n" ++ @typeName(@TypeOf(T.get)) ++ "\nexpected:\n" ++ @typeName(fn (_: *T, _: Allocator, _: *Context, _: zap.Request) anyerror!void));
                        }
                    } else {
                        @compileError(@typeName(T) ++ " has no method named `" ++ method ++ "`");
                    }
                }
            }
        };

        pub const ListenerSettings = struct {
            port: usize,
            interface: [*c]const u8 = null,
            public_folder: ?[]const u8 = null,
            max_clients: ?isize = null,
            max_body_size: ?usize = null,
            timeout: ?u8 = null,
            tls: ?zap.Tls = null,
        };

        pub fn init(gpa_: Allocator, context_: *Context, opts_: Opts) !App {
            if (App._static._there_can_be_only_one) {
                return error.OnlyOneAppAllowed;
            }
            App._static.context = context_;
            App._static.gpa = gpa_;
            App._static.opts = opts_;
            App._static.there_can_be_only_one = true;
            return .{};
        }

        pub fn deinit() void {
            App._static.endpoints.deinit(_static.gpa);

            App._static.track_arena_lock.lock();
            defer App._static.track_arena_lock.unlock();
            for (App._static.track_arenas.items) |arena| {
                arena.deinit();
            }
        }

        fn get_arena() !*ArenaAllocator {
            App._static.track_arena_lock.lockShared();
            if (_arena == null) {
                App._static.track_arena_lock.unlockShared();
                App._static.track_arena_lock.lock();
                defer App._static.track_arena_lock.unlock();
                _arena = ArenaAllocator.init(App._static.gpa);
                try App._static.track_arenas.append(App._static.gpa, &_arena.?);
            } else {
                App._static.track_arena_lock.unlockShared();
                return &_arena.?;
            }
        }

        /// Register an endpoint with this listener.
        /// NOTE: endpoint paths are matched with startsWith -> so use endpoints with distinctly starting names!!
        /// If you try to register an endpoint whose path would shadow an already registered one, you will
        /// receive an EndpointPathShadowError.
        pub fn register(self: *App, endpoint: anytype) !void {
            for (App._static.endpoints.items) |other| {
                if (std.mem.startsWith(
                    u8,
                    other.path,
                    endpoint.path,
                ) or std.mem.startsWith(
                    u8,
                    endpoint.path,
                    other.path,
                )) {
                    return zap.Endpoint.ListenerError.EndpointPathShadowError;
                }
            }
            const EndpointType = @typeInfo(@TypeOf(endpoint)).pointer.child;
            Endpoint.checkEndpointType(EndpointType);
            const wrapper = try self.gpa.create(Endpoint.Wrap(EndpointType));
            wrapper.* = Endpoint.init(EndpointType, endpoint);
            try App._static.endpoints.append(self.gpa, &wrapper.wrapper);
        }

        pub fn listen(self: *App, l: ListenerSettings) !void {
            _ = self;
            _ = l;
            // TODO: do it
        }

        fn onRequest(r: Request) !void {
            if (r.path) |p| {
                for (App._static.endpoints.items) |wrapper| {
                    if (std.mem.startsWith(u8, p, wrapper.path)) {
                        return try wrapper.call(wrapper, r);
                    }
                }
            }
            if (on_request) |foo| {
                if (_arena == null) {
                    _arena = ArenaAllocator.init(App._static.gpa);
                }
                foo(_arena.allocator(), App._static.context, r) catch |err| {
                    switch (App._static.opts.default_error_strategy) {
                        .raise => return err,
                        .log_to_response => return r.sendError(err, if (@errorReturnTrace()) |t| t.* else null, 505),
                        .log_to_console => zap.debug("Error in {} {s} : {}", .{ App, r.method orelse "(no method)", err }),
                    }
                };
            }
        }
    };
}
