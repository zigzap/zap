const std = @import("std");
const zap = @import("zap");
const Users = @import("users.zig");
const User = Users.User;

// an Endpoint

pub const Self = @This();

alloc: std.mem.Allocator = undefined,
_users: Users = undefined,

path: []const u8,
error_strategy: zap.Endpoint.ErrorStrategy = .log_to_response,

pub fn init(
    a: std.mem.Allocator,
    user_path: []const u8,
) Self {
    return .{
        .alloc = a,
        ._users = Users.init(a),
        .path = user_path,
    };
}

pub fn deinit(self: *Self) void {
    self._users.deinit();
}

pub fn users(self: *Self) *Users {
    return &self._users;
}

fn userIdFromPath(self: *Self, path: []const u8) ?usize {
    if (path.len >= self.path.len + 2) {
        if (path[self.path.len] != '/') {
            return null;
        }
        const idstr = path[self.path.len + 1 ..];
        return std.fmt.parseUnsigned(usize, idstr, 10) catch null;
    }
    return null;
}

pub fn put(_: *Self, _: zap.Request) anyerror!void {}
pub fn get(self: *Self, r: zap.Request) anyerror!void {
    if (r.path) |path| {
        // /users
        if (path.len == self.path.len) {
            return self.listUsers(r);
        }
        var jsonbuf: [256]u8 = undefined;
        if (self.userIdFromPath(path)) |id| {
            if (self._users.get(id)) |user| {
                const json = try zap.util.stringifyBuf(&jsonbuf, user, .{});
                try r.sendJson(json);
            }
        }
    }
}

fn listUsers(self: *Self, r: zap.Request) !void {
    if (self._users.toJSON()) |json| {
        defer self.alloc.free(json);
        try r.sendJson(json);
    } else |err| {
        return err;
    }
}

pub fn post(self: *Self, r: zap.Request) anyerror!void {
    if (r.body) |body| {
        const maybe_user: ?std.json.Parsed(User) = std.json.parseFromSlice(User, self.alloc, body, .{}) catch null;
        if (maybe_user) |u| {
            defer u.deinit();
            if (self._users.addByName(u.value.first_name, u.value.last_name)) |id| {
                var jsonbuf: [128]u8 = undefined;
                const json = try zap.util.stringifyBuf(&jsonbuf, .{ .status = "OK", .id = id }, .{});
                try r.sendJson(json);
            } else |err| {
                std.debug.print("ADDING error: {}\n", .{err});
                return;
            }
        }
    }
}

pub fn patch(self: *Self, r: zap.Request) anyerror!void {
    if (r.path) |path| {
        if (self.userIdFromPath(path)) |id| {
            if (self._users.get(id)) |_| {
                if (r.body) |body| {
                    const maybe_user: ?std.json.Parsed(User) = std.json.parseFromSlice(User, self.alloc, body, .{}) catch null;
                    if (maybe_user) |u| {
                        defer u.deinit();
                        var jsonbuf: [128]u8 = undefined;
                        if (self._users.update(id, u.value.first_name, u.value.last_name)) {
                            const json = try zap.util.stringifyBuf(&jsonbuf, .{ .status = "OK", .id = id }, .{});
                            try r.sendJson(json);
                        } else {
                            const json = try zap.util.stringifyBuf(&jsonbuf, .{ .status = "ERROR", .id = id }, .{});
                            try r.sendJson(json);
                        }
                    }
                }
            }
        }
    }
}

pub fn delete(self: *Self, r: zap.Request) anyerror!void {
    if (r.path) |path| {
        if (self.userIdFromPath(path)) |id| {
            var jsonbuf: [128]u8 = undefined;
            if (self._users.delete(id)) {
                const json = try zap.util.stringifyBuf(&jsonbuf, .{ .status = "OK", .id = id }, .{});
                try r.sendJson(json);
            } else {
                const json = try zap.util.stringifyBuf(&jsonbuf, .{ .status = "ERROR", .id = id }, .{});
                try r.sendJson(json);
            }
        }
    }
}

pub fn options(_: *Self, r: zap.Request) anyerror!void {
    try r.setHeader("Access-Control-Allow-Origin", "*");
    try r.setHeader("Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE, OPTIONS");
    r.setStatus(zap.http.StatusCode.no_content);
    r.markAsFinished(true);
}
