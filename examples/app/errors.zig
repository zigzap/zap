//!
//! Part of the Zap examples.
//!
//! Build me with `zig build     app_errors`.
//! Run   me with `zig build run-app_errors`.
//!
const std = @import("std");
const Allocator = std.mem.Allocator;

const zap = @import("zap");

// The global Application Context
const MyContext = struct {
    db_connection: []const u8,

    // we don't use this
    pub fn unhandledRequest(_: *MyContext, _: Allocator, _: zap.Request) anyerror!void {}

    pub fn unhandledError(_: *MyContext, _: zap.Request, err: anyerror) void {
        std.debug.print("\n\n\nUNHANDLED ERROR: {} !!! \n\n\n", .{err});
    }
};

// A very simple endpoint handling only GET requests
const ErrorEndpoint = struct {

    // zap.App.Endpoint Interface part
    path: []const u8,
    error_strategy: zap.Endpoint.ErrorStrategy = .raise,

    // data specific for this endpoint
    some_data: []const u8,

    pub fn init(path: []const u8, data: []const u8) ErrorEndpoint {
        return .{
            .path = path,
            .some_data = data,
        };
    }

    // handle GET requests
    pub fn get(_: *ErrorEndpoint, _: Allocator, _: *MyContext, _: zap.Request) !void {

        // we just return an error
        // our error_strategy = .raise
        // -> error will be raised and dispatched to MyContext.unhandledError
        return error.@"Oh-No!";
    }

    // empty stubs for all other request methods
    pub fn post(_: *ErrorEndpoint, _: Allocator, _: *MyContext, _: zap.Request) !void {}
    pub fn put(_: *ErrorEndpoint, _: Allocator, _: *MyContext, _: zap.Request) !void {}
    pub fn delete(_: *ErrorEndpoint, _: Allocator, _: *MyContext, _: zap.Request) !void {}
    pub fn patch(_: *ErrorEndpoint, _: Allocator, _: *MyContext, _: zap.Request) !void {}
    pub fn options(_: *ErrorEndpoint, _: Allocator, _: *MyContext, _: zap.Request) !void {}
    pub fn head(_: *ErrorEndpoint, _: Allocator, _: *MyContext, _: zap.Request) !void {}
};

const StopEndpoint = struct {
    path: []const u8,
    error_strategy: zap.Endpoint.ErrorStrategy = .log_to_response,

    pub fn get(_: *StopEndpoint, _: Allocator, context: *MyContext, _: zap.Request) !void {
        std.debug.print(
            \\Before I stop, let me dump the app context:
            \\db_connection='{s}'
            \\ 
            \\
        , .{context.*.db_connection});
        zap.stop();
    }

    pub fn post(_: *StopEndpoint, _: Allocator, _: *MyContext, _: zap.Request) !void {}
    pub fn put(_: *StopEndpoint, _: Allocator, _: *MyContext, _: zap.Request) !void {}
    pub fn delete(_: *StopEndpoint, _: Allocator, _: *MyContext, _: zap.Request) !void {}
    pub fn patch(_: *StopEndpoint, _: Allocator, _: *MyContext, _: zap.Request) !void {}
    pub fn options(_: *StopEndpoint, _: Allocator, _: *MyContext, _: zap.Request) !void {}
};

pub fn main() !void {
    // setup allocations
    var gpa: std.heap.GeneralPurposeAllocator(.{
        // just to be explicit
        .thread_safe = true,
    }) = .{};
    defer std.debug.print("\n\nLeaks detected: {}\n\n", .{gpa.deinit() != .ok});
    const allocator = gpa.allocator();

    // create an App context
    var my_context: MyContext = .{ .db_connection = "db connection established!" };

    // create an App instance
    const App = zap.App.Create(MyContext);
    try App.init(allocator, &my_context, .{});
    defer App.deinit();

    // create the endpoints
    var my_endpoint = ErrorEndpoint.init("/error", "some endpoint specific data");
    var stop_endpoint: StopEndpoint = .{ .path = "/stop" };
    //
    // register the endpoints with the App
    try App.register(&my_endpoint);
    try App.register(&stop_endpoint);

    // listen on the network
    try App.listen(.{
        .interface = "0.0.0.0",
        .port = 3000,
    });
    std.debug.print("Listening on 0.0.0.0:3000\n", .{});

    std.debug.print(
        \\ Try me via:
        \\ curl http://localhost:3000/error
        \\ Stop me via:
        \\ curl http://localhost:3000/stop
        \\
    , .{});

    // start worker threads -- only 1 process!!!
    zap.start(.{
        .threads = 2,
        .workers = 1,
    });
}
