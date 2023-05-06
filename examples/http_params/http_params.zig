const std = @import("std");
const zap = @import("zap");

// We send ourselves a request
fn makeRequest(a: std.mem.Allocator, url: []const u8) !void {
    const uri = try std.Uri.parse(url);

    var h = std.http.Headers{ .allocator = a };
    defer h.deinit();

    var http_client: std.http.Client = .{ .allocator = a };
    defer http_client.deinit();

    var req = try http_client.request(.GET, uri, h, .{});
    defer req.deinit();

    try req.start();
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
    var allocator = gpa.allocator();

    const Handler = struct {
        var alloc: std.mem.Allocator = undefined;

        pub fn on_request(r: zap.SimpleRequest) void {
            std.debug.print("\n=====================================================\n", .{});
            defer std.debug.print("=====================================================\n\n", .{});

            // check for FORM parameters
            r.parseBody() catch |err| {
                std.log.err("Parse Body error: {any}. Expected if body is empty", .{err});
            };

            // check for query parameters
            r.parseQuery();

            var param_count = r.getParamCount();
            std.log.info("param_count: {}", .{param_count});

            // iterate over all params as strings
            var strparams = r.parametersToOwnedStrList(alloc, false) catch unreachable;
            defer strparams.deinit();
            std.debug.print("\n", .{});
            for (strparams.items) |kv| {
                std.log.info("ParamStr `{s}` is `{s}`", .{ kv.key.str, kv.value.str });
            }

            std.debug.print("\n", .{});

            // iterate over all params
            const params = r.parametersToOwnedList(alloc, false) catch unreachable;
            defer params.deinit();
            for (params.items) |kv| {
                std.log.info("Param `{s}` is {any}", .{ kv.key.str, kv.value });
            }

            // let's get param "one" by name
            std.debug.print("\n", .{});
            if (r.getParamStr("one", alloc, false)) |maybe_str| {
                if (maybe_str) |*s| {
                    defer s.deinit();

                    std.log.info("Param one = {s}", .{s.str});
                } else {
                    std.log.info("Param one not found!", .{});
                }
            }
            // since we provided "false" for duplicating strings in the call
            // to getParamStr(), there won't be an allocation error
            else |err| {
                std.log.err("cannot check for `one` param: {any}\n", .{err});
            }

            // check if we received a terminate=true parameter
            if (r.getParamStr("terminate", alloc, false)) |maybe_str| {
                if (maybe_str) |*s| {
                    defer s.deinit();
                    if (std.mem.eql(u8, s.str, "true")) {
                        zap.fio_stop();
                    }
                }
            } else |err| {
                std.log.err("cannot check for terminate param: {any}\n", .{err});
            }
        }
    };

    Handler.alloc = allocator;

    // setup listener
    var listener = zap.SimpleHttpListener.init(
        .{
            .port = 3000,
            .on_request = Handler.on_request,
            .log = false,
            .max_clients = 10,
            .max_body_size = 1 * 1024,
        },
    );
    zap.enableDebugLog();
    try listener.listen();
    std.log.info("\n\nTerminate with CTRL+C or by sending query param terminate=true\n", .{});

    const thread = try makeRequestThread(allocator, "http://127.0.0.1:3000/?one=1&two=2&string=hello+world&float=6.28&bool=true");
    defer thread.join();
    zap.start(.{
        .threads = 1,
        .workers = 0,
    });
}
