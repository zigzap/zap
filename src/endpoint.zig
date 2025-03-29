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
//!     pub fn get(self: *StopEndpoint, r: zap.Request) void {
//!         _ = self;
//!         _ = r;
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
    inline for (methods_to_check) |method| {
        if (@hasDecl(T, method)) {
            if (@TypeOf(@field(T, method)) != fn (_: *T, _: Request) anyerror!void) {
                @compileError(method ++ " method of " ++ @typeName(T) ++ " has wrong type:\n" ++ @typeName(@TypeOf(T.get)) ++ "\nexpected:\n" ++ @typeName(fn (_: *T, _: Request) anyerror!void));
            }
        } else {
            @compileError(@typeName(T) ++ " has no method named `" ++ method ++ "`");
        }
    }
}

pub const Wrapper = struct {
    pub const Interface = struct {
        call: *const fn (*Interface, zap.Request) anyerror!void = undefined,
        path: []const u8,
        destroy: *const fn (allocator: std.mem.Allocator, *Interface) void = undefined,
    };
    pub fn Wrap(T: type) type {
        return struct {
            wrapped: *T,
            wrapper: Interface,

            const Wrapped = @This();

            pub fn unwrap(wrapper: *Interface) *Wrapped {
                const self: *Wrapped = @alignCast(@fieldParentPtr("wrapper", wrapper));
                return self;
            }

            pub fn destroy(allocator: std.mem.Allocator, wrapper: *Interface) void {
                const self: *Wrapped = @alignCast(@fieldParentPtr("wrapper", wrapper));
                allocator.destroy(self);
            }

            pub fn onRequestWrapped(wrapper: *Interface, r: zap.Request) !void {
                var self: *Wrapped = Wrapped.unwrap(wrapper);
                try self.onRequest(r);
            }

            pub fn onRequest(self: *Wrapped, r: zap.Request) !void {
                const ret = switch (r.methodAsEnum()) {
                    .GET => self.wrapped.*.get(r),
                    .POST => self.wrapped.*.post(r),
                    .PUT => self.wrapped.*.put(r),
                    .DELETE => self.wrapped.*.delete(r),
                    .PATCH => self.wrapped.*.patch(r),
                    .OPTIONS => self.wrapped.*.options(r),
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

    pub fn init(T: type, value: *T) Wrapper.Wrap(T) {
        checkEndpointType(T);
        var ret: Wrapper.Wrap(T) = .{
            .wrapped = value,
            .wrapper = .{ .path = value.path },
        };
        ret.wrapper.call = Wrapper.Wrap(T).onRequestWrapped;
        ret.wrapper.destroy = Wrapper.Wrap(T).destroy;
        return ret;
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
    var endpoints: std.ArrayListUnmanaged(*Wrapper.Interface) = .empty;

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
        for (endpoints.items) |endpoint_wrapper| {
            endpoint_wrapper.destroy(self.allocator, endpoint_wrapper);
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
        const wrapper = try self.allocator.create(Wrapper.Wrap(EndpointType));
        wrapper.* = Wrapper.init(EndpointType, e);
        try endpoints.append(self.allocator, &wrapper.wrapper);
    }

    fn onRequest(r: Request) !void {
        if (r.path) |p| {
            for (endpoints.items) |wrapper| {
                if (std.mem.startsWith(u8, p, wrapper.path)) {
                    return try wrapper.call(wrapper, r);
                }
            }
        }
        // if set, call the user-provided default callback
        if (on_request) |foo| {
            try foo(r);
        }
    }
};
