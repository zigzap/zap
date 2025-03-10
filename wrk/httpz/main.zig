const std = @import("std");
const httpz = @import("httpz");

pub fn main() !void {
    var gpa = std.heap.GeneralPurposeAllocator(.{
        .thread_safe = true,
    }){};
    const allocator = gpa.allocator();

    var server = try httpz.Server(void).init(allocator, .{ .port = 3000 }, {});

    defer server.deinit();
    var router = server.router(.{});

    router.get("/", index, .{});
    try server.listen();
}

fn index(_: *httpz.Request, res: *httpz.Response) !void {
    res.body = "HI FROM ZIG-HTTPZ";
}
