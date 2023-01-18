const std = @import("std");

alloc: std.mem.Allocator = undefined,
users: std.AutoHashMap(usize, InternalUser) = undefined,
count: usize = 0,

pub const Self = @This();

const InternalUser = struct {
    id: usize = 0,
    firstnamebuf: [64]u8,
    firstnamelen: usize,
    lastnamebuf: [64]u8,
    lastnamelen: usize,
};

pub const User = struct {
    id: usize = 0,
    first_name: []const u8,
    last_name: []const u8,
};

pub fn init(a: std.mem.Allocator) Self {
    return .{
        .alloc = a,
        .users = std.AutoHashMap(usize, InternalUser).init(a),
    };
}

// the request will be freed (and its mem reused by facilio) when it's
// completed, so we take copies of the names
pub fn addByName(self: *Self, first: ?[]const u8, last: ?[]const u8) !usize {
    // TODO: get rid of the temp allocation here
    var temp = try self.alloc.alloc(InternalUser, 1);
    defer self.alloc.free(temp);
    var user = temp[0];
    self.count = self.count + 1;
    user.id = self.count;
    user.firstnamelen = 0;
    user.lastnamelen = 0;
    if (first) |firstname| {
        std.mem.copy(u8, user.firstnamebuf[0..], firstname);
        user.firstnamelen = firstname.len;
    }
    if (last) |lastname| {
        std.mem.copy(u8, user.lastnamebuf[0..], lastname);
        user.lastnamelen = lastname.len;
    }
    try self.users.put(user.id, user);
    return user.id;
}

pub fn delete(self: *Self, id: usize) bool {
    return self.users.remove(id);
}

pub fn get(self: *Self, id: usize) ?User {
    if (self.users.get(id)) |pUser| {
        return .{
            .id = pUser.id,
            .first_name = pUser.firstnamebuf[0..pUser.firstnamelen],
            .last_name = pUser.lastnamebuf[0..pUser.lastnamelen],
        };
    }
    return null;
}

pub fn update(
    self: *Self,
    id: usize,
    first: ?[]const u8,
    last: ?[]const u8,
) bool {
    var user: ?InternalUser = self.users.get(id);
    // we got a copy apparently, so we need to put again
    if (user) |*pUser| {
        pUser.*.firstnamelen = 0;
        pUser.*.lastnamelen = 0;
        if (first) |firstname| {
            std.mem.copy(u8, pUser.firstnamebuf[0..], firstname);
            pUser.firstnamelen = firstname.len;
        }
        if (last) |lastname| {
            std.mem.copy(u8, pUser.lastnamebuf[0..], lastname);
            pUser.lastnamelen = lastname.len;
        }
        _ = self.users.remove(id);
        if (self.users.put(id, pUser.*)) {
            return true;
        } else |_| {
            return false;
        }
    }
    return false;
}

// populate the list
pub fn list(self: *Self, out: *std.ArrayList(User)) !void {
    var it = JsonUserIterator.init(&self.users);
    while (it.next()) |user| {
        try out.append(user);
    }
}

const JsonUserIterator = struct {
    it: std.AutoHashMap(usize, InternalUser).ValueIterator = undefined,
    const This = @This();

    // careful:
    // - Self refers to the file's struct
    // - This refers to the JsonUserIterator struct
    pub fn init(internal_users: *std.AutoHashMap(usize, InternalUser)) This {
        return .{
            .it = internal_users.valueIterator(),
        };
    }

    pub fn next(this: *This) ?User {
        if (this.it.next()) |pUser| {
            return User{
                .id = pUser.id,
                .first_name = pUser.firstnamebuf[0..pUser.firstnamelen],
                .last_name = pUser.lastnamebuf[0..pUser.lastnamelen],
            };
        }
        return null;
    }
};
