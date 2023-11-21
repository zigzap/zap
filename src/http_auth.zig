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

pub const AuthResult = enum {
    /// authentication / authorization was successful
    AuthOK,
    /// authentication / authorization failed
    AuthFailed,
    /// the authenticator handled the request that didn't pass authentication /
    /// authorization .
    /// this is used to implement authenticators that redirect to a login
    /// page. An AuthenticatingEndpoint will not do the default, which is trying
    /// to call the `unauthorized` callback or.
    Handled,
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
        pub fn authenticateUserPass(self: *Self, auth_header: []const u8) AuthResult {
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
                    return .AuthFailed;
                }
                const decoded = buffer[0..decoded_size];
                decoder.decode(decoded, encoded) catch |err| {
                    zap.debug(
                        "ERROR: UserPassAuth: unable to decode `{s}`: {any}\n",
                        .{ encoded, err },
                    );
                    return .AuthFailed;
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
                    return .AuthFailed;
                }
                // now, do the lookup
                const actual_pw = self.lookup.*.get(user.?);
                if (actual_pw) |pw| {
                    const ret = std.mem.eql(u8, pass.?, pw);
                    zap.debug(
                        "INFO: UserPassAuth for user `{s}`: `{s}` == pass `{s}` = {}\n",
                        .{ user.?, pw, pass.?, ret },
                    );
                    return if (ret) .AuthOK else .AuthFailed;
                } else {
                    zap.debug(
                        "ERROR: UserPassAuth: user `{s}` not found in map of size {d}!\n",
                        .{ user.?, self.lookup.*.count() },
                    );
                    return .AuthFailed;
                }
            } else |err| {
                // can't calc slice size --> fallthrough to return false
                zap.debug(
                    "ERROR: UserPassAuth: cannot calc slize size for encoded `{s}`: {any} \n",
                    .{ encoded, err },
                );
                return .AuthFailed;
            }
            zap.debug("UNREACHABLE\n", .{});
            return .AuthFailed;
        }

        /// Use this to just look up if the base64-encoded auth_header exists in lookup
        pub fn authenticateToken68(self: *Self, auth_header: []const u8) AuthResult {
            const token = auth_header[AuthScheme.Basic.str().len..];
            return if (self.lookup.*.contains(token)) .AuthOK else .AuthFailed;
        }

        // dispatch based on kind
        pub fn authenticate(self: *Self, auth_header: []const u8) AuthResult {
            zap.debug("AUTHENTICATE\n", .{});
            // switch (self.kind) {
            switch (kind) {
                .UserPass => return self.authenticateUserPass(auth_header),
                .Token68 => return self.authenticateToken68(auth_header),
            }
        }
        pub fn authenticateRequest(self: *Self, r: *const zap.SimpleRequest) AuthResult {
            zap.debug("AUTHENTICATE REQUEST\n", .{});
            if (extractAuthHeader(.Basic, r)) |auth_header| {
                zap.debug("Authentication Header found!\n", .{});
                return self.authenticate(auth_header);
            } else {
                // try with .Authorization
                if (extractAuthHeader(.Bearer, r)) |auth_header| {
                    zap.debug("Authorization Header found!\n", .{});
                    return self.authenticate(auth_header);
                }
            }
            zap.debug("NO fitting Auth Header found!\n", .{});
            return .AuthFailed;
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
    pub fn authenticate(self: *Self, auth_header: []const u8) AuthResult {
        if (checkAuthHeader(.Bearer, auth_header) == false) {
            return .AuthFailed;
        }
        const token = auth_header[AuthScheme.Bearer.str().len..];
        return if (std.mem.eql(u8, token, self.token)) .AuthOK else .AuthFailed;
    }

    pub fn authenticateRequest(self: *Self, r: *const zap.SimpleRequest) AuthResult {
        if (extractAuthHeader(.Bearer, r)) |auth_header| {
            return self.authenticate(auth_header);
        }
        return .AuthFailed;
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

        pub fn authenticate(self: *Self, auth_header: []const u8) AuthResult {
            if (checkAuthHeader(.Bearer, auth_header) == false) {
                return .AuthFailed;
            }
            const token = auth_header[AuthScheme.Bearer.str().len..];
            return if (self.lookup.*.contains(token)) .AuthOK else .AuthFailed;
        }

        pub fn authenticateRequest(self: *Self, r: *const zap.SimpleRequest) AuthResult {
            if (extractAuthHeader(.Bearer, r)) |auth_header| {
                return self.authenticate(auth_header);
            }
            return .AuthFailed;
        }
    };
}

pub const UserPassSessionAuthArgs = struct {
    /// username body parameter
    usernameParam: []const u8,
    /// password body parameter
    passwordParam: []const u8,
    /// redirect to this page if auth fails
    loginPage: []const u8,
    /// name of the cookie
    cookieName: []const u8,
    /// cookie max age in seconds; 0 -> session cookie
    cookieMaxAge: u8 = 0,
    /// redirect status code, defaults to 302 found
    redirectCode: zap.StatusCode = .found,
};

/// UserPassSessionAuth supports the following use case:
///
/// - checks every request: is it going to the login page? -> let the request through.
/// - else:
///   - checks every request for a session token in a cookie
///   - if there is no token, it checks for correct username and password body params
///     - if username and password are present and correct, it will create a session token,
///       create a response cookie containing the token, and carry on with the request
///     - else it will redirect to the login page
///   - if the session token is present and correct: it will let the request through
///   - else: it will redirect to the login page
///
/// Please note the implications of this simple approach: IF YOU REUSE "username"
/// and "password" body params for anything else in your application, then the
/// mechanisms described above will kick in. For that reason: please know what you're
/// doing.
///
/// See UserPassSessionAuthArgs:
/// - username & password param names can be defined by you
/// - session cookie name and max-age can be defined by you
/// - login page and redirect code (.302) can be defined by you
///
/// Comptime Parameters:
///
/// - `Lookup` must implement .get([]const u8) -> []const u8 for user password retrieval
/// - `lockedPwLookups` : if true, accessing the provided Lookup instance will be protected
///    by a Mutex. You can access the mutex yourself via the `passwordLookupLock`.
///
/// Note: In order to be quick, you can set lockedTokenLookups to false.
///       -> we generate it on init() and leave it static
///       -> there is no way to 100% log out apart from re-starting the server
///       -> because: we send a cookie to the browser that invalidates the session cookie
///       -> another browser program with the page still open would still be able to use
///       -> the session. Which is kindof OK, but not as cool as erasing the token
///       -> on the server side which immediately block all other browsers as well.
pub fn UserPassSessionAuth(comptime Lookup: type, comptime lockedPwLookups: bool) type {
    return struct {
        allocator: std.mem.Allocator,
        lookup: *Lookup,
        settings: UserPassSessionAuthArgs,

        // TODO: cookie store per user
        sessionTokens: SessionTokenMap,
        passwordLookupLock: std.Thread.Mutex = .{},
        tokenLookupLock: std.Thread.Mutex = .{},

        const Self = @This();
        const SessionTokenMap = std.StringHashMap(void);
        const Hash = std.crypto.hash.sha2.Sha256;

        const Token = [Hash.digest_length * 2]u8;

        pub fn init(
            allocator: std.mem.Allocator,
            lookup: *Lookup,
            args: UserPassSessionAuthArgs,
        ) !Self {
            const ret: Self = .{
                .allocator = allocator,
                .settings = .{
                    .usernameParam = try allocator.dupe(u8, args.usernameParam),
                    .passwordParam = try allocator.dupe(u8, args.passwordParam),
                    .loginPage = try allocator.dupe(u8, args.loginPage),
                    .cookieName = try allocator.dupe(u8, args.cookieName),
                    .cookieMaxAge = args.cookieMaxAge,
                    .redirectCode = args.redirectCode,
                },
                .lookup = lookup,
                .sessionTokens = SessionTokenMap.init(allocator),
            };

            return ret;
        }

        pub fn deinit(self: *Self) void {
            self.allocator.free(self.settings.usernameParam);
            self.allocator.free(self.settings.passwordParam);
            self.allocator.free(self.settings.loginPage);
            self.allocator.free(self.settings.cookieName);

            // clean up the session tokens: the key strings are duped
            var key_it = self.sessionTokens.keyIterator();
            while (key_it.next()) |key_ptr| {
                self.allocator.free(key_ptr.*);
            }
            self.sessionTokens.deinit();
        }

        /// Check for session token cookie, remove the token from the valid tokens
        pub fn logout(self: *Self, r: *const zap.SimpleRequest) void {
            // we  erase the list of valid tokens server-side
            if (r.setCookie(.{
                .name = self.settings.cookieName,
                .value = "invalid",
                .max_age_s = -1,
            })) {
                zap.debug("logout ok\n", .{});
            } else |err| {
                zap.debug("logout cookie setting failed: {any}\n", .{err});
            }

            r.parseCookies(false);

            // check for session cookie
            if (r.getCookieStr(self.settings.cookieName, self.allocator, false)) |maybe_cookie| {
                if (maybe_cookie) |cookie| {
                    defer cookie.deinit();
                    self.tokenLookupLock.lock();
                    defer self.tokenLookupLock.unlock();
                    if (self.sessionTokens.getKeyPtr(cookie.str)) |keyPtr| {
                        const keySlice = keyPtr.*;
                        // if cookie is a valid session, remove it!
                        _ = self.sessionTokens.remove(cookie.str);
                        // only now can we let go of the cookie str slice that
                        // was used as the key
                        self.allocator.free(keySlice);
                    }
                }
            } else |err| {
                zap.debug("unreachable: UserPassSessionAuth.logout: {any}", .{err});
            }
        }

        fn _internal_authenticateRequest(self: *Self, r: *const zap.SimpleRequest) AuthResult {
            // if we're requesting the login page, let the request through
            if (r.path) |p| {
                if (std.mem.startsWith(u8, p, self.settings.loginPage)) {
                    return .AuthOK;
                }
            }

            // parse body
            r.parseBody() catch {
                // zap.debug("warning: parseBody() failed in UserPassSessionAuth: {any}", .{err});
                // this is not an error in case of e.g. gets with querystrings
            };

            r.parseCookies(false);

            // check for session cookie
            if (r.getCookieStr(self.settings.cookieName, self.allocator, false)) |maybe_cookie| {
                if (maybe_cookie) |cookie| {
                    defer cookie.deinit();
                    // locked or unlocked token lookup
                    self.tokenLookupLock.lock();
                    defer self.tokenLookupLock.unlock();
                    if (self.sessionTokens.contains(cookie.str)) {
                        // cookie is a valid session!
                        zap.debug("Auth: COOKIE IS OK!!!!: {s}\n", .{cookie.str});
                        return .AuthOK;
                    } else {
                        zap.debug("Auth: COOKIE IS BAD!!!!: {s}\n", .{cookie.str});
                        // this is not necessarily a bad thing. it could be a
                        // stale cookie from a previous session. So let's check
                        // if username and password are being sent and correct.
                    }
                }
            } else |err| {
                zap.debug("unreachable: could not check for cookie in UserPassSessionAuth: {any}", .{err});
            }

            // get params of username and password
            if (r.getParamStr(self.settings.usernameParam, self.allocator, false)) |maybe_username| {
                if (maybe_username) |*username| {
                    defer username.deinit();
                    if (r.getParamStr(self.settings.passwordParam, self.allocator, false)) |maybe_pw| {
                        if (maybe_pw) |*pw| {
                            defer pw.deinit();

                            // now check
                            const correct_pw_optional = brk: {
                                if (lockedPwLookups) {
                                    self.passwordLookupLock.lock();
                                    defer self.passwordLookupLock.unlock();
                                    break :brk self.lookup.*.get(username.str);
                                } else {
                                    break :brk self.lookup.*.get(username.str);
                                }
                            };
                            if (correct_pw_optional) |correct_pw| {
                                if (std.mem.eql(u8, pw.str, correct_pw)) {
                                    // create session token
                                    if (self.createAndStoreSessionToken(username.str, pw.str)) |token| {
                                        defer self.allocator.free(token);
                                        // now set the cookie header
                                        if (r.setCookie(.{
                                            .name = self.settings.cookieName,
                                            .value = token,
                                            .max_age_s = self.settings.cookieMaxAge,
                                        })) {
                                            return .AuthOK;
                                        } else |err| {
                                            zap.debug("could not set session token: {any}", .{err});
                                        }
                                    } else |err| {
                                        zap.debug("could not create session token: {any}", .{err});
                                    }
                                    // errors with token don't mean the auth itself wasn't OK
                                    return .AuthOK;
                                }
                            }
                        }
                    } else |err| {
                        zap.debug("getParamSt() for password failed in UserPassSessionAuth: {any}", .{err});
                        return .AuthFailed;
                    }
                }
            } else |err| {
                zap.debug("getParamSt() for user failed in UserPassSessionAuth: {any}", .{err});
                return .AuthFailed;
            }
            return .AuthFailed;
        }

        pub fn authenticateRequest(self: *Self, r: *const zap.SimpleRequest) AuthResult {
            switch (self._internal_authenticateRequest(r)) {
                .AuthOK => {
                    // username and pass are ok -> created token, set header, caller can continue
                    return .AuthOK;
                },
                // this does not happen, just for completeness
                .Handled => return .Handled,
                // auth failed -> redirect
                .AuthFailed => {
                    // we need to redirect and return .Handled
                    self.redirect(r) catch |err| {
                        // we just give up
                        zap.debug("redirect() failed in UserPassSessionAuth: {any}", .{err});
                    };
                    return .Handled;
                },
            }
        }

        fn redirect(self: *Self, r: *const zap.SimpleRequest) !void {
            try r.redirectTo(self.settings.loginPage, self.settings.redirectCode);
        }

        fn createSessionToken(self: *Self, username: []const u8, password: []const u8) ![]const u8 {
            var hasher = Hash.init(.{});
            hasher.update(username);
            hasher.update(password);
            var buf: [16]u8 = undefined;
            const time_nano = std.time.nanoTimestamp();
            const timestampHex = try std.fmt.bufPrint(&buf, "{0x}", .{time_nano});
            hasher.update(timestampHex);

            var digest: [Hash.digest_length]u8 = undefined;
            hasher.final(&digest);
            const token: Token = std.fmt.bytesToHex(digest, .lower);
            const token_str = try self.allocator.dupe(u8, token[0..token.len]);
            return token_str;
        }

        fn createAndStoreSessionToken(self: *Self, username: []const u8, password: []const u8) ![]const u8 {
            const token = try self.createSessionToken(username, password);
            self.tokenLookupLock.lock();
            defer self.tokenLookupLock.unlock();

            if (!self.sessionTokens.contains(token)) {
                try self.sessionTokens.put(try self.allocator.dupe(u8, token), {});
            }
            return token;
        }
    };
}
