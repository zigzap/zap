const std = @import("std");
const zap = @import("zap");

fn on_request(r: zap.SimpleRequest) void {
    const m = r.method orelse "";
    const p = r.path orelse "/";
    const qm = if (r.query) |_| "?" else "";
    const qq = r.query orelse "";

    std.debug.print(">> {s} {s}{s}{s}\n", .{ m, p, qm, qq });

    if (r.body) |the_body| {
        std.debug.print(">> BODY: {s}\n", .{the_body});
    }

    r.setContentTypeFromPath();
    _ = r.sendBody(
        \\ <html><body>
        \\   <h1>Hello from ZAP!!!</h1>
        \\   <form action="/" method="post">
        \\     <label for="fname">First name:</label><br>
        \\     <input type="text" id="fname" name="fname"><br>
        \\     <label for="lname">Last name:</label><br>
        \\     <input type="text" id="lname" name="lname">
        \\     <button>Send</button>
        \\   </form>
        \\ </body></html>
    );
}

pub fn main() !void {
    var listener = zap.SimpleHttpListener.init(.{
        .port = 3000,
        .on_request = on_request,
        .log = false,
    });
    try listener.listen();

    std.debug.print("Listening on 0.0.0.0:3000\n", .{});

    // start worker threads
    zap.start(.{
        .threads = 2,
        .workers = 2,
    });
}
