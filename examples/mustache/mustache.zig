//!
//! Part of the Zap examples.
//!
//! Build me with `zig build     mustache`.
//! Run   me with `zig build run-mustache`.
//!
const std = @import("std");
const zap = @import("zap");
const Mustache = @import("zap").Mustache;

// set default log level to .info and ZAP log level to .debug
pub const std_options: std.Options = .{
    .log_level = .info,
    .log_scope_levels = &[_]std.log.ScopeLevel{
        .{ .scope = .zap, .level = .debug },
    },
};

fn on_request(r: zap.Request) !void {
    const template =
        \\ {{=<< >>=}}
        \\ * Users:
        \\ <<#users>>
        \\ <<id>>. <<& name>> (<<name>>)
        \\ <</users>>
        \\ Nested: <<& nested.item >>.
    ;

    var mustache = Mustache.fromData(template) catch return;
    defer mustache.deinit();

    const User = struct {
        name: []const u8,
        id: isize,
    };

    const ret = mustache.build(.{
        .users = [_]User{
            .{
                .name = "Rene",
                .id = 1,
            },
            .{
                .name = "Caro",
                .id = 6,
            },
        },
        .nested = .{
            .item = "nesting works",
        },
    });
    defer ret.deinit();

    if (r.setContentType(.TEXT)) {
        if (ret.str()) |s| {
            r.sendBody(s) catch return;
        } else {
            r.sendBody("<html><body><h1>mustacheBuild() failed!</h1></body></html>") catch return;
        }
    } else |err| {
        std.debug.print("Error while setting content type: {}\n", .{err});
    }
}

pub fn main() !void {
    var listener = zap.HttpListener.init(.{
        .port = 3000,
        .on_request = on_request,
        .log = true,
        .max_clients = 100000,
    });
    try listener.listen();

    // we can also use facilio logging
    // zap.Logging.fio_set_log_level(zap.Log.fio_log_level_debug);
    // zap.Logging.fio_log_debug("hello from fio\n");

    std.debug.print("Listening on 0.0.0.0:3000\n", .{});

    // start worker threads
    zap.start(.{
        .threads = 1,
        .workers = 1,
    });
}
