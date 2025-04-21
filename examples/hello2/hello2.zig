//!
//! Part of the Zap examples.
//!
//! Build me with `zig build     hello2`.
//! Run   me with `zig build run-hello2`.
//!
const std = @import("std");
const zap = @import("zap");

fn on_request(r: zap.Request) !void {
    const m = r.methodAsEnum();
    const m_str = r.method orelse "";
    const p = r.path orelse "/";
    const qm = if (r.query) |_| "?" else "";
    const qq = r.query orelse "";

    // curl -H "special-header: hello" http://localhost:3000
    if (r.getHeader("special-header")) |clstr| {
        std.debug.print(">> Special Header: {s}\n", .{clstr});
    } else {
        std.debug.print(">> Special Header: <unknown>\n", .{});
    }
    std.debug.print(">> {s}({}) {s}{s}{s}\n", .{ m_str, m, p, qm, qq });

    if (r.body) |the_body| {
        std.debug.print(">> BODY: {s}\n", .{the_body});
    }

    try r.setContentTypeFromPath();
    try r.sendBody(
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
    var listener = zap.HttpListener.init(.{
        .port = 3000,
        .on_request = on_request,
        .log = false,
    });
    try listener.listen();

    std.debug.print(
        \\ Listening on 0.0.0.0:3000
        \\
        \\ Test me with:
        \\    curl http://localhost:3000
        \\    curl --header "special-header: test" localhost:3000
        \\
        \\ ... or open http://localhost:3000 in the browser
        \\     and watch the log output here
        \\
        \\
    , .{});

    // start worker threads
    zap.start(.{
        .threads = 2,
        .workers = 1,
    });
}
