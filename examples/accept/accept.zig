//!
//! Part of the Zap examples.
//!
//! Build me with `zig build     accept`.
//! Run   me with `zig build run-accept`.
//!
const std = @import("std");
const zap = @import("zap");

var gpa = std.heap.GeneralPurposeAllocator(.{
    .thread_safe = true,
}){};

fn on_request_verbose(r: zap.Request) !void {
    // use a local buffer for the parsed accept headers
    var accept_buffer: [1024]u8 = undefined;
    var fba = std.heap.FixedBufferAllocator.init(&accept_buffer);
    const accept_allocator = fba.allocator();

    const content_type: zap.ContentType = content_type: {
        var accept_list = r.parseAcceptHeaders(accept_allocator) catch break :content_type .HTML;
        defer accept_list.deinit(accept_allocator);

        for (accept_list.items) |accept| {
            break :content_type accept.toContentType() orelse continue;
        }
        break :content_type .HTML;
    };

    // just for fun: print ALL headers
    var maybe_headers: ?zap.Request.HttpParamStrKVList = blk: {
        const h = r.headersToOwnedList(gpa.allocator()) catch |err| {
            std.debug.print("Error getting headers: {}\n", .{err});
            break :blk null;
        };
        break :blk h;
    };
    if (maybe_headers) |*headers| {
        defer headers.deinit();
        for (headers.items) |header| {
            std.debug.print("Header {s} = {s}\n", .{ header.key, header.value });
        }
    }

    try r.setContentType(content_type);
    switch (content_type) {
        .TEXT => {
            try r.sendBody("Hello from ZAP!!!");
        },
        .HTML => {
            try r.sendBody("<html><body><h1>Hello from ZAP!!!</h1></body></html>");
        },
        .XML => {
            try r.sendBody(
                \\<?xml version="1.0" encoding="UTF-8"?>
                \\<message>
                \\    <warning>
                \\        Hello from ZAP!!!
                \\    </warning>
                \\</message>
            );
        },
        .JSON => {
            var buffer: [128]u8 = undefined;
            const json = try zap.util.stringifyBuf(&buffer, .{ .message = "Hello from ZAP!!!" }, .{});
            try r.sendJson(json);
        },
        .XHTML => {
            try r.sendBody(
                \\<?xml version="1.0" encoding="UTF-8"?>
                \\<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en-US">
                \\  <body>
                \\    <h1>Hello from ZAP!!!</h1>
                \\  </body>
                \\</html>
            );
        },
    }
}

pub fn main() !void {
    var listener = zap.HttpListener.init(.{
        .port = 3000,
        .on_request = on_request_verbose,
        .log = true,
        .max_clients = 100000,
    });
    try listener.listen();

    std.debug.print(
        \\ Listening on 0.0.0.0:3000
        \\
        \\ Test me with:
        \\    curl --header "Accept: text/plain"            localhost:3000
        \\    curl --header "Accept: text/html"             localhost:3000
        \\    curl --header "Accept: application/xml"       localhost:3000
        \\    curl --header "Accept: application/json"      localhost:3000
        \\    curl --header "Accept: application/xhtml+xml" localhost:3000
        \\
        \\
    , .{});

    // start worker threads
    zap.start(.{
        .threads = 2,
        .workers = 2,
    });
}
