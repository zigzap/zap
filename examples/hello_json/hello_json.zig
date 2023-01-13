const std = @import("std");
const zap = @import("zap");

const User = struct {
    first_name: ?[]const u8 = null,
    last_name: ?[]const u8 = null,
};

fn on_request(r: zap.SimpleRequest) void {
    if (!std.mem.eql(u8, r.method.?, "GET"))
        return;

    // /user/n
    if (r.path) |the_path| {
        if (the_path.len < 7 or !std.mem.startsWith(u8, the_path, "/user/"))
            return;

        const user_id: usize = @intCast(usize, the_path[6] - 0x30);
        const user = users.get(user_id);

        var json_to_send: []const u8 = undefined;
        if (JsonHelper.stringify(user, .{})) |json| {
            json_to_send = json;
        } else {
            json_to_send = "null";
        }
        std.debug.print("<< json: {s}\n", .{json_to_send});
        r.setContentType(.JSON);
        _ = r.sendBody(json_to_send);
    }
}

const UserMap = std.AutoHashMap(usize, User);

var users: UserMap = undefined;
fn setupUserData(a: std.mem.Allocator) !void {
    users = UserMap.init(a);
    try users.put(1, .{ .first_name = "renerocksai" });
    try users.put(2, .{ .first_name = "Your", .last_name = "Mom" });
}

const JsonHelper = struct {
    // "static" instance of buffer
    var buf: [100]u8 = undefined;
    var fba = std.heap.FixedBufferAllocator.init(&buf);

    fn stringify(value: anytype, options: std.json.StringifyOptions) ?[]const u8 {
        var string = std.ArrayList(u8).init(fba.allocator());
        if (std.json.stringify(value, options, string.writer())) {
            return string.items;
        } else |_| { // error
            return null;
        }
    }
};

pub fn main() !void {
    var a = std.heap.page_allocator;
    try setupUserData(a);
    var listener = zap.SimpleHttpListener.init(.{
        .port = 3000,
        .on_request = on_request,
        .log = false,
    });
    try listener.listen();

    std.debug.print("Listening on 0.0.0.0:3000\n", .{});

    // start worker threads
    zap.start(.{
        .threads = 2,
        .workers = 2,
    });
}
