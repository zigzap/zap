const std = @import("std");

alloc: std.mem.Allocator = undefined,
users: std.AutoHashMap(usize, InternalUser) = undefined,

pub const Self = @This();

const InternalUser = struct {
    id: usize = 0,
    firstnamebuf: [64]u8,
    firstnamelen: usize,
    lastnamebuf: [64]u8,
    lastnamelen: usize,
};

pub const User = struct {
    first_name: []const u8,
    last_name: []const u8,
};

pub fn init(a: std.mem.Allocator) Self {
    return .{
        .alloc = a,
        .users = std.AutoHashMap(usize, InternalUser).init(a),
    };
}

/// the request will be freed (and reused by facilio) when it's
/// completed, so we take copies of the names
pub fn addByName(self: *Self, first: ?[]const u8, last: ?[]const u8) !usize {
    var created = try self.alloc.alloc(InternalUser, 1);
    var user = created[0];
    user.id = self.users.count() + 1;
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
    if (self.users.get(id)) |pUser| {
        self.alloc.free(pUser);
        return self.users.remove(id);
    }
    return false;
}

pub fn get(self: *Self, id: usize) ?User {
    if (self.users.get(id)) |pUser| {
        return .{
            .first_name = pUser.firstnamebuf[0..pUser.firstnamelen],
            .last_name = pUser.lastnamebuf[0..pUser.lastnamelen],
        };
    }
    return null;
}

pub fn update(self: *Self, id: usize, first: ?[]const u8, last: ?[]const u8) bool {
    if (self.users.get(id)) |pUser| {
        pUser.firstnamelen = 0;
        pUser.lastnamelen = 0;
        if (first) |firstname| {
            std.mem.copy(u8, pUser.firstnamebuf[0..], firstname);
            pUser.firstnamelen = firstname.len;
        }
        if (last) |lastname| {
            std.mem.copy(u8, pUser.lastname[0..], lastname);
            pUser.lastnamelen = lastname.len;
        }
        return true;
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
                .first_name = pUser.firstnamebuf[0..pUser.firstnamelen],
                .last_name = pUser.lastnamebuf[0..pUser.lastnamelen],
            };
        }
        return null;
    }
};

//
// JSON helpers
//
var jsonbuf: [100 * 1024]u8 = undefined;

pub fn stringify(value: anytype, options: std.json.StringifyOptions) ?[]const u8 {
    var fba = std.heap.FixedBufferAllocator.init(&jsonbuf);
    var string = std.ArrayList(u8).init(fba.allocator());
    if (std.json.stringify(value, options, string.writer())) {
        return string.items;
    } else |_| { // error
        return null;
    }
}

pub fn stringifyUserList(
    userlist: *std.ArrayList(User),
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
