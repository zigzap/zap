const std = @import("std");
const zap = @import("zap");

// We send ourselves a request with a cookie
fn makeRequest(a: std.mem.Allocator, url: []const u8) !void {
    const uri = try std.Uri.parse(url);

    var h = std.http.Headers{ .allocator = a };
    defer h.deinit();

    var http_client: std.http.Client = .{ .allocator = a };
    defer http_client.deinit();

    var req = try http_client.request(.GET, uri, h, .{});
    defer req.deinit();

    try req.headers.append("cookie", "ZIG_ZAP=awesome");
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

            r.parseCookies(false);

            var cookie_count = r.getCookiesCount();
            std.log.info("cookie_count: {}", .{cookie_count});

            // iterate over all cookies as strings
            var strCookies = r.cookiesToOwnedStrList(alloc, false) catch unreachable;
            defer strCookies.deinit();
            std.debug.print("\n", .{});
            for (strCookies.items) |kv| {
                std.log.info("CookieStr `{s}` is `{s}`", .{ kv.key.str, kv.value.str });
            }

            std.debug.print("\n", .{});

            // // iterate over all cookies
            const cookies = r.cookiesToOwnedList(alloc, false) catch unreachable;
            defer cookies.deinit();
            for (cookies.items) |kv| {
                std.log.info("cookie `{s}` is {any}", .{ kv.key.str, kv.value });
            }

            // let's get cookie "ZIG_ZAP" by name
            std.debug.print("\n", .{});
            if (r.getCookieStr("ZIG_ZAP", alloc, false)) |maybe_str| {
                if (maybe_str) |*s| {
                    defer s.deinit();

                    std.log.info("Cookie ZIG_ZAP = {s}", .{s.str});
                } else {
                    std.log.info("Cookie ZIG_ZAP not found!", .{});
                }
            }
            // since we provided "false" for duplicating strings in the call
            // to getCookieStr(), there won't be an allocation error
            else |err| {
                std.log.err("ERROR!\n", .{});
                std.log.err("cannot check for `ZIG_ZAP` cookie: {any}\n", .{err});
            }

            r.setCookie(.{
                .name = "rene",
                .value = "rocksai",
                .path = "/xxx",
                .domain = "yyy",
                // if we leave .max_age_s = 0 -> session cookie
                .max_age_s = 60,
                //
                // check out other params: domain, path, secure, http_only
            }) catch |err| {
                std.log.err("ERROR!\n", .{});
                std.log.err("cannot set cookie: {any}\n", .{err});
            };

            r.sendBody("Hello") catch unreachable;
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
    std.log.info("\n\nTerminate with CTRL+C", .{});

    const thread = try makeRequestThread(allocator, "http://127.0.0.1:3000");
    defer thread.join();
    zap.start(.{
        .threads = 1,
        .workers = 0,
    });
}
