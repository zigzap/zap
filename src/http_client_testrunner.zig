const std = @import("std");

pub fn main() !void {
    const a = std.heap.page_allocator;
    var p = std.ChildProcess.init(&.{
        "./zig-out/bin/http_client",
        "http://127.0.0.1:3000/test",
        "Bearer",
        "ABCDEFG",
    }, a);
    _ = try p.spawnAndWait();

    std.time.sleep(3 * std.time.ns_per_s);

    p = std.ChildProcess.init(&.{
        "./zig-out/bin/http_client",
        "http://127.0.0.1:3000/test",
        "Bearer",
        "invalid",
    }, a);
    _ = try p.spawnAndWait();

    std.time.sleep(3 * std.time.ns_per_s);

    p = std.ChildProcess.init(&.{
        "./zig-out/bin/http_client",
        "http://127.0.0.1:3000/test",
        "Basic",
        "QWxhZGRpbjpvcGVuIHNlc2FtZQ==",
    }, a);
    _ = try p.spawnAndWait();

    std.time.sleep(3 * std.time.ns_per_s);

    p = std.ChildProcess.init(&.{
        "./zig-out/bin/http_client",
        "http://127.0.0.1:3000/test",
        "Basic",
        "invalid",
    }, a);
    _ = try p.spawnAndWait();

    std.time.sleep(3 * std.time.ns_per_s);

    p = std.ChildProcess.init(&.{
        "./zig-out/bin/http_client",
        "http://127.0.0.1:3000/test",
        "Basic",
        "QWxsYWRkaW46b3BlbnNlc2FtZQ==",
    }, a);
    _ = try p.spawnAndWait();

    std.time.sleep(3 * std.time.ns_per_s);

    p = std.ChildProcess.init(&.{
        "./zig-out/bin/http_client",
        "http://127.0.0.1:3000/test",
        "Basic",
        "QWxsYWRkaW46b3BlbnNlc2FtZQ==-invalid",
    }, a);
    _ = try p.spawnAndWait();

    // std.time.sleep(3 * std.time.ns_per_s);
}
