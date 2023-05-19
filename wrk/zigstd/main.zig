const std = @import("std");

pub fn main() !void {
    var gpa = std.heap.GeneralPurposeAllocator(.{
        .thread_safe = true,
    }){};
    var allocator = gpa.allocator();

    var server = std.http.Server.init(allocator, .{
        .reuse_address = true,
    });
    defer server.deinit();

    const address = try std.net.Address.parseIp("127.0.0.1", 3000);
    try server.listen(address);

    const max_header_size = 8192;

    while (true) {
        var res = try server.accept(.{
            .allocator = allocator,
            .header_strategy = .{ .dynamic = max_header_size },
        });
        // const start_time = std.time.nanoTimestamp();
        defer res.deinit();
        defer _ = res.reset();
        try res.wait();

        const server_body: []const u8 = "HI FROM ZIG STD!\n";
        res.transfer_encoding = .{ .content_length = server_body.len };
        try res.headers.append("content-type", "text/plain");
        try res.headers.append("connection", "close");
        try res.do();

        var buf: [128]u8 = undefined;
        _ = try res.readAll(&buf);
        _ = try res.writer().writeAll(server_body);
        try res.finish();
        // const end_time = std.time.nanoTimestamp();
        // const diff = end_time - start_time;
        // std.debug.print("{d}\n", .{diff});
    }
}
