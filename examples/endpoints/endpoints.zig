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

fn getUser(e: *zap.SimpleEndpoint, r: zap.SimpleRequest) void {
    _ = e;
    if (r.path) |path| {
        if (userIdFromPath(path)) |id| {
            if (users.get(id)) |user| {
                if (zap.stringify(user, .{})) |json| {
                    _ = r.sendJson(json);
                }
            }
        }
    }
}

fn listUsers(e: *zap.SimpleEndpoint, r: zap.SimpleRequest) void {
    _ = e;
    var l: std.ArrayList(Users.User) = std.ArrayList(Users.User).init(alloc);
    if (users.list(&l)) {} else |_| {
        return;
    }
    if (zap.stringifyArrayList(Users.User, &l, .{})) |maybe_json| {
        if (maybe_json) |json| {
            _ = r.sendJson(json);
        }
    } else |_| {
        return;
    }
}

fn postUser(e: *zap.SimpleEndpoint, r: zap.SimpleRequest) void {
    _ = e;
    if (r.path) |path| {
        if (userIdFromPath(path)) |id| {
            if (users.get(id)) |user| {
                if (zap.stringify(user, .{})) |json| {
                    _ = r.sendJson(json);
                }
            }
        }
    }
}
