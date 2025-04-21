//!
//! Part of the Zap examples.
//!
//! Build me with `zig build     routes`.
//! Run   me with `zig build run-routes`.
//!
const std = @import("std");
const zap = @import("zap");

// NOTE: this is a super simplified example, just using a hashmap to map
// from HTTP path to request function.
fn dispatch_routes(r: zap.Request) !void {
    // dispatch
    if (r.path) |the_path| {
        if (routes.get(the_path)) |foo| {
            try foo(r);
            return;
        }
    }
    // or default: present menu
    try r.sendBody(
        \\ <html>
        \\   <body>
        \\     <p><a href="/static">static</a></p>
        \\     <p><a href="/dynamic">dynamic</a></p>
        \\   </body>
        \\ </html>
    );
}

fn static_site(r: zap.Request) !void {
    try r.sendBody("<html><body><h1>Hello from STATIC ZAP!</h1></body></html>");
}

var dynamic_counter: i32 = 0;
fn dynamic_site(r: zap.Request) !void {
    dynamic_counter += 1;
    var buf: [128]u8 = undefined;
    const filled_buf = try std.fmt.bufPrintZ(
        &buf,
        "<html><body><h1>Hello # {d} from DYNAMIC ZAP!!!</h1></body></html>",
        .{dynamic_counter},
    );
    try r.sendBody(filled_buf);
}

fn setup_routes(a: std.mem.Allocator) !void {
    routes = std.StringHashMap(zap.HttpRequestFn).init(a);
    try routes.put("/static", static_site);
    try routes.put("/dynamic", dynamic_site);
}

var routes: std.StringHashMap(zap.HttpRequestFn) = undefined;

pub fn main() !void {
    try setup_routes(std.heap.page_allocator);
    var listener = zap.HttpListener.init(.{
        .port = 3000,
        .on_request = dispatch_routes,
        .log = true,
    });
    try listener.listen();

    std.debug.print("Listening on 0.0.0.0:3000\n", .{});

    zap.start(.{
        .threads = 2,
        .workers = 2,
    });
}
