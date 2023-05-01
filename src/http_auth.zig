const std = @import("std");
const zap = @import("zap.zig");

pub const AuthScheme = enum {
    Basic,
    Bearer,

    pub fn str(self: AuthScheme) []const u8 {
        return switch (self) {
            .Basic => "Basic ",
            .Bearer => "Bearer ",
        };
    }

    pub fn headerFieldStrFio(self: AuthScheme) []const u8 {
        return switch (self) {
            .Basic => "authentication",
            .Bearer => "authorization",
        };
    }

    pub fn headerFieldStrHeader(self: AuthScheme) [:0]const u8 {
        return switch (self) {
            .Basic => "Authentication",
            .Bearer => "Authorization",
        };
    }
};

pub fn checkAuthHeader(scheme: AuthScheme, auth_header: []const u8) bool {
    return switch (scheme) {
        .Basic => |b| std.mem.startsWith(u8, auth_header, b.str()) and auth_header.len > b.str().len,
        .Bearer => |b| std.mem.startsWith(u8, auth_header, b.str()) and auth_header.len > b.str().len,
    };
}

pub fn extractAuthHeader(scheme: AuthScheme, r: *const zap.SimpleRequest) ?[]const u8 {
    return switch (scheme) {
        .Basic => |b| r.getHeader(b.headerFieldStrFio()),
        .Bearer => |b| r.getHeader(b.headerFieldStrFio()),
    };
}

const BasicAuthStrategy = enum {
    /// decode into user and pass, then check pass
    UserPass,
    /// just look up the encoded user:pass token
    Token68,
};

/// HTTP Basic Authentication RFC 7617
/// "Authorization: Basic QWxhZGRpbjpvcGVuIHNlc2FtZQ=="
/// user-pass strings: "$username:$password" -> base64
///
/// Notes:
///   - we only look at the Authentication header
///   - we ignore the required realm parameter
///   - we ignore the optional charset parameter
///
/// Errors:
/// WWW-Authenticate: Basic realm="this"
///
/// T : any kind of map that implements get([]const u8) -> []const u8
pub fn BasicAuth(comptime Lookup: type, comptime kind: BasicAuthStrategy) type {
    return struct {
        // kind: BasicAuthStrategy,
        allocator: std.mem.Allocator,
        realm: ?[]const u8,
        lookup: *Lookup,

        const Self = @This();

        /// Creates a BasicAuth. `lookup` must implement `.get([]const u8) -> []const u8`
        /// different implementations can
        ///   - either decode, lookup and compare passwords
        ///   - or just check for existence of the base64-encoded user:pass combination
        /// if realm is provided (not null), a copy is taken -> call deinit() to clean up
        pub fn init(allocator: std.mem.Allocator, lookup: *Lookup, realm: ?[]const u8) !Self {
            return .{
                // .kind = kind,
                .allocator = allocator,
                .lookup = lookup,
                .realm = if (realm) |the_realm| try allocator.dupe(u8, the_realm) else null,
            };
        }

        pub fn deinit(self: *Self) void {
            if (self.realm) |the_realm| {
                self.allocator.free(the_realm);
            }
        }

        /// Use this to decode the auth_header into user:pass, lookup pass in lookup
        pub fn authenticateUserPass(self: *Self, auth_header: []const u8) bool {
            zap.debug("AuthenticateUserPass\n", .{});
            const encoded = auth_header[AuthScheme.Basic.str().len..];
            const decoder = std.base64.standard.Decoder;
            var buffer: [0x100]u8 = undefined;
            if (decoder.calcSizeForSlice(encoded)) |decoded_size| {
                if (decoded_size >= buffer.len) {
                    zap.debug(
                        "ERROR: UserPassAuth: decoded_size {d} >= buffer.len {d}\n",
                        .{ decoded_size, buffer.len },
                    );
                    return false;
                }
                var decoded = buffer[0..decoded_size];
                decoder.decode(decoded, encoded) catch |err| {
                    zap.debug(
                        "ERROR: UserPassAuth: unable to decode `{s}`: {any}\n",
                        .{ encoded, err },
                    );
                    return false;
                };
                // we have decoded
                // we can split
                var it = std.mem.split(u8, decoded, ":");
                const user = it.next();
                const pass = it.next();
                if (user == null or pass == null) {
                    zap.debug(
                        "ERROR: UserPassAuth: user {any} or pass {any} is null\n",
                        .{ user, pass },
                    );
                    return false;
                }
                // now, do the lookup
                const actual_pw = self.lookup.*.get(user.?);
                if (actual_pw) |pw| {
                    const ret = std.mem.eql(u8, pass.?, pw);
                    zap.debug(
                        "INFO: UserPassAuth for user `{s}`: `{s}` == pass `{s}` = {}\n",
                        .{ user.?, pw, pass.?, ret },
                    );
                    return ret;
                } else {
                    zap.debug(
                        "ERROR: UserPassAuth: user `{s}` not found in map of size {d}!\n",
                        .{ user.?, self.lookup.*.count() },
                    );
                    return false;
                }
            } else |err| {
                // can't calc slice size --> fallthrough to return false
                zap.debug(
                    "ERROR: UserPassAuth: cannot calc slize size for encoded `{s}`: {any} \n",
                    .{ encoded, err },
                );
                return false;
            }
            zap.debug("UNREACHABLE\n", .{});
            return false;
        }

        /// Use this to just look up if the base64-encoded auth_header exists in lookup
        pub fn authenticateToken68(self: *Self, auth_header: []const u8) bool {
            const token = auth_header[AuthScheme.Basic.str().len..];
            return self.lookup.*.contains(token);
        }

        // dispatch based on kind
        pub fn authenticate(self: *Self, auth_header: []const u8) bool {
            zap.debug("AUTHENTICATE\n", .{});
            // switch (self.kind) {
            switch (kind) {
                .UserPass => return self.authenticateUserPass(auth_header),
                .Token68 => return self.authenticateToken68(auth_header),
            }
        }
        pub fn authenticateRequest(self: *Self, r: *const zap.SimpleRequest) bool {
            zap.debug("AUTHENTICATE REQUEST\n", .{});
            if (extractAuthHeader(.Basic, r)) |auth_header| {
                zap.debug("Auth Header found!\n", .{});
                return self.authenticate(auth_header);
            }
            zap.debug("NO Auth Header found!\n", .{});
            return false;
        }
    };
}

/// HTTP bearer authentication for a single token
/// RFC 6750
/// "Authentication: Bearer TOKEN"
/// `Bearer` is case-sensitive
///   - we don't support form-encoded `access_token` body parameter
///   - we don't support URI query parameter `access_token`
///
/// Errors:
/// HTTP/1.1 401 Unauthorized
/// WWW-Authenticate: Bearer realm="example", error="invalid_token", error_description="..."
pub const BearerAuthSingle = struct {
    allocator: std.mem.Allocator,
    token: []const u8,
    realm: ?[]const u8,

    const Self = @This();

    /// Creates a Single-Token Bearer Authenticator
    /// takes a copy of the token
    /// if realm is provided (not null), a copy is taken
    /// call deinit() to clean up
    pub fn init(allocator: std.mem.Allocator, token: []const u8, realm: ?[]const u8) !Self {
        return .{
            .allocator = allocator,
            .token = try allocator.dupe(u8, token),
            .realm = if (realm) |the_realm| try allocator.dupe(u8, the_realm) else null,
        };
    }
    pub fn authenticate(self: *Self, auth_header: []const u8) bool {
        if (checkAuthHeader(.Bearer, auth_header) == false) {
            return false;
        }
        const token = auth_header[AuthScheme.Bearer.str().len..];
        return std.mem.eql(u8, token, self.token);
    }

    pub fn authenticateRequest(self: *Self, r: *const zap.SimpleRequest) bool {
        if (extractAuthHeader(.Bearer, r)) |auth_header| {
            return self.authenticate(auth_header);
        }
        return false;
    }

    pub fn deinit(self: *Self) void {
        if (self.realm) |the_realm| {
            self.allocator.free(the_realm);
        }
        self.allocator.free(self.token);
    }
};

/// HTTP bearer authentication for multiple tokens
/// RFC 6750
/// "Authentication: Bearer TOKEN"
/// `Bearer` is case-sensitive
///   - we don't support form-encoded `access_token` body parameter
///   - we don't support URI query parameter `access_token`
///
/// Errors:
/// HTTP/1.1 401 Unauthorized
/// WWW-Authenticate: Bearer realm="example", error="invalid_token", error_description="..."
pub fn BearerAuthMulti(comptime Lookup: type) type {
    return struct {
        allocator: std.mem.Allocator,
        lookup: *Lookup,
        realm: ?[]const u8,

        const Self = @This();

        /// Creates a BasicAuth. `lookup` must implement `.get([]const u8) -> []const u8`
        /// to look up tokens
        /// if realm is provided (not null), a copy is taken -> call deinit() to clean up
        pub fn init(allocator: std.mem.Allocator, lookup: *Lookup, realm: ?[]const u8) !Self {
            return .{
                .allocator = allocator,
                .lookup = lookup,
                .realm = if (realm) |the_realm| try allocator.dupe(u8, the_realm) else null,
            };
        }

        pub fn deinit(self: *Self) void {
            if (self.realm) |the_realm| {
                self.allocator.free(the_realm);
            }
        }

        pub fn authenticate(self: *Self, auth_header: []const u8) bool {
            if (checkAuthHeader(.Bearer, auth_header) == false) {
                return false;
            }
            const token = auth_header[AuthScheme.Bearer.str().len..];
            return self.lookup.*.contains(token);
        }

        pub fn authenticateRequest(self: *Self, r: *const zap.SimpleRequest) bool {
            if (extractAuthHeader(.Bearer, r)) |auth_header| {
                return self.authenticate(auth_header);
            }
            return false;
        }
    };
}
