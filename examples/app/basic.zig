//!
//! Part of the Zap examples.
//!
//! Build me with `zig build     app_basic`.
//! Run   me with `zig build run-app_basic`.
//!
const std = @import("std");
const Allocator = std.mem.Allocator;

const zap = @import("zap");

// The global Application Context
const MyContext = struct {
    db_connection: []const u8,

    pub fn init(connection: []const u8) MyContext {
        return .{
            .db_connection = connection,
        };
    }
};

// A very simple endpoint handling only GET requests
const SimpleEndpoint = struct {

    // zap.App.Endpoint Interface part
    path: []const u8,
    error_strategy: zap.Endpoint.ErrorStrategy = .log_to_response,

    // data specific for this endpoint
    some_data: []const u8,

    pub fn init(path: []const u8, data: []const u8) SimpleEndpoint {
        return .{
            .path = path,
            .some_data = data,
        };
    }

    // handle GET requests
    pub fn get(e: *SimpleEndpoint, arena: Allocator, context: *MyContext, r: zap.Request) !void {
        const thread_id = std.Thread.getCurrentId();

        r.setStatus(.ok);

        // look, we use the arena allocator here -> no need to free the response_text later!
        // and we also just `try` it, not worrying about errors
        const response_text = try std.fmt.allocPrint(
            arena,
            \\Hello!
            \\context.db_connection: {s}
            \\endpoint.data: {s}
            \\arena: {}
            \\thread_id: {}
            \\
        ,
            .{ context.db_connection, e.some_data, arena.ptr, thread_id },
        );
        try r.sendBody(response_text);
        std.time.sleep(std.time.ns_per_ms * 300);
    }

    // empty stubs for all other request methods
    pub fn post(_: *SimpleEndpoint, _: Allocator, _: *MyContext, _: zap.Request) !void {}
    pub fn put(_: *SimpleEndpoint, _: Allocator, _: *MyContext, _: zap.Request) !void {}
    pub fn delete(_: *SimpleEndpoint, _: Allocator, _: *MyContext, _: zap.Request) !void {}
    pub fn patch(_: *SimpleEndpoint, _: Allocator, _: *MyContext, _: zap.Request) !void {}
    pub fn options(_: *SimpleEndpoint, _: Allocator, _: *MyContext, _: zap.Request) !void {}
};

pub fn main() !void {
    // setup allocations
    var gpa: std.heap.GeneralPurposeAllocator(.{
        // just to be explicit
        .thread_safe = true,
    }) = .{};
    defer std.debug.print("\n\nLeaks detected: {}\n\n", .{gpa.deinit() != .ok});
    const allocator = gpa.allocator();

    // create an app context
    var my_context = MyContext.init("db connection established!");

    // create an App instance
    const App = zap.App.Create(MyContext);
    var app = try App.init(allocator, &my_context, .{});
    defer app.deinit();

    // create the endpoint
    var my_endpoint = SimpleEndpoint.init("/", "some endpoint specific data");

    // register the endpoint with the app
    try app.register(&my_endpoint);

    // listen on the network
    try app.listen(.{
        .interface = "0.0.0.0",
        .port = 3000,
    });
    std.debug.print("Listening on 0.0.0.0:3000\n", .{});

    // start worker threads -- only 1 process!!!
    zap.start(.{
        .threads = 2,
        .workers = 1,
    });
}
