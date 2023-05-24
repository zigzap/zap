const std = @import("std");
const zap = @import("zap");

const Lookup = std.StringHashMap([]const u8);
const auth_lock_pw_table = false;

// see the source for more info
const Authenticator = zap.UserPassSessionAuth(
    Lookup,
    // we may set this to true if we expect our username -> password map
    // to change. in that case the authenticator must lock the table for
    // every lookup
    auth_lock_pw_table,
);

const loginpath = "/login";

// we bake the login page and its displayed image into the the executable
const loginpage = @embedFile("html/login.html");
const img = @embedFile("./html/Ziggy_the_Ziguana.svg.png");

// global vars yeah!
//     in bigger projects, we'd probably make use of zap.SimpleEndpoint or
//     zap.Middleware and "hide" stuff like authenticators in there
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
    // note, the link below doesn't matter as the authenticator will send us
    // straight to the /login page
    r.sendBody(
        \\ <html><body>
        \\ <p>You are logged out!!!</p>
        \\ <br>
        \\ <p> <a href="/">Log back in</a></p>
        \\ </body></html>
    ) catch return;
}

fn on_request(r: zap.SimpleRequest) void {
    switch (authenticator.authenticateRequest(&r)) {
        .Handled => {
            // the authenticator handled the entire request for us.
            // that means: it re-directed to the /login page because of a
            // missing or invalid session cookie
            std.log.info("Auth FAILED -> authenticator handled it", .{});
            return;
        },

        // never returned by this type of authenticator
        .AuthFailed => unreachable,

        .AuthOK => {
            // the authenticator says it is ok to proceed as usual
            std.log.info("Auth OK", .{});
            // dispatch to target path
            if (r.path) |p| {
                // used in the login page
                // note: our login page is /login
                // so, anything that starts with /login will not be touched by
                // the authenticator. Hence, we name the img for the /login
                // page: /login/Ziggy....png
                if (std.mem.startsWith(u8, p, "/login/Ziggy_the_Ziguana.svg.png")) {
                    r.setContentTypeFromPath() catch unreachable;
                    r.sendBody(img) catch unreachable;
                    return;
                }

                // aha! probably got redirected to /login
                if (std.mem.startsWith(u8, p, loginpath)) {
                    std.log.info("    + for /login --> login page", .{});
                    return on_login(r);
                }

                // /logout can be shown since we're authenticated
                if (std.mem.startsWith(u8, p, "/logout")) {
                    std.log.info("    + for /logout --> logout page", .{});
                    return on_logout(r);
                }

                // /stop can be executed, as we're authenticated
                if (std.mem.startsWith(u8, p, "/stop")) {
                    std.log.info("    + for /stop --> logout page", .{});
                    zap.stop();
                    return on_logout(r);
                }

                // any other paths will show the normal page
                std.log.info("    + --> normal page", .{});
                return on_normal_page(r);
            }
            // if there is no path but we're authenticated, so let's show
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

        // Usernames -> Passwords for the /login page
        // ------------------------------------------
        var userpass = Lookup.init(allocator);
        defer userpass.deinit();
        try userpass.put("zap", "awesome");

        // init our authenticator. it will redirect all un-authenticated
        // requests to the /login page. on POST of correct username, password
        // pair, it will create an ephermal session token and let subsequent
        // requests that present the cookie through until, in our case: /logout.
        authenticator = try Authenticator.init(
            allocator,
            &userpass,
            .{
                .usernameParam = "username", // form param name
                .passwordParam = "password", // form param name
                .loginPage = loginpath,
                .cookieName = "zap-session", // cookie name for session
            },
        );
        defer authenticator.deinit();

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
