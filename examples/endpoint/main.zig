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
            .public_folder = "examples/endpoint/html",
            .max_clients = 100000,
            .max_body_size = 100 * 1024 * 1024,
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
        .threads = 2000,
        // IMPORTANT! It is crucial to only have a single worker for this example to work!
        // Multiple workers would have multiple copies of the users hashmap.
        //
        // Since zap is quite fast, you can do A LOT with a single worker.
        // Try it with `zig build run-endpoint -Drelease-fast`
        .workers = 1,
    });
}
