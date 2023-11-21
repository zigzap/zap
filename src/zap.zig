// zig type definitions for facilio lib
// or maybe let's just make it zap directly...

const std = @import("std");
const fio = @import("fio.zig");

pub usingnamespace @import("fio.zig");
pub usingnamespace @import("endpoint.zig");
pub usingnamespace @import("util.zig");
pub usingnamespace @import("http.zig");
pub usingnamespace @import("mustache.zig");
pub usingnamespace @import("http_auth.zig");
pub const Middleware = @import("middleware.zig");
pub const WebSockets = @import("websockets.zig");

pub const Log = @import("log.zig");
const http = @import("http.zig");

const util = @import("util.zig");

// TODO: replace with comptime debug logger like in log.zig
var _debug: bool = false;

/// Start the IO reactor
///
/// Will start listeners etc.
pub fn start(args: fio.fio_start_args) void {
    fio.fio_start(args);
}

/// Stop ZAP:
///
/// 1. Stop accepting further incoming requests
/// 2. Wait for all running request handlers to return
/// 3. return from `zap.start(...)`
pub fn stop() void {
    fio.fio_stop();
}

pub fn debug(comptime fmt: []const u8, args: anytype) void {
    if (_debug) {
        std.debug.print("[zap] - " ++ fmt, args);
    }
}

pub fn enableDebugLog() void {
    _debug = true;
}

pub fn startWithLogging(args: fio.fio_start_args) void {
    debug = true;
    fio.fio_start(args);
}

pub const ListenError = error{
    AlreadyListening,
    ListenError,
};

pub const HttpError = error{
    HttpSendBody,
    HttpSetContentType,
    HttpSetHeader,
    HttpParseBody,
    HttpIterParams,
    SetCookie,
    SendFile,
};

pub const ContentType = enum {
    TEXT,
    HTML,
    JSON,
};

pub const SimpleRequest = struct {
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
    /// field of `h`. So copies of the SimpleRequest will all have way to the
    /// same instance of this field.
    _is_finished_request_global: bool,
    /// NEVER touch this field!!!!
    /// this is part of the hack.
    _is_finished: *bool = undefined,

    const UserContext = struct {
        user_context: ?*anyopaque = null,
    };

    const Self = @This();

    pub fn markAsFinished(self: *const Self, finished: bool) void {
        // we might be a copy
        self._is_finished.* = finished;
    }

    pub fn isFinished(self: *const Self) bool {
        // we might be a copy
        return self._is_finished.*;
    }

    /// if you absolutely must, you can set any context here
    // (note, this line is linked to from the readme)
    pub fn setUserContext(self: *const Self, context: *anyopaque) void {
        self._user_context.*.user_context = context;
    }

    pub fn getUserContext(self: *const Self, comptime Context: type) ?*Context {
        if (self._user_context.*.user_context) |ptr| {
            return @as(*Context, @ptrCast(@alignCast(ptr)));
        } else {
            return null;
        }
    }

    pub fn sendError(self: *const Self, err: anyerror, errorcode_num: usize) void {
        // TODO: query accept headers
        if (self._internal_sendError(err, errorcode_num)) {
            return;
        } else |_| {
            self.sendBody(@errorName(err)) catch return;
        }
    }
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

    pub fn sendBody(self: *const Self, body: []const u8) HttpError!void {
        const ret = fio.http_send_body(self.h, @as(
            *anyopaque,
            @ptrFromInt(@intFromPtr(body.ptr)),
        ), body.len);
        debug("SimpleRequest.sendBody(): ret = {}\n", .{ret});
        if (ret == -1) return error.HttpSendBody;
        self.markAsFinished(true);
    }

    pub fn sendJson(self: *const Self, json: []const u8) HttpError!void {
        if (self.setContentType(.JSON)) {
            if (fio.http_send_body(self.h, @as(
                *anyopaque,
                @ptrFromInt(@intFromPtr(json.ptr)),
            ), json.len) != 0) return error.HttpSendBody;
            self.markAsFinished(true);
        } else |err| return err;
    }

    pub fn setContentType(self: *const Self, c: ContentType) HttpError!void {
        const s = switch (c) {
            .TEXT => "text/plain",
            .JSON => "application/json",
            else => "text/html",
        };
        debug("setting content-type to {s}\n", .{s});
        return self.setHeader("content-type", s);
    }

    // redirect to path with status code 302 by default
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

    pub fn getHeader(self: *const Self, name: []const u8) ?[]const u8 {
        const hname = fio.fiobj_str_new(util.toCharPtr(name), name.len);
        defer fio.fiobj_free_wrapped(hname);
        return util.fio2str(fio.fiobj_hash_get(self.h.*.headers, hname));
    }

    pub fn setHeader(self: *const Self, name: []const u8, value: []const u8) HttpError!void {
        const hname: fio.fio_str_info_s = .{
            .data = util.toCharPtr(name),
            .len = name.len,
            .capa = name.len,
        };

        debug("setHeader: hname = {s}\n", .{name});
        const vname: fio.fio_str_info_s = .{
            .data = util.toCharPtr(value),
            .len = value.len,
            .capa = value.len,
        };
        debug("setHeader: vname = {s}\n", .{value});
        const ret = fio.http_set_header2(self.h, hname, vname);

        // FIXME without the following if, we get errors in release builds
        // at least we don't have to log unconditionally
        if (ret == -1) {
            std.debug.print("***************** zap.zig:145\n", .{});
        }
        debug("setHeader: ret = {}\n", .{ret});

        if (ret == 0) return;
        return error.HttpSetHeader;
    }

    pub fn setStatusNumeric(self: *const Self, status: usize) void {
        self.h.*.status = status;
    }

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

    pub fn parseCookies(self: *const Self, url_encoded: bool) void {
        fio.http_parse_cookies(self.h, if (url_encoded) 1 else 0);
    }

    // Set a response cookie
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
        const ret = fio.http_set_cookie(self.h, c);
        if (ret == -1) {
            std.log.err("fio.http_set_cookie returned: {}\n", .{ret});
            return error.SetCookie;
        }
    }

    /// Returns named cookie. Works like getParamStr()
    pub fn getCookieStr(self: *const Self, name: []const u8, a: std.mem.Allocator, always_alloc: bool) !?util.FreeOrNot {
        if (self.h.*.cookies == 0) return null;
        const key = fio.fiobj_str_new(name.ptr, name.len);
        defer fio.fiobj_free_wrapped(key);
        const value = fio.fiobj_hash_get(self.h.*.cookies, key);
        if (value == fio.FIOBJ_INVALID) {
            return null;
        }
        return try util.fio2strAllocOrNot(value, a, always_alloc);
    }

    /// Returns the number of parameters after parsing.
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
            .key = util.fio2strAllocOrNot(fiobj_key, ctx.allocator, ctx.always_alloc) catch |err| {
                ctx.last_error = err;
                return -1;
            },
            .value = util.fio2strAllocOrNot(fiobj_value, ctx.allocator, ctx.always_alloc) catch |err| {
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
            .key = util.fio2strAllocOrNot(fiobj_key, ctx.allocator, ctx.dupe_strings) catch |err| {
                ctx.last_error = err;
                return -1;
            },
            .value = Fiobj2HttpParam(fiobj_value, ctx.allocator, ctx.dupe_strings) catch |err| {
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
    pub fn getParamStr(self: *const Self, name: []const u8, a: std.mem.Allocator, always_alloc: bool) !?util.FreeOrNot {
        if (self.h.*.params == 0) return null;
        const key = fio.fiobj_str_new(name.ptr, name.len);
        defer fio.fiobj_free_wrapped(key);
        const value = fio.fiobj_hash_get(self.h.*.params, key);
        if (value == fio.FIOBJ_INVALID) {
            return null;
        }
        return try util.fio2strAllocOrNot(value, a, always_alloc);
    }
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

pub const HttpParamBinaryFile = struct {
    ///  file contents
    data: ?[]const u8 = null,
    /// mimetype
    mimetype: ?[]const u8 = null,
    /// filename
    filename: ?[]const u8 = null,

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

pub fn Fiobj2HttpParam(o: fio.FIOBJ, a: std.mem.Allocator, dupe_string: bool) !?HttpParam {
    return switch (fio.fiobj_type(o)) {
        fio.FIOBJ_T_NULL => null,
        fio.FIOBJ_T_TRUE => .{ .Bool = true },
        fio.FIOBJ_T_FALSE => .{ .Bool = false },
        fio.FIOBJ_T_NUMBER => .{ .Int = fio.fiobj_obj2num(o) },
        fio.FIOBJ_T_FLOAT => .{ .Float = fio.fiobj_obj2float(o) },
        fio.FIOBJ_T_STRING => .{ .String = try util.fio2strAllocOrNot(o, a, dupe_string) },
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

pub const HttpRequestFn = *const fn (r: [*c]fio.http_s) callconv(.C) void;
pub const SimpleHttpRequestFn = *const fn (SimpleRequest) void;

/// websocket connection upgrade
/// fn(request, targetstring)
pub const SimpleHttpUpgradeFn = *const fn (r: SimpleRequest, target_protocol: []const u8) void;

/// http finish, called when zap finishes. You get your udata back in the
/// struct.
pub const SimpleHttpFinishSettings = [*c]fio.struct_http_settings_s;
pub const SimpleHttpFinishFn = *const fn (SimpleHttpFinishSettings) void;

pub const SimpleHttpListenerSettings = struct {
    port: usize,
    interface: [*c]const u8 = null,
    on_request: ?SimpleHttpRequestFn,
    on_response: ?SimpleHttpRequestFn = null,
    on_upgrade: ?SimpleHttpUpgradeFn = null,
    on_finish: ?SimpleHttpFinishFn = null,
    // provide any pointer in there for "user data". it will be passed pack in
    // on_finish()'s copy of the struct_http_settings_s
    udata: ?*anyopaque = null,
    public_folder: ?[]const u8 = null,
    max_clients: ?isize = null,
    max_body_size: ?usize = null,
    timeout: ?u8 = null,
    log: bool = false,
    ws_timeout: u8 = 40,
    ws_max_msg_size: usize = 262144,
};

pub const SimpleHttpListener = struct {
    settings: SimpleHttpListenerSettings,

    const Self = @This();
    var the_one_and_only_listener: ?*SimpleHttpListener = null;

    pub fn init(settings: SimpleHttpListenerSettings) Self {
        std.debug.assert(settings.on_request != null);
        return .{
            .settings = settings,
        };
    }

    // on_upgrade: ?*const fn ([*c]fio.http_s, [*c]u8, usize) callconv(.C) void = null,
    // on_finish: ?*const fn ([*c]fio.struct_http_settings_s) callconv(.C) void = null,

    // we could make it dynamic by passing a SimpleHttpListener via udata
    pub fn theOneAndOnlyRequestCallBack(r: [*c]fio.http_s) callconv(.C) void {
        if (the_one_and_only_listener) |l| {
            var req: SimpleRequest = .{
                .path = util.fio2str(r.*.path),
                .query = util.fio2str(r.*.query),
                .body = util.fio2str(r.*.body),
                .method = util.fio2str(r.*.method),
                .h = r,
                ._is_finished_request_global = false,
                ._user_context = undefined,
            };
            req._is_finished = &req._is_finished_request_global;

            var user_context: SimpleRequest.UserContext = .{};
            req._user_context = &user_context;

            req.markAsFinished(false);
            std.debug.assert(l.settings.on_request != null);
            if (l.settings.on_request) |on_request| {
                // l.settings.on_request.?(req);
                on_request(req);
            }
        }
    }

    pub fn theOneAndOnlyResponseCallBack(r: [*c]fio.http_s) callconv(.C) void {
        if (the_one_and_only_listener) |l| {
            var req: SimpleRequest = .{
                .path = util.fio2str(r.*.path),
                .query = util.fio2str(r.*.query),
                .body = util.fio2str(r.*.body),
                .method = util.fio2str(r.*.method),
                .h = r,
                ._is_finished_request_global = false,
                ._user_context = undefined,
            };
            req._is_finished = &req._is_finished_request_global;

            var user_context: SimpleRequest.UserContext = .{};
            req._user_context = &user_context;

            l.settings.on_response.?(req);
        }
    }

    pub fn theOneAndOnlyUpgradeCallBack(r: [*c]fio.http_s, target: [*c]u8, target_len: usize) callconv(.C) void {
        if (the_one_and_only_listener) |l| {
            var req: SimpleRequest = .{
                .path = util.fio2str(r.*.path),
                .query = util.fio2str(r.*.query),
                .body = util.fio2str(r.*.body),
                .method = util.fio2str(r.*.method),
                .h = r,
                ._is_finished_request_global = false,
                ._user_context = undefined,
            };
            const zigtarget: []u8 = target[0..target_len];
            req._is_finished = &req._is_finished_request_global;

            var user_context: SimpleRequest.UserContext = .{};
            req._user_context = &user_context;

            l.settings.on_upgrade.?(req, zigtarget);
        }
    }

    pub fn theOneAndOnlyFinishCallBack(s: [*c]fio.struct_http_settings_s) callconv(.C) void {
        if (the_one_and_only_listener) |l| {
            l.settings.on_finish.?(s);
        }
    }

    pub fn listen(self: *Self) !void {
        var pfolder: [*c]const u8 = null;
        var pfolder_len: usize = 0;

        if (self.settings.public_folder) |pf| {
            debug("SimpleHttpListener.listen(): public folder is {s}\n", .{pf});
            pfolder_len = pf.len;
            pfolder = pf.ptr;
        }

        const x: fio.http_settings_s = .{
            .on_request = if (self.settings.on_request) |_| Self.theOneAndOnlyRequestCallBack else null,
            .on_upgrade = if (self.settings.on_upgrade) |_| Self.theOneAndOnlyUpgradeCallBack else null,
            .on_response = if (self.settings.on_response) |_| Self.theOneAndOnlyResponseCallBack else null,
            .on_finish = if (self.settings.on_finish) |_| Self.theOneAndOnlyFinishCallBack else null,
            .udata = null,
            .public_folder = pfolder,
            .public_folder_length = pfolder_len,
            .max_header_size = 32 * 1024,
            .max_body_size = self.settings.max_body_size orelse 50 * 1024 * 1024,
            // fio provides good default:
            .max_clients = self.settings.max_clients orelse 0,
            .tls = null,
            .reserved1 = 0,
            .reserved2 = 0,
            .reserved3 = 0,
            .ws_max_msg_size = 0,
            .timeout = self.settings.timeout orelse 5,
            .ws_timeout = self.settings.ws_timeout,
            .log = if (self.settings.log) 1 else 0,
            .is_client = 0,
        };
        // TODO: BUG: without this print/sleep statement, -Drelease* loop forever
        // in debug2 and debug3 of hello example
        // std.debug.print("X\n", .{});
        std.time.sleep(500 * 1000 * 1000);

        var portbuf: [100]u8 = undefined;
        const printed_port = try std.fmt.bufPrintZ(&portbuf, "{d}", .{self.settings.port});

        // pub fn bufPrintZ(buf: []u8, comptime fmt: []const u8, args: anytype) BufPrintError![:0]u8 {
        //     const result = try bufPrint(buf, fmt ++ "\x00", args);
        //     return result[0 .. result.len - 1 :0];
        // }
        const ret = fio.http_listen(printed_port.ptr, self.settings.interface, x);
        if (ret == -1) {
            return error.ListenError;
        }

        // set ourselves up to handle requests:
        // TODO: do we mind the race condition?
        // the SimpleHttpRequestFn will check if this is null and not process
        // the request if it isn't set. hence, if started under full load, the
        // first request(s) might not be serviced, as long as it takes from
        // fio.http_listen() to here
        Self.the_one_and_only_listener = self;
    }
};

//
// lower level listening
//
pub const ListenSettings = struct {
    on_request: ?*const fn ([*c]fio.http_s) callconv(.C) void = null,
    on_upgrade: ?*const fn ([*c]fio.http_s, [*c]u8, usize) callconv(.C) void = null,
    on_response: ?*const fn ([*c]fio.http_s) callconv(.C) void = null,
    on_finish: ?*const fn ([*c]fio.struct_http_settings_s) callconv(.C) void = null,
    public_folder: ?[]const u8 = null,
    max_header_size: usize = 32 * 1024,
    max_body_size: usize = 50 * 1024 * 1024,
    max_clients: isize = 100,
    keepalive_timeout_s: u8 = 5,
    log: bool = false,

    const Self = @This();

    pub fn init() Self {
        return .{};
    }
};

pub fn listen(port: [*c]const u8, interface: [*c]const u8, settings: ListenSettings) ListenError!void {
    var pfolder: [*c]const u8 = null;
    var pfolder_len: usize = 0;

    if (settings.public_folder) |pf| {
        pfolder_len = pf.len;
        pfolder = pf.ptr;
    }
    const x: fio.http_settings_s = .{
        .on_request = settings.on_request,
        .on_upgrade = settings.on_upgrade,
        .on_response = settings.on_response,
        .on_finish = settings.on_finish,
        .udata = null,
        .public_folder = pfolder,
        .public_folder_length = pfolder_len,
        .max_header_size = settings.max_header_size,
        .max_body_size = settings.max_body_size,
        .max_clients = settings.max_clients,
        .tls = null,
        .reserved1 = 0,
        .reserved2 = 0,
        .reserved3 = 0,
        .ws_max_msg_size = settings.ws_max_msg_size,
        .timeout = settings.keepalive_timeout_s,
        .ws_timeout = 0,
        .log = if (settings.log) 1 else 0,
        .is_client = 0,
    };
    // TODO: BUG: without this print/sleep statement, -Drelease* loop forever
    // in debug2 and debug3 of hello example
    // std.debug.print("X\n", .{});
    std.time.sleep(500 * 1000 * 1000);

    if (fio.http_listen(port, interface, x) == -1) {
        return error.ListenError;
    }
}

// lower level sendBody
pub fn sendBody(request: [*c]fio.http_s, body: []const u8) HttpError!void {
    const ret = fio.http_send_body(request, @as(
        *anyopaque,
        @ptrFromInt(@intFromPtr(body.ptr)),
    ), body.len);
    debug("sendBody(): ret = {}\n", .{ret});
    if (ret != -1) return error.HttpSendBody;
}
