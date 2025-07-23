const std = @import("std");
const zap = @import("zap");

// set default log level to .info and ZAP log level to .debug
pub const std_options: std.Options = .{
    .log_level = .info,
    .log_scope_levels = &[_]std.log.ScopeLevel{
        .{ .scope = .zap, .level = .debug },
    },
};

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
        var paramOneStr: ?[]const u8 = null;
        var paramOneSlice: ?[]const u8 = null;
        var paramSlices: zap.Request.ParamSliceIterator = undefined;

        pub fn on_request(r: zap.Request) !void {
            ran = true;
            r.parseQuery();
            param_count = r.getParamCount();

            // true -> make copies of temp strings
            strParams = r.parametersToOwnedStrList(alloc) catch unreachable;

            // true -> make copies of temp strings
            params = r.parametersToOwnedList(alloc) catch unreachable;

            paramOneStr = r.getParamStr(alloc, "one") catch unreachable;

            // we need to dupe it here because the request object r will get
            // invalidated at the end of the function but we need to check
            // its correctness later in the test
            paramOneSlice = if (r.getParamSlice("one")) |slice| alloc.dupe(u8, slice) catch unreachable else null;

            paramSlices = r.getParamSlices();
        }
    };

    Handler.alloc = allocator;

    // setup listener
    var listener = zap.HttpListener.init(
        .{
            .port = 3010,
            .on_request = Handler.on_request,
            .log = false,
            .max_clients = 10,
            .max_body_size = 1 * 1024,
        },
    );
    try listener.listen();

    const thread = try makeRequestThread(allocator, "http://127.0.0.1:3010/?one=1&two=2&string=hello+world&float=6.28&bool=true");
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

        if (Handler.paramOneStr) |p| {
            allocator.free(p);
        }

        if (Handler.paramOneSlice) |p| {
            allocator.free(p);
        }
    }

    try std.testing.expectEqual(true, Handler.ran);
    try std.testing.expectEqual(5, Handler.param_count);
    try std.testing.expect(Handler.paramOneStr != null);
    try std.testing.expectEqualStrings("1", Handler.paramOneStr.?);
    try std.testing.expect(Handler.paramOneSlice != null);
    try std.testing.expectEqualStrings("1", Handler.paramOneSlice.?);
    try std.testing.expect(Handler.strParams != null);
    for (Handler.strParams.?.items, 0..) |kv, i| {
        switch (i) {
            0 => {
                try std.testing.expectEqualStrings("one", kv.key);
                try std.testing.expectEqualStrings("1", kv.value);
            },
            1 => {
                try std.testing.expectEqualStrings("two", kv.key);
                try std.testing.expectEqualStrings("2", kv.value);
            },
            2 => {
                try std.testing.expectEqualStrings("string", kv.key);
                try std.testing.expectEqualStrings("hello world", kv.value);
            },
            3 => {
                try std.testing.expectEqualStrings("float", kv.key);
                try std.testing.expectEqualStrings("6.28", kv.value);
            },
            4 => {
                try std.testing.expectEqualStrings("bool", kv.key);
                try std.testing.expectEqualStrings("true", kv.value);
            },
            else => return error.TooManyArgs,
        }
    }

    var pindex: usize = 0;
    while (Handler.paramSlices.next()) |param| {
        switch (pindex) {
            0 => {
                try std.testing.expectEqualStrings("one", param.name);
                try std.testing.expectEqualStrings("1", param.value);
            },
            1 => {
                try std.testing.expectEqualStrings("two", param.name);
                try std.testing.expectEqualStrings("2", param.value);
            },
            2 => {
                try std.testing.expectEqualStrings("string", param.name);
                try std.testing.expectEqualStrings("hello+world", param.value);
            },
            3 => {
                try std.testing.expectEqualStrings("float", param.name);
                try std.testing.expectEqualStrings("6.28", param.value);
            },
            4 => {
                try std.testing.expectEqualStrings("bool", param.name);
                try std.testing.expectEqualStrings("true", param.value);
            },
            else => return error.TooManyArgs,
        }
        pindex += 1;
    }

    for (Handler.params.?.items, 0..) |kv, i| {
        switch (i) {
            0 => {
                try std.testing.expectEqualStrings("one", kv.key);
                try std.testing.expect(kv.value != null);
                switch (kv.value.?) {
                    .Int => |n| try std.testing.expectEqual(1, n),
                    else => return error.InvalidHttpParamType,
                }
            },
            1 => {
                try std.testing.expectEqualStrings("two", kv.key);
                try std.testing.expect(kv.value != null);
                switch (kv.value.?) {
                    .Int => |n| try std.testing.expectEqual(2, n),
                    else => return error.InvalidHttpParamType,
                }
            },
            2 => {
                try std.testing.expectEqualStrings("string", kv.key);
                try std.testing.expect(kv.value != null);
                switch (kv.value.?) {
                    .String => |s| try std.testing.expectEqualStrings("hello world", s),
                    else => return error.InvalidHttpParamType,
                }
            },
            3 => {
                try std.testing.expectEqualStrings("float", kv.key);
                try std.testing.expect(kv.value != null);
                switch (kv.value.?) {
                    .Float => |f| try std.testing.expectEqual(6.28, f),
                    else => return error.InvalidHttpParamType,
                }
            },
            4 => {
                try std.testing.expectEqualStrings("bool", kv.key);
                try std.testing.expect(kv.value != null);
                switch (kv.value.?) {
                    .Bool => |b| try std.testing.expectEqual(true, b),
                    else => return error.InvalidHttpParamType,
                }
            },
            else => return error.TooManyArgs,
        }
    }
}
