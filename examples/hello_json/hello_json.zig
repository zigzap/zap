const std = @import("std");
const zap = @import("zap");

const User = struct {
    first_name: ?[]const u8 = null,
    last_name: ?[]const u8 = null,
};

fn on_request(r: zap.Request) !void {
    if (r.methodAsEnum() != .GET) return;

    // /user/n
    if (r.path) |the_path| {
        if (the_path.len < 7 or !std.mem.startsWith(u8, the_path, "/user/"))
            return;

        const user_id: usize = @intCast(the_path[6] - 0x30);
        std.debug.print("user_id: {d}\n", .{user_id});
        std.debug.print("users: {d}\n", .{users.count()});
        const user = users.get(user_id);
        std.debug.print("user: {?}\n", .{user});

        var buf: [256]u8 = undefined;
        var json_to_send: []const u8 = undefined;
        json_to_send = try zap.util.stringifyBuf(&buf, user, .{});

        std.debug.print("<< json: {s}\n", .{json_to_send});
        r.setContentType(.JSON) catch return;
        r.setContentTypeFromFilename("test.json") catch return;
        r.sendBody(json_to_send) catch return;
    }
}

const UserMap = std.AutoHashMap(usize, User);

var users: UserMap = undefined;
fn setupUserData(a: std.mem.Allocator) !void {
    users = UserMap.init(a);
    try users.put(1, .{ .first_name = "renerocksai" });
    try users.put(2, .{ .first_name = "Your", .last_name = "Mom" });
}

pub fn main() !void {
    const a = std.heap.page_allocator;
    try setupUserData(a);
    defer users.deinit();
    var listener = zap.HttpListener.init(.{
        .port = 3000,
        .on_request = on_request,
        .log = false,
    });
    try listener.listen();

    std.debug.print(
        \\ Listening on 0.0.0.0:3000
        \\ 
        \\ Check out:
        \\ http://localhost:3000/user/1   # -- first user
        \\ http://localhost:3000/user/2   # -- second user
        \\ http://localhost:3000/user/3   # -- non-existing user
        \\
    , .{});

    // start worker threads
    zap.start(.{
        .threads = 2,
        .workers = 1, // user map cannot be shared among multiple worker processes
    });
}
