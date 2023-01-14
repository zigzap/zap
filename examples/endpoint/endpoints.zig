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
            std.debug.print("no slash\n", .{});
            return null;
        }
        const idstr = path[endpoint.settings.path.len + 1 ..];
        std.debug.print("idstr={s}\n", .{idstr});
        return std.fmt.parseUnsigned(usize, idstr, 10) catch null;
    }
    return null;
}

var jsonbuf: [1024]u8 = undefined;
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
    std.debug.print("getUser()\n", .{});
    if (r.path) |path| {
        std.debug.print("getUser({s})\n", .{path});
        if (userIdFromPath(path)) |id| {
            std.debug.print("getUser({})\n", .{id});
            if (users.get(id)) |user| {
                std.debug.print("getUser(): {}\n", .{user});
                if (stringify(user, .{})) |json| {
                    _ = r.sendJson(json);
                }
            }
        } else {
            std.debug.print("User not found\n", .{});
        }
    }
}

pub fn listUsers(e: *zap.SimpleEndpoint, r: zap.SimpleRequest) void {
    std.debug.print("listUsers()\n", .{});
    _ = r;
    _ = e;
    var l = std.ArrayList(Users.User).init(alloc);
    if (users.list(&l)) {} else |_| {
        return;
    }
    // if (stringify(l, .{})) |json| {
    //     _ = r.sendJson(json);
    // }
}
