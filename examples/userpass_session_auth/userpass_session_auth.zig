const std = @import("std");
const zap = @import("zap");

const Lookup = std.StringHashMap([]const u8);
const auth_lock_token_table = false;
const auth_lock_pw_table = false;

// see the source for more info
const Authenticator = zap.UserPassSessionAuth(
    Lookup,
    auth_lock_pw_table, // we may set this to true if we expect our username -> password map to change
    auth_lock_token_table, // we may set this to true to have session tokens deleted server-side on logout
);

const loginpath = "/login";
const loginpage = @embedFile("html/login.html");
const img = @embedFile("./html/Ziggy_the_Ziguana.svg.png");

// global vars yeah!
var authenticator: Authenticator = undefined;

// the login page (embedded)
fn on_login(r: zap.SimpleRequest) void {
    r.sendBody(loginpage) catch return;
}

// the "normal page"
fn on_normal_page(r: zap.SimpleRequest) void {
    zap.debug("on_normal_page()\n", .{});
    r.sendBody(
        \\ <html><body>
        \\ <h1>Hello from ZAP!!!</h1>
        \\ <p>You are logged in!!!</>
        \\ <center><a href="/logout">logout</a></center>
        \\ </body></html>
    ) catch return;
}

// the logged-out page
fn on_logout(r: zap.SimpleRequest) void {
    zap.debug("on_logout()\n", .{});
    authenticator.logout(&r);
    r.sendBody(
        \\ <html><body>
        \\ <p>You are logged out!!!</p>
        \\ </body></html>
    ) catch return;
}

fn on_request(r: zap.SimpleRequest) void {
    switch (authenticator.authenticateRequest(&r)) {
        .Handled => {
            // the authenticator handled the entire request for us. probably
            // a redirect to the login page
            std.log.info("Auth FAILED -> authenticator handled it", .{});
            return;
        },
        .AuthFailed => unreachable,
        .AuthOK => {
            // the authenticator says it is ok to proceed as usual
            std.log.info("Auth OK", .{});
            // dispatch to target path
            if (r.path) |p| {
                // used in the login page
                // note: our login page is /login
                // so, anything that starts with /login will not be touched by
                // the authenticator. Hence, we name the img /login/Ziggy....png
                if (std.mem.startsWith(u8, p, "/login/Ziggy_the_Ziguana.svg.png")) {
                    std.log.info("Auth OK for img", .{});
                    r.setContentTypeFromPath() catch unreachable;
                    r.sendBody(img) catch unreachable;
                    return;
                }

                // aha! got redirected to /login
                if (std.mem.startsWith(u8, p, loginpath)) {
                    std.log.info("    + for /login --> login page", .{});
                    return on_login(r);
                }

                // /logout can be shown since we're still authenticated for this
                // very request
                if (std.mem.startsWith(u8, p, "/logout")) {
                    std.log.info("    + for /logout --> logout page", .{});
                    return on_logout(r);
                }

                // /logout can be shown since we're still authenticated for this
                // very request
                if (std.mem.startsWith(u8, p, "/stop")) {
                    std.log.info("    + for /stop --> logout page", .{});
                    zap.stop();
                    return on_logout(r);
                }

                // any other paths will still show the normal page
                std.log.info("    + --> normal page", .{});
                return on_normal_page(r);
            }
            // if there is no path, we're still authenticated, so let's show
            // the user something
            return on_normal_page(r);
        },
    }
}

pub fn main() !void {
    var gpa = std.heap.GeneralPurposeAllocator(.{
        .thread_safe = true,
    }){};

    // we start a block here so the defers will run before we call the gpa
    // to detect leaks
    {
        var allocator = gpa.allocator();
        var listener = zap.SimpleHttpListener.init(.{
            .port = 3000,
            .on_request = on_request,
            .log = true,
            .max_clients = 100000,
        });
        try listener.listen();

        zap.enableDebugLog();

        // add a single user to our allowed users
        var userpass = Lookup.init(allocator);
        defer userpass.deinit();
        try userpass.put("zap", "awesome");

        // init our auth
        authenticator = try Authenticator.init(
            allocator,
            &userpass,
            .{
                .usernameParam = "username",
                .passwordParam = "password",
                .loginPage = loginpath,
                .cookieName = "zap-session",
            },
        );
        defer authenticator.deinit();

        // just some debug output: listing the session tokens the authenticator may
        // have generated already (if auth_lock_token_table == false).
        const lookup = authenticator.sessionTokens;
        std.debug.print("\nauth token list len: {d}\n", .{lookup.count()});
        var it = lookup.iterator();
        while (it.next()) |item| {
            std.debug.print("    {s}\n", .{item.key_ptr.*});
        }

        std.debug.print("Visit me on http://127.0.0.1:3000\n", .{});

        // start worker threads
        zap.start(.{
            .threads = 2,
            .workers = 1,
        });
    }

    // all defers should have run by now
    std.debug.print("\n\nSTOPPED!\n\n", .{});
    const leaked = gpa.detectLeaks();
    std.debug.print("Leaks detected: {}\n", .{leaked});
}
