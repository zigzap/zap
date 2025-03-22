const std = @import("std");

/// Bind a function with specific signature to a method of any instance of a given type
pub fn OldBind(func: anytype, instance: anytype) OldBound(@typeInfo(@TypeOf(instance)).pointer.child, func) {
    const Instance = @typeInfo(@TypeOf(instance)).pointer.child;
    return OldBound(Instance, func).init(@constCast(instance));
}

pub fn OldBound(Instance: type, func: anytype) type {
    // Verify Func is a function type
    const func_info = @typeInfo(@TypeOf(func));
    if (func_info != .@"fn") {
        @compileError("OldBound expexts a function type as second parameter");
    }

    // Verify first parameter is pointer to Instance type
    const params = func_info.@"fn".params;
    if (params.len == 0 or (params[0].type != *Instance and params[0].type != *const Instance)) {
        @compileError("Function's first parameter must be " ++ @typeName(Instance) ++ " but got: " ++ @typeName(params[0].type.?));
    }

    return struct {
        instance: *Instance,

        const OldBoundFunction = @This();

        pub fn call(self: OldBoundFunction, args: anytype) func_info.@"fn".return_type.? {
            return @call(.auto, func, .{self.instance} ++ args);
        }

        // convenience init
        pub fn init(instance_: *Instance) OldBoundFunction {
            return .{ .instance = instance_ };
        }
    };
}

test "OldBound" {
    const Person = struct {
        name: []const u8,
        _buf: [1024]u8 = undefined,

        // takes const instance
        pub fn greet(self: *const @This(), gpa: std.mem.Allocator, greeting: []const u8) ![]const u8 {
            return std.fmt.allocPrint(gpa, "{s}, {s}!\n", .{ greeting, self.name });
        }

        // takes non-const instance
        pub fn farewell(self: *@This(), message: []const u8) ![]const u8 {
            return std.fmt.bufPrint(self._buf[0..], "{s}, {s}!\n", .{ message, self.name });
        }
    };

    var alice: Person = .{ .name = "Alice" };

    // creation variant a: manually instantiate
    const bound_greet: OldBound(Person, Person.greet) = .{ .instance = &alice };

    // creation variant b: call init function
    const bound_farewell = OldBound(Person, Person.farewell).init(&alice);

    const ta = std.testing.allocator;
    const greeting = try bound_greet.call(.{ ta, "Hello" });
    defer ta.free(greeting);

    try std.testing.expectEqualStrings("Hello, Alice!\n", greeting);
    try std.testing.expectEqualStrings("Goodbye, Alice!\n", try bound_farewell.call(.{"Goodbye"}));
}

test OldBind {
    const Person = struct {
        name: []const u8,
        _buf: [1024]u8 = undefined,

        // takes const instance
        pub fn greet(self: *const @This(), gpa: std.mem.Allocator, greeting: []const u8) ![]const u8 {
            return std.fmt.allocPrint(gpa, "{s}, {s}!\n", .{ greeting, self.name });
        }

        // takes non-const instance
        pub fn farewell(self: *@This(), message: []const u8) ![]const u8 {
            return std.fmt.bufPrint(self._buf[0..], "{s}, {s}!\n", .{ message, self.name });
        }
    };

    var alice: Person = .{ .name = "Alice" };

    const bound_greet = OldBind(Person.greet, &alice);
    const bound_farewell = OldBind(Person.farewell, &alice);

    const ta = std.testing.allocator;
    const greeting = try bound_greet.call(.{ ta, "Hello" });
    defer ta.free(greeting);

    try std.testing.expectEqualStrings("Hello, Alice!\n", greeting);
    try std.testing.expectEqualStrings("Goodbye, Alice!\n", try bound_farewell.call(.{"Goodbye"}));
}

/// Bind functions like `fn(a: X, b: Y)` to an instance of a struct. When called, the instance's `pub fn(self: *This(), a: X, b: Y)` is called.
///
/// make callbacks stateful when they're not meant to be?
// pub fn Bound(Instance: type, Func: type, func: anytype) type {
pub fn Bound(Instance: type, Func: type, DFunc: type) type {

    // TODO: construct DFunc on-the-fly

    // Verify Func is a function type
    const func_info = @typeInfo(Func);
    // if (func_info != .@"fn") {
    //     @compileError("Bound expexts a function type as second parameter");
    // }
    return struct {
        instance: *Instance,
        foo: *const DFunc,

        const BoundFunction = @This();

        pub fn call(self: BoundFunction, args: anytype) func_info.@"fn".return_type.? {
            return @call(.auto, self.foo, .{self.instance} ++ args);
        }

        // convenience init
        pub fn init(instance_: *Instance, foo_: *const DFunc) BoundFunction {
            return .{ .instance = instance_, .foo = foo_ };
        }
    };
}

test Bound {
    const Person = struct {
        name: []const u8,
        _buf: [1024]u8 = undefined,

        pub fn speak(self: *@This(), message: []const u8) ![]const u8 {
            return std.fmt.bufPrint(self._buf[0..], "{s} says: >>{s}!<<\n", .{ self.name, message });
        }
    };

    const CallBack = fn ([]const u8) anyerror![]const u8;

    var alice: Person = .{ .name = "Alice" };

    const bound_greet = Bound(Person, CallBack, @TypeOf(Person.speak)).init(&alice, &Person.speak);

    const greeting = try bound_greet.call(.{"Hello"});

    try std.testing.expectEqualStrings("Alice says: >>Hello!<<\n", greeting);
}
