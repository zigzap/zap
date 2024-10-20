const std = @import("std");
const zap = @import("zap.zig");

/// Your middleware components need to contain a handler.
///
/// A Handler is one element in the chain of request handlers that will be tried
/// by the listener when a request arrives. Handlers indicate to the previous
/// handler whether they processed a request by returning `true` from their
/// `on_request` function, in which case a typical request handler would stop
/// trying to pass the request on to the next handler in the chain. See
/// the `handle_other` function in this struct.
pub fn Handler(comptime ContextType: anytype) type {
    return struct {
        other_handler: ?*Self = null,
        on_request: ?RequestFn = null,

        // will be set
        allocator: ?std.mem.Allocator = null,

        pub const RequestFn = *const fn (*Self, zap.Request, *ContextType) bool;
        const Self = @This();

        pub fn init(on_request: RequestFn, other: ?*Self) Self {
            return .{
                .other_handler = other,
                .on_request = on_request,
            };
        }

        // example for handling a request request
        // which you can use in your components, e.g.:
        // return self.handler.handleOther(r, context);
        pub fn handleOther(self: *Self, r: zap.Request, context: *ContextType) bool {
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

/// Options used to change the behavior of an `EndpointHandler`
pub const EndpointHandlerOptions = struct {
    /// If `true`, the handler will stop handing requests down the chain if the
    /// endpoint processed the request.
    breakOnFinish: bool = true,

    /// If `true`, the handler will only execute against requests that match
    /// the endpoint's `path` setting.
    checkPath: bool = false,
};

/// A convenience handler for artibrary zap.Endpoint
pub fn EndpointHandler(comptime HandlerType: anytype, comptime ContextType: anytype) type {
    return struct {
        handler: HandlerType,
        endpoint: *zap.Endpoint,
        options: EndpointHandlerOptions,

        const Self = @This();

        /// Create an endpointhandler from an endpoint and pass in the next (other) handler in the chain.
        ///
        /// By default no routing is performed on requests. This behavior can be changed by setting
        /// `checkPath` in the provided options.
        ///
        /// If the `breakOnFinish` option is `true`, the handler will stop handing requests down the chain
        /// if the endpoint processed the request.
        pub fn init(endpoint: *zap.Endpoint, other: ?*HandlerType, options: EndpointHandlerOptions) Self {
            return .{
                .handler = HandlerType.init(onRequest, other),
                .endpoint = endpoint,
                .options = options,
            };
        }

        /// Provides the handler as a common interface to chain stuff
        pub fn getHandler(self: *Self) *HandlerType {
            return &self.handler;
        }

        /// The Handler's request handling function. Gets called from the listener
        /// with the request and a context instance. Calls the endpoint.
        ///
        /// If `breakOnFinish` is `true`, the handler will stop handing requests down the chain if
        /// the endpoint processed the request.
        pub fn onRequest(handler: *HandlerType, r: zap.Request, context: *ContextType) bool {
            const self: *Self = @fieldParentPtr("handler", handler);
            r.setUserContext(context);
            if (!self.options.checkPath or
                std.mem.startsWith(u8, r.path orelse "", self.endpoint.settings.path))
            {
                self.endpoint.onRequest(r);
            }

            // if the request was handled by the endpoint, we may break the chain here
            if (r.isFinished() and self.options.breakOnFinish) {
                return true;
            }
            return self.handler.handleOther(r, context);
        }
    };
}

pub const Error = error{
    /// The listener could not be created because the settings provided to its
    /// init() function contained an `on_request` callback that was not null.
    InitOnRequestIsNotNull,
};

pub const RequestAllocatorFn = *const fn () std.mem.Allocator;

/// Special Listener that supports chaining request handlers.
pub fn Listener(comptime ContextType: anytype) type {
    return struct {
        listener: zap.HttpListener = undefined,
        settings: zap.HttpListenerSettings,

        // static initial handler
        var handler: ?*Handler(ContextType) = undefined;
        // static allocator getter
        var requestAllocator: ?RequestAllocatorFn = null;

        const Self = @This();

        /// Construct and initialize a middleware handler.
        /// The passed in settings must have on_request set to null! If that is
        /// not the case, an InitOnRequestIsNotNull error will be returned.
        pub fn init(settings: zap.HttpListenerSettings, initial_handler: *Handler(ContextType), request_alloc: ?RequestAllocatorFn) Error!Self {
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
            ret.listener = zap.HttpListener.init(ret.settings);
            handler = initial_handler;
            return ret;
        }

        /// Start listening.
        pub fn listen(self: *Self) !void {
            try self.listener.listen();
        }

        /// The listener's request handler, stepping through the chain of Handlers
        /// by calling the initial one which takes it from there.
        ///
        /// This is just a reference implementation that you can use by default.
        /// Create your own listener if you want different behavior.
        /// (Didn't want to make this a callback. Submit an issue if you really
        /// think that's an issue).
        pub fn onRequest(r: zap.Request) void {
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
