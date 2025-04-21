//! Endpoint and supporting types.
//!
//! An Endpoint can be any zig struct that defines all the callbacks lilsted
//! below.
//! Pass an instance of an Endpoint struct  to zap.Endpoint.Listener.register()
//! function to register with the listener.
//!
//! **NOTE**: Endpoints must implement the following "interface":
//!
//! ```zig
//! /// The http request path / slug of the endpoint
//! path: []const u8,
//!
//! /// Handlers by request method:
//! pub fn get(_: *Self, _: zap.Request) void {}
//! pub fn post(_: *Self, _: zap.Request) void {}
//! pub fn put(_: *Self, _: zap.Request) void {}
//! pub fn delete(_: *Self, _: zap.Request) void {}
//! pub fn patch(_: *Self, _: zap.Request) void {}
//! pub fn options(_: *Self, _: zap.Request) void {}
//!
//! // optional, if auth stuff is used:
//! pub fn unauthorized(_: *Self, _: zap.Request) void {}
//! ```
//!
//! Example:
//! A simple endpoint listening on the /stop route that shuts down zap. The
//! main thread usually continues at the instructions after the call to
//! zap.start().
//!
//! ```zig
//! const StopEndpoint = struct {
//!
//!     pub fn init( path: []const u8,) StopEndpoint {
//!         return .{
//!             .path = path,
//!         };
//!     }
//!
//!     pub fn post(_: *StopEndpoint, _: zap.Request) void {}
//!     pub fn put(_: *StopEndpoint, _: zap.Request) void {}
//!     pub fn delete(_: *StopEndpoint, _: zap.Request) void {}
//!     pub fn patch(_: *StopEndpoint, _: zap.Request) void {}
//!     pub fn options(_: *StopEndpoint, _: zap.Request) void {}
//!
//!     pub fn get(_: *StopEndpoint, _: zap.Request) void {
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
    /// log errors to console AND generate a HTML response
    log_to_response,
    /// raise errors -> TODO: clarify: where can they be caught? in App.run()
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
            @compileError(@typeName(T) ++ " has no method named `" ++ method ++ "`");
        }
    }
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
                    .GET => self.endpoint.*.get(r),
                    .POST => self.endpoint.*.post(r),
                    .PUT => self.endpoint.*.put(r),
                    .DELETE => self.endpoint.*.delete(r),
                    .PATCH => self.endpoint.*.patch(r),
                    .OPTIONS => self.endpoint.*.options(r),
                    else => error.UnsupportedHtmlRequestMethod,
                };
                if (ret) {
                    // handled without error
                } else |err| {
                    switch (self.endpoint.*.error_strategy) {
                        .raise => return err,
                        .log_to_response => return r.sendError(err, if (@errorReturnTrace()) |t| t.* else null, 505),
                        .log_to_console => zap.debug("Error in {} {s} : {}", .{ Bound, r.method orelse "(no method)", err }),
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
                .AuthFailed => return self.ep.*.unauthorized(r),
                .AuthOK => self.ep.*.get(r),
                .Handled => {},
            };
        }

        /// Authenticates POST requests using the Authenticator.
        pub fn post(self: *AuthenticatingEndpoint, r: zap.Request) anyerror!void {
            try switch (self.authenticator.authenticateRequest(&r)) {
                .AuthFailed => return self.ep.*.unauthorized(r),
                .AuthOK => self.ep.*.post(r),
                .Handled => {},
            };
        }

        /// Authenticates PUT requests using the Authenticator.
        pub fn put(self: *AuthenticatingEndpoint, r: zap.Request) anyerror!void {
            try switch (self.authenticator.authenticateRequest(&r)) {
                .AuthFailed => return self.ep.*.unauthorized(r),
                .AuthOK => self.ep.*.put(r),
                .Handled => {},
            };
        }

        /// Authenticates DELETE requests using the Authenticator.
        pub fn delete(self: *AuthenticatingEndpoint, r: zap.Request) anyerror!void {
            try switch (self.authenticator.authenticateRequest(&r)) {
                .AuthFailed => return self.ep.*.unauthorized(r),
                .AuthOK => self.ep.*.delete(r),
                .Handled => {},
            };
        }

        /// Authenticates PATCH requests using the Authenticator.
        pub fn patch(self: *AuthenticatingEndpoint, r: zap.Request) anyerror!void {
            try switch (self.authenticator.authenticateRequest(&r)) {
                .AuthFailed => return self.ep.*.unauthorized(r),
                .AuthOK => self.ep.*.patch(r),
                .Handled => {},
            };
        }

        /// Authenticates OPTIONS requests using the Authenticator.
        pub fn options(self: *AuthenticatingEndpoint, r: zap.Request) anyerror!void {
            try switch (self.authenticator.authenticateRequest(&r)) {
                .AuthFailed => return self.ep.*.unauthorized(r),
                .AuthOK => self.ep.*.put(r),
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

    /// Internal static interface struct of member endpoints
    var endpoints: std.ArrayListUnmanaged(*Binder.Interface) = .empty;

    /// Internal, static request handler callback. Will be set to the optional,
    /// user-defined request callback that only gets called if no endpoints match
    /// a request.
    var on_request: ?zap.HttpRequestFn = null;

    /// Initialize a new endpoint listener. Note, if you pass an `on_request`
    /// callback in the provided ListenerSettings, this request callback will be
    /// called every time a request arrives that no endpoint matches.
    pub fn init(a: std.mem.Allocator, l: ListenerSettings) Listener {
        // reset the global in case init is called multiple times, as is the
        // case in the authentication tests
        endpoints = .empty;

        // take copy of listener settings before modifying the callback field
        var ls = l;

        // override the settings with our internal, actual callback function
        // so that "we" will be called on request
        ls.on_request = Listener.onRequest;

        // store the settings-provided request callback for later use
        on_request = l.on_request;
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
                    return try interface.call(interface, r);
                }
            }
        }
        // if set, call the user-provided default callback
        if (on_request) |foo| {
            try foo(r);
        }
    }
};
