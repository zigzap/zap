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
const Context = struct {
    user: ?UserMiddleWare.User = null,
    session: ?SessionMiddleWare.Session = null,
};

// we create a Handler type based on our Context
const Handler = zap.Middleware.Handler(Context);

//
// ZIG-CEPTION!!!
//
// Note how amazing zig is:
// - we create the "mixed" context based on the both middleware structs
// - we create the handler based on this context
// - we create the middleware structs based on the handler
//     - which needs the context
//     - which needs the middleware structs
//     - ZIG-CEPTION!

// Example user middleware: puts user info into the context
const UserMiddleWare = struct {
    handler: Handler,

    const Self = @This();

    // Just some arbitrary struct we want in the per-request context
    // note: it MUST have all default values!!!
    // note: it MUST have all default values!!!
    // note: it MUST have all default values!!!
    // note: it MUST have all default values!!!
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
    pub fn onRequest(handler: *Handler, r: zap.SimpleRequest, context: *Context) bool {

        // this is how we would get our self pointer
        var self = @fieldParentPtr(Self, "handler", handler);
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

// Example session middleware: puts session info into the context
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
    pub fn onRequest(handler: *Handler, r: zap.SimpleRequest, context: *Context) bool {
        // this is how we would get our self pointer
        var self = @fieldParentPtr(Self, "handler", handler);
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

// Example html middleware: handles the request and sends a response
const HtmlMiddleWare = struct {
    handler: Handler,

    const Self = @This();

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
    pub fn onRequest(handler: *Handler, r: zap.SimpleRequest, context: *Context) bool {

        // this is how we would get our self pointer
        var self = @fieldParentPtr(Self, "handler", handler);
        _ = self;

        std.debug.print("\n\nHtmlMiddleware: handling request with context: {any}\n\n", .{context});

        var buf: [1024]u8 = undefined;
        var userFound: bool = false;
        var sessionFound: bool = false;
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
                return true;
            }
        }

        const message = std.fmt.bufPrint(&buf, "User info found: {}, session info found: {}", .{ userFound, sessionFound }) catch unreachable;

        r.setContentType(.TEXT) catch unreachable;
        r.sendBody(message) catch unreachable;
        return true;
    }
};

pub fn main() !void {
    var gpa = std.heap.GeneralPurposeAllocator(.{
        .thread_safe = true,
    }){};
    var allocator = gpa.allocator();
    SharedAllocator.init(allocator);

    // we create our HTML middleware component that handles the request
    var htmlHandler = HtmlMiddleWare.init(null);

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
