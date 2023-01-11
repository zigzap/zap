// zig type definitions for facilio lib

pub const Http = @cImport({
    @cInclude("http.h");
    @cInclude("fio.h");
});
