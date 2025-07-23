//! Endpoint and supporting types.
//!
//! An Endpoint can be any zig struct that defines all the callbacks lilsted
//! below.
//! Pass an instance of an Endpoint struct  to zap.Endpoint.Listener.register()
//! function to register with the listener.
//!
//! **NOTE**: Endpoints can implement the following "interface":
//!
//! Any method handler that's not implemented will be handled automatically:
//! - zap will log it
//! - a response with status code 405 (method not allowed) is sent to the client
//!
//! ```zig
//! /// The http request path / slug of the endpoint
//! path: []const u8,
//! error_strategy: zap.Endpoint.ErrorStrategy,
//!
//! /// Handlers by request method:
//! /// implement any of the following
//! pub fn get(_: *Self, _: zap.Request) !void {}
//! pub fn post(_: *Self, _: zap.Request) !void {}
//! pub fn put(_: *Self, _: zap.Request) !void {}
//! pub fn delete(_: *Self, _: zap.Request) !void {}
//! pub fn patch(_: *Self, _: zap.Request) !void {}
//! pub fn options(_: *Self, _: zap.Request) !void {}
//! pub fn head(_: *Self, _: zap.Request) !void {}
//!
//! // optional, if auth stuff is used:
//! pub fn unauthorized(_: *Self, _: zap.Request) !void {}
//! ```
//!
//! Example:
//! A simple endpoint listening on the /stop route that shuts down zap. The
//! main thread usually continues at the instructions after the call to
//! zap.start().
//!
//! ```zig
//! const StopEndpoint = struct {
//!     path: []const u8,
//!     error_strategy: zap.Endpoint.ErrorStrategy = .log_to_response,
//!
//!     pub fn init(path: []const u8) StopEndpoint {
//!         return .{
//!             .path = path,
//!         };
//!     }
//!
//!     pub fn get(_: *StopEndpoint, _: zap.Request) !void {
//!         zap.stop();
//!     }
//! };
//! ```

const std = @import("std");
const zap = @import("zap.zig");
const auth = @import("http_auth.zig");

/// Endpoint request error handling strategy
pub const ErrorStrategy = enum {
    /// log errors to console
    log_to_console,
    /// send an HTML response containing an error trace
    log_to_response,
    /// raise errors.
    /// raised errors, if kept unhandled,  will ultimately be logged by
    /// zap.Logging.on_uncaught_error()
    raise,
};

// zap types
const Request = zap.Request;
const ListenerSettings = zap.HttpListenerSettings;
const HttpListener = zap.HttpListener;

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

            // check return type
            const ret_type = method_info.@"fn".return_type.?;
            const ret_info = @typeInfo(ret_type);
            if (ret_info != .error_union) {
                @compileError("Expected return type of method `" ++ @typeName(T) ++ "." ++ method ++ "` to be !void, got: " ++ @typeName(ret_type));
            }
            if (ret_info.error_union.payload != void) {
                @compileError("Expected return type of method `" ++ @typeName(T) ++ "." ++ method ++ "` to be !void, got: !" ++ @typeName(ret_info.error_union.payload));
            }
        } else {
            // it is ok not to implement a method handler
            // pass
        }
    }
}
// This can be resolved at comptime so *perhaps it does affect optimiazation
pub fn callHandlerIfExist(comptime fn_name: []const u8, e: anytype, r: Request) anyerror!void {
    const EndPointType = @TypeOf(e.*);
    if (@hasDecl(EndPointType, fn_name)) {
        return @field(EndPointType, fn_name)(e, r);
    }
    zap.log.debug(
        "Unhandled `{s}` {s} request ({s} not implemented in {s})",
        .{ r.method orelse "<unknown>", r.path orelse "", fn_name, @typeName(EndPointType) },
    );
    r.setStatus(.method_not_allowed);
    try r.sendBody("405 - method not allowed\r\n");
    return;
}

pub const Binder = struct {
    pub const Interface = struct {
        call: *const fn (*Interface, zap.Request) anyerror!void = undefined,
        path: []const u8,
        destroy: *const fn (*Interface, std.mem.Allocator) void = undefined,
    };
    pub fn Bind(ArbitraryEndpoint: type) type {
        return struct {
            endpoint: *ArbitraryEndpoint,
            interface: Interface,

            const Bound = @This();

            pub fn unwrap(interface: *Interface) *Bound {
                const self: *Bound = @alignCast(@fieldParentPtr("interface", interface));
                return self;
            }

            pub fn destroy(interface: *Interface, allocator: std.mem.Allocator) void {
                const self: *Bound = @alignCast(@fieldParentPtr("interface", interface));
                allocator.destroy(self);
            }

            pub fn onRequestInterface(interface: *Interface, r: zap.Request) !void {
                var self: *Bound = Bound.unwrap(interface);
                try self.onRequest(r);
            }

            pub fn onRequest(self: *Bound, r: zap.Request) !void {
                const ret = switch (r.methodAsEnum()) {
                    .GET => callHandlerIfExist("get", self.endpoint, r),
                    .POST => callHandlerIfExist("post", self.endpoint, r),
                    .PUT => callHandlerIfExist("put", self.endpoint, r),
                    .DELETE => callHandlerIfExist("delete", self.endpoint, r),
                    .PATCH => callHandlerIfExist("patch", self.endpoint, r),
                    .OPTIONS => callHandlerIfExist("options", self.endpoint, r),
                    .HEAD => callHandlerIfExist("head", self.endpoint, r),
                    else => error.UnsupportedHtmlRequestMethod,
                };
                if (ret) {
                    // handled without error
                } else |err| {
                    switch (self.endpoint.*.error_strategy) {
                        .raise => return err,
                        .log_to_response => return r.sendError(err, if (@errorReturnTrace()) |t| t.* else null, 505),
                        .log_to_console => zap.log.err("Error in {} {s} : {}", .{ Bound, r.method orelse "(no method)", err }),
                    }
                }
            }
        };
    }

    pub fn init(ArbitraryEndpoint: type, value: *ArbitraryEndpoint) Binder.Bind(ArbitraryEndpoint) {
        checkEndpointType(ArbitraryEndpoint);
        const BoundEp = Binder.Bind(ArbitraryEndpoint);
        return .{
            .endpoint = value,
            .interface = .{
                .path = value.path,
                .call = BoundEp.onRequestInterface,
                .destroy = BoundEp.destroy,
            },
        };
    }
};

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
        pub fn get(self: *AuthenticatingEndpoint, r: zap.Request) anyerror!void {
            try switch (self.authenticator.authenticateRequest(&r)) {
                .AuthFailed => callHandlerIfExist("unauthorized", self.ep, r),
                .AuthOK => callHandlerIfExist("get", self.ep, r),
                .Handled => {},
            };
        }

        /// Authenticates POST requests using the Authenticator.
        pub fn post(self: *AuthenticatingEndpoint, r: zap.Request) anyerror!void {
            try switch (self.authenticator.authenticateRequest(&r)) {
                .AuthFailed => callHandlerIfExist("unauthorized", self.ep, r),
                .AuthOK => callHandlerIfExist("post", self.ep, r),
                .Handled => {},
            };
        }

        /// Authenticates PUT requests using the Authenticator.
        pub fn put(self: *AuthenticatingEndpoint, r: zap.Request) anyerror!void {
            try switch (self.authenticator.authenticateRequest(&r)) {
                .AuthFailed => callHandlerIfExist("unauthorized", self.ep, r),
                .AuthOK => callHandlerIfExist("put", self.ep, r),
                .Handled => {},
            };
        }

        /// Authenticates DELETE requests using the Authenticator.
        pub fn delete(self: *AuthenticatingEndpoint, r: zap.Request) anyerror!void {
            try switch (self.authenticator.authenticateRequest(&r)) {
                .AuthFailed => callHandlerIfExist("unauthorized", self.ep, r),
                .AuthOK => callHandlerIfExist("delete", self.ep, r),
                .Handled => {},
            };
        }

        /// Authenticates PATCH requests using the Authenticator.
        pub fn patch(self: *AuthenticatingEndpoint, r: zap.Request) anyerror!void {
            try switch (self.authenticator.authenticateRequest(&r)) {
                .AuthFailed => callHandlerIfExist("unauthorized", self.ep, r),
                .AuthOK => callHandlerIfExist("patch", self.ep, r),
                .Handled => {},
            };
        }

        /// Authenticates OPTIONS requests using the Authenticator.
        pub fn options(self: *AuthenticatingEndpoint, r: zap.Request) anyerror!void {
            try switch (self.authenticator.authenticateRequest(&r)) {
                .AuthFailed => callHandlerIfExist("unauthorized", self.ep, r),
                .AuthOK => callHandlerIfExist("options", self.ep, r),
                .Handled => {},
            };
        }

        /// Authenticates HEAD requests using the Authenticator.
        pub fn head(self: *AuthenticatingEndpoint, r: zap.Request) anyerror!void {
            try switch (self.authenticator.authenticateRequest(&r)) {
                .AuthFailed => callHandlerIfExist("unauthorized", self.ep, r),
                .AuthOK => callHandlerIfExist("head", self.ep, r),
                .Handled => {},
            };
        }
    };
}

pub const ListenerError = error{
    /// Since we use .startsWith to check for matching paths, you cannot use
    /// endpoint paths that overlap at the beginning. --> When trying to register
    /// an endpoint whose path would shadow an already registered one, you will
    /// receive this error.
    EndpointPathShadowError,
};

/// The listener with endpoint support
///
/// NOTE: It switches on path.startsWith -> so use endpoints with distinctly starting names!!
pub const Listener = struct {
    listener: HttpListener,
    allocator: std.mem.Allocator,

    pub const Settings = struct {
        port: usize,
        interface: [*c]const u8 = null,

        /// User-defined request callback that only gets called if no endpoints
        /// match a request.
        on_request: ?zap.HttpRequestFn,
        on_response: ?zap.HttpRequestFn = null,
        on_upgrade: ?zap.HttpUpgradeFn = null,
        on_finish: ?zap.HttpFinishFn = null,

        /// Callback, called if an error is raised and not caught by the
        /// ErrorStrategy
        on_error: ?*const fn (Request, anyerror) void = null,

        // provide any pointer in there for "user data". it will be passed pack in
        // on_finish()'s copy of the struct_http_settings_s
        udata: ?*anyopaque = null,
        public_folder: ?[]const u8 = null,
        max_clients: ?isize = null,
        max_body_size: ?usize = null,
        timeout: ?u8 = null,
        log: bool = false,
        ws_timeout: u8 = 40,
        ws_max_msg_size: usize = 262144,
        tls: ?zap.Tls = null,
    };
    /// Internal static interface struct of member endpoints
    var endpoints: std.ArrayListUnmanaged(*Binder.Interface) = .empty;

    /// Internal, static request handler callback. Will be set to the optional,
    /// user-defined request callback that only gets called if no endpoints match
    /// a request.
    var on_request: ?zap.HttpRequestFn = null;

    /// Callback, called if an error is raised and not caught by the ErrorStrategy
    var on_error: ?*const fn (Request, anyerror) void = null;

    /// Initialize a new endpoint listener. Note, if you pass an `on_request`
    /// callback in the provided ListenerSettings, this request callback will be
    /// called every time a request arrives that no endpoint matches.
    pub fn init(a: std.mem.Allocator, settings: Settings) Listener {
        // reset the global in case init is called multiple times, as is the
        // case in the authentication tests
        endpoints = .empty;

        var ls: zap.HttpListenerSettings = .{
            .port = settings.port,
            .interface = settings.interface,

            // we set to our own handler
            .on_request = onRequest,

            .on_response = settings.on_response,
            .on_upgrade = settings.on_upgrade,
            .on_finish = settings.on_finish,
            .udata = settings.udata,
            .public_folder = settings.public_folder,
            .max_clients = settings.max_clients,
            .max_body_size = settings.max_body_size,
            .timeout = settings.timeout,
            .log = settings.log,
            .ws_timeout = settings.ws_timeout,
            .ws_max_msg_size = settings.ws_max_msg_size,
            .tls = settings.tls,
        };

        // override the settings with our internal, actual callback function
        // so that "we" will be called on request
        ls.on_request = Listener.onRequest;

        // store the settings-provided request callbacks for later use
        on_request = settings.on_request;
        on_error = settings.on_error;

        return .{
            .listener = HttpListener.init(ls),
            .allocator = a,
        };
    }

    /// De-init the listener and free its resources.
    /// Registered endpoints will not be de-initialized automatically; just removed
    /// from the internal map.
    pub fn deinit(self: *Listener) void {
        for (endpoints.items) |interface| {
            interface.destroy(interface, self.allocator);
        }
        endpoints.deinit(self.allocator);
    }

    /// Call this to start listening. After this, no more endpoints can be
    /// registered.
    pub fn listen(self: *Listener) !void {
        try self.listener.listen();
    }

    /// Register an endpoint with this listener.
    /// NOTE: endpoint paths are matched with startsWith -> so use endpoints with distinctly starting names!!
    /// If you try to register an endpoint whose path would shadow an already registered one, you will
    /// receive an EndpointPathShadowError.
    pub fn register(self: *Listener, e: anytype) !void {
        for (endpoints.items) |other| {
            if (std.mem.startsWith(
                u8,
                other.path,
                e.path,
            ) or std.mem.startsWith(
                u8,
                e.path,
                other.path,
            )) {
                return ListenerError.EndpointPathShadowError;
            }
        }
        const EndpointType = @typeInfo(@TypeOf(e)).pointer.child;
        checkEndpointType(EndpointType);
        const bound = try self.allocator.create(Binder.Bind(EndpointType));
        bound.* = Binder.init(EndpointType, e);
        try endpoints.append(self.allocator, &bound.interface);
    }

    fn onRequest(r: Request) !void {
        if (r.path) |p| {
            for (endpoints.items) |interface| {
                if (std.mem.startsWith(u8, p, interface.path)) {
                    return interface.call(interface, r) catch |err| {
                        // if error is not dealt with in the entpoint, e.g.
                        // if error strategy is .raise:
                        if (on_error) |error_cb| {
                            error_cb(r, err);
                        } else {
                            zap.log.err(
                                "Endpoint onRequest error {} in endpoint interface {}\n",
                                .{ err, interface },
                            );
                        }
                    };
                }
            }
        }
        // if set, call the user-provided default callback
        if (on_request) |foo| {
            foo(r) catch |err| {
                // if error is not dealt with in the entpoint, e.g.
                // if error strategy is .raise:
                if (on_error) |error_cb| {
                    error_cb(r, err);
                } else {
                    zap.Logging.on_uncaught_error("Endpoint on_request", err);
                }
            };
        }
    }
};
