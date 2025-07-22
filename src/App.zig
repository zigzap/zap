//! zap.App takes the zap.Endpoint concept one step further: instead of having
//! only per-endpoint instance data (fields of your Endpoint struct), endpoints
//! in a zap.App easily share a global 'App Context'.
//!
//! In addition to the global App Context, all Endpoint request handlers also
//! receive an arena allocator for easy, care-free allocations. There is one
//! arena allocator per thread, and arenas are reset after each request.
//!
//! Just like regular / legacy zap.Endpoints, returning errors from request
//! handlers is OK. It's decided on a per-endpoint basis how errors are dealt
//! with, via the ErrorStrategy enum field.
//!
//! See `App.Create()`.
const std = @import("std");
const Allocator = std.mem.Allocator;
const ArenaAllocator = std.heap.ArenaAllocator;
const Thread = std.Thread;
const RwLock = Thread.RwLock;

const zap = @import("zap.zig");
const Request = zap.Request;
const HttpListener = zap.HttpListener;
const ErrorStrategy = zap.Endpoint.ErrorStrategy;

pub const AppOpts = struct {
    /// ErrorStrategy for (optional) request handler if no endpoint matches
    default_error_strategy: ErrorStrategy = .log_to_console,
    arena_retain_capacity: usize = 16 * 1024 * 1024,
};

/// creates an App with custom app context
///
/// About App Contexts:
///
/// ```zig
/// const MyContext = struct {
///     // You may (optionally) define the following global handlers:
///     pub fn unhandledRequest(_: *MyContext, _: Allocator, _: Request) anyerror!void {}
///     pub fn unhandledError(_: *MyContext, _: Request, _: anyerror) void {}
/// };
/// ```
pub fn Create(
    /// Your user-defined "Global App Context" type
    comptime Context: type,
) type {
    return struct {
        const App = @This();

        // we make the following fields static so we can access them from a
        // context-free, pure zap request handler
        const InstanceData = struct {
            context: *Context = undefined,
            gpa: Allocator = undefined,
            opts: AppOpts = undefined,
            endpoints: std.ArrayListUnmanaged(*Endpoint.Interface) = .empty,

            there_can_be_only_one: bool = false,
            track_arenas: std.AutoHashMapUnmanaged(Thread.Id, ArenaAllocator) = .empty,
            track_arena_lock: RwLock = .{},

            /// the internal http listener
            listener: HttpListener = undefined,

            /// function pointer to handler for otherwise unhandled requests.
            /// Will automatically be set if your Context provides an
            /// `unhandledRequest` function of type `fn(*Context, Allocator,
            /// Request) !void`.
            unhandled_request: ?*const fn (*Context, Allocator, Request) anyerror!void = null,

            /// function pointer to handler for unhandled errors.
            /// Errors are unhandled if they are not logged but raised by the
            /// ErrorStrategy. Will automatically be set if your Context
            /// provides an `unhandledError` function of type `fn(*Context,
            /// Allocator, Request, anyerror) void`.
            unhandled_error: ?*const fn (*Context, Request, anyerror) void = null,
        };
        var _static: InstanceData = .{};

        pub const Endpoint = struct {
            pub const Interface = struct {
                call: *const fn (*Interface, Request) anyerror!void = undefined,
                path: []const u8,
                destroy: *const fn (*Interface, Allocator) void = undefined,
            };
            pub fn Bind(ArbitraryEndpoint: type) type {
                return struct {
                    endpoint: *ArbitraryEndpoint,
                    interface: Interface,

                    // tbh: unnecessary, since we have it in _static
                    app_context: *Context,

                    const Bound = @This();

                    pub fn unwrap(interface: *Interface) *Bound {
                        const self: *Bound = @alignCast(@fieldParentPtr("interface", interface));
                        return self;
                    }

                    pub fn destroy(interface: *Interface, allocator: Allocator) void {
                        const self: *Bound = @alignCast(@fieldParentPtr("interface", interface));
                        allocator.destroy(self);
                    }

                    pub fn onRequestInterface(interface: *Interface, r: Request) !void {
                        var self: *Bound = Bound.unwrap(interface);
                        var arena = try get_arena();
                        try self.onRequest(arena.allocator(), self.app_context, r);
                        _ = arena.reset(.{ .retain_with_limit = _static.opts.arena_retain_capacity });
                    }

                    pub fn onRequest(self: *Bound, arena: Allocator, app_context: *Context, r: Request) !void {
                        // TODO: simplitfy this with @tagName?
                        const ret = switch (r.methodAsEnum()) {
                            .GET => callHandlerIfExist("get", self.endpoint, arena, app_context, r),
                            .POST => callHandlerIfExist("post", self.endpoint, arena, app_context, r),
                            .PUT => callHandlerIfExist("put", self.endpoint, arena, app_context, r),
                            .DELETE => callHandlerIfExist("delete", self.endpoint, arena, app_context, r),
                            .PATCH => callHandlerIfExist("patch", self.endpoint, arena, app_context, r),
                            .OPTIONS => callHandlerIfExist("options", self.endpoint, arena, app_context, r),
                            .HEAD => callHandlerIfExist("head", self.endpoint, arena, app_context, r),
                            else => error.UnsupportedHtmlRequestMethod,
                        };
                        if (ret) {
                            // handled without error
                        } else |err| {
                            switch (self.endpoint.*.error_strategy) {
                                .raise => return err,
                                .log_to_response => return r.sendError(err, if (@errorReturnTrace()) |t| t.* else null, 505),
                                .log_to_console => zap.log.err(
                                    "Error in {} {s} : {}",
                                    .{ Bound, r.method orelse "(no method)", err },
                                ),
                            }
                        }
                    }
                };
            }

            pub fn init(ArbitraryEndpoint: type, endpoint: *ArbitraryEndpoint) Endpoint.Bind(ArbitraryEndpoint) {
                checkEndpointType(ArbitraryEndpoint);
                const BoundEp = Endpoint.Bind(ArbitraryEndpoint);
                return .{
                    .endpoint = endpoint,
                    .interface = .{
                        .path = endpoint.path,
                        .call = BoundEp.onRequestInterface,
                        .destroy = BoundEp.destroy,
                    },
                    .app_context = _static.context,
                };
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
                    if (@FieldType(T, "error_strategy") != ErrorStrategy) {
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
                    "head",
                };
                const params_to_check = [_]type{
                    *T,
                    Allocator,
                    *Context,
                    Request,
                };
                inline for (methods_to_check) |method| {
                    if (@hasDecl(T, method)) {
                        const Method = @TypeOf(@field(T, method));
                        const method_info = @typeInfo(Method);
                        if (method_info != .@"fn") {
                            @compileError("Expected `" ++ @typeName(T) ++ "." ++ method ++ "` to be a request handler method, got: " ++ @typeName(Method));
                        }

                        // now check parameters
                        const params = method_info.@"fn".params;
                        if (params.len != params_to_check.len) {
                            @compileError(std.fmt.comptimePrint(
                                "Expected method `{s}.{s}` to have {d} parameters, got {d}",
                                .{
                                    @typeName(T),
                                    method,
                                    params_to_check.len,
                                    params.len,
                                },
                            ));
                        }

                        inline for (params_to_check, 0..) |param_type_expected, i| {
                            if (params[i].type.? != param_type_expected) {
                                @compileError(std.fmt.comptimePrint(
                                    "Expected parameter {d} of method {s}.{s} to be {s}, got {s}",
                                    .{
                                        i + 1,
                                        @typeName(T),
                                        method,
                                        @typeName(param_type_expected),
                                        @typeName(params[i].type.?),
                                    },
                                ));
                            }
                        }

                        const ret_type = method_info.@"fn".return_type.?;
                        const ret_info = @typeInfo(ret_type);
                        if (ret_info != .error_union) {
                            @compileError("Expected return type of method `" ++ @typeName(T) ++ "." ++ method ++ "` to be !void, got: " ++ @typeName(ret_type));
                        }
                        if (ret_info.error_union.payload != void) {
                            @compileError("Expected return type of method `" ++ @typeName(T) ++ "." ++ method ++ "` to be !void, got: !" ++ @typeName(ret_info.error_union.payload));
                        }
                    }
                }
            }

            /// Wrap an endpoint with an Authenticator
            pub fn Authenticating(EndpointType: type, Authenticator: type) type {
                return struct {
                    authenticator: *Authenticator,
                    ep: *EndpointType,
                    path: []const u8,
                    error_strategy: ErrorStrategy,
                    const AuthenticatingEndpoint = @This();

                    /// Init the authenticating endpoint. Pass in a pointer to the endpoint
                    /// you want to wrap, and the Authenticator that takes care of authenticating
                    /// requests.
                    pub fn init(e: *EndpointType, authenticator: *Authenticator) AuthenticatingEndpoint {
                        return .{
                            .authenticator = authenticator,
                            .ep = e,
                            .path = e.path,
                            .error_strategy = e.error_strategy,
                        };
                    }

                    /// Authenticates GET requests using the Authenticator.
                    pub fn get(self: *AuthenticatingEndpoint, arena: Allocator, context: *Context, request: Request) anyerror!void {
                        try switch (self.authenticator.authenticateRequest(&request)) {
                            .AuthFailed => callHandlerIfExist("unauthorized", self.ep, arena, context, request),
                            .AuthOK => callHandlerIfExist("get", self.ep, arena, context, request),
                            .Handled => {},
                        };
                    }

                    /// Authenticates POST requests using the Authenticator.
                    pub fn post(self: *AuthenticatingEndpoint, arena: Allocator, context: *Context, request: Request) anyerror!void {
                        try switch (self.authenticator.authenticateRequest(&request)) {
                            .AuthFailed => callHandlerIfExist("unauthorized", self.ep, arena, context, request),
                            .AuthOK => callHandlerIfExist("post", self.ep, arena, context, request),
                            .Handled => {},
                        };
                    }

                    /// Authenticates PUT requests using the Authenticator.
                    pub fn put(self: *AuthenticatingEndpoint, arena: Allocator, context: *Context, request: zap.Request) anyerror!void {
                        try switch (self.authenticator.authenticateRequest(&request)) {
                            .AuthFailed => callHandlerIfExist("unauthorized", self.ep, arena, context, request),
                            .AuthOK => callHandlerIfExist("put", self.ep, arena, context, request),
                            .Handled => {},
                        };
                    }

                    /// Authenticates DELETE requests using the Authenticator.
                    pub fn delete(self: *AuthenticatingEndpoint, arena: Allocator, context: *Context, request: zap.Request) anyerror!void {
                        try switch (self.authenticator.authenticateRequest(&request)) {
                            .AuthFailed => callHandlerIfExist("unauthorized", self.ep, arena, context, request),
                            .AuthOK => callHandlerIfExist("delete", self.ep, arena, context, request),
                            .Handled => {},
                        };
                    }

                    /// Authenticates PATCH requests using the Authenticator.
                    pub fn patch(self: *AuthenticatingEndpoint, arena: Allocator, context: *Context, request: zap.Request) anyerror!void {
                        try switch (self.authenticator.authenticateRequest(&request)) {
                            .AuthFailed => callHandlerIfExist("unauthorized", self.ep, arena, context, request),
                            .AuthOK => callHandlerIfExist("patch", self.ep, arena, context, request),
                            .Handled => {},
                        };
                    }

                    /// Authenticates OPTIONS requests using the Authenticator.
                    pub fn options(self: *AuthenticatingEndpoint, arena: Allocator, context: *Context, request: zap.Request) anyerror!void {
                        try switch (self.authenticator.authenticateRequest(&request)) {
                            .AuthFailed => callHandlerIfExist("unauthorized", self.ep, arena, context, request),
                            .AuthOK => callHandlerIfExist("options", self.ep, arena, context, request),
                            .Handled => {},
                        };
                    }

                    /// Authenticates HEAD requests using the Authenticator.
                    pub fn head(self: *AuthenticatingEndpoint, arena: Allocator, context: *Context, request: zap.Request) anyerror!void {
                        try switch (self.authenticator.authenticateRequest(&request)) {
                            .AuthFailed => callHandlerIfExist("unauthorized", self.ep, arena, context, request),
                            .AuthOK => callHandlerIfExist("head", self.ep, arena, context, request),
                            .Handled => {},
                        };
                    }
                };
            }
        };

        pub const ListenerSettings = struct {
            /// IP interface, e.g. 0.0.0.0
            interface: [*c]const u8 = null,
            /// IP port to listen on
            port: usize,
            public_folder: ?[]const u8 = null,
            max_clients: ?isize = null,
            max_body_size: ?usize = null,
            timeout: ?u8 = null,
            tls: ?zap.Tls = null,
        };

        pub fn init(gpa_: Allocator, context_: *Context, opts_: AppOpts) !void {
            if (_static.there_can_be_only_one) {
                return error.OnlyOneAppAllowed;
            }
            _static.context = context_;
            _static.gpa = gpa_;
            _static.opts = opts_;
            _static.there_can_be_only_one = true;

            // set unhandled_request callback if provided by Context
            if (@hasDecl(Context, "unhandledRequest")) {
                // try if we can use it
                const Unhandled = @TypeOf(@field(Context, "unhandledRequest"));
                const Expected = fn (_: *Context, _: Allocator, _: Request) anyerror!void;
                if (Unhandled != Expected) {
                    @compileError("`unhandledRequest` method of " ++ @typeName(Context) ++ " has wrong type:\n" ++ @typeName(Unhandled) ++ "\nexpected:\n" ++ @typeName(Expected));
                }
                _static.unhandled_request = Context.unhandledRequest;
            }
            if (@hasDecl(Context, "unhandledError")) {
                // try if we can use it
                const Unhandled = @TypeOf(@field(Context, "unhandledError"));
                const Expected = fn (_: *Context, _: Request, _: anyerror) void;
                if (Unhandled != Expected) {
                    @compileError("`unhandledError` method of " ++ @typeName(Context) ++ " has wrong type:\n" ++ @typeName(Unhandled) ++ "\nexpected:\n" ++ @typeName(Expected));
                }
                _static.unhandled_error = Context.unhandledError;
            }
        }

        pub fn deinit() void {
            // we created endpoint wrappers but only tracked their interfaces
            // hence, we need to destroy the wrappers through their interfaces
            if (false) {
                var it = _static.endpoints.iterator();
                while (it.next()) |kv| {
                    const interface = kv.value_ptr;
                    interface.*.destroy(_static.gpa);
                }
            } else {
                for (_static.endpoints.items) |interface| {
                    interface.destroy(interface, _static.gpa);
                }
            }
            _static.endpoints.deinit(_static.gpa);

            _static.track_arena_lock.lock();
            defer _static.track_arena_lock.unlock();

            var it = _static.track_arenas.valueIterator();
            while (it.next()) |arena| {
                arena.deinit();
            }
            _static.track_arenas.deinit(_static.gpa);
        }

        // This can be resolved at comptime so *perhaps it does affect optimiazation
        pub fn callHandlerIfExist(comptime fn_name: []const u8, e: anytype, arena: Allocator, ctx: *Context, r: Request) anyerror!void {
            const EndPoint = @TypeOf(e.*);
            if (@hasDecl(EndPoint, fn_name)) {
                return @field(EndPoint, fn_name)(e, arena, ctx, r);
            }
            zap.log.debug(
                "Unhandled `{s}` {s} request ({s} not implemented in {s})",
                .{ r.method orelse "<unknown>", r.path orelse "", fn_name, @typeName(Endpoint) },
            );
            r.setStatus(.method_not_allowed);
            try r.sendBody("405 - method not allowed\r\n");
            return;
        }

        pub fn get_arena() !*ArenaAllocator {
            const thread_id = std.Thread.getCurrentId();
            _static.track_arena_lock.lockShared();
            if (_static.track_arenas.getPtr(thread_id)) |arena| {
                _static.track_arena_lock.unlockShared();
                return arena;
            } else {
                _static.track_arena_lock.unlockShared();
                _static.track_arena_lock.lock();
                defer _static.track_arena_lock.unlock();
                const arena = ArenaAllocator.init(_static.gpa);
                try _static.track_arenas.put(_static.gpa, thread_id, arena);
                return _static.track_arenas.getPtr(thread_id).?;
            }
        }

        /// Register an endpoint with this listener.
        /// NOTE: endpoint paths are matched with startsWith
        /// -> so use endpoints with distinctly starting names!!
        /// If you try to register an endpoint whose path would shadow an
        /// already registered one, you will receive an
        /// EndpointPathShadowError.
        pub fn register(endpoint: anytype) !void {
            for (_static.endpoints.items) |other| {
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
            const bound = try _static.gpa.create(Endpoint.Bind(EndpointType));
            bound.* = Endpoint.init(EndpointType, endpoint);
            try _static.endpoints.append(_static.gpa, &bound.interface);
        }

        pub fn listen(l: ListenerSettings) !void {
            _static.listener = HttpListener.init(.{
                .interface = l.interface,
                .port = l.port,
                .public_folder = l.public_folder,
                .max_clients = l.max_clients,
                .max_body_size = l.max_body_size,
                .timeout = l.timeout,
                .tls = l.tls,

                .on_request = onRequest,
            });
            try _static.listener.listen();
        }

        fn onRequest(r: Request) !void {
            if (r.path) |p| {
                for (_static.endpoints.items) |interface| {
                    if (std.mem.startsWith(u8, p, interface.path)) {
                        return interface.call(interface, r) catch |err| {
                            // if error is not dealt with in the interface, e.g.
                            // if error strategy is .raise:
                            if (_static.unhandled_error) |error_cb| {
                                error_cb(_static.context, r, err);
                            } else {
                                zap.log.err(
                                    "App.Endpoint onRequest error {} in endpoint interface {}\n",
                                    .{ err, interface },
                                );
                            }
                        };
                    }
                }
            }

            // this is basically the "not found" handler
            if (_static.unhandled_request) |foo| {
                var arena = try get_arena();
                foo(_static.context, arena.allocator(), r) catch |err| {
                    switch (_static.opts.default_error_strategy) {
                        .raise => if (_static.unhandled_error) |error_cb| {
                            error_cb(_static.context, r, err);
                        } else {
                            zap.Logging.on_uncaught_error("App on_request", err);
                        },
                        .log_to_response => return r.sendError(err, if (@errorReturnTrace()) |t| t.* else null, 505),
                        .log_to_console => zap.log.err("Error in {} {s} : {}", .{ App, r.method orelse "(no method)", err }),
                    }
                };
            }
        }
    };
}
