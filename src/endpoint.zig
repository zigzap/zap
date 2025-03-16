//! Endpoint and supporting types.
//! Create one and define all callbacks. Then, pass it to a HttpListener's
//! `register()` function to register with the listener.
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
//!     pub fn post(_: *Self, _: zap.Request) void {}
//!     pub fn put(_: *Self, _: zap.Request) void {}
//!     pub fn delete(_: *Self, _: zap.Request) void {}
//!     pub fn patch(_: *Self, _: zap.Request) void {}
//!     pub fn options(_: *Self, _: zap.Request) void {}
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
            if (@TypeOf(@field(T, method)) != fn (_: *T, _: Request) void) {
                @compileError(method ++ " method of " ++ @typeName(T) ++ " has wrong type:\n" ++ @typeName(@TypeOf(T.get)) ++ "\nexpected:\n" ++ @typeName(fn (_: *T, _: Request) void));
            }
        } else {
            @compileError(@typeName(T) ++ " has no method named `" ++ method ++ "`");
        }
    }
}

const EndpointWrapper = struct {
    pub const Wrapper = struct {
        call: *const fn (*Wrapper, zap.Request) void = undefined,
        path: []const u8,
        destroy: *const fn (allocator: std.mem.Allocator, *Wrapper) void = undefined,
    };
    pub fn Wrap(T: type) type {
        return struct {
            wrapped: *T,
            wrapper: Wrapper,

            const Self = @This();

            pub fn unwrap(wrapper: *Wrapper) *Self {
                const self: *Self = @alignCast(@fieldParentPtr("wrapper", wrapper));
                return self;
            }

            pub fn destroy(allocator: std.mem.Allocator, wrapper: *Wrapper) void {
                const self: *Self = @alignCast(@fieldParentPtr("wrapper", wrapper));
                allocator.destroy(self);
            }

            pub fn onRequestWrapped(wrapper: *Wrapper, r: zap.Request) void {
                var self: *Self = Self.unwrap(wrapper);
                self.onRequest(r);
            }

            pub fn onRequest(self: *Self, r: zap.Request) void {
                switch (r.methodAsEnum()) {
                    .GET => return self.wrapped.*.get(r),
                    .POST => return self.wrapped.*.post(r),
                    .PUT => return self.wrapped.*.put(r),
                    .DELETE => return self.wrapped.*.delete(r),
                    .PATCH => return self.wrapped.*.patch(r),
                    .OPTIONS => return self.wrapped.*.options(r),
                    else => {
                        // TODO: log that method is ignored
                    },
                }
            }
        };
    }

    pub fn init(T: type, value: *T) EndpointWrapper.Wrap(T) {
        checkEndpointType(T);
        var ret: EndpointWrapper.Wrap(T) = .{
            .wrapped = value,
            .wrapper = .{ .path = value.path },
        };
        ret.wrapper.call = EndpointWrapper.Wrap(T).onRequestWrapped;
        ret.wrapper.destroy = EndpointWrapper.Wrap(T).destroy;
        return ret;
    }
};

/// Wrap an endpoint with an Authenticator
pub fn Authenticating(EndpointType: type, Authenticator: type) type {
    return struct {
        authenticator: *Authenticator,
        ep: *EndpointType,
        path: []const u8,
        const Self = @This();

        /// Init the authenticating endpoint. Pass in a pointer to the endpoint
        /// you want to wrap, and the Authenticator that takes care of authenticating
        /// requests.
        pub fn init(e: *EndpointType, authenticator: *Authenticator) Self {
            return .{
                .authenticator = authenticator,
                .ep = e,
                .path = e.path,
            };
        }

        /// Authenticates GET requests using the Authenticator.
        pub fn get(self: *Self, r: zap.Request) void {
            switch (self.authenticator.authenticateRequest(&r)) {
                .AuthFailed => return self.ep.*.unauthorized(r),
                .AuthOK => self.ep.*.get(r),
                .Handled => {},
            }
        }

        /// Authenticates POST requests using the Authenticator.
        pub fn post(self: *Self, r: zap.Request) void {
            switch (self.authenticator.authenticateRequest(&r)) {
                .AuthFailed => return self.ep.*.unauthorized(r),
                .AuthOK => self.ep.*.post(r),
                .Handled => {},
            }
        }

        /// Authenticates PUT requests using the Authenticator.
        pub fn put(self: *Self, r: zap.Request) void {
            switch (self.authenticator.authenticateRequest(&r)) {
                .AuthFailed => return self.ep.*.unauthorized(r),
                .AuthOK => self.ep.*.put(r),
                .Handled => {},
            }
        }

        /// Authenticates DELETE requests using the Authenticator.
        pub fn delete(self: *Self, r: zap.Request) void {
            switch (self.authenticator.authenticateRequest(&r)) {
                .AuthFailed => return self.ep.*.unauthorized(r),
                .AuthOK => self.ep.*.delete(r),
                .Handled => {},
            }
        }

        /// Authenticates PATCH requests using the Authenticator.
        pub fn patch(self: *Self, r: zap.Request) void {
            switch (self.authenticator.authenticateRequest(&r)) {
                .AuthFailed => return self.ep.*.unauthorized(r),
                .AuthOK => self.ep.*.patch(r),
                .Handled => {},
            }
        }

        /// Authenticates OPTIONS requests using the Authenticator.
        pub fn options(self: *Self, r: zap.Request) void {
            switch (self.authenticator.authenticateRequest(&r)) {
                .AuthFailed => return self.ep.*.unauthorized(r),
                .AuthOK => self.ep.*.put(r),
                .Handled => {},
            }
        }
    };
}

pub const EndpointListenerError = error{
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

    const Self = @This();

    /// Internal static struct of member endpoints
    var endpoints: std.ArrayListUnmanaged(*EndpointWrapper.Wrapper) = .empty;

    /// Internal, static request handler callback. Will be set to the optional,
    /// user-defined request callback that only gets called if no endpoints match
    /// a request.
    var on_request: ?zap.HttpRequestFn = null;

    /// Initialize a new endpoint listener. Note, if you pass an `on_request`
    /// callback in the provided ListenerSettings, this request callback will be
    /// called every time a request arrives that no endpoint matches.
    pub fn init(a: std.mem.Allocator, l: ListenerSettings) Self {
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
    pub fn deinit(self: *Self) void {
        for (endpoints.items) |endpoint_wrapper| {
            endpoint_wrapper.destroy(self.allocator, endpoint_wrapper);
        }
        endpoints.deinit(self.allocator);
    }

    /// Call this to start listening. After this, no more endpoints can be
    /// registered.
    pub fn listen(self: *Self) !void {
        try self.listener.listen();
    }

    /// Register an endpoint with this listener.
    /// NOTE: endpoint paths are matched with startsWith -> so use endpoints with distinctly starting names!!
    /// If you try to register an endpoint whose path would shadow an already registered one, you will
    /// receive an EndpointPathShadowError.
    pub fn register(self: *Self, e: anytype) !void {
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
                return EndpointListenerError.EndpointPathShadowError;
            }
        }
        const EndpointType = @typeInfo(@TypeOf(e)).pointer.child;
        checkEndpointType(EndpointType);
        const wrapper = try self.allocator.create(EndpointWrapper.Wrap(EndpointType));
        wrapper.* = EndpointWrapper.init(EndpointType, e);
        try endpoints.append(self.allocator, &wrapper.wrapper);
    }

    fn onRequest(r: Request) void {
        if (r.path) |p| {
            for (endpoints.items) |wrapper| {
                if (std.mem.startsWith(u8, p, wrapper.path)) {
                    wrapper.call(wrapper, r);
                    return;
                }
            }
        }
        // if set, call the user-provided default callback
        if (on_request) |foo| {
            foo(r);
        }
    }
};
