const std = @import("std");
const zap = @import("zap.zig");
const fio = @import("fio.zig");
const util = @import("util.zig");

pub const WsHandle = ?*fio.ws_s;
pub fn Handler(comptime ContextType: type) type {
    return struct {
        /// OnMessage Callback on a websocket
        pub const WsOnMessageFn = *const fn (
            /// user-provided context, passed in from websocketHttpUpgrade()
            context: ?*ContextType,
            /// websocket handle, used to identify the websocket internally
            handle: WsHandle,
            /// the received message
            message: []const u8,
            /// indicator if message is text or binary
            is_text: bool,
        ) void;

        /// Callback when websocket is closed. uuid is a connection identifier,
        /// it is -1 if a connection could not be established
        pub const WsOnCloseFn = *const fn (context: ?*ContextType, uuid: isize) void;

        /// A websocket callback function. provides the context passed in at
        /// websocketHttpUpgrade().
        pub const WsFn = *const fn (context: ?*ContextType, handle: WsHandle) void;

        pub const WebSocketSettings = struct {
            /// on_message(context, handle, message, is_text)
            on_message: ?WsOnMessageFn = null,
            /// on_open(context)
            on_open: ?WsFn = null,
            /// on_ready(context)
            on_ready: ?WsFn = null,
            /// on_shutdown(context, uuid)
            on_shutdown: ?WsFn = null,
            /// on_close(context)
            on_close: ?WsOnCloseFn = null,
            /// passed-in user-defined context
            context: ?*ContextType = null,
        };

        /// This function will end the HTTP stage of the connection and attempt to "upgrade" to a WebSockets connection.
        pub fn upgrade(h: [*c]fio.http_s, settings: *WebSocketSettings) WebSocketError!void {
            const fio_settings: fio.websocket_settings_s = .{
                .on_message = internal_on_message,
                .on_open = internal_on_open,
                .on_ready = internal_on_ready,
                .on_shutdown = internal_on_shutdown,
                .on_close = internal_on_close,
                .udata = settings,
            };
            if (fio.http_upgrade2ws(h, fio_settings) != 0) {
                return error.UpgradeError;
            }
        }

        fn internal_on_message(handle: WsHandle, msg: fio.fio_str_info_s, is_text: u8) callconv(.C) void {
            const user_provided_settings: ?*WebSocketSettings = @as(?*WebSocketSettings, @ptrCast(@alignCast(fio.websocket_udata_get(handle))));
            const message = msg.data[0..msg.len];
            if (user_provided_settings) |settings| {
                if (settings.on_message) |on_message| {
                    on_message(settings.context, handle, message, is_text == 1);
                }
            }
        }

        fn internal_on_open(handle: WsHandle) callconv(.C) void {
            const user_provided_settings: ?*WebSocketSettings = @as(?*WebSocketSettings, @ptrCast(@alignCast(fio.websocket_udata_get(handle))));
            if (user_provided_settings) |settings| {
                if (settings.on_open) |on_open| {
                    on_open(settings.context, handle);
                }
            }
        }

        fn internal_on_ready(handle: WsHandle) callconv(.C) void {
            const user_provided_settings: ?*WebSocketSettings = @as(?*WebSocketSettings, @ptrCast(@alignCast(fio.websocket_udata_get(handle))));
            if (user_provided_settings) |settings| {
                if (settings.on_ready) |on_ready| {
                    on_ready(settings.context, handle);
                }
            }
        }

        fn internal_on_shutdown(handle: WsHandle) callconv(.C) void {
            const user_provided_settings: ?*WebSocketSettings = @as(?*WebSocketSettings, @ptrCast(@alignCast(fio.websocket_udata_get(handle))));
            if (user_provided_settings) |settings| {
                if (settings.on_shutdown) |on_shutdown| {
                    on_shutdown(settings.context, handle);
                }
            }
        }

        fn internal_on_close(uuid: isize, udata: ?*anyopaque) callconv(.C) void {
            const user_provided_settings: ?*WebSocketSettings = @as(?*WebSocketSettings, @ptrCast(@alignCast(udata)));
            if (user_provided_settings) |settings| {
                if (settings.on_close) |on_close| {
                    on_close(settings.context, uuid);
                }
            }
        }

        const WebSocketError = error{
            WriteError,
            UpgradeError,
            SubscribeError,
        };

        pub inline fn write(handle: WsHandle, message: []const u8, is_text: bool) WebSocketError!void {
            if (fio.websocket_write(
                handle,
                fio.str2fio(message),
                if (is_text) 1 else 0,
            ) != 0) {
                return error.WriteError;
            }
        }

        pub fn udataToContext(udata: *anyopaque) *ContextType {
            return @as(*ContextType, @ptrCast(@alignCast(udata)));
        }

        pub inline fn close(handle: WsHandle) void {
            fio.websocket_close(handle);
        }

        const PublishArgs = struct {
            channel: []const u8,
            message: []const u8,
            is_json: bool = false,
        };

        /// publish a message in a channel
        pub inline fn publish(args: PublishArgs) void {
            fio.fio_publish(.{
                .channel = util.str2fio(args.channel),
                .message = util.str2fio(args.message),
                .is_json = if (args.is_json) 1 else 0,
            });
        }

        pub const SubscriptionOnMessageFn = *const fn (context: ?*ContextType, handle: WsHandle, channel: []const u8, message: []const u8) void;
        pub const SubscriptionOnUnsubscribeFn = *const fn (context: ?*ContextType) void;

        pub const SubscribeArgs = struct {
            channel: []const u8,
            on_message: ?SubscriptionOnMessageFn = null,
            on_unsubscribe: ?SubscriptionOnUnsubscribeFn = null,
            /// this is not wrapped nicely yet
            match: fio.fio_match_fn = null,
            /// When using direct message forwarding (no on_message callback), this indicates if
            /// messages should be sent to the client as binary blobs, which is the safest approach.
            /// By default, facil.io will test for UTF-8 data validity and send the data as text if
            /// it's a valid UTF-8. Messages above ~32Kb might be assumed to be binary rather than
            /// tested.
            force_binary: bool = false,
            /// When using direct message forwarding (no on_message callback), this indicates if
            /// messages should be sent to the client as UTF-8 text. By default, facil.io will test
            /// for UTF-8 data validity and send the data as text if it's a valid UTF-8. Messages
            /// above ~32Kb might be assumed to be binary rather than tested. force_binary has
            /// precedence over force_text.
            force_text: bool = false,
            context: ?*ContextType = null,
        };

        /// Returns a subscription ID on success and 0 on failure.
        /// we copy the pointer so make sure the struct stays  valid.
        /// we need it to look up the ziggified callbacks.
        pub inline fn subscribe(handle: WsHandle, args: *SubscribeArgs) WebSocketError!usize {
            if (handle == null) return error.SubscribeError;
            const fio_args: fio.websocket_subscribe_s_zigcompat = .{
                .ws = handle.?,
                .channel = util.str2fio(args.channel),
                .on_message = if (args.on_message) |_| internal_subscription_on_message else null,
                .on_unsubscribe = if (args.on_unsubscribe) |_| internal_subscription_on_unsubscribe else null,
                .match = args.match,
                .force_binary = if (args.force_binary) 1 else 0,
                .force_text = if (args.force_text) 1 else 0,
                .udata = args,
            };
            const ret = fio.websocket_subscribe_zigcompat(fio_args);
            if (ret == 0) {
                return error.SubscribeError;
            }
            return ret;
        }

        pub fn internal_subscription_on_message(handle: WsHandle, channel: fio.fio_str_info_s, message: fio.fio_str_info_s, udata: ?*anyopaque) callconv(.C) void {
            if (udata) |p| {
                const args = @as(*SubscribeArgs, @ptrCast(@alignCast(p)));
                if (args.on_message) |on_message| {
                    on_message(args.context, handle, channel.data[0..channel.len], message.data[0..message.len]);
                }
            }
        }
        pub fn internal_subscription_on_unsubscribe(udata: ?*anyopaque) callconv(.C) void {
            if (udata) |p| {
                const args = @as(*SubscribeArgs, @ptrCast(@alignCast(p)));
                if (args.on_unsubscribe) |on_unsubscribe| {
                    on_unsubscribe(args.context);
                }
            }
        }
    };
}
