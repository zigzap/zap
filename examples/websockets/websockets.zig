const std = @import("std");
const zap = @import("zap");
const WebSockets = zap.WebSockets;

const Context = struct {
    userName: []const u8,
    channel: []const u8,
    // we need to hold on to them and just re-use them for every incoming connection
    subscribeArgs: WebsocketHandler.SubscribeArgs,
    settings: WebsocketHandler.WebSocketSettings,
};

const ContextList = std.ArrayList(*Context);

const ContextManager = struct {
    allocator: std.mem.Allocator,
    channel: []const u8,
    usernamePrefix: []const u8,
    lock: std.Thread.Mutex = .{},
    contexts: ContextList = undefined,

    const Self = @This();

    pub fn init(allocator: std.mem.Allocator, channelName: []const u8, usernamePrefix: []const u8) Self {
        return .{
            .allocator = allocator,
            .channel = channelName,
            .usernamePrefix = usernamePrefix,
            .contexts = ContextList.init(allocator),
        };
    }

    pub fn deinit(self: *Self) void {
        for (self.contexts.items) |ctx| {
            self.allocator.free(ctx.userName);
        }
        self.contexts.deinit();
    }

    pub fn newContext(self: *Self) !*Context {
        self.lock.lock();
        defer self.lock.unlock();

        var ctx = try self.allocator.create(Context);
        var userName = try std.fmt.allocPrint(self.allocator, "{s}{d}", .{ self.usernamePrefix, self.contexts.items.len });
        ctx.* = .{
            .userName = userName,
            .channel = self.channel,
            // used in subscribe()
            .subscribeArgs = .{
                .channel = self.channel,
                .force_text = true,
                .context = ctx,
            },
            // used in upgrade()
            .settings = .{
                .on_open = on_open_websocket,
                .on_close = on_close_websocket,
                .on_message = handle_websocket_message,
                .context = ctx,
            },
        };
        try self.contexts.append(ctx);
        return ctx;
    }
};

//
// Websocket Callbacks
//
fn on_open_websocket(context: ?*Context, handle: WebSockets.WsHandle) void {
    if (context) |ctx| {
        _ = WebsocketHandler.subscribe(handle, &ctx.subscribeArgs) catch |err| {
            std.log.err("Error opening websocket: {any}", .{err});
            return;
        };

        // say hello
        var buf: [128]u8 = undefined;
        const message = std.fmt.bufPrint(&buf, "{s} joined the chat.", .{ctx.userName}) catch unreachable;

        // send notification to all others
        WebsocketHandler.publish(.{ .channel = ctx.channel, .message = message });
        std.log.info("new websocket opened: {s}", .{message});
    }
}

fn on_close_websocket(context: ?*Context, uuid: isize) void {
    _ = uuid;
    if (context) |ctx| {
        // say goodbye
        var buf: [128]u8 = undefined;
        const message = std.fmt.bufPrint(&buf, "{s} left the chat.", .{ctx.userName}) catch unreachable;

        // send notification to all others
        WebsocketHandler.publish(.{ .channel = ctx.channel, .message = message });
        std.log.info("websocket closed: {s}", .{message});
    }
}
fn handle_websocket_message(context: ?*Context, handle: WebSockets.WsHandle, message: []const u8, is_text: bool) void {
    _ = is_text;
    _ = handle;
    if (context) |ctx| {
        // say goodbye
        var buf: [128]u8 = undefined;
        const chat_message = std.fmt.bufPrint(&buf, "{s}: {s}", .{ ctx.userName, message }) catch unreachable;

        // send notification to all others
        WebsocketHandler.publish(.{ .channel = ctx.channel, .message = chat_message });
        std.log.info("{s}", .{chat_message});
    }
}

//
// HTTP stuff
//
fn on_request(r: zap.SimpleRequest) void {
    r.setHeader("Server", "zap.example") catch unreachable;
    r.sendBody("<html><body><h1>This is a simple Websocket chatroom example</h1></body></html>") catch return;
}

fn on_upgrade(r: zap.SimpleRequest, target_protocol: []const u8) void {
    // make sure we're talking the right protocol
    if (!std.mem.eql(u8, target_protocol, "websocket")) {
        std.log.warn("received illegal target protocol: {s}", .{target_protocol});
        r.setStatus(.bad_request);
        r.sendBody("400 - BAD REQUEST") catch unreachable;
        return;
    }
    var context = GlobalContextManager.newContext() catch |err| {
        std.log.err("Error creating context: {any}", .{err});
        return;
    };

    WebsocketHandler.upgrade(r.h, &context.settings) catch |err| {
        std.log.err("Error in websocketUpgrade(): {any}", .{err});
        return;
    };
    std.log.info("connection upgrade OK", .{});
}

// global variables, yeah!
var GlobalContextManager: ContextManager = undefined;

const WebsocketHandler = WebSockets.Handler(Context);
var handler_instance: WebsocketHandler = .{};

// here we go
pub fn main() !void {
    var gpa = std.heap.GeneralPurposeAllocator(.{
        .thread_safe = true,
    }){};
    var allocator = gpa.allocator();

    GlobalContextManager = ContextManager.init(allocator, "chatroom", "user-");
    defer GlobalContextManager.deinit();

    // setup listener
    var listener = zap.SimpleHttpListener.init(
        .{
            .port = 3010,
            .on_request = on_request,
            .on_upgrade = on_upgrade,
            .max_clients = 1000,
            .max_body_size = 1 * 1024,
            .public_folder = "examples/websockets/frontend",
            .log = true,
        },
    );
    try listener.listen();
    std.log.info("", .{});
    std.log.info("Connect with browser to http://localhost:3010.", .{});
    std.log.info("Connect to websocket on ws://localhost:3010.", .{});
    std.log.info("Terminate with CTRL+C", .{});

    zap.start(.{
        .threads = 1,
        .workers = 1,
    });
}
