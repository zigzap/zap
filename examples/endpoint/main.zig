const std = @import("std");
const zap = @import("zap");
const Endpoint = @import("endpoint.zig");
const StopEndpoint = @import("stopendpoint.zig");

// this is just to demo that we can catch arbitrary slugs
fn on_request(r: zap.SimpleRequest) void {
    if (r.path) |the_path| {
        std.debug.print("REQUESTED PATH: {s}\n", .{the_path});
    }

    r.sendBody("<html><body><h1>Hello from ZAP!!!</h1></body></html>") catch return;
}

pub fn main() !void {
    var gpa = std.heap.GeneralPurposeAllocator(.{
        .thread_safe = true,
    }){};
    var allocator = gpa.allocator();

    // we scope everything that can allocate within this block for leak detection
    {
        // setup listener
        var listener = zap.SimpleEndpointListener.init(
            allocator,
            .{
                .port = 3000,
                .on_request = on_request,
                .log = true,
                .public_folder = "examples/endpoint/html",
                .max_clients = 100000,
                .max_body_size = 100 * 1024 * 1024,
            },
        );
        defer listener.deinit();

        var endpoint = Endpoint.init(allocator, "/users");
        defer endpoint.deinit();

        var stopEp = StopEndpoint.init("/stop");

        // add endpoint
        try listener.addEndpoint(endpoint.getUserEndpoint());
        try listener.addEndpoint(stopEp.getEndpoint());

        // fake some users
        var uid: usize = undefined;
        uid = try endpoint.getUsers().addByName("renerocksai", null);
        uid = try endpoint.getUsers().addByName("renerocksai", "your mom");

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

    // show potential memory leaks when ZAP is shut down
    const has_leaked = gpa.detectLeaks();
    std.log.debug("Has leaked: {}\n", .{has_leaked});
}
