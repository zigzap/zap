const std = @import("std");
const zap = @import("zap");

// set default log level to .info and ZAP log level to .debug
pub const std_options: std.Options = .{
    .log_level = .info,
    .log_scope_levels = &[_]std.log.ScopeLevel{
        .{ .scope = .zap, .level = .debug },
    },
};

var buffer: [1024]u8 = undefined;
var read_len: ?usize = null;

const testfile = @embedFile("testfile.txt");

fn makeRequest(a: std.mem.Allocator, url: []const u8) !void {
    var http_client: std.http.Client = .{ .allocator = a };
    defer http_client.deinit();

    var response_writer = std.io.Writer.Allocating.init(a);
    defer response_writer.deinit();

    _ = try http_client.fetch(.{
        .location = .{ .url = url },
        .response_writer = &response_writer.writer,
    });

    const response_text = response_writer.written();
    read_len = response_text.len;
    @memcpy(buffer[0..read_len.?], response_text);

    zap.stop();
}

fn makeRequestThread(a: std.mem.Allocator, url: []const u8) !std.Thread {
    return try std.Thread.spawn(.{}, makeRequest, .{ a, url });
}
pub fn on_request(r: zap.Request) !void {
    r.sendFile("src/tests/testfile.txt") catch unreachable;
}

test "send file" {
    const allocator = std.testing.allocator;

    // setup listener
    var listener = zap.HttpListener.init(
        .{
            .port = 3040,
            .on_request = on_request,
            .log = false,
            .max_clients = 10,
            .max_body_size = 1 * 1024,
        },
    );
    try listener.listen();

    const thread = try makeRequestThread(allocator, "http://127.0.0.1:3040/?file=src/tests/testfile.txt");
    defer thread.join();
    zap.start(.{
        .threads = 1,
        .workers = 1,
    });

    if (read_len) |rl| {
        try std.testing.expectEqual(testfile.len, rl);
        try std.testing.expectEqualSlices(u8, testfile, buffer[0..rl]);
    } else {
        return error.Wrong;
    }
}
