const std = @import("std");

pub fn main() !void {
    // var gpa = std.heap.GeneralPurposeAllocator(.{
    //     .thread_safe = true,
    // }){};
    // const allocator = gpa.allocator();

    const address = try std.net.Address.parseIp("127.0.0.1", 3000);
    var http_server = try address.listen(.{
        .reuse_address = true,
    });

    var read_buffer: [2048]u8 = undefined;

    // const max_header_size = 8192;

    while (true) {
        const connection = try http_server.accept();
        defer connection.stream.close();
        var server = std.http.Server.init(connection, &read_buffer);

        var request = try server.receiveHead();
        const server_body: []const u8 = "HI FROM ZIG STD!\n";

        try request.respond(server_body, .{
            .extra_headers = &.{
                .{ .name = "content_type", .value = "text/plain" },
                .{ .name = "connection", .value = "close" },
            },
        });
    }
}
