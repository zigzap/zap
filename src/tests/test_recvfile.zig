const std = @import("std");
const zap = @import("zap");

// set default log level to .info and ZAP log level to .debug
pub const std_options: std.Options = .{
    .log_level = .info,
    .log_scope_levels = &[_]std.log.ScopeLevel{
        .{ .scope = .zap, .level = .debug },
    },
};

const BOUNDARY = "---XcPmntPm3EGd9NaxNUPFFL";
const PARAM_NAME = "file";

const EXPECTED_CONTENT = "Hello, this is a test file.";
const EXPECTED_MIMETYPE = "text/plain";
const EXPECTED_FILENAME = "myfile.txt";

var test_error: ?anyerror = null;

fn makeRequest(allocator: std.mem.Allocator, url: []const u8) !void {
    var http_client: std.http.Client = .{ .allocator = allocator };
    defer http_client.deinit();

    const payload_wrong_line_ending = try std.fmt.allocPrint(allocator,
        \\--{s}
        \\Content-Disposition: form-data; name="{s}"; filename="{s}"
        \\Content-Type: {s}
        \\
        \\{s}
        \\--{s}--
        \\
    , .{ BOUNDARY, PARAM_NAME, EXPECTED_FILENAME, EXPECTED_MIMETYPE, EXPECTED_CONTENT.*, BOUNDARY });
    defer allocator.free(payload_wrong_line_ending);

    const payload = try std.mem.replaceOwned(u8, allocator, payload_wrong_line_ending, "\n", "\r\n");
    defer allocator.free(payload);

    const request_content_type = try std.fmt.allocPrint(
        allocator,
        "multipart/form-data; boundary={s}",
        .{BOUNDARY},
    );
    defer allocator.free(request_content_type);

    // Allocate a buffer for server headers
    _ = try http_client.fetch(.{
        .method = .POST,
        .location = .{ .url = url },
        .headers = .{
            .content_type = .{ .override = request_content_type },
        },
        .payload = payload,
    });

    zap.stop();
}

pub fn on_request(r: zap.Request) !void {
    on_request_inner(r) catch |err| {
        test_error = err;
        return;
    };
}

pub fn on_request_inner(r: zap.Request) !void {
    try r.parseBody();
    var params = try r.parametersToOwnedList(std.testing.allocator);
    defer params.deinit();

    std.testing.expect(params.items.len == 1) catch |err| {
        std.debug.print("Expected exactly one parameter, got {}\n", .{params.items.len});
        return err;
    };
    const param = params.items[0];
    std.testing.expect(param.value != null) catch |err| {
        std.debug.print("Expected parameter value to be non-null, got null\n", .{});
        return err;
    };
    const value = param.value.?;

    std.testing.expect(value == .Hash_Binfile) catch |err| {
        std.debug.print("Expected parameter type to be Hash_Binfile, got {}\n", .{value});
        return err;
    };
    const file = value.Hash_Binfile;
    std.testing.expect(file.data != null) catch |err| {
        std.debug.print("Expected file data to be non-null, got null\n", .{});
        return err;
    };
    std.testing.expect(file.mimetype != null) catch |err| {
        std.debug.print("Expected file mimetype to be non-null, got null\n", .{});
        return err;
    };
    std.testing.expect(file.filename != null) catch |err| {
        std.debug.print("Expected file filename to be non-null, got null\n", .{});
        return err;
    };

    // These will print the error if the test fails
    try std.testing.expectEqualStrings(file.data.?, &EXPECTED_CONTENT.*);
    try std.testing.expectEqualStrings(file.mimetype.?, &EXPECTED_MIMETYPE.*);
    try std.testing.expectEqualStrings(file.filename.?, &EXPECTED_FILENAME.*);
}

test "recv file" {
    const allocator = std.testing.allocator;

    var listener = zap.HttpListener.init(
        .{
            .port = 3020,
            .on_request = on_request,
            .log = false,
            .max_clients = 10,
            .max_body_size = 1 * 1024,
        },
    );
    try listener.listen();

    const t1 = try std.Thread.spawn(.{}, makeRequest, .{ allocator, "http://127.0.0.1:3020" });
    defer t1.join();

    zap.start(.{
        .threads = 1,
        .workers = 1,
    });

    if (test_error) |err| {
        return err;
    }
}
