const std = @import("std");
const zap = @import("zap");

const Handler = struct {
    var alloc: std.mem.Allocator = undefined;

    pub fn on_request(r: zap.SimpleRequest) void {
        // check for FORM parameters
        r.parseBody() catch |err| {
            std.log.err("Parse Body error: {any}. Expected if body is empty", .{err});
        };

        if (r.body) |body| {
            std.log.info("Body length is {any}\n", .{body.len});
            std.log.info("Body is {s}\n", .{body});
        }
        // check for query params (for ?terminate=true)
        r.parseQuery();

        var param_count = r.getParamCount();
        std.log.info("param_count: {}", .{param_count});

        // iterate over all params
        //
        // HERE WE HANDLE THE BINARY FILE
        //
        const params = r.parametersToOwnedList(Handler.alloc, false) catch unreachable;
        defer params.deinit();
        for (params.items) |kv| {
            if (kv.value) |v| {
                std.debug.print("\n", .{});
                std.log.info("Param `{s}` in owned list is {any}\n", .{ kv.key.str, v });
                switch (v) {
                    zap.HttpParam.Hash_Binfile => |*file| {
                        const filename = file.filename orelse "(no filename)";
                        const mimetype = file.mimetype orelse "(no mimetype)";
                        const data = file.data orelse "";

                        std.log.debug("    filename: `{s}`\n", .{filename});
                        std.log.debug("    mimetype: {s}\n", .{mimetype});
                        std.log.debug("    contents: {any}\n", .{data});
                    },
                    else => {
                        // might be a string param, we don't care
                        // let's just get it as string
                        if (r.getParamStr(kv.key.str, Handler.alloc, false)) |maybe_str| {
                            const value: []const u8 = if (maybe_str) |s| s.str else "(no value)";
                            std.log.debug("   {s} = {s}", .{ kv.key.str, value });
                        } else |err| {
                            std.log.err("Error: {any}\n", .{err});
                        }
                    },
                }
            }
        }

        // check if we received a terminate=true parameter
        if (r.getParamStr("terminate", Handler.alloc, false)) |maybe_str| {
            if (maybe_str) |*s| {
                defer s.deinit();
                std.log.info("?terminate={s}\n", .{s.str});
                if (std.mem.eql(u8, s.str, "true")) {
                    zap.fio_stop();
                }
            }
        } else |err| {
            std.log.err("cannot check for terminate param: {any}\n", .{err});
        }
    }
};

pub fn main() !void {
    var gpa = std.heap.GeneralPurposeAllocator(.{
        .thread_safe = true,
    }){};
    var allocator = gpa.allocator();

    Handler.alloc = allocator;

    // setup listener
    var listener = zap.SimpleHttpListener.init(
        .{
            .port = 3000,
            .on_request = Handler.on_request,
            .log = true,
            .max_clients = 10,
            .max_body_size = 1 * 1024,
        },
    );
    zap.enableDebugLog();
    try listener.listen();
    std.log.info("\n\nURL is http://localhost:3000\n", .{});
    std.log.info("\ncurl -v --request POST -F img=@test012345.bin http://127.0.0.1:3000\n", .{});
    std.log.info("\n\nTerminate with CTRL+C or by sending query param terminate=true\n", .{});

    zap.start(.{
        .threads = 1,
        .workers = 0,
    });
}
