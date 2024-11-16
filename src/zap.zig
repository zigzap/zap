// zig type definitions for facilio lib
// or maybe let's just make it zap directly...

const std = @import("std");

/// The facilio C API. No need to use this.
pub const fio = @import("fio.zig");

/// Server-Side TLS function wrapper
pub const Tls = @import("tls.zig");

/// Endpoint and supporting types.
/// Create one and pass in your callbacks. Then,
/// pass it to a HttpListener's `register()` function to register with the
/// listener.
///
/// **NOTE**: A common endpoint pattern for zap is to create your own struct
/// that embeds an Endpoint, provides specific callbacks, and uses
/// `@fieldParentPtr` to get a reference to itself.
///
/// Example:
/// A simple endpoint listening on the /stop route that shuts down zap.
/// The main thread usually continues at the instructions after the call to zap.start().
///
/// ```zig
/// const StopEndpoint = struct {
///     ep: zap.Endpoint = undefined,
///
///     pub fn init(
///         path: []const u8,
///     ) StopEndpoint {
///         return .{
///             .ep = zap.Endpoint.init(.{
///                 .path = path,
///                 .get = get,
///                 .unset = Endpoint.dummy_handler,
///             }),
///         };
///     }
///
///     // access the internal Endpoint
///     pub fn endpoint(self: *StopEndpoint) *zap.Endpoint {
///         return &self.ep;
///     }
///
///     fn get(e: *zap.Endpoint, r: zap.Request) void {
///         const self: *StopEndpoint = @fieldParentPtr("ep", e);
///         _ = self;
///         _ = r;
///         zap.stop();
///     }
/// };
/// ```
pub const Endpoint = @import("endpoint.zig");

pub const Router = @import("router.zig");

pub usingnamespace @import("util.zig");
pub usingnamespace @import("http.zig");

/// A struct to handle Mustache templating.
///
/// This is a wrapper around fiobj's mustache template handling.
/// See http://facil.io/0.7.x/fiobj_mustache for more information.
pub const Mustache = @import("mustache.zig");

/// Authenticators
pub const Auth = @import("http_auth.zig");

/// Http request and supporting types.
pub const Request = @import("request.zig");

/// Middleware support.
/// Contains a special Listener and a Handler struct that support chaining
/// requests handlers, with an optional stop once a handler indicates it
/// processed the request. Also sports an EndpointHandler for using regular zap
/// Endpoints as Handlers.
pub const Middleware = @import("middleware.zig");

/// Websocket API
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

/// Extremely simplistic zap debug function.
/// TODO: re-wwrite logging or use std.log
pub fn debug(comptime fmt: []const u8, args: anytype) void {
    if (_debug) {
        std.debug.print("[zap] - " ++ fmt, args);
    }
}

/// Enable zap debug logging
pub fn enableDebugLog() void {
    _debug = true;
}

/// start Zap with debug logging on
pub fn startWithLogging(args: fio.fio_start_args) void {
    _debug = true;
    fio.fio_start(args);
}

/// Registers a new mimetype to be used for files ending with the given extension.
pub fn mimetypeRegister(file_extension: []const u8, mime_type_str: []const u8) void {
    // NOTE: facil.io is expecting a non-const pointer to u8 values, but it does not
    // not appear to actually modify the value.  Here we do a const cast so
    // that it is easy to pass static strings to http_mimetype_register without
    // needing to allocate a buffer on the heap.
    const extension = @constCast(file_extension);
    const mimetype = fio.fiobj_str_new(mime_type_str.ptr, mime_type_str.len);

    fio.http_mimetype_register(extension.ptr, extension.len, mimetype);
}

/// Clears the Mime-Type registry (it will be empty after this call).
pub fn mimetypeClear() void {
    fio.http_mimetype_clear();
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

/// Http Content Type enum.
/// Needs some love.
pub const ContentType = enum {
    TEXT,
    HTML,
    XML,
    JSON,
    XHTML,
    // TODO: more content types

    pub const string_map = std.StaticStringMap(ContentType).initComptime(.{
        .{ "text/plain", .TEXT },
        .{ "text/html", .HTML },
        .{ "application/xml", .XML },
        .{ "application/json", .JSON },
        .{ "application/xhtml+xml", .XHTML },
    });
};

/// Used internally: facilio Http request callback function type
pub const FioHttpRequestFn = *const fn (r: [*c]fio.http_s) callconv(.C) void;

/// Zap Http request callback function type.
pub const HttpRequestFn = *const fn (Request) void;

/// websocket connection upgrade callback type
/// fn(request, targetstring)
pub const HttpUpgradeFn = *const fn (r: Request, target_protocol: []const u8) void;

/// http finish, called when zap finishes. You get your udata back in the
/// HttpFinishSetting struct.
pub const HttpFinishSettings = [*c]fio.struct_http_settings_s;

/// Http finish callback type
pub const HttpFinishFn = *const fn (HttpFinishSettings) void;

/// Listener settings
pub const HttpListenerSettings = struct {
    port: usize,
    interface: [*c]const u8 = null,
    on_request: ?HttpRequestFn,
    on_response: ?HttpRequestFn = null,
    on_upgrade: ?HttpUpgradeFn = null,
    on_finish: ?HttpFinishFn = null,
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
    tls: ?Tls = null,
};

/// Http listener
pub const HttpListener = struct {
    settings: HttpListenerSettings,

    const Self = @This();
    var the_one_and_only_listener: ?*HttpListener = null;

    /// Create a listener
    pub fn init(settings: HttpListenerSettings) Self {
        std.debug.assert(settings.on_request != null);
        return .{
            .settings = settings,
        };
    }

    // we could make it dynamic by passing a HttpListener via udata
    /// Used internally: the listener's facilio request callback
    pub fn theOneAndOnlyRequestCallBack(r: [*c]fio.http_s) callconv(.C) void {
        if (the_one_and_only_listener) |l| {
            var req: Request = .{
                .path = util.fio2str(r.*.path),
                .query = util.fio2str(r.*.query),
                .body = util.fio2str(r.*.body),
                .method = util.fio2str(r.*.method),
                .h = r,
                ._is_finished_request_global = false,
                ._user_context = undefined,
            };
            req._is_finished = &req._is_finished_request_global;

            var user_context: Request.UserContext = .{};
            req._user_context = &user_context;

            req.markAsFinished(false);
            std.debug.assert(l.settings.on_request != null);
            if (l.settings.on_request) |on_request| {
                // l.settings.on_request.?(req);
                on_request(req);
            }
        }
    }

    /// Used internally: the listener's facilio response callback
    pub fn theOneAndOnlyResponseCallBack(r: [*c]fio.http_s) callconv(.C) void {
        if (the_one_and_only_listener) |l| {
            var req: Request = .{
                .path = util.fio2str(r.*.path),
                .query = util.fio2str(r.*.query),
                .body = util.fio2str(r.*.body),
                .method = util.fio2str(r.*.method),
                .h = r,
                ._is_finished_request_global = false,
                ._user_context = undefined,
            };
            req._is_finished = &req._is_finished_request_global;

            var user_context: Request.UserContext = .{};
            req._user_context = &user_context;

            l.settings.on_response.?(req);
        }
    }

    /// Used internally: the listener's facilio upgrade callback
    pub fn theOneAndOnlyUpgradeCallBack(r: [*c]fio.http_s, target: [*c]u8, target_len: usize) callconv(.C) void {
        if (the_one_and_only_listener) |l| {
            var req: Request = .{
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

            var user_context: Request.UserContext = .{};
            req._user_context = &user_context;

            l.settings.on_upgrade.?(req, zigtarget);
        }
    }

    /// Used internally: the listener's facilio finish callback
    pub fn theOneAndOnlyFinishCallBack(s: [*c]fio.struct_http_settings_s) callconv(.C) void {
        if (the_one_and_only_listener) |l| {
            l.settings.on_finish.?(s);
        }
    }

    /// Start listening
    pub fn listen(self: *Self) !void {
        var pfolder: [*c]const u8 = null;
        var pfolder_len: usize = 0;

        if (self.settings.public_folder) |pf| {
            debug("HttpListener.listen(): public folder is {s}\n", .{pf});
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
            .tls = if (self.settings.tls) |tls| tls.fio_tls else null,
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
        // TODO: still happening?
        std.time.sleep(500 * std.time.ns_per_ms);

        var portbuf: [100]u8 = undefined;
        const printed_port = try std.fmt.bufPrintZ(&portbuf, "{d}", .{self.settings.port});

        const ret = fio.http_listen(printed_port.ptr, self.settings.interface, x);
        if (ret == -1) {
            return error.ListenError;
        }

        // set ourselves up to handle requests:
        // TODO: do we mind the race condition?
        // the HttpRequestFn will check if this is null and not process
        // the request if it isn't set. hence, if started under full load, the
        // first request(s) might not be serviced, as long as it takes from
        // fio.http_listen() to here
        Self.the_one_and_only_listener = self;
    }
};

/// Low-level API
pub const LowLevel = struct {
    /// lower level listening, if you don't want to use a listener but rather use
    /// the listen() function.
    pub const ListenSettings = struct {
        on_request: ?FioHttpRequestFn = null,
        on_upgrade: ?FioHttpRequestFn = null,
        on_response: ?FioHttpRequestFn = null,
        on_finish: ?FioHttpRequestFn = null,
        public_folder: ?[]const u8 = null,
        max_header_size: usize = 32 * 1024,
        max_body_size: usize = 50 * 1024 * 1024,
        max_clients: isize = 100,
        keepalive_timeout_s: u8 = 5,
        log: bool = false,

        const Self = @This();

        /// Create settings with defaults
        pub fn init() Self {
            return .{};
        }
    };

    /// Low level listen function
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
        // TODO: still happening?
        std.time.sleep(500 * std.time.ns_per_ms);

        if (fio.http_listen(port, interface, x) == -1) {
            return error.ListenError;
        }
    }

    /// lower level sendBody
    pub fn sendBody(request: [*c]fio.http_s, body: []const u8) HttpError!void {
        const ret = fio.http_send_body(request, @as(
            *anyopaque,
            @ptrFromInt(@intFromPtr(body.ptr)),
        ), body.len);
        debug("sendBody(): ret = {}\n", .{ret});
        if (ret != -1) return error.HttpSendBody;
    }
};
