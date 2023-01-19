const std = @import("std");
const zap = @import("zap");
const Endpoint = @import("endpoint.zig");

pub fn main() !void {
    var gpa = std.heap.GeneralPurposeAllocator(.{
        .thread_safe = true,
    }){};
    var allocator = gpa.allocator();
    // setup listener
    var listener = zap.SimpleEndpointListener.init(
        allocator,
        .{
            .port = 3000,
            .on_request = null,
            .log = true,
            .public_folder = "./examples/endpoint/html",
        },
    );

    Endpoint.init(allocator, "/users");

    // add endpoint
    try listener.addEndpoint(Endpoint.getUserEndpoint());

    // fake some users
    var uid: usize = undefined;
    uid = try Endpoint.getUsers().addByName("renerocksai", null);
    uid = try Endpoint.getUsers().addByName("renerocksai", "your mom");

    // listen
    try listener.listen();

    std.debug.print("Listening on 0.0.0.0:3000\n", .{});

    // and run
    zap.start(.{
        .threads = 2,
        .workers = 1, // to stay in-process, users list shared between threads
    });
}
