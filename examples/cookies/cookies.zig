//!
//! Part of the Zap examples.
//!
//! Build me with `zig build     cookies`.
//! Run   me with `zig build run-cookies`.
//!
const std = @import("std");
const zap = @import("zap");

// We send ourselves a request with a cookie
fn makeRequest(a: std.mem.Allocator, url: []const u8) !void {
    const uri = try std.Uri.parse(url);

    var http_client: std.http.Client = .{ .allocator = a };
    defer http_client.deinit();

    var server_header_buffer: [2048]u8 = undefined;
    var req = try http_client.open(.GET, uri, .{
        .server_header_buffer = &server_header_buffer,
        .extra_headers = &.{
            .{ .name = "cookie", .value = "ZIG_ZAP=awesome" },
        },
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
    const allocator = gpa.allocator();

    const Handler = struct {
        var alloc: std.mem.Allocator = undefined;

        pub fn on_request(r: zap.Request) !void {
            std.debug.print("\n=====================================================\n", .{});
            defer std.debug.print("=====================================================\n\n", .{});

            r.parseCookies(false); // url_encoded = false

            const cookie_count = r.getCookiesCount();
            std.log.info("cookie_count: {}", .{cookie_count});

            // iterate over all cookies as strings
            var strCookies = r.cookiesToOwnedStrList(alloc) catch unreachable;
            defer strCookies.deinit();
            std.debug.print("\n", .{});
            for (strCookies.items) |kv| {
                std.log.info("CookieStr `{s}` is `{s}`", .{ kv.key, kv.value });
                // we don't need to deinit kv.key and kv.value because we requested always_alloc=false
                // so they are just slices into the request buffer
            }

            std.debug.print("\n", .{});

            // // iterate over all cookies
            const cookies = r.cookiesToOwnedList(alloc) catch unreachable;
            defer cookies.deinit();
            for (cookies.items) |kv| {
                std.log.info("cookie `{s}` is {any}", .{ kv.key, kv.value });
            }

            // let's get cookie "ZIG_ZAP" by name
            std.debug.print("\n", .{});
            if (r.getCookieStr(alloc, "ZIG_ZAP")) |maybe_str| {
                if (maybe_str) |s| {
                    defer alloc.free(s);
                    std.log.info("Cookie ZIG_ZAP = {s}", .{s});
                } else {
                    std.log.info("Cookie ZIG_ZAP not found!", .{});
                }
            } else |err| {
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
    var listener = zap.HttpListener.init(
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
        .workers = 1,
    });
}
