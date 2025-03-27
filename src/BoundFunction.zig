const std = @import("std");

/// Helper function that returns a function type with ArgType prepended to the
/// function's args.
/// Example:
///     Func    = fn(usize) void
///     ArgType = *Instance
///     --------------------------
///     Result  = fn(*Instance, usize) void
fn PrependFnArg(Func: type, ArgType: type) type {
    const fn_info = @typeInfo(Func);
    if (fn_info != .@"fn") @compileError("First argument must be a function type");

    comptime var new_params: [fn_info.@"fn".params.len + 1]std.builtin.Type.Fn.Param = undefined;
    new_params[0] = .{ .is_generic = false, .is_noalias = false, .type = ArgType };
    for (fn_info.@"fn".params, 0..) |param, i| {
        new_params[i + 1] = param;
    }

    return @Type(.{
        .@"fn" = .{
            .calling_convention = fn_info.@"fn".calling_convention,
            .is_generic = fn_info.@"fn".is_generic,
            .is_var_args = fn_info.@"fn".is_var_args,
            .return_type = fn_info.@"fn".return_type,
            .params = &new_params,
        },
    });
}

// External Generic Interface (CallbackInterface)
pub fn CallbackInterface(comptime Func: type) type {
    const func_info = @typeInfo(Func);
    if (func_info != .@"fn") @compileError("CallbackInterface expects a function type");
    if (func_info.@"fn".is_generic) @compileError("CallbackInterface does not support generic functions");
    if (func_info.@"fn".is_var_args) @compileError("CallbackInterface does not support var_args functions");

    const ArgsTupleType = std.meta.ArgsTuple(Func);
    const ReturnType = func_info.@"fn".return_type.?;
    const FnPtrType = *const fn (ctx: ?*const anyopaque, args: ArgsTupleType) ReturnType;

    return struct {
        ctx: ?*const anyopaque,
        callFn: FnPtrType,
        pub const Interface = @This();

        pub fn call(self: Interface, args: ArgsTupleType) ReturnType {
            if (self.ctx == null) @panic("Called uninitialized CallbackInterface");
            if (ReturnType == void) {
                self.callFn(self.ctx, args);
            } else {
                return self.callFn(self.ctx, args);
            }
        }
    };
}

pub fn Bind(Instance: type, Func: type) type {
    const func_info = @typeInfo(Func);
    if (func_info != .@"fn") @compileError("Bind expects a function type as second parameter");
    if (func_info.@"fn".is_generic) @compileError("Binding generic functions is not supported");
    if (func_info.@"fn".is_var_args) @compileError("Binding var_args functions is not currently supported");

    const ReturnType = func_info.@"fn".return_type.?;
    const OriginalParams = func_info.@"fn".params; // Needed for comptime loops
    const ArgsTupleType = std.meta.ArgsTuple(Func);
    const InstanceMethod = PrependFnArg(Func, *Instance);
    const InterfaceType = CallbackInterface(Func);

    return struct {
        instance: *Instance,
        method: *const InstanceMethod,
        pub const BoundFunction = @This();

        // Trampoline function using runtime tuple construction
        fn callDetached(ctx: ?*const anyopaque, args: ArgsTupleType) ReturnType {
            if (ctx == null) @panic("callDetached called with null context");
            const self: *const BoundFunction = @ptrCast(@alignCast(ctx.?));

            // 1. Define the tuple type needed for the call: .{*Instance, OriginalArgs...}
            const CallArgsTupleType = comptime T: {
                var tuple_fields: [OriginalParams.len + 1]std.builtin.Type.StructField = undefined;
                // Field 0: *Instance type
                tuple_fields[0] = .{
                    .name = "0",
                    .type = @TypeOf(self.instance),
                    .default_value_ptr = null,
                    .is_comptime = false,
                    .alignment = 0,
                };
                // Fields 1..N: Original argument types (use ArgsTupleType fields)
                for (std.meta.fields(ArgsTupleType), 0..) |field, i| {
                    tuple_fields[i + 1] = .{
                        .name = std.fmt.comptimePrint("{d}", .{i + 1}),
                        .type = field.type,
                        .default_value_ptr = null,
                        .is_comptime = false,
                        .alignment = 0,
                    };
                }
                break :T @Type(.{ .@"struct" = .{
                    .layout = .auto,
                    .fields = &tuple_fields,
                    .decls = &.{},
                    .is_tuple = true,
                } });
            };

            // 2. Create and populate the tuple at runtime
            var call_args_tuple: CallArgsTupleType = undefined;
            @field(call_args_tuple, "0") = self.instance; // Set the instance pointer

            // Copy original args from 'args' tuple to 'call_args_tuple'
            comptime var i = 0;
            inline while (i < OriginalParams.len) : (i += 1) {
                const src_field_name = comptime std.fmt.comptimePrint("{}", .{i});
                const dest_field_name = comptime std.fmt.comptimePrint("{}", .{i + 1});
                @field(call_args_tuple, dest_field_name) = @field(args, src_field_name);
            }

            // 3. Perform the call using the populated tuple
            if (ReturnType == void) {
                @call(.auto, self.method, call_args_tuple);
            } else {
                return @call(.auto, self.method, call_args_tuple);
            }
        }

        pub fn interface(self: *const BoundFunction) InterfaceType {
            return .{ .ctx = @ptrCast(self), .callFn = &callDetached };
        }

        // Direct call convenience method using runtime tuple construction
        pub fn call(self: *const BoundFunction, args: anytype) ReturnType {
            // 1. Verify 'args' is the correct ArgsTupleType or compatible tuple literal
            // (This check could be more robust if needed)
            if (@TypeOf(args) != ArgsTupleType) {
                // Attempt reasonable check for tuple literal compatibility
                if (@typeInfo(@TypeOf(args)) != .@"struct" or !@typeInfo(@TypeOf(args)).@"struct".is_tuple) {
                    @compileError(std.fmt.comptimePrint(
                        "Direct .call expects arguments as a tuple literal compatible with {}, found type {}",
                        .{ ArgsTupleType, @TypeOf(args) },
                    ));
                }
                // Further check field count/types if necessary
                if (std.meta.fields(@TypeOf(args)).len != OriginalParams.len) {
                    @compileError(std.fmt.comptimePrint(
                        "Direct .call tuple literal has wrong number of arguments (expected {}, got {}) for {}",
                        .{ OriginalParams.len, std.meta.fields(@TypeOf(args)).len, ArgsTupleType },
                    ));
                }
                // Could add type checks per field here too
            }

            // 2. Define the tuple type needed for the call: .{*Instance, OriginalArgs...}
            const CallArgsTupleType = comptime T: {
                var tuple_fields: [OriginalParams.len + 1]std.builtin.Type.StructField = undefined;
                tuple_fields[0] = .{
                    .name = "0",
                    .type = @TypeOf(self.instance),
                    .default_value_ptr = null,
                    .is_comptime = false,
                    .alignment = 0,
                };
                for (std.meta.fields(ArgsTupleType), 0..) |field, i| {
                    tuple_fields[i + 1] = .{
                        .name = std.fmt.comptimePrint("{d}", .{i + 1}),
                        .type = field.type,
                        .default_value_ptr = null,
                        .is_comptime = false,
                        .alignment = 0,
                    };
                }
                break :T @Type(.{ .@"struct" = .{
                    .layout = .auto,
                    .fields = &tuple_fields,
                    .decls = &.{},
                    .is_tuple = true,
                } });
            };

            // 3. Create and populate the tuple at runtime
            var call_args_tuple: CallArgsTupleType = undefined;
            @field(call_args_tuple, "0") = self.instance;

            comptime var i = 0;
            inline while (i < OriginalParams.len) : (i += 1) {
                const field_name = comptime std.fmt.comptimePrint("{}", .{i});
                // Check if field exists in args (useful for struct literals, less for tuples)
                // For tuple literals, direct access should work if type check passed.
                // if (@hasField(@TypeOf(args), field_name)) { ... }
                const dest_field_name = comptime std.fmt.comptimePrint("{}", .{i + 1});
                @field(call_args_tuple, dest_field_name) = @field(args, field_name);
            }

            // 4. Perform the call using the populated tuple
            if (ReturnType == void) {
                @call(.auto, self.method, call_args_tuple);
            } else {
                return @call(.auto, self.method, call_args_tuple);
            }
        }

        pub fn init(instance_: *Instance, method_: *const InstanceMethod) BoundFunction {
            return .{ .instance = instance_, .method = method_ };
        }
    };
}

const testing = std.testing;

test "Bind Direct Call" {
    const Person = struct {
        name: []const u8,
        _buf: [1024]u8 = undefined,
        pub fn speak(self: *@This(), msg: []const u8) ![]const u8 {
            return std.fmt.bufPrint(&self._buf, "{s}: {s}", .{ self.name, msg });
        }
    };
    const FuncSig = fn ([]const u8) anyerror![]const u8;
    var p = Person{ .name = "Alice" };
    const bound = Bind(Person, FuncSig).init(&p, &Person.speak);
    const res = try bound.call(.{"Hi"}); // Pass tuple literal
    try testing.expectEqualStrings("Alice: Hi", res);
}

test "BindInterface Call (External)" {
    const Person = struct {
        name: []const u8,
        _buf: [1024]u8 = undefined,
        pub fn speak(self: *@This(), message: []const u8) ![]const u8 {
            return std.fmt.bufPrint(&self._buf, "{s} says: >>{s}!<<\n", .{ self.name, message });
        }
    };
    const CallBack = fn ([]const u8) anyerror![]const u8;
    var alice: Person = .{ .name = "Alice" };
    const BoundSpeak = Bind(Person, CallBack);
    const bound_speak = BoundSpeak.init(&alice, &Person.speak);
    var alice_interface = bound_speak.interface();
    const greeting = try alice_interface.call(.{"Hello"}); // Pass tuple literal
    try testing.expectEqualStrings("Alice says: >>Hello!<<\n", greeting);
}

test "BindInterface Polymorphism (External)" {
    const Person = struct {
        name: []const u8,
        _buf: [1024]u8 = undefined,
        pub fn speak(self: *@This(), message: []const u8) ![]const u8 {
            return std.fmt.bufPrint(&self._buf, "{s} says: >>{s}!<<\n", .{ self.name, message });
        }
    };
    const Dog = struct {
        name: []const u8,
        _buf: [1024]u8 = undefined,
        pub fn bark(self: *@This(), message: []const u8) ![]const u8 {
            return std.fmt.bufPrint(&self._buf, "{s} barks: >>{s}!<<\n", .{ self.name, message });
        }
    };
    const CallBack = fn ([]const u8) anyerror![]const u8;
    const CbInterface = CallbackInterface(CallBack);

    var alice: Person = .{ .name = "Alice" };
    const bound_alice = Bind(Person, CallBack).init(&alice, &Person.speak);
    const alice_interface = bound_alice.interface();

    var bob: Dog = .{ .name = "Bob" };
    const bound_bob = Bind(Dog, CallBack).init(&bob, &Dog.bark);
    const bob_interface = bound_bob.interface();

    const interfaces = [_]CbInterface{ alice_interface, bob_interface };
    var results: [2][]const u8 = undefined;
    for (interfaces, 0..) |iface, i| {
        results[i] = try iface.call(.{"Test"});
    } // Pass tuple literal

    try testing.expectEqualStrings("Alice says: >>Test!<<\n", results[0]);
    try testing.expectEqualStrings("Bob barks: >>Test!<<\n", results[1]);
}

test "Void Return Type (External Interface)" {
    var counter: u32 = 0;
    const Counter = struct {
        count: *u32,
        pub fn increment(self: *@This(), amount: u32) void {
            self.count.* += amount;
        }
    };
    const Decrementer = struct {
        count: *u32,
        pub fn decrement(self: *@This(), amount: u32) void {
            self.count.* -= amount;
        }
    };
    const IncrementFn = fn (u32) void;
    const IncInterface = CallbackInterface(IncrementFn);

    var my_counter = Counter{ .count = &counter };
    const bound_inc = Bind(Counter, IncrementFn).init(&my_counter, &Counter.increment);
    bound_inc.call(.{5});
    try testing.expectEqual(@as(u32, 5), counter);

    var my_dec = Decrementer{ .count = &counter };
    const bound_dec = Bind(Decrementer, IncrementFn).init(&my_dec, &Decrementer.decrement);

    const iface1 = bound_inc.interface();
    const iface2 = bound_dec.interface();
    const void_ifaces = [_]IncInterface{ iface1, iface2 };

    void_ifaces[0].call(.{3}); // counter = 5 + 3 = 8
    try testing.expectEqual(@as(u32, 8), counter);
    void_ifaces[1].call(.{2}); // counter = 8 - 2 = 6
    try testing.expectEqual(@as(u32, 6), counter);
}
