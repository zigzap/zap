const std = @import("std");
const zap = @import("zap");

fn on_request(r: zap.SimpleRequest) void {
    const template = "{{=<< >>=}}* Users:\r\n<<#users>><<id>>. <<& name>> (<<name>>)\r\n<</users>>\r\nNested: <<& nested.item >>.";
    const p = zap.MustacheNew(template) catch return;
    defer zap.MustacheFree(p);
    const User = struct {
        name: []const u8,
        id: isize,
    };
    const ret = zap.MustacheBuild(p, .{
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
            r.sendBody("<html><body><h1>MustacheBuild() failed!</h1></body></html>") catch return;
        }
    } else |err| {
        std.debug.print("Error while setting content type: {}\n", .{err});
    }
}

pub fn main() !void {
    var listener = zap.SimpleHttpListener.init(.{
        .port = 3000,
        .on_request = on_request,
        .log = true,
        .max_clients = 100000,
    });
    try listener.listen();

    // zap.enableDebugLog();
    // zap.debug("ZAP debug logging is on\n", .{});

    // we can also use facilio logging
    // zap.Log.fio_set_log_level(zap.Log.fio_log_level_debug);
    // zap.Log.fio_log_debug("hello from fio\n");

    std.debug.print("Listening on 0.0.0.0:3000\n", .{});

    // start worker threads
    zap.start(.{
        .threads = 1,
        .workers = 1,
    });
}
