const std = @import("std");
const zap = @import("zap");
const Users = @import("users.zig");
const User = Users.User;

// an Endpoint

pub const Self = @This();

alloc: std.mem.Allocator = undefined,
endpoint: zap.SimpleEndpoint = undefined,
users: Users = undefined,

pub fn init(
    a: std.mem.Allocator,
    user_path: []const u8,
) Self {
    return .{
        .alloc = a,
        .users = Users.init(a),
        .endpoint = zap.SimpleEndpoint.init(.{
            .path = user_path,
            .get = getUser,
            .post = postUser,
            .put = putUser,
            .patch = putUser,
            .delete = deleteUser,
        }),
    };
}

pub fn deinit(self: *Self) void {
    self.users.deinit();
}

pub fn getUsers(self: *Self) *Users {
    return &self.users;
}

pub fn getUserEndpoint(self: *Self) *zap.SimpleEndpoint {
    return &self.endpoint;
}

fn userIdFromPath(self: *Self, path: []const u8) ?usize {
    if (path.len >= self.endpoint.settings.path.len + 2) {
        if (path[self.endpoint.settings.path.len] != '/') {
            return null;
        }
        const idstr = path[self.endpoint.settings.path.len + 1 ..];
        return std.fmt.parseUnsigned(usize, idstr, 10) catch null;
    }
    return null;
}

fn getUser(e: *zap.SimpleEndpoint, r: zap.SimpleRequest) void {
    const self = @fieldParentPtr(Self, "endpoint", e);
    if (r.path) |path| {
        // /users
        if (path.len == e.settings.path.len) {
            return self.listUsers(r);
        }
        var jsonbuf: [256]u8 = undefined;
        if (self.userIdFromPath(path)) |id| {
            if (self.users.get(id)) |user| {
                if (zap.stringifyBuf(&jsonbuf, user, .{})) |json| {
                    r.sendJson(json) catch return;
                }
            }
        }
    }
}

fn listUsers(self: *Self, r: zap.SimpleRequest) void {
    if (self.users.toJSON()) |json| {
        defer self.alloc.free(json);
        r.sendJson(json) catch return;
    } else |err| {
        std.debug.print("LIST error: {}\n", .{err});
    }
}

fn postUser(e: *zap.SimpleEndpoint, r: zap.SimpleRequest) void {
    const self = @fieldParentPtr(Self, "endpoint", e);
    if (r.body) |body| {
        var maybe_user: ?std.json.Parsed(User) = std.json.parseFromSlice(User, self.alloc, body, .{}) catch null;
        if (maybe_user) |u| {
            defer u.deinit();
            if (self.users.addByName(u.value.first_name, u.value.last_name)) |id| {
                var jsonbuf: [128]u8 = undefined;
                if (zap.stringifyBuf(&jsonbuf, .{ .status = "OK", .id = id }, .{})) |json| {
                    r.sendJson(json) catch return;
                }
            } else |err| {
                std.debug.print("ADDING error: {}\n", .{err});
                return;
            }
        }
    }
}

fn putUser(e: *zap.SimpleEndpoint, r: zap.SimpleRequest) void {
    const self = @fieldParentPtr(Self, "endpoint", e);
    if (r.path) |path| {
        if (self.userIdFromPath(path)) |id| {
            if (self.users.get(id)) |_| {
                if (r.body) |body| {
                    var maybe_user: ?std.json.Parsed(User) = std.json.parseFromSlice(User, self.alloc, body, .{}) catch null;
                    if (maybe_user) |u| {
                        defer u.deinit();
                        var jsonbuf: [128]u8 = undefined;
                        if (self.users.update(id, u.value.first_name, u.value.last_name)) {
                            if (zap.stringifyBuf(&jsonbuf, .{ .status = "OK", .id = id }, .{})) |json| {
                                r.sendJson(json) catch return;
                            }
                        } else {
                            if (zap.stringifyBuf(&jsonbuf, .{ .status = "ERROR", .id = id }, .{})) |json| {
                                r.sendJson(json) catch return;
                            }
                        }
                    }
                }
            }
        }
    }
}

fn deleteUser(e: *zap.SimpleEndpoint, r: zap.SimpleRequest) void {
    const self = @fieldParentPtr(Self, "endpoint", e);
    if (r.path) |path| {
        if (self.userIdFromPath(path)) |id| {
            var jsonbuf: [128]u8 = undefined;
            if (self.users.delete(id)) {
                if (zap.stringifyBuf(&jsonbuf, .{ .status = "OK", .id = id }, .{})) |json| {
                    r.sendJson(json) catch return;
                }
            } else {
                if (zap.stringifyBuf(&jsonbuf, .{ .status = "ERROR", .id = id }, .{})) |json| {
                    r.sendJson(json) catch return;
                }
            }
        }
    }
}
