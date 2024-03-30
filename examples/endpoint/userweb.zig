const std = @import("std");
const zap = @import("zap");
const Users = @import("users.zig");
const User = Users.User;

// an Endpoint

pub const Self = @This();

alloc: std.mem.Allocator = undefined,
ep: zap.Endpoint = undefined,
_users: Users = undefined,

pub fn init(
    a: std.mem.Allocator,
    user_path: []const u8,
) Self {
    return .{
        .alloc = a,
        ._users = Users.init(a),
        .ep = zap.Endpoint.init(.{
            .path = user_path,
            .get = getUser,
            .post = postUser,
            .put = putUser,
            .patch = putUser,
            .delete = deleteUser,
            .options = optionsUser,
        }),
    };
}

pub fn deinit(self: *Self) void {
    self._users.deinit();
}

pub fn users(self: *Self) *Users {
    return &self._users;
}

pub fn endpoint(self: *Self) *zap.Endpoint {
    return &self.ep;
}

fn userIdFromPath(self: *Self, path: []const u8) ?usize {
    if (path.len >= self.ep.settings.path.len + 2) {
        if (path[self.ep.settings.path.len] != '/') {
            return null;
        }
        const idstr = path[self.ep.settings.path.len + 1 ..];
        return std.fmt.parseUnsigned(usize, idstr, 10) catch null;
    }
    return null;
}

fn getUser(e: *zap.Endpoint, r: zap.Request) void {
    const self = @fieldParentPtr(Self, "ep", e);
    if (r.path) |path| {
        // /users
        if (path.len == e.settings.path.len) {
            return self.listUsers(r);
        }
        var jsonbuf: [256]u8 = undefined;
        if (self.userIdFromPath(path)) |id| {
            if (self._users.get(id)) |user| {
                if (zap.stringifyBuf(&jsonbuf, user, .{})) |json| {
                    r.sendJson(json) catch return;
                }
            }
        }
    }
}

fn listUsers(self: *Self, r: zap.Request) void {
    if (self._users.toJSON()) |json| {
        defer self.alloc.free(json);
        r.sendJson(json) catch return;
    } else |err| {
        std.debug.print("LIST error: {}\n", .{err});
    }
}

fn postUser(e: *zap.Endpoint, r: zap.Request) void {
    const self = @fieldParentPtr(Self, "ep", e);
    if (r.body) |body| {
        const maybe_user: ?std.json.Parsed(User) = std.json.parseFromSlice(User, self.alloc, body, .{}) catch null;
        if (maybe_user) |u| {
            defer u.deinit();
            if (self._users.addByName(u.value.first_name, u.value.last_name)) |id| {
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

fn putUser(e: *zap.Endpoint, r: zap.Request) void {
    const self = @fieldParentPtr(Self, "ep", e);
    if (r.path) |path| {
        if (self.userIdFromPath(path)) |id| {
            if (self._users.get(id)) |_| {
                if (r.body) |body| {
                    const maybe_user: ?std.json.Parsed(User) = std.json.parseFromSlice(User, self.alloc, body, .{}) catch null;
                    if (maybe_user) |u| {
                        defer u.deinit();
                        var jsonbuf: [128]u8 = undefined;
                        if (self._users.update(id, u.value.first_name, u.value.last_name)) {
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

fn deleteUser(e: *zap.Endpoint, r: zap.Request) void {
    const self = @fieldParentPtr(Self, "ep", e);
    if (r.path) |path| {
        if (self.userIdFromPath(path)) |id| {
            var jsonbuf: [128]u8 = undefined;
            if (self._users.delete(id)) {
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

fn optionsUser(e: *zap.Endpoint, r: zap.Request) void {
    _ = e;
    r.setHeader("Access-Control-Allow-Origin", "*") catch return;
    r.setHeader("Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE, OPTIONS") catch return;
    r.setStatus(zap.StatusCode.no_content);
    r.markAsFinished(true);
}
