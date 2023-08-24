const httpz = @import("httpz");
const std = @import("std");

pub fn main() !void {
    var gpa = std.heap.GeneralPurposeAllocator(.{}){};
    const allocator = gpa.allocator();

    var server = try httpz.Server().init(allocator, .{ .port = 5882 });

    var router = server.router();

    // use get/post/put/head/patch/options/delete
    // you can also use "all" to attach to all methods
    router.get("/", getRequest);

    // start the server in the current thread, blocking.
    try server.listen();
}

fn getRequest(req: *httpz.Request, res: *httpz.Response) !void {
    // try res.json(.{ .id = req.param("id").?, .name = "Teg" }, .{});
    _ = req;
    res.body = "Hello http.zig!!!";
}
