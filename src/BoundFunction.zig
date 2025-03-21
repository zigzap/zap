const std = @import("std");

// attempt 1: explicitly typed
pub fn Create(Fn: type, Instance: type) type {
    return struct {
        instance: *Instance,
        function: *const Fn,

        const BoundFunction = @This();

        pub fn init(function: *const Fn, instance: *Instance) BoundFunction {
            return .{
                .instance = instance,
                .function = function,
            };
        }

        pub fn call(self: *const BoundFunction, arg: anytype) void {
            @call(.auto, self.function, .{ self.instance, arg });
        }
    };
}

test "BoundFunction" {
    const X = struct {
        field: usize = 0,
        pub fn foo(self: *@This(), other: usize) void {
            std.debug.print("field={d}, other={d}\n", .{ self.field, other });
        }
    };

    var x: X = .{ .field = 27 };

    var bound = Create(@TypeOf(X.foo), X).init(X.foo, &x);
    bound.call(3);
}
