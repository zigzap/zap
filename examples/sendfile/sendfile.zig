const std = @import("std");
const zap = @import("zap");

var buffer: [1024]u8 = undefined;
var read_len: ?usize = null;

const testfile = @embedFile("testfile.txt");

pub fn on_request(r: zap.SimpleRequest) void {
    // Sends a file if present in the filesystem orelse returns an error.
    //
    // - efficiently sends a file using gzip compression
    // - also handles range requests if `Range` or `If-Range` headers are present in the request.
    // - sends the response headers and the specified file (the response's body).
    //
    // On success, the `r.h` handle will be consumed and invalid.
    // On error, the handle will still be valid and should be used to send an error response
    //
    // Important: sets last-modified and cache-control headers with a max-age value of 1 hour!

    // In this example, we disable caching
    r.setHeader("Cache-Control", "no-cache") catch unreachable;
    if (r.sendFile("examples/sendfile/testfile.txt")) {} else |err| {
        std.log.err("Unable to send file: {any}", .{err});
    }
}

pub fn main() !void {
    // setup listener
    var listener = zap.SimpleHttpListener.init(
        .{
            .port = 3000,
            .on_request = on_request,
            .log = true,
            .max_clients = 10,
            .max_body_size = 1 * 1024, // careful here
        },
    );

    zap.enableDebugLog();
    try listener.listen();

    std.debug.print("Visit me on http://127.0.0.1:3000\n", .{});

    zap.start(.{
        .threads = 1,
        .workers = 0,
    });
}
