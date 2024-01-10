const std = @import("std");
const Log = @import("log.zig");
const http = @import("http.zig");
const fio = @import("fio.zig");

const util = @import("util.zig");
const zap = @import("zap.zig");

pub const HttpError = error{
    HttpSendBody,
    HttpSetContentType,
    HttpSetHeader,
    HttpParseBody,
    HttpIterParams,
    SetCookie,
    SendFile,
};

/// Http Content Type enum.
/// Needs some love.
pub const ContentType = enum {
    TEXT,
    HTML,
    JSON,
    // TODO: more content types
};

/// Key value pair of strings from HTTP parameters
pub const HttpParamStrKV = struct {
    key: util.FreeOrNot,
    value: util.FreeOrNot,
    pub fn deinit(self: *@This()) void {
        self.key.deinit();
        self.value.deinit();
    }
};

/// List of key value pairs of Http param strings.
pub const HttpParamStrKVList = struct {
    items: []HttpParamStrKV,
    allocator: std.mem.Allocator,
    pub fn deinit(self: *@This()) void {
        for (self.items) |*item| {
            item.deinit();
        }
        self.allocator.free(self.items);
    }
};

/// List of key value pairs of Http params (might be of different types).
pub const HttpParamKVList = struct {
    items: []HttpParamKV,
    allocator: std.mem.Allocator,
    pub fn deinit(self: *const @This()) void {
        for (self.items) |*item| {
            item.deinit();
        }
        self.allocator.free(self.items);
    }
};

/// Enum for HttpParam tagged union
pub const HttpParamValueType = enum {
    // Null,
    Bool,
    Int,
    Float,
    String,
    Unsupported,
    Hash_Binfile,
    Array_Binfile,
};

/// Tagged union holding a typed Http param
pub const HttpParam = union(HttpParamValueType) {
    Bool: bool,
    Int: isize,
    Float: f64,
    /// we don't do writable strings here
    String: util.FreeOrNot,
    /// value will always be null
    Unsupported: ?void,
    /// we assume hashes are because of file transmissions
    Hash_Binfile: HttpParamBinaryFile,
    /// value will always be null
    Array_Binfile: std.ArrayList(HttpParamBinaryFile),
};

/// Key value pair of one typed Http param
pub const HttpParamKV = struct {
    key: util.FreeOrNot,
    value: ?HttpParam,
    pub fn deinit(self: *@This()) void {
        self.key.deinit();
        if (self.value) |p| {
            switch (p) {
                .String => |*s| s.deinit(),
                else => {},
            }
        }
    }
};

/// Struct representing an uploaded file.
pub const HttpParamBinaryFile = struct {
    ///  file contents
    data: ?[]const u8 = null,
    /// mimetype
    mimetype: ?[]const u8 = null,
    /// filename
    filename: ?[]const u8 = null,

    /// format function for printing file upload data
    pub fn format(value: @This(), comptime _: []const u8, _: std.fmt.FormatOptions, writer: anytype) std.os.WriteError!void {
        const d = value.data orelse "\\0";
        const m = value.mimetype orelse "null";
        const f = value.filename orelse "null";
        return writer.print("<{s} ({s}): {any}>", .{ f, m, d });
    }
};

fn parseBinfilesFrom(a: std.mem.Allocator, o: fio.FIOBJ) !HttpParam {
    const key_name = fio.fiobj_str_new("name", 4);
    const key_data = fio.fiobj_str_new("data", 4);
    const key_type = fio.fiobj_str_new("type", 4);
    defer {
        fio.fiobj_free_wrapped(key_name);
        fio.fiobj_free_wrapped(key_data);
        fio.fiobj_free_wrapped(key_type);
    } // files: they should have "data", "type", and "filename" keys
    if (fio.fiobj_hash_haskey(o, key_data) == 1 and fio.fiobj_hash_haskey(o, key_type) == 1 and fio.fiobj_hash_haskey(o, key_name) == 1) {
        const filename = fio.fiobj_obj2cstr(fio.fiobj_hash_get(o, key_name));
        const mimetype = fio.fiobj_obj2cstr(fio.fiobj_hash_get(o, key_type));
        const data = fio.fiobj_hash_get(o, key_data);

        var data_slice: ?[]const u8 = null;

        switch (fio.fiobj_type(data)) {
            fio.FIOBJ_T_DATA => {
                if (fio.is_invalid(data) == 1) {
                    data_slice = "(zap: invalid data)";
                    std.log.warn("WARNING: HTTP param binary file is not a data object\n", .{});
                } else {
                    // the data
                    const data_len = fio.fiobj_data_len(data);
                    var data_buf = fio.fiobj_data_read(data, data_len);

                    if (data_len < 0) {
                        std.log.warn("WARNING: HTTP param binary file size negative: {d}\n", .{data_len});
                        std.log.warn("FIOBJ_TYPE of data is: {d}\n", .{fio.fiobj_type(data)});
                    } else {
                        if (data_buf.len != data_len) {
                            std.log.warn("WARNING: HTTP param binary file size mismatch: should {d}, is: {d}\n", .{ data_len, data_buf.len });
                        }

                        if (data_buf.len > 0) {
                            data_slice = data_buf.data[0..data_buf.len];
                        } else {
                            std.log.warn("WARNING: HTTP param binary file buffer size negative: {d}\n", .{data_buf.len});
                            data_slice = "(zap: invalid data: negative BUFFER size)";
                        }
                    }
                }
            },
            fio.FIOBJ_T_STRING => {
                const fiostr = fio.fiobj_obj2cstr(data);
                if (fiostr.len == 0) {
                    data_slice = "(zap: empty string data)";
                    std.log.warn("WARNING: HTTP param binary file has empty string object\n", .{});
                } else {
                    data_slice = fiostr.data[0..fiostr.len];
                }
            },
            fio.FIOBJ_T_ARRAY => {
                // OK, data is an array
                const len = fio.fiobj_ary_count(data);
                const fn_ary = fio.fiobj_hash_get(o, key_name);
                const mt_ary = fio.fiobj_hash_get(o, key_type);

                if (fio.fiobj_ary_count(fn_ary) == len and fio.fiobj_ary_count(mt_ary) == len) {
                    var i: isize = 0;
                    var ret = std.ArrayList(HttpParamBinaryFile).init(a);
                    while (i < len) : (i += 1) {
                        const file_data_obj = fio.fiobj_ary_entry(data, i);
                        const file_name_obj = fio.fiobj_ary_entry(fn_ary, i);
                        const file_mimetype_obj = fio.fiobj_ary_entry(mt_ary, i);
                        var has_error: bool = false;
                        if (fio.is_invalid(file_data_obj) == 1) {
                            std.log.debug("file data invalid in array", .{});
                            has_error = true;
                        }
                        if (fio.is_invalid(file_name_obj) == 1) {
                            std.log.debug("file name invalid in array", .{});
                            has_error = true;
                        }
                        if (fio.is_invalid(file_mimetype_obj) == 1) {
                            std.log.debug("file mimetype invalid in array", .{});
                            has_error = true;
                        }
                        if (has_error) {
                            return error.Invalid;
                        }

                        const file_data = fio.fiobj_obj2cstr(file_data_obj);
                        const file_name = fio.fiobj_obj2cstr(file_name_obj);
                        const file_mimetype = fio.fiobj_obj2cstr(file_mimetype_obj);
                        try ret.append(.{
                            .data = file_data.data[0..file_data.len],
                            .mimetype = file_mimetype.data[0..file_mimetype.len],
                            .filename = file_name.data[0..file_name.len],
                        });
                    }
                    return .{ .Array_Binfile = ret };
                } else {
                    return error.ArrayLenMismatch;
                }
            },
            else => {
                // don't know what to do
                return error.Unsupported;
            },
        }

        return .{ .Hash_Binfile = .{
            .filename = filename.data[0..filename.len],
            .mimetype = mimetype.data[0..mimetype.len],
            .data = data_slice,
        } };
    } else {
        return .{ .Hash_Binfile = .{} };
    }
}

/// Parse FIO object into a typed Http param. Supports file uploads.
pub fn Fiobj2HttpParam(a: std.mem.Allocator, o: fio.FIOBJ, dupe_string: bool) !?HttpParam {
    return switch (fio.fiobj_type(o)) {
        fio.FIOBJ_T_NULL => null,
        fio.FIOBJ_T_TRUE => .{ .Bool = true },
        fio.FIOBJ_T_FALSE => .{ .Bool = false },
        fio.FIOBJ_T_NUMBER => .{ .Int = fio.fiobj_obj2num(o) },
        fio.FIOBJ_T_FLOAT => .{ .Float = fio.fiobj_obj2float(o) },
        fio.FIOBJ_T_STRING => .{ .String = try util.fio2strAllocOrNot(a, o, dupe_string) },
        fio.FIOBJ_T_ARRAY => {
            return .{ .Unsupported = null };
        },
        fio.FIOBJ_T_HASH => {
            const file = try parseBinfilesFrom(a, o);
            return file;
        },
        else => .{ .Unsupported = null },
    };
}

/// Args for setting a cookie
pub const CookieArgs = struct {
    name: []const u8,
    value: []const u8,
    domain: ?[]const u8 = null,
    path: ?[]const u8 = null,
    /// max age in seconds. 0 -> session
    max_age_s: c_int = 0,
    secure: bool = true,
    http_only: bool = true,
};

path: ?[]const u8,
query: ?[]const u8,
body: ?[]const u8,
method: ?[]const u8,
h: [*c]fio.http_s,

/// NEVER touch this field!!!!
/// if you absolutely MUST, then you may provide context here
/// via setUserContext and getUserContext
_user_context: *UserContext,
/// NEVER touch this field!!!!
/// use markAsFinished() and isFinished() instead
/// this is a hack: the listener will put a pointer to this into the udata
/// field of `h`. So copies of the Request will all have way to the
/// same instance of this field.
_is_finished_request_global: bool,
/// NEVER touch this field!!!!
/// this is part of the hack.
_is_finished: *bool = undefined,

pub const UserContext = struct {
    user_context: ?*anyopaque = null,
};

const Self = @This();

/// mark the current request as finished. Important for middleware-style
/// request handler chaining. Called when sending a body, redirecting, etc.
pub fn markAsFinished(self: *const Self, finished: bool) void {
    // we might be a copy
    self._is_finished.* = finished;
}

/// tell whether request processing has finished. (e.g. response sent,
/// redirected, ...)
pub fn isFinished(self: *const Self) bool {
    // we might be a copy
    return self._is_finished.*;
}

/// if you absolutely must, you can set any context on the request here
// (note, this line is linked to from the readme) -- TODO: sync
pub fn setUserContext(self: *const Self, context: *anyopaque) void {
    self._user_context.*.user_context = context;
}

/// get the associated user context of the request.
pub fn getUserContext(self: *const Self, comptime Context: type) ?*Context {
    if (self._user_context.*.user_context) |ptr| {
        return @as(*Context, @ptrCast(@alignCast(ptr)));
    } else {
        return null;
    }
}

/// Tries to send an error stack trace.
pub fn sendError(self: *const Self, err: anyerror, errorcode_num: usize) void {
    // TODO: query accept headers
    if (self._internal_sendError(err, errorcode_num)) {
        return;
    } else |_| {
        self.sendBody(@errorName(err)) catch return;
    }
}

/// Used internally. Probably does not need to be public.
pub fn _internal_sendError(self: *const Self, err: anyerror, errorcode_num: usize) !void {
    // TODO: query accept headers
    // TODO: let's hope 20k is enough. Maybe just really allocate here
    self.h.*.status = errorcode_num;
    var buf: [20 * 1024]u8 = undefined;
    var fba = std.heap.FixedBufferAllocator.init(&buf);
    var string = std.ArrayList(u8).init(fba.allocator());
    var writer = string.writer();
    try writer.print("ERROR: {any}\n\n", .{err});

    const debugInfo = try std.debug.getSelfDebugInfo();
    const ttyConfig: std.io.tty.Config = .no_color;
    try std.debug.writeCurrentStackTrace(writer, debugInfo, ttyConfig, null);
    try self.sendBody(string.items);
}

/// Send body.
pub fn sendBody(self: *const Self, body: []const u8) HttpError!void {
    const ret = fio.http_send_body(self.h, @as(
        *anyopaque,
        @ptrFromInt(@intFromPtr(body.ptr)),
    ), body.len);
    zap.debug("Request.sendBody(): ret = {}\n", .{ret});
    if (ret == -1) return error.HttpSendBody;
    self.markAsFinished(true);
}

/// Set content type and send json buffer.
pub fn sendJson(self: *const Self, json: []const u8) HttpError!void {
    if (self.setContentType(.JSON)) {
        if (fio.http_send_body(self.h, @as(
            *anyopaque,
            @ptrFromInt(@intFromPtr(json.ptr)),
        ), json.len) != 0) return error.HttpSendBody;
        self.markAsFinished(true);
    } else |err| return err;
}

/// Set content type.
pub fn setContentType(self: *const Self, c: ContentType) HttpError!void {
    const s = switch (c) {
        .TEXT => "text/plain",
        .JSON => "application/json",
        else => "text/html",
    };
    zap.debug("setting content-type to {s}\n", .{s});
    return self.setHeader("content-type", s);
}

/// redirect to path with status code 302 by default
pub fn redirectTo(self: *const Self, path: []const u8, code: ?http.StatusCode) HttpError!void {
    self.setStatus(if (code) |status| status else .found);
    try self.setHeader("Location", path);
    try self.sendBody("moved");
    self.markAsFinished(true);
}

/// shows how to use the logger
pub fn setContentTypeWithLogger(
    self: *const Self,
    c: ContentType,
    logger: *const Log,
) HttpError!void {
    const s = switch (c) {
        .TEXT => "text/plain",
        .JSON => "application/json",
        else => "text/html",
    };
    logger.log("setting content-type to {s}\n", .{s});
    return self.setHeader("content-type", s);
}

/// Tries to determine the content type by file extension of request path, and sets it.
pub fn setContentTypeFromPath(self: *const Self) !void {
    const t = fio.http_mimetype_find2(self.h.*.path);
    if (fio.is_invalid(t) == 1) return error.HttpSetContentType;
    const ret = fio.fiobj_hash_set(
        self.h.*.private_data.out_headers,
        fio.HTTP_HEADER_CONTENT_TYPE,
        t,
    );
    if (ret == -1) return error.HttpSetContentType;
}

/// Tries to determine the content type by filename extension, and sets it.
/// If the extension cannot be determined, NoExtensionInFilename error is
/// returned.
pub fn setContentTypeFromFilename(self: *const Self, filename: []const u8) !void {
    const ext = std.fs.path.extension(filename);

    if (ext.len > 1) {
        const e = ext[1..];
        const obj = fio.http_mimetype_find(@constCast(e.ptr), e.len);

        if (util.fio2str(obj)) |mime_str| {
            try self.setHeader("content-type", mime_str);
        }
    } else {
        return error.NoExtensionInFilename;
    }
}

/// Returns the header value of given key name. Returned mem is temp.
/// Do not free it.
pub fn getHeader(self: *const Self, name: []const u8) ?[]const u8 {
    const hname = fio.fiobj_str_new(util.toCharPtr(name), name.len);
    defer fio.fiobj_free_wrapped(hname);
    return util.fio2str(fio.fiobj_hash_get(self.h.*.headers, hname));
}

/// Set header.
pub fn setHeader(self: *const Self, name: []const u8, value: []const u8) HttpError!void {
    const hname: fio.fio_str_info_s = .{
        .data = util.toCharPtr(name),
        .len = name.len,
        .capa = name.len,
    };

    zap.debug("setHeader: hname = {s}\n", .{name});
    const vname: fio.fio_str_info_s = .{
        .data = util.toCharPtr(value),
        .len = value.len,
        .capa = value.len,
    };
    zap.debug("setHeader: vname = {s}\n", .{value});
    const ret = fio.http_set_header2(self.h, hname, vname);

    // FIXME without the following if, we get errors in release builds
    // at least we don't have to log unconditionally
    if (ret == -1) {
        std.debug.print("***************** zap.zig:274\n", .{});
    }
    zap.debug("setHeader: ret = {}\n", .{ret});

    if (ret == 0) return;
    return error.HttpSetHeader;
}

/// Set status by numeric value.
pub fn setStatusNumeric(self: *const Self, status: usize) void {
    self.h.*.status = status;
}

/// Set status by enum.
pub fn setStatus(self: *const Self, status: http.StatusCode) void {
    self.h.*.status = @as(usize, @intCast(@intFromEnum(status)));
}

/// Sends a file if present in the filesystem orelse returns an error.
///
/// - efficiently sends a file using gzip compression
/// - also handles range requests if `Range` or `If-Range` headers are present in the request.
/// - sends the response headers and the specified file (the response's body).
///
/// On success, the `self.h` handle will be consumed and invalid.
/// On error, the handle will still be valid and should be used to send an error response
///
/// Important: sets last-modified and cache-control headers with a max-age value of 1 hour!
/// You can override that by setting those headers yourself, e.g.: setHeader("Cache-Control", "no-cache")
pub fn sendFile(self: *const Self, file_path: []const u8) !void {
    if (fio.http_sendfile2(self.h, util.toCharPtr(file_path), file_path.len, null, 0) != 0)
        return error.SendFile;
    self.markAsFinished(true);
}

/// Attempts to decode the request's body.
/// This should be called BEFORE parseQuery
/// Result is accessible via parametersToOwnedSlice(), parametersToOwnedStrSlice()
///
/// Supported body types:
/// - application/x-www-form-urlencoded
/// - application/json
/// - multipart/form-data
pub fn parseBody(self: *const Self) HttpError!void {
    if (fio.http_parse_body(self.h) == -1) return error.HttpParseBody;
}

/// Parses the query part of an HTTP request
/// This should be called AFTER parseBody(), just in case the body is a JSON
/// object that doesn't have a hash map at its root.
///
/// Result is accessible via parametersToOwnedSlice(), parametersToOwnedStrSlice()
pub fn parseQuery(self: *const Self) void {
    fio.http_parse_query(self.h);
}

/// Parse received cookie headers
pub fn parseCookies(self: *const Self, url_encoded: bool) void {
    fio.http_parse_cookies(self.h, if (url_encoded) 1 else 0);
}

/// Set a response cookie
pub fn setCookie(self: *const Self, args: CookieArgs) HttpError!void {
    const c: fio.http_cookie_args_s = .{
        .name = util.toCharPtr(args.name),
        .name_len = @as(isize, @intCast(args.name.len)),
        .value = util.toCharPtr(args.value),
        .value_len = @as(isize, @intCast(args.value.len)),
        .domain = if (args.domain) |p| util.toCharPtr(p) else null,
        .domain_len = if (args.domain) |p| @as(isize, @intCast(p.len)) else 0,
        .path = if (args.path) |p| util.toCharPtr(p) else null,
        .path_len = if (args.path) |p| @as(isize, @intCast(p.len)) else 0,
        .max_age = args.max_age_s,
        .secure = if (args.secure) 1 else 0,
        .http_only = if (args.http_only) 1 else 0,
    };

    // TODO WAT?
    // if we:
    //     if(fio.http_set_cookie(...) == -1)
    // instead of capturing it in `ret` first and then checking it,
    // all ReleaseXXX builds return an error!
    // TODO: still happening?
    const ret = fio.http_set_cookie(self.h, c);
    if (ret == -1) {
        std.log.err("fio.http_set_cookie returned: {}\n", .{ret});
        return error.SetCookie;
    }
}

/// Returns named cookie. Works like getParamStr().
pub fn getCookieStr(self: *const Self, a: std.mem.Allocator, name: []const u8, always_alloc: bool) !?util.FreeOrNot {
    if (self.h.*.cookies == 0) return null;
    const key = fio.fiobj_str_new(name.ptr, name.len);
    defer fio.fiobj_free_wrapped(key);
    const value = fio.fiobj_hash_get(self.h.*.cookies, key);
    if (value == fio.FIOBJ_INVALID) {
        return null;
    }
    return try util.fio2strAllocOrNot(a, value, always_alloc);
}

/// Returns the number of cookies after parsing.
///
/// Parse with parseCookies()
pub fn getCookiesCount(self: *const Self) isize {
    if (self.h.*.cookies == 0) return 0;
    return fio.fiobj_obj2num(self.h.*.cookies);
}

/// Returns the number of parameters after parsing.
///
/// Parse with parseBody() and / or parseQuery()
pub fn getParamCount(self: *const Self) isize {
    if (self.h.*.params == 0) return 0;
    return fio.fiobj_obj2num(self.h.*.params);
}

/// Same as parametersToOwnedStrList() but for cookies
pub fn cookiesToOwnedStrList(self: *const Self, a: std.mem.Allocator, always_alloc: bool) anyerror!HttpParamStrKVList {
    var params = try std.ArrayList(HttpParamStrKV).initCapacity(a, @as(usize, @intCast(self.getCookiesCount())));
    var context: _parametersToOwnedStrSliceContext = .{
        .params = &params,
        .allocator = a,
        .always_alloc = always_alloc,
    };
    const howmany = fio.fiobj_each1(self.h.*.cookies, 0, _each_nextParamStr, &context);
    if (howmany != self.getCookiesCount()) {
        return error.HttpIterParams;
    }
    return .{ .items = try params.toOwnedSlice(), .allocator = a };
}

/// Same as parametersToOwnedList() but for cookies
pub fn cookiesToOwnedList(self: *const Self, a: std.mem.Allocator, dupe_strings: bool) !HttpParamKVList {
    var params = try std.ArrayList(HttpParamKV).initCapacity(a, @as(usize, @intCast(self.getCookiesCount())));
    var context: _parametersToOwnedSliceContext = .{ .params = &params, .allocator = a, .dupe_strings = dupe_strings };
    const howmany = fio.fiobj_each1(self.h.*.cookies, 0, _each_nextParam, &context);
    if (howmany != self.getCookiesCount()) {
        return error.HttpIterParams;
    }
    return .{ .items = try params.toOwnedSlice(), .allocator = a };
}

/// Returns the query / body parameters as key/value pairs, as strings.
/// Supported param types that will be converted:
///
/// - Bool
/// - Int
/// - Float
/// - String
///
/// At the moment, no fio ARRAYs are supported as well as HASH maps.
/// So, for JSON body payloads: parse the body instead.
///
/// Requires parseBody() and/or parseQuery() have been called.
/// Returned list needs to be deinited.
pub fn parametersToOwnedStrList(self: *const Self, a: std.mem.Allocator, always_alloc: bool) anyerror!HttpParamStrKVList {
    var params = try std.ArrayList(HttpParamStrKV).initCapacity(a, @as(usize, @intCast(self.getParamCount())));
    var context: _parametersToOwnedStrSliceContext = .{
        .params = &params,
        .allocator = a,
        .always_alloc = always_alloc,
    };
    const howmany = fio.fiobj_each1(self.h.*.params, 0, _each_nextParamStr, &context);
    if (howmany != self.getParamCount()) {
        return error.HttpIterParams;
    }
    return .{ .items = try params.toOwnedSlice(), .allocator = a };
}

const _parametersToOwnedStrSliceContext = struct {
    allocator: std.mem.Allocator,
    params: *std.ArrayList(HttpParamStrKV),
    last_error: ?anyerror = null,
    always_alloc: bool,
};

fn _each_nextParamStr(fiobj_value: fio.FIOBJ, context: ?*anyopaque) callconv(.C) c_int {
    const ctx: *_parametersToOwnedStrSliceContext = @as(*_parametersToOwnedStrSliceContext, @ptrCast(@alignCast(context)));
    // this is thread-safe, guaranteed by fio
    const fiobj_key: fio.FIOBJ = fio.fiobj_hash_key_in_loop();
    ctx.params.append(.{
        .key = util.fio2strAllocOrNot(ctx.allocator, fiobj_key, ctx.always_alloc) catch |err| {
            ctx.last_error = err;
            return -1;
        },
        .value = util.fio2strAllocOrNot(ctx.allocator, fiobj_value, ctx.always_alloc) catch |err| {
            ctx.last_error = err;
            return -1;
        },
    }) catch |err| {
        // what to do?
        // signal the caller that an error occured by returning -1
        // also, set the error
        ctx.last_error = err;
        return -1;
    };
    return 0;
}

/// Returns the query / body parameters as key/value pairs
/// Supported param types that will be converted:
///
/// - Bool
/// - Int
/// - Float
/// - String
///
/// At the moment, no fio ARRAYs are supported as well as HASH maps.
/// So, for JSON body payloads: parse the body instead.
///
/// Requires parseBody() and/or parseQuery() have been called.
/// Returned slice needs to be freed.
pub fn parametersToOwnedList(self: *const Self, a: std.mem.Allocator, dupe_strings: bool) !HttpParamKVList {
    var params = try std.ArrayList(HttpParamKV).initCapacity(a, @as(usize, @intCast(self.getParamCount())));
    var context: _parametersToOwnedSliceContext = .{ .params = &params, .allocator = a, .dupe_strings = dupe_strings };
    const howmany = fio.fiobj_each1(self.h.*.params, 0, _each_nextParam, &context);
    if (howmany != self.getParamCount()) {
        return error.HttpIterParams;
    }
    return .{ .items = try params.toOwnedSlice(), .allocator = a };
}

const _parametersToOwnedSliceContext = struct {
    params: *std.ArrayList(HttpParamKV),
    last_error: ?anyerror = null,
    allocator: std.mem.Allocator,
    dupe_strings: bool,
};

fn _each_nextParam(fiobj_value: fio.FIOBJ, context: ?*anyopaque) callconv(.C) c_int {
    const ctx: *_parametersToOwnedSliceContext = @as(*_parametersToOwnedSliceContext, @ptrCast(@alignCast(context)));
    // this is thread-safe, guaranteed by fio
    const fiobj_key: fio.FIOBJ = fio.fiobj_hash_key_in_loop();
    ctx.params.append(.{
        .key = util.fio2strAllocOrNot(ctx.allocator, fiobj_key, ctx.dupe_strings) catch |err| {
            ctx.last_error = err;
            return -1;
        },
        .value = Fiobj2HttpParam(ctx.allocator, fiobj_value, ctx.dupe_strings) catch |err| {
            ctx.last_error = err;
            return -1;
        },
    }) catch |err| {
        // what to do?
        // signal the caller that an error occured by returning -1
        // also, set the error
        ctx.last_error = err;
        return -1;
    };
    return 0;
}

/// get named parameter as string
/// Supported param types that will be converted:
///
/// - Bool
/// - Int
/// - Float
/// - String
///
/// At the moment, no fio ARRAYs are supported as well as HASH maps.
/// So, for JSON body payloads: parse the body instead.
///
/// Requires parseBody() and/or parseQuery() have been called.
/// The returned string needs to be deinited with .deinit()
pub fn getParamStr(self: *const Self, a: std.mem.Allocator, name: []const u8, always_alloc: bool) !?util.FreeOrNot {
    if (self.h.*.params == 0) return null;
    const key = fio.fiobj_str_new(name.ptr, name.len);
    defer fio.fiobj_free_wrapped(key);
    const value = fio.fiobj_hash_get(self.h.*.params, key);
    if (value == fio.FIOBJ_INVALID) {
        return null;
    }
    return try util.fio2strAllocOrNot(a, value, always_alloc);
}
