//!
//! Part of the Zap examples.
//!
//! Build me with `zig build     bindataformpost`.
//! Run   me with `zig build run-bindataformpost`.
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

const Handler = struct {
    var alloc: std.mem.Allocator = undefined;

    pub fn on_request(r: zap.Request) !void {
        // parse for FORM (body) parameters first
        r.parseBody() catch |err| {
            std.log.err("Parse Body error: {any}. Expected if body is empty", .{err});
        };

        if (r.body) |body| {
            std.log.info("Body length is {any}\n", .{body.len});
        }

        // parse potential query params (for ?terminate=true)
        r.parseQuery();

        const param_count = r.getParamCount();
        std.log.info("param_count: {}", .{param_count});

        // iterate over all params
        //
        // HERE WE HANDLE THE BINARY FILE
        //
        const params = try r.parametersToOwnedList(Handler.alloc);
        defer params.deinit();
        for (params.items) |kv| {
            if (kv.value) |v| {
                std.debug.print("\n", .{});
                std.log.info("Param `{s}` in owned list is {any}\n", .{ kv.key, v });
                switch (v) {
                    // single-file upload
                    zap.Request.HttpParam.Hash_Binfile => |*file| {
                        const filename = file.filename orelse "(no filename)";
                        const mimetype = file.mimetype orelse "(no mimetype)";
                        const data = file.data orelse "";

                        std.log.debug("    filename: `{s}`\n", .{filename});
                        std.log.debug("    mimetype: {s}\n", .{mimetype});
                        std.log.debug("    contents: {any}\n", .{data});
                    },
                    // multi-file upload
                    zap.Request.HttpParam.Array_Binfile => |*files| {
                        for (files.*.items) |file| {
                            const filename = file.filename orelse "(no filename)";
                            const mimetype = file.mimetype orelse "(no mimetype)";
                            const data = file.data orelse "";

                            std.log.debug("    filename: `{s}`\n", .{filename});
                            std.log.debug("    mimetype: {s}\n", .{mimetype});
                            std.log.debug("    contents: {any}\n", .{data});
                        }
                        files.*.deinit();
                    },
                    else => {
                        // let's just get it as its raw slice
                        const value: []const u8 = r.getParamSlice(kv.key) orelse "(no value)";
                        std.log.debug("   {s} = {s}", .{ kv.key, value });
                    },
                }
            }
        }

        // check if we received a terminate=true parameter
        if (r.getParamSlice("terminate")) |str| {
            std.log.info("?terminate={s}\n", .{str});
            if (std.mem.eql(u8, str, "true")) {
                zap.stop();
            }
        }
        try r.sendJson("{ \"ok\": true }");
    }
};

pub fn main() !void {
    var gpa = std.heap.GeneralPurposeAllocator(.{
        .thread_safe = true,
    }){};
    defer _ = gpa.detectLeaks();
    const allocator = gpa.allocator();

    Handler.alloc = allocator;

    // setup listener
    var listener = zap.HttpListener.init(
        .{
            .port = 3000,
            .on_request = Handler.on_request,
            .log = true,
            .max_clients = 10,
            .max_body_size = 10 * 1024 * 1024,
            .public_folder = ".",
        },
    );

    try listener.listen();
    std.log.info("\n\nURL is http://localhost:3000\n", .{});
    std.log.info("\ncurl -v --request POST -F img=@test012345.bin http://127.0.0.1:3000\n", .{});
    std.log.info("\n\nTerminate with CTRL+C or by sending query param terminate=true\n", .{});

    zap.start(.{
        .threads = 1,
        .workers = 1,
    });
}
