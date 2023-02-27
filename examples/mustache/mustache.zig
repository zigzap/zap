const std = @import("std");
const zap = @import("zap");

fn on_request_verbose(r: zap.SimpleRequest) void {
    if (r.path) |the_path| {
        std.debug.print("PATH: {s}\n", .{the_path});
    }

    if (r.query) |the_query| {
        std.debug.print("QUERY: {s}\n", .{the_query});
    }
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
    if (ret.str()) |s| {
        std.debug.print("{s}\n", .{s});
        _ = r.sendBody(s);
    }
    _ = r.sendBody("<html><body><h1>MustacheBuild() failed!</h1></body></html>");
}

fn on_request_minimal(r: zap.SimpleRequest) void {
    _ = r.sendBody("<html><body><h1>Hello from ZAP!!!</h1></body></html>");
}

pub fn main() !void {
    var listener = zap.SimpleHttpListener.init(.{
        .port = 3000,
        .on_request = on_request_verbose,
        .log = true,
        .max_clients = 100000,
    });
    try listener.listen();

    std.debug.print("Listening on 0.0.0.0:3000\n", .{});

    // start worker threads
    zap.start(.{
        .threads = 1,
        .workers = 1,
    });
}
