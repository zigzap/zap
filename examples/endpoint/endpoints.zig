const std = @import("std");
const zap = @import("zap");
const Users = @import("users.zig");

// the Endpoints

pub const Self = @This();

var alloc: std.mem.Allocator = undefined;
var endpoint: zap.SimpleEndpoint = undefined;
var list_endpoint: zap.SimpleEndpoint = undefined;
var users: Users = undefined;

pub fn init(
    a: std.mem.Allocator,
    user_path: []const u8,
    userlist_path: []const u8,
) void {
    users = Users.init(a);
    alloc = a;
    endpoint = zap.SimpleEndpoint.init(.{
        .path = user_path,
        .get = getUser,
        .post = null,
        .put = null,
        .delete = null,
    });
    list_endpoint = zap.SimpleEndpoint.init(.{
        .path = userlist_path,
        .get = listUsers,
        .post = null,
        .put = null,
        .delete = null,
    });
}

pub fn getUsers() *Users {
    return &users;
}

pub fn getUserEndpoint() *zap.SimpleEndpoint {
    return &endpoint;
}

pub fn getUserListEndpoint() *zap.SimpleEndpoint {
    return &list_endpoint;
}

fn userIdFromPath(path: []const u8) ?usize {
    if (path.len >= endpoint.settings.path.len + 2) {
        if (path[endpoint.settings.path.len] != '/') {
            return null;
        }
        const idstr = path[endpoint.settings.path.len + 1 ..];
        return std.fmt.parseUnsigned(usize, idstr, 10) catch null;
    }
    return null;
}

var jsonbuf: [100 * 1024]u8 = undefined;
fn stringify(value: anytype, options: std.json.StringifyOptions) ?[]const u8 {
    var fba = std.heap.FixedBufferAllocator.init(&jsonbuf);
    var string = std.ArrayList(u8).init(fba.allocator());
    if (std.json.stringify(value, options, string.writer())) {
        return string.items;
    } else |_| { // error
        return null;
    }
}

pub fn getUser(e: *zap.SimpleEndpoint, r: zap.SimpleRequest) void {
    _ = e;
    if (r.path) |path| {
        if (userIdFromPath(path)) |id| {
            if (users.get(id)) |user| {
                if (stringify(user, .{})) |json| {
                    _ = r.sendJson(json);
                }
            }
        }
    }
}

fn stringifyUserList(
    userlist: *std.ArrayList(Users.User),
    options: std.json.StringifyOptions,
) !?[]const u8 {
    var fba = std.heap.FixedBufferAllocator.init(&jsonbuf);
    var string = std.ArrayList(u8).init(fba.allocator());
    var writer = string.writer();
    try writer.writeByte('[');
    var first: bool = true;
    for (userlist.items) |user| {
        if (!first) try writer.writeByte(',');
        first = false;
        try std.json.stringify(user, options, string.writer());
    }
    try writer.writeByte(']');
    return string.items;
}

pub fn listUsers(e: *zap.SimpleEndpoint, r: zap.SimpleRequest) void {
    _ = e;
    var l: std.ArrayList(Users.User) = std.ArrayList(Users.User).init(alloc);
    if (users.list(&l)) {} else |_| {
        return;
    }
    if (stringifyUserList(&l, .{})) |maybe_json| {
        if (maybe_json) |json| {
            _ = r.sendJson(json);
        }
    } else |_| {
        return;
    }
}
