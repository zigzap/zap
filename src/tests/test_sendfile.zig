const std = @import("std");
const zap = @import("zap");

var buffer: [1024]u8 = undefined;
var read_len: ?usize = null;

const testfile = @embedFile("testfile.txt");

fn makeRequest(a: std.mem.Allocator, url: []const u8) !void {
    const uri = try std.Uri.parse(url);

    var h = std.http.Headers{ .allocator = a };
    defer h.deinit();

    var http_client: std.http.Client = .{ .allocator = a };
    defer http_client.deinit();

    var req = try http_client.request(.GET, uri, h, .{});
    defer req.deinit();

    try req.start();
    try req.wait();
    read_len = try req.readAll(&buffer);

    zap.fio_stop();
}

fn makeRequestThread(a: std.mem.Allocator, url: []const u8) !std.Thread {
    return try std.Thread.spawn(.{}, makeRequest, .{ a, url });
}
pub fn on_request(r: zap.SimpleRequest) void {
    r.sendFile("src/tests/testfile.txt") catch unreachable;
}

test "send file" {
    var allocator = std.testing.allocator;

    // setup listener
    var listener = zap.SimpleHttpListener.init(
        .{
            .port = 3002,
            .on_request = on_request,
            .log = false,
            .max_clients = 10,
            .max_body_size = 1 * 1024,
        },
    );
    zap.enableDebugLog();
    try listener.listen();

    const thread = try makeRequestThread(allocator, "http://127.0.0.1:3002/?file=src/tests/testfile.txt");
    defer thread.join();
    zap.start(.{
        .threads = 1,
        .workers = 0,
    });

    if (read_len) |rl| {
        try std.testing.expectEqual(testfile.len, rl);
        try std.testing.expectEqualSlices(u8, testfile, buffer[0..rl]);
    } else {
        return error.Wrong;
    }
}
