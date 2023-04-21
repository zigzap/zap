const std = @import("std");
const zap = @import("zap.zig");
const auth = @import("http_auth.zig");

const Request = zap.SimpleRequest;
const ListenerSettings = zap.SimpleHttpListenerSettings;
const Listener = zap.SimpleHttpListener;

pub const RequestFn = *const fn (self: *SimpleEndpoint, r: Request) void;
pub const SimpleEndpointSettings = struct {
    path: []const u8,
    get: ?RequestFn = null,
    post: ?RequestFn = null,
    put: ?RequestFn = null,
    delete: ?RequestFn = null,
    /// only applicable to AuthenticatingEndpoint
    unauthorized: ?RequestFn = null,
};

pub const SimpleEndpoint = struct {
    settings: SimpleEndpointSettings,

    const Self = @This();

    pub fn init(s: SimpleEndpointSettings) Self {
        return .{
            .settings = .{
                .path = s.path,
                .get = s.get orelse &nop,
                .post = s.post orelse &nop,
                .put = s.put orelse &nop,
                .delete = s.delete orelse &nop,
                .unauthorized = s.unauthorized orelse &nop,
            },
        };
    }

    fn nop(self: *SimpleEndpoint, r: Request) void {
        _ = self;
        _ = r;
    }

    pub fn onRequest(self: *SimpleEndpoint, r: zap.SimpleRequest) void {
        if (r.method) |m| {
            if (std.mem.eql(u8, m, "GET"))
                return self.settings.get.?(self, r);
            if (std.mem.eql(u8, m, "POST"))
                return self.settings.post.?(self, r);
            if (std.mem.eql(u8, m, "PUT"))
                return self.settings.put.?(self, r);
            if (std.mem.eql(u8, m, "DELETE"))
                return self.settings.delete.?(self, r);
        }
    }
};

/// Wrap an endpoint with an authenticator
pub fn AuthenticatingEndpoint(comptime Authenticator: type) type {
    return struct {
        authenticator: *Authenticator,
        endpoint: *SimpleEndpoint,
        auth_endpoint: SimpleEndpoint,
        const Self = @This();

        pub fn init(e: *SimpleEndpoint, authenticator: *Authenticator) Self {
            return .{
                .authenticator = authenticator,
                .endpoint = e,
                .auth_endpoint = SimpleEndpoint.init(.{
                    .path = e.settings.path,
                    // we override only the set ones. the other ones
                    // are set to null anyway -> will be nopped out
                    .get = if (e.settings.get != null) get else null,
                    .post = if (e.settings.post != null) post else null,
                    .put = if (e.settings.put != null) put else null,
                    .delete = if (e.settings.delete != null) delete else null,
                    .unauthorized = e.settings.unauthorized,
                }),
            };
        }

        /// get the auth endpoint struct so we can be stored in the listener
        /// when the listener calls the auth_endpoint, onRequest will have
        /// access to all of us via fieldParentPtr
        pub fn getEndpoint(self: *Self) *SimpleEndpoint {
            return &self.auth_endpoint;
        }

        /// here, the auth_endpoint will be passed in
        pub fn get(e: *SimpleEndpoint, r: zap.SimpleRequest) void {
            const authEp: *Self = @fieldParentPtr(Self, "auth_endpoint", e);
            if (authEp.authenticator.authenticateRequest(&r) == false) {
                if (e.settings.unauthorized) |foo| {
                    foo(e, r);
                    return;
                } else {
                    r.setStatus(.unauthorized);
                    r.sendBody("UNAUTHORIZED") catch return;
                    return;
                }
            }
            // auth successful
            authEp.endpoint.settings.get.?(e, r);
        }

        /// here, the auth_endpoint will be passed in
        pub fn post(e: *SimpleEndpoint, r: zap.SimpleRequest) void {
            const authEp: *Self = @fieldParentPtr(Self, "auth_endpoint", e);
            if (authEp.authenticator.authenticateRequest(&r) == false) {
                if (e.settings.unauthorized) |foo| {
                    foo(e, r);
                    return;
                } else {
                    r.setStatus(.unauthorized);
                    r.sendBody("UNAUTHORIZED") catch return;
                    return;
                }
            }
            // auth successful
            authEp.endpoint.settings.post.?(e, r);
        }

        /// here, the auth_endpoint will be passed in
        pub fn put(e: *SimpleEndpoint, r: zap.SimpleRequest) void {
            const authEp: *Self = @fieldParentPtr(Self, "auth_endpoint", e);
            if (authEp.authenticator.authenticateRequest(&r) == false) {
                if (e.settings.unauthorized) |foo| {
                    foo(e, r);
                    return;
                } else {
                    r.setStatus(.unauthorized);
                    r.sendBody("UNAUTHORIZED") catch return;
                    return;
                }
            }
            // auth successful
            authEp.endpoint.settings.put.?(e, r);
        }

        /// here, the auth_endpoint will be passed in
        pub fn delete(e: *SimpleEndpoint, r: zap.SimpleRequest) void {
            const authEp: *Self = @fieldParentPtr(Self, "auth_endpoint", e);
            if (authEp.authenticator.authenticateRequest(&r) == false) {
                if (e.settings.unauthorized) |foo| {
                    foo(e, r);
                    return;
                } else {
                    r.setStatus(.unauthorized);
                    r.sendBody("UNAUTHORIZED") catch return;
                    return;
                }
            }
            // auth successful
            authEp.endpoint.settings.delete.?(e, r);
        }
    };
}

pub const EndpointListenerError = error{
    EndpointPathShadowError,
};

// NOTE: We switch on path.startsWith -> so use endpoints with distinctly
// starting names!!
pub const SimpleEndpointListener = struct {
    listener: Listener,
    allocator: std.mem.Allocator,

    const Self = @This();

    /// static struct member endpoints
    var endpoints: std.ArrayList(*SimpleEndpoint) = undefined;

    pub fn init(a: std.mem.Allocator, l: ListenerSettings) Self {
        endpoints = std.ArrayList(*SimpleEndpoint).init(a);

        var ls = l; // take copy of listener settings
        ls.on_request = onRequest;

        return .{
            .listener = Listener.init(ls),
            .allocator = a,
        };
    }

    pub fn deinit(self: *Self) void {
        _ = self;
        endpoints.deinit();
    }

    pub fn listen(self: *Self) !void {
        try self.listener.listen();
    }

    pub fn addEndpoint(self: *Self, e: *SimpleEndpoint) !void {
        _ = self;
        for (endpoints.items) |other| {
            if (std.mem.startsWith(
                u8,
                other.settings.path,
                e.settings.path,
            ) or std.mem.startsWith(
                u8,
                e.settings.path,
                other.settings.path,
            )) {
                return EndpointListenerError.EndpointPathShadowError;
            }
        }
        try endpoints.append(e);
    }

    fn onRequest(r: Request) void {
        if (r.path) |p| {
            for (endpoints.items) |e| {
                if (std.mem.startsWith(u8, p, e.settings.path)) {
                    e.onRequest(r);
                    return;
                }
            }
        }
    }
};
