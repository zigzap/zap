// zig type definitions for facilio lib

pub const Http = @cImport({
    @cInclude("http.h");
    @cInclude("fio.h");
});

pub fn sendBody(request: [*c]Http.http_s, body: []const u8) void {
    _ = Http.http_send_body(request, @intToPtr(
        *anyopaque,
        @ptrToInt(body.ptr),
    ), body.len);
}
