// zig type definitions for facilio lib

pub const C = @cImport({
    @cInclude("http.h");
    @cInclude("fio.h");
});

pub fn sendBody(request: [*c]C.http_s, body: []const u8) void {
    _ = C.http_send_body(request, @intToPtr(
        *anyopaque,
        @ptrToInt(body.ptr),
    ), body.len);
}

pub fn start(args: C.fio_start_args) void {
    C.fio_start(args);
}
