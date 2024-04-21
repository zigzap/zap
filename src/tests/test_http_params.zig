const std = @import("std");
const zap = @import("zap");

fn makeRequest(a: std.mem.Allocator, url: []const u8) !void {
    var http_client: std.http.Client = .{ .allocator = a };
    defer http_client.deinit();

    _ = try http_client.fetch(.{
        .location = .{ .url = url },
    });

    zap.stop();
}

fn makeRequestThread(a: std.mem.Allocator, url: []const u8) !std.Thread {
    return try std.Thread.spawn(.{}, makeRequest, .{ a, url });
}

test "http parameters" {
    const allocator = std.testing.allocator;

    const Handler = struct {
        var alloc: std.mem.Allocator = undefined;
        var ran: bool = false;
        var param_count: isize = 0;

        var strParams: ?zap.Request.HttpParamStrKVList = null;
        var params: ?zap.Request.HttpParamKVList = null;
        var paramOneStr: ?zap.FreeOrNot = null;
        var paramOneSlice: ?[]const u8 = null;
        var paramSlices: zap.Request.ParamSliceIterator = undefined;

        pub fn on_request(r: zap.Request) void {
            ran = true;
            r.parseQuery();
            param_count = r.getParamCount();

            // true -> make copies of temp strings
            strParams = r.parametersToOwnedStrList(alloc, true) catch unreachable;

            // true -> make copies of temp strings
            params = r.parametersToOwnedList(alloc, true) catch unreachable;

            var maybe_str = r.getParamStr(alloc, "one", true) catch unreachable;
            if (maybe_str) |*s| {
                paramOneStr = s.*;
            }

            paramOneSlice = blk: {
                if (r.getParamSlice("one")) |val| break :blk alloc.dupe(u8, val) catch unreachable;
                break :blk null;
            };

            paramSlices = r.getParamSlices();
        }
    };

    Handler.alloc = allocator;

    // setup listener
    var listener = zap.HttpListener.init(
        .{
            .port = 3001,
            .on_request = Handler.on_request,
            .log = false,
            .max_clients = 10,
            .max_body_size = 1 * 1024,
        },
    );
    zap.enableDebugLog();
    try listener.listen();

    const thread = try makeRequestThread(allocator, "http://127.0.0.1:3001/?one=1&two=2&string=hello+world&float=6.28&bool=true");
    defer thread.join();
    zap.start(.{
        .threads = 1,
        .workers = 1,
    });

    defer {
        if (Handler.strParams) |*p| {
            p.deinit();
        }
        if (Handler.params) |*p| {
            p.deinit();
        }
        if (Handler.paramOneStr) |*p| {
            // allocator.free(p);
            p.deinit();
        }

        if (Handler.paramOneSlice) |p| {
            Handler.alloc.free(p);
        }
    }

    try std.testing.expectEqual(Handler.ran, true);
    try std.testing.expectEqual(Handler.param_count, 5);
    try std.testing.expect(Handler.paramOneStr != null);
    try std.testing.expectEqualStrings(Handler.paramOneStr.?.str, "1");
    try std.testing.expect(Handler.paramOneSlice != null);
    try std.testing.expectEqualStrings(Handler.paramOneSlice.?, "1");
    try std.testing.expect(Handler.strParams != null);
    for (Handler.strParams.?.items, 0..) |kv, i| {
        switch (i) {
            0 => {
                try std.testing.expectEqualStrings(kv.key.str, "one");
                try std.testing.expectEqualStrings(kv.value.str, "1");
            },
            1 => {
                try std.testing.expectEqualStrings(kv.key.str, "two");
                try std.testing.expectEqualStrings(kv.value.str, "2");
            },
            2 => {
                try std.testing.expectEqualStrings(kv.key.str, "string");
                try std.testing.expectEqualStrings(kv.value.str, "hello world");
            },
            3 => {
                try std.testing.expectEqualStrings(kv.key.str, "float");
                try std.testing.expectEqualStrings(kv.value.str, "6.28");
            },
            4 => {
                try std.testing.expectEqualStrings(kv.key.str, "bool");
                try std.testing.expectEqualStrings(kv.value.str, "true");
            },
            else => return error.TooManyArgs,
        }
    }

    var pindex: usize = 0;
    while (Handler.paramSlices.next()) |param| {
        switch (pindex) {
            0 => {
                try std.testing.expectEqualStrings(param.name, "one");
                try std.testing.expectEqualStrings(param.value, "1");
            },
            1 => {
                try std.testing.expectEqualStrings(param.name, "two");
                try std.testing.expectEqualStrings(param.value, "2");
            },
            2 => {
                try std.testing.expectEqualStrings(param.name, "string");
                try std.testing.expectEqualStrings(param.value, "hello+world");
            },
            3 => {
                try std.testing.expectEqualStrings(param.name, "float");
                try std.testing.expectEqualStrings(param.value, "6.28");
            },
            4 => {
                try std.testing.expectEqualStrings(param.name, "bool");
                try std.testing.expectEqualStrings(param.value, "true");
            },
            else => return error.TooManyArgs,
        }
        pindex += 1;
    }

    for (Handler.params.?.items, 0..) |kv, i| {
        switch (i) {
            0 => {
                try std.testing.expectEqualStrings(kv.key.str, "one");
                try std.testing.expect(kv.value != null);
                switch (kv.value.?) {
                    .Int => |n| try std.testing.expectEqual(n, 1),
                    else => return error.InvalidHttpParamType,
                }
            },
            1 => {
                try std.testing.expectEqualStrings(kv.key.str, "two");
                try std.testing.expect(kv.value != null);
                switch (kv.value.?) {
                    .Int => |n| try std.testing.expectEqual(n, 2),
                    else => return error.InvalidHttpParamType,
                }
            },
            2 => {
                try std.testing.expectEqualStrings(kv.key.str, "string");
                try std.testing.expect(kv.value != null);
                switch (kv.value.?) {
                    .String => |s| try std.testing.expectEqualStrings(s.str, "hello world"),
                    else => return error.InvalidHttpParamType,
                }
            },
            3 => {
                try std.testing.expectEqualStrings(kv.key.str, "float");
                try std.testing.expect(kv.value != null);
                switch (kv.value.?) {
                    .Float => |f| try std.testing.expectEqual(f, 6.28),
                    else => return error.InvalidHttpParamType,
                }
            },
            4 => {
                try std.testing.expectEqualStrings(kv.key.str, "bool");
                try std.testing.expect(kv.value != null);
                switch (kv.value.?) {
                    .Bool => |b| try std.testing.expectEqual(b, true),
                    else => return error.InvalidHttpParamType,
                }
            },
            else => return error.TooManyArgs,
        }
    }
}
