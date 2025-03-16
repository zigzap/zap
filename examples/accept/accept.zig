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
        defer accept_list.deinit();

        for (accept_list.items) |accept| {
            break :content_type accept.toContentType() orelse continue;
        }
        break :content_type .HTML;
    };

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

    std.debug.print("Listening on 0.0.0.0:3000\n", .{});

    // start worker threads
    zap.start(.{
        .threads = 2,
        .workers = 2,
    });
}
