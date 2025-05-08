//!
//! Part of the Zap examples.
//!
//! Build me with `zig build     http_params`.
//! Run   me with `zig build run-http_params`.
//!
const std = @import("std");
const zap = @import("zap");

// set default log level to .info and ZAP log level to .debug
pub const std_options: std.Options = .{
    .log_level = .info,
    .log_scope_levels = &[_]std.log.ScopeLevel{
        .{ .scope = .zap, .level = .debug },
    },
};

// We send ourselves a request
fn makeRequest(a: std.mem.Allocator, url: []const u8) !void {
    const uri = try std.Uri.parse(url);

    var http_client: std.http.Client = .{ .allocator = a };
    defer http_client.deinit();

    var server_header_buffer: [2048]u8 = undefined;
    var req = try http_client.open(.GET, uri, .{
        .server_header_buffer = &server_header_buffer,
    });
    defer req.deinit();

    try req.send();
    try req.wait();
}

fn makeRequestThread(a: std.mem.Allocator, url: []const u8) !std.Thread {
    return try std.Thread.spawn(.{}, makeRequest, .{ a, url });
}

// here we go
pub fn main() !void {
    var gpa = std.heap.GeneralPurposeAllocator(.{
        .thread_safe = true,
    }){};
    defer _ = gpa.detectLeaks();

    const allocator = gpa.allocator();

    const Handler = struct {
        var alloc: std.mem.Allocator = undefined;

        pub fn on_request(r: zap.Request) !void {
            std.debug.print("\n=====================================================\n", .{});
            defer std.debug.print("=====================================================\n\n", .{});

            // check for FORM parameters
            r.parseBody() catch |err| {
                std.log.err("Parse Body error: {any}. Expected if body is empty", .{err});
            };

            // check for query parameters
            r.parseQuery();

            const param_count = r.getParamCount();
            std.log.info("param_count: {}", .{param_count});

            // ================================================================
            // Access RAW params from querystring
            // ================================================================

            // let's get param "one" by name
            std.debug.print("\n", .{});
            if (r.getParamSlice("one")) |value| {
                std.log.info("Param one = {s}", .{value});
            } else {
                std.log.info("Param one not found!", .{});
            }

            var arg_it = r.getParamSlices();
            while (arg_it.next()) |param| {
                std.log.info("ParamStr `{s}` is `{s}`", .{ param.name, param.value });
            }

            // ================================================================
            // Access DECODED and typed params
            // ================================================================

            // iterate over all params as strings
            var strparams = try r.parametersToOwnedStrList(alloc);
            defer strparams.deinit();
            std.debug.print("\n", .{});
            for (strparams.items) |kv| {
                std.log.info("ParamStr `{s}` is `{s}`", .{ kv.key, kv.value });
            }

            std.debug.print("\n", .{});

            // iterate over all params
            const params = try r.parametersToOwnedList(alloc);
            defer params.deinit();
            for (params.items) |kv| {
                std.log.info("Param `{s}` is {any}", .{ kv.key, kv.value });
            }

            // let's get param "one" by name
            std.debug.print("\n", .{});
            if (r.getParamStr(alloc, "one")) |maybe_str| {
                if (maybe_str) |s| {
                    defer alloc.free(s);
                    std.log.info("Param one = {s}", .{s});
                } else {
                    std.log.info("Param one not found!", .{});
                }
            } else |err| {
                std.log.err("cannot check for `one` param: {any}", .{err});
            }

            // check if we received a terminate=true parameter
            if (r.getParamSlice("terminate")) |s| {
                if (std.mem.eql(u8, s, "true")) {
                    zap.stop();
                }
            }
        }
    };

    Handler.alloc = allocator;

    // setup listener
    var listener = zap.HttpListener.init(
        .{
            .port = 3000,
            .on_request = Handler.on_request,
            .log = false,
            .max_clients = 10,
            .max_body_size = 1 * 1024,
        },
    );

    try listener.listen();
    std.log.info("\n\nTerminate with CTRL+C or by sending query param terminate=true", .{});

    const thread = try makeRequestThread(allocator, "http://127.0.0.1:3000/?one=1&two=2&string=hello+world&float=6.28&bool=true");
    defer thread.join();
    zap.start(.{
        .threads = 1,
        .workers = 1,
    });
}
