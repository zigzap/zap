const std = @import("std");
const zap = @import("zap.zig");
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

pub const EndpointListenerError = error{
    EndpointPathShadowError,
};

// var endpoints: std.StringHashMap(*SimpleEndpoint) = undefined;
var endpoints: std.ArrayList(*SimpleEndpoint) = undefined;

// NOTE: We switch on path.startsWith -> so use endpoints with distinctly
// starting names!!
pub const SimpleEndpointListener = struct {
    listener: Listener,
    allocator: std.mem.Allocator,

    const Self = @This();

    pub fn init(a: std.mem.Allocator, l: ListenerSettings) Self {
        var ls = l;
        ls.on_request = onRequest;
        endpoints = std.ArrayList(*SimpleEndpoint).init(a);
        return .{
            .listener = Listener.init(ls),
            .allocator = a,
        };
    }

    pub fn listen(self: *SimpleEndpointListener) !void {
        try self.listener.listen();
    }

    pub fn addEndpoint(self: *SimpleEndpointListener, e: *SimpleEndpoint) !void {
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
