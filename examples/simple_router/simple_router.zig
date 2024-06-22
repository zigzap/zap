const std = @import("std");
const zap = @import("zap");
const Allocator = std.mem.Allocator;

fn on_request_verbose(r: zap.Request) void {
    if (r.path) |the_path| {
        std.debug.print("PATH: {s}\n", .{the_path});
    }

    if (r.query) |the_query| {
        std.debug.print("QUERY: {s}\n", .{the_query});
    }
    r.sendBody("<html><body><h1>Hello from ZAP!!!</h1></body></html>") catch return;
}

pub const SomePackage = struct {
    const Self = @This();

    allocator: Allocator,
    a: i8,
    b: i8,

    pub fn init(allocator: Allocator, a: i8, b: i8) Self {
        return .{
            .allocator = allocator,
            .a = a,
            .b = b,
        };
    }

    pub fn getA(self: *Self, req: zap.Request) void {
        std.log.warn("get_a_requested", .{});

        const string = std.fmt.allocPrint(
            self.allocator,
            "A value is {d}\n",
            .{self.a},
        ) catch return;
        defer self.allocator.free(string);

        req.sendBody(string) catch return;
    }

    pub fn getB(self: *Self, req: zap.Request) void {
        std.log.warn("get_b_requested", .{});

        const string = std.fmt.allocPrint(
            self.allocator,
            "B value is {d}\n",
            .{self.b},
        ) catch return;
        defer self.allocator.free(string);

        req.sendBody(string) catch return;
    }

    pub fn incrementA(self: *Self, req: zap.Request) void {
        std.log.warn("increment_a_requested", .{});

        self.a += 1;

        req.sendBody("incremented A") catch return;
    }
};

fn not_found(req: zap.Request) void {
    std.debug.print("not found handler", .{});

    req.sendBody("Not found") catch return;
}

pub fn main() !void {
    var gpa = std.heap.GeneralPurposeAllocator(.{
        .thread_safe = true,
    }){};
    const allocator = gpa.allocator();

    var simpleRouter = zap.Router.init(allocator, .{
        .not_found = not_found,
    });
    defer simpleRouter.deinit();

    var somePackage = SomePackage.init(allocator, 1, 2);

    try simpleRouter.handle_func_unbound("/", on_request_verbose);

    try simpleRouter.handle_func("/geta", &somePackage, &SomePackage.getA);

    try simpleRouter.handle_func("/getb", &somePackage, &SomePackage.getB);

    try simpleRouter.handle_func("/inca", &somePackage, &SomePackage.incrementA);

    var listener = zap.HttpListener.init(.{
        .port = 3000,
        .on_request = simpleRouter.on_request_handler(),
        .log = true,
        .max_clients = 100000,
    });
    try listener.listen();

    std.debug.print("Listening on 0.0.0.0:3000\n", .{});

    // start worker threads
    zap.start(.{
        .threads = 2,

        // Must be 1 if state is shared
        .workers = 1,
    });
}
