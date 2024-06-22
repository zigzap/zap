const std = @import("std");
const zap = @import("zap");

fn on_request(r: zap.Request) void {
    r.setStatus(.not_found);
    r.sendBody("<html><body><h1>404 - File not found</h1></body></html>") catch return;
}

pub fn main() !void {
    var args_it = std.process.args();
    var port: usize = 8080;
    var docs_dir: []const u8 = "zig-out/zap";

    while (args_it.next()) |arg| {
        if (std.mem.startsWith(u8, arg, "--port=")) {
            // try to parse port
            if (std.fmt.parseUnsigned(usize, arg[7..], 0)) |the_port| {
                port = the_port;
            } else |_| {
                std.debug.print("Invalid port number. Using default port {}\n", .{port});
            }
        }

        if (std.mem.startsWith(u8, arg, "--docs=")) {
            docs_dir = arg[7..];
        }
    }

    zap.mimetypeRegister("wasm", "application/wasm");

    var listener = zap.HttpListener.init(.{
        .port = port,
        .on_request = on_request,
        .public_folder = docs_dir,
        .log = true,
    });
    try listener.listen();

    std.debug.print("\nServing docs from {s} at 0.0.0.0:{}\n", .{ docs_dir, port });
    std.debug.print("\nSee docs at http://localhost:{}\n\n", .{port});

    // start worker threads
    zap.start(.{
        .threads = 2,
        .workers = 1,
    });
}
