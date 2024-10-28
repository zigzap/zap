const std = @import("std");
const zap = @import("zap");

// just a way to share our allocator via callback
const SharedAllocator = struct {
    // static
    var allocator: std.mem.Allocator = undefined;

    const Self = @This();

    // just a convenience function
    pub fn init(a: std.mem.Allocator) void {
        allocator = a;
    }

    // static function we can pass to the listener later
    pub fn getAllocator() std.mem.Allocator {
        return allocator;
    }
};

// create a combined context struct
// NOTE: context struct members need to be optionals which default to null!!!
const Context = struct {
    user: ?UserMiddleWare.User = null,
    session: ?SessionMiddleWare.Session = null,
};

// we create a Handler type based on our Context
const Handler = zap.Middleware.Handler(Context);

//
// Example user middleware: puts user info into the context
//
const UserMiddleWare = struct {
    handler: Handler,

    const Self = @This();

    // Just some arbitrary struct we want in the per-request context
    // This is so that it can be constructed via .{}
    // as we can't expect the listener to know how to initialize our context structs
    const User = struct {
        name: []const u8 = undefined,
        email: []const u8 = undefined,
    };

    pub fn init(other: ?*Handler) Self {
        return .{
            .handler = Handler.init(onRequest, other),
        };
    }

    // we need the handler as a common interface to chain stuff
    pub fn getHandler(self: *Self) *Handler {
        return &self.handler;
    }

    // note that the first parameter is of type *Handler, not *Self !!!
    pub fn onRequest(handler: *Handler, r: zap.Request, context: *Context) bool {

        // this is how we would get our self pointer
        const self: *Self = @fieldParentPtr("handler", handler);
        _ = self;

        // do our work: fill in the user field of the context
        context.user = User{
            .name = "renerocksai",
            .email = "supa@secret.org",
        };

        std.debug.print("\n\nUser Middleware: set user in context {any}\n\n", .{context.user});

        // continue in the chain
        return handler.handleOther(r, context);
    }
};

//
// Example session middleware: puts session info into the context
//
const SessionMiddleWare = struct {
    handler: Handler,

    const Self = @This();

    // Just some arbitrary struct we want in the per-request context
    // note: it MUST have all default values!!!
    const Session = struct {
        info: []const u8 = undefined,
        token: []const u8 = undefined,
    };

    pub fn init(other: ?*Handler) Self {
        return .{
            .handler = Handler.init(onRequest, other),
        };
    }

    // we need the handler as a common interface to chain stuff
    pub fn getHandler(self: *Self) *Handler {
        return &self.handler;
    }

    // note that the first parameter is of type *Handler, not *Self !!!
    pub fn onRequest(handler: *Handler, r: zap.Request, context: *Context) bool {
        // this is how we would get our self pointer
        const self: *Self = @fieldParentPtr("handler", handler);
        _ = self;

        context.session = Session{
            .info = "secret session",
            .token = "rot47-asdlkfjsaklfdj",
        };

        std.debug.print("\n\nSessionMiddleware: set session in context {any}\n\n", .{context.session});

        // continue in the chain
        return handler.handleOther(r, context);
    }
};

//
// !!!! ENDPOINT !!!
//
// We define an endpoint as we usually would; however,
// NO ROUTING IS PERFORMED BY DEFAULT!
//
// This can be changed using the EndpointHandlerOptions passed into the
// EndpointHandler.init() method.
//
// N.B. the EndpointHandler checks if the endpoint turned the request into
// "finished" state, e.g. by sending anything. If the endpoint didn't finish the
// request, the EndpointHandler will pass the request on to the next handler in
// the chain if there is one. See also the EndpointHandlerOptions's
// `breakOnFinish` parameter.
//
const HtmlEndpoint = struct {
    ep: zap.Endpoint = undefined,
    const Self = @This();

    pub fn init() Self {
        return .{
            .ep = zap.Endpoint.init(.{
                .path = "/doesn't+matter",
                .get = get,
                .unset = zap.Endpoint.dummy_handler,
            }),
        };
    }

    pub fn endpoint(self: *Self) *zap.Endpoint {
        return &self.ep;
    }

    pub fn get(ep: *zap.Endpoint, r: zap.Request) void {
        const self: *Self = @fieldParentPtr("ep", ep);
        _ = self;

        var buf: [1024]u8 = undefined;
        var userFound: bool = false;
        var sessionFound: bool = false;

        // the EndpointHandler set this for us!
        // we got middleware context!!!
        const maybe_context: ?*Context = r.getUserContext(Context);
        if (maybe_context) |context| {
            std.debug.print("\n\nHtmlEndpoint: handling request with context: {any}\n\n", .{context});

            if (context.user) |user| {
                userFound = true;
                if (context.session) |session| {
                    sessionFound = true;

                    std.debug.assert(r.isFinished() == false);
                    const message = std.fmt.bufPrint(&buf, "User: {s} / {s}, Session: {s} / {s}", .{
                        user.name,
                        user.email,
                        session.info,
                        session.token,
                    }) catch unreachable;
                    r.setContentType(.TEXT) catch unreachable;
                    r.sendBody(message) catch unreachable;
                    std.debug.assert(r.isFinished() == true);
                    return;
                }
            }
        }

        const message = std.fmt.bufPrint(&buf, "User info found: {}, session info found: {}", .{ userFound, sessionFound }) catch unreachable;

        r.setContentType(.TEXT) catch unreachable;
        r.sendBody(message) catch unreachable;
        return;
    }
};

pub fn main() !void {
    var gpa = std.heap.GeneralPurposeAllocator(.{
        .thread_safe = true,
    }){};
    const allocator = gpa.allocator();
    SharedAllocator.init(allocator);

    // create the endpoint
    var htmlEndpoint = HtmlEndpoint.init();

    // we wrap the endpoint with a middleware handler
    var htmlHandler = zap.Middleware.EndpointHandler(Handler, Context).init(
        htmlEndpoint.endpoint(), // the endpoint
        null, // no other handler (we are the last in the chain)
        .{}, // We can set custom EndpointHandlerOptions here
    );

    // we wrap it in the session Middleware component
    var sessionHandler = SessionMiddleWare.init(htmlHandler.getHandler());

    // we wrap that in the user Middleware component
    var userHandler = UserMiddleWare.init(sessionHandler.getHandler());

    // we create a listener with our combined context
    // and pass it the initial handler: the user handler
    var listener = try zap.Middleware.Listener(Context).init(
        .{
            .on_request = null, // must be null
            .port = 3000,
            .log = true,
            .max_clients = 100000,
        },
        userHandler.getHandler(),
        SharedAllocator.getAllocator,
    );
    zap.enableDebugLog();
    listener.listen() catch |err| {
        std.debug.print("\nLISTEN ERROR: {any}\n", .{err});
        return;
    };

    std.debug.print("Visit me on http://127.0.0.1:3000\n", .{});

    // start worker threads
    zap.start(.{
        .threads = 2,
        .workers = 1,
    });
}
