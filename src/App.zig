//! WIP: zap.App.
//!
//! - Per Request Arena(s) thread-local?
//! - Custom "State" Context, type-safe
//! - route handlers
//! - automatic error catching & logging, optional report to HTML

const std = @import("std");
const zap = @import("zap.zig");

pub const Opts = struct {
    request_error_strategy: enum {
        /// log errors to console
        log_to_console,
        /// log errors to console AND generate a HTML response
        log_to_response,
        /// raise errors -> TODO: clarify: where can they be caught? in App.run()
        raise,
    },
};

threadlocal var arena: ?std.heap.ArenaAllocator = null;

pub fn create(comptime Context: type, context: *Context, opts: Opts) type {
    return struct {
        context: *Context = context,
        error_strategy: @TypeOf(opts.request_error_strategy) = opts.request_error_strategy,
        endpoints: std.StringArrayHashMapUnmanaged(*zap.Endpoint.Wrapper.Internal) = .empty,
    };
}
