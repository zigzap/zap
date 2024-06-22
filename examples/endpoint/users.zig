const std = @import("std");

alloc: std.mem.Allocator = undefined,
users: std.AutoHashMap(usize, InternalUser) = undefined,
lock: std.Thread.Mutex = undefined,
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
        .lock = std.Thread.Mutex{},
    };
}

pub fn deinit(self: *Self) void {
    self.users.deinit();
}

// the request will be freed (and its mem reused by facilio) when it's
// completed, so we take copies of the names
pub fn addByName(self: *Self, first: ?[]const u8, last: ?[]const u8) !usize {
    var user: InternalUser = undefined;
    user.firstnamelen = 0;
    user.lastnamelen = 0;

    if (first) |firstname| {
        @memcpy(user.firstnamebuf[0..firstname.len], firstname);
        user.firstnamelen = firstname.len;
    }

    if (last) |lastname| {
        @memcpy(user.lastnamebuf[0..lastname.len], lastname);
        user.lastnamelen = lastname.len;
    }

    // We lock only on insertion, deletion, and listing
    self.lock.lock();
    defer self.lock.unlock();
    user.id = self.count + 1;
    if (self.users.put(user.id, user)) {
        self.count += 1;
        return user.id;
    } else |err| {
        std.debug.print("addByName error: {}\n", .{err});
        // make sure we pass on the error
        return err;
    }
}

pub fn delete(self: *Self, id: usize) bool {
    // We lock only on insertion, deletion, and listing
    self.lock.lock();
    defer self.lock.unlock();

    const ret = self.users.remove(id);
    if (ret) {
        self.count -= 1;
    }
    return ret;
}

pub fn get(self: *Self, id: usize) ?User {
    // we don't care about locking here, as our usage-pattern is unlikely to
    // get a user by id that is not known yet
    if (self.users.getPtr(id)) |pUser| {
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
    // we don't care about locking here
    // we update in-place, via getPtr
    if (self.users.getPtr(id)) |pUser| {
        pUser.firstnamelen = 0;
        pUser.lastnamelen = 0;
        if (first) |firstname| {
            @memcpy(pUser.firstnamebuf[0..firstname.len], firstname);
            pUser.firstnamelen = firstname.len;
        }
        if (last) |lastname| {
            @memcpy(pUser.lastnamebuf[0..lastname.len], lastname);
            pUser.lastnamelen = lastname.len;
        }
    }
    return false;
}

pub fn toJSON(self: *Self) ![]const u8 {
    self.lock.lock();
    defer self.lock.unlock();

    // We create a User list that's JSON-friendly
    // NOTE: we could also implement the whole JSON writing ourselves here,
    // working directly with InternalUser elements of the users hashmap.
    // might actually save some memory
    // TODO: maybe do it directly with the user.items
    var l: std.ArrayList(User) = std.ArrayList(User).init(self.alloc);
    defer l.deinit();

    // the potential race condition is fixed by jsonifying with the mutex locked
    var it = JsonUserIteratorWithRaceCondition.init(&self.users);
    while (it.next()) |user| {
        try l.append(user);
    }
    std.debug.assert(self.users.count() == l.items.len);
    std.debug.assert(self.count == l.items.len);
    return std.json.stringifyAlloc(self.alloc, l.items, .{});
}

//
// Note: the following code is kept in here because it taught us a lesson
//
pub fn listWithRaceCondition(self: *Self, out: *std.ArrayList(User)) !void {
    // We lock only on insertion, deletion, and listing
    //
    // NOTE: race condition:
    // =====================
    //
    // the list returned from here contains elements whose slice fields
    // (.first_name and .last_name) point to char buffers of elements of the
    // users list:
    //
    // user.first_name -> internal_user.firstnamebuf[..]
    //
    // -> we're only referencing the memory of first and last names.
    // -> while the caller works with this list, e.g. "slowly" converting it to
    //    JSON, the users hashmap might be added to massively in the background,
    //    causing it to GROW -> realloc -> all slices get invalidated!
    //
    // So, to mitigate that, either:
    // - [x] listing and converting to JSON must become one locked operation
    // - or: the iterator must make copies of the strings
    self.lock.lock();
    defer self.lock.unlock();
    var it = JsonUserIteratorWithRaceCondition.init(&self.users);
    while (it.next()) |user| {
        try out.append(user);
    }
    std.debug.assert(self.users.count() == out.items.len);
    std.debug.assert(self.count == out.items.len);
}

const JsonUserIteratorWithRaceCondition = struct {
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
            // we get a pointer to the internal user. so it should be safe to
            // create slices from its first and last name buffers
            //
            // SEE ABOVE NOTE regarding race condition why this is can be problematic
            var user: User = .{
                // we don't need .* syntax but want to make it obvious
                .id = pUser.*.id,
                .first_name = pUser.*.firstnamebuf[0..pUser.*.firstnamelen],
                .last_name = pUser.*.lastnamebuf[0..pUser.*.lastnamelen],
            };
            if (pUser.*.firstnamelen == 0) {
                user.first_name = "";
            }
            if (pUser.*.lastnamelen == 0) {
                user.last_name = "";
            }
            return user;
        }
        return null;
    }
};
