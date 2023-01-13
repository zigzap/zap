const std = @import("std");
const zap = @import("zap.zig");
const Request = zap.SimpleRequest;
const RequestFn = zap.SimpleHttpRequestFn;
const ListenerSettings = zap.SimpleHttpListenerSettings;
const Listener = zap.SimpleHttpListener;

const SimpleEndpointSettings = struct {
    path: []const u8,
    get: ?RequestFn,
    post: ?RequestFn,
    put: ?RequestFn,
    delete: ?RequestFn,
};

const SimpleEndpoint = struct {
    settings: SimpleEndpointSettings,

    var Self = @This();

    pub fn init(s: SimpleEndpointSettings) Self {
        return .{
            .path = s.path,
            .get = s.get orelse &nop,
            .post = s.post orelse &nop,
            .put = s.put orelse &nop,
            .delete = s.delete orelse &nop,
        };
    }

    fn nop(r: Request) void {
        _ = r;
    }

    pub fn onRequest(r: zap.SimpleRequest) void {
        if (r.method) |m| {
            if (std.mem.eql(u8, m, "GET"))
                // TODO
                nop();
        }
    }
};

const EndpointListenerError = error{
    EndpointPathShadowError,
};

var endpoints: std.StringHashMap(*SimpleEndpoint) = undefined;

// NOTE: We switch on path.startsWith -> so use endpoints with distinctly
// starting names!!
const SimpleEndpointListener = struct {
    listener: Listener,
    allocator: std.mem.Allocator,

    var Self = @This();

    pub fn init(a: std.mem.Allocator, l: ListenerSettings) Self {
        l.on_request = onRequest;
        endpoints = std.StringHashMap(*SimpleEndpoint).init(a);
        return .{
            .listener = Listener.init(l),
            .allocator = a,
        };
    }

    pub fn addEndpoint(self: *SimpleEndpointListener, e: *SimpleEndpoint) !void {
        var it = endpoints.keyIterator();
        while (it.next()) |existing_path| {
            if (std.mem.startsWith(
                u8,
                existing_path,
                e.path,
            ) or std.mem.startsWith(
                u8,
                e.path,
                existing_path,
            )) {
                return EndpointListenerError.EndpointPathShadowError;
            }
        }
        try self.endpoints.put(e.path, e);
    }

    fn onRequest(r: Request) void {
        if (r.path) |p| {
            for (endpoints) |e| {
                if (std.mem.startsWith(u8, p, e.path)) {
                    e.onRequest(r);
                    return;
                }
            }
        }
    }
};
