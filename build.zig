const std = @import("std");
const build_facilio = @import("facil.io/build.zig").build_facilio;

// Basically a wrapper around some common params that you would pass around to create tests (zig made them very verbose lately, unfortunately),
// save these to a struct so you don't have to pass the same params all the time.
const TestSystem = struct {
    b: *std.Build,
    zap_module: *std.Build.Module,
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
    combine_test_step: *std.Build.Step,

    pub fn addTest(self: TestSystem, root_src: []const u8, test_name: []const u8) void {
        const tests_module = self.b.addModule(test_name, .{
            .root_source_file = self.b.path(root_src),
            .target = self.target,
            .optimize = self.optimize,
        });
        const tests = self.b.addTest(.{
            .name = self.b.fmt("{s}_tests", .{test_name}),
            .root_module = tests_module,
        });
        tests.root_module.addImport("zap", self.zap_module);

        const step = self.b.step(self.b.fmt("test-{s}", .{test_name}), self.b.fmt("Run {s} unit tests [REMOVE zig-cache!]", .{test_name}));
        self.addRunInstallToStep(tests, step);
    }

    fn addRunInstallToStep(self: TestSystem, tests: *std.Build.Step.Compile, step: *std.Build.Step) void {
        const run_tests = self.b.addRunArtifact(tests);
        const install_tests = self.b.addInstallArtifact(tests, .{});
        step.dependOn(&run_tests.step);
        step.dependOn(&install_tests.step);

        self.combine_test_step.dependOn(step);
    }
};

pub fn build(b: *std.Build) !void {
    const target = b.standardTargetOptions(.{});
    if (target.result.os.tag == .windows) {
        std.log.err("\x1b[31mPlatform Not Supported\x1b[0m\nCurrently, Facil.io and Zap are not compatible with Windows. Consider using Linux or Windows Subsystem for Linux (WSL) instead.\nFor more information, please see:\n- https://github.com/zigzap/zap#most-faq\n- https://facil.io/#forking-contributing-and-all-that-jazz\n", .{});
        std.process.exit(1);
    }
    // Standard release options allow the person running `zig build` to select
    // between Debug, ReleaseSafe, ReleaseFast, and ReleaseSmall.
    const optimize = b.standardOptimizeOption(.{});

    const use_openssl = b.option(bool, "openssl", "Use system-installed openssl for TLS support in zap") orelse blk: {
        // Alternatively, use an os env var to determine whether to build openssl support
        if (std.process.getEnvVarOwned(b.allocator, "ZAP_USE_OPENSSL")) |val| {
            defer b.allocator.free(val);
            if (std.mem.eql(u8, val, "true")) break :blk true;
        } else |_| {}
        break :blk false;
    };

    const facilio = try build_facilio("facil.io", b, target, optimize, use_openssl);

    const zap_module = b.addModule("zap", .{
        .root_source_file = b.path("src/zap.zig"),
        .target = target,
        .optimize = optimize,
    });
    zap_module.linkLibrary(facilio);

    const all_step = b.step("all", "build all examples");

    // -- Docs
    const docs_obj = b.addObject(.{
        .name = "zap", // name doesn't seem to matter
        .root_module = zap_module,
    });
    const install_docs = b.addInstallDirectory(.{
        .install_dir = .prefix,
        .install_subdir = "zap", // will also be the main namespace in the docs
        .source_dir = docs_obj.getEmittedDocs(),
    });
    b.step("docs", "Build docs").dependOn(&install_docs.step);
    // --

    inline for ([_]struct {
        name: []const u8,
        src: []const u8,
    }{
        .{ .name = "app_basic", .src = "examples/app/basic.zig" },
        .{ .name = "app_auth", .src = "examples/app/auth.zig" },
        .{ .name = "app_errors", .src = "examples/app/errors.zig" },
        .{ .name = "hello", .src = "examples/hello/hello.zig" },
        .{ .name = "https", .src = "examples/https/https.zig" },
        .{ .name = "hello2", .src = "examples/hello2/hello2.zig" },
        .{ .name = "simple_router", .src = "examples/simple_router/simple_router.zig" },
        .{ .name = "routes", .src = "examples/routes/routes.zig" },
        .{ .name = "serve", .src = "examples/serve/serve.zig" },
        .{ .name = "hello_json", .src = "examples/hello_json/hello_json.zig" },
        .{ .name = "endpoint", .src = "examples/endpoint/main.zig" },
        .{ .name = "mustache", .src = "examples/mustache/mustache.zig" },
        .{ .name = "endpoint_auth", .src = "examples/endpoint_auth/endpoint_auth.zig" },
        .{ .name = "http_params", .src = "examples/http_params/http_params.zig" },
        .{ .name = "cookies", .src = "examples/cookies/cookies.zig" },
        .{ .name = "websockets", .src = "examples/websockets/websockets.zig" },
        .{ .name = "userpass_session", .src = "examples/userpass_session_auth/userpass_session_auth.zig" },
        .{ .name = "sendfile", .src = "examples/sendfile/sendfile.zig" },
        .{ .name = "middleware", .src = "examples/middleware/middleware.zig" },
        .{ .name = "middleware_with_endpoint", .src = "examples/middleware_with_endpoint/middleware_with_endpoint.zig" },
        .{ .name = "senderror", .src = "examples/senderror/senderror.zig" },
        .{ .name = "bindataformpost", .src = "examples/bindataformpost/bindataformpost.zig" },
        .{ .name = "accept", .src = "examples/accept/accept.zig" },
    }) |excfg| {
        const ex_name = excfg.name;
        const ex_src = excfg.src;

        const ex_build_desc = try std.fmt.allocPrint(
            b.allocator,
            "build the {s} example",
            .{ex_name},
        );
        const ex_run_stepname = try std.fmt.allocPrint(
            b.allocator,
            "run-{s}",
            .{ex_name},
        );
        const ex_run_stepdesc = try std.fmt.allocPrint(
            b.allocator,
            "run the {s} example",
            .{ex_name},
        );
        const example_run_step = b.step(ex_run_stepname, ex_run_stepdesc);
        const example_step = b.step(ex_name, ex_build_desc);

        const exe_mod = b.addModule(ex_name, .{
            .root_source_file = b.path(ex_src),
            .target = target,
            .optimize = optimize,
        });

        var example = b.addExecutable(.{
            .name = ex_name,
            .root_module = exe_mod,
        });

        example.root_module.addImport("zap", zap_module);

        // const example_run = example.run();
        const example_run = b.addRunArtifact(example);
        example_run_step.dependOn(&example_run.step);

        // install the artifact - depending on the "example"
        const example_build_step = b.addInstallArtifact(example, .{});
        example_step.dependOn(&example_build_step.step);

        // ignore https in all because of required -Dopenssl=true
        // TODO: fix GH pipeline to take care of that
        // or: auto-provide openssl for https in build.zig
        if (!std.mem.eql(u8, ex_name, "https")) {
            all_step.dependOn(&example_build_step.step);
        }
    }

    //
    // TOOLS & TESTING
    //
    // n.b.: tests run in parallel, so we need all tests that use the network
    //       to run sequentially, since zap doesn't like to be started multiple
    //       times on different threads
    //
    // TODO: for some reason, tests aren't run more than once unless
    //       dependencies have changed.
    //       So, for now, we just force the exe to be built, so in order that
    //       we can call it again when needed.

    const test_step = b.step("test", "Run unit tests");
    const test_system = TestSystem{ .b = b, .zap_module = zap_module, .target = target, .optimize = optimize, .combine_test_step = test_step };
    // authentication tests
    test_system.addTest("src/tests/test_auth.zig", "auth");
    // mustache tests
    test_system.addTest("src/tests/test_mustache.zig", "mustache");
    // http paramters (qyery, body) tests
    test_system.addTest("src/tests/test_http_params.zig", "http_params");
    // http paramters (qyery, body) tests
    test_system.addTest("src/tests/test_sendfile.zig", "sendfile");
    test_system.addTest("src/tests/test_recvfile.zig", "recv");
    test_system.addTest("src/tests/test_recvfile_notype.zig", "recv_notype");
    // TODO: for some reason, tests aren't run more than once unless
    //       dependencies have changed.
    //       So, for now, we just force the exe to be built, so in order that
    //       we can call it again when needed.

    //
    // docserver
    const docserver_mod = b.addModule("docserver", .{
        .root_source_file = b.path("./tools/docserver.zig"),
        .target = target,
        .optimize = optimize,
    });
    const docserver_exe = b.addExecutable(.{
        .name = "docserver",
        .root_module = docserver_mod,
    });
    docserver_exe.root_module.addImport("zap", zap_module);
    var docserver_step = b.step("docserver", "Build docserver");
    const docserver_build_step = b.addInstallArtifact(docserver_exe, .{});
    docserver_step.dependOn(&docserver_build_step.step);
    docserver_step.dependOn(&install_docs.step);

    const docserver_run_step = b.step("run-docserver", "run the docserver");
    const docserver_run = b.addRunArtifact(docserver_exe);
    docserver_run.addPrefixedDirectoryArg("--docs=", docs_obj.getEmittedDocs());

    docserver_run_step.dependOn(&docserver_run.step);
    docserver_run_step.dependOn(docserver_step);

    all_step.dependOn(&docserver_build_step.step);

    //
    // announceybot
    //
    const announceybot_mod = b.addModule("announceybot", .{
        .root_source_file = b.path("./tools/announceybot.zig"),
        .target = target,
        .optimize = optimize,
    });
    const announceybot_exe = b.addExecutable(.{
        .name = "announceybot",
        .root_module = announceybot_mod,
    });
    var announceybot_step = b.step("announceybot", "Build announceybot");
    const announceybot_build_step = b.addInstallArtifact(announceybot_exe, .{});
    announceybot_step.dependOn(&announceybot_build_step.step);
    all_step.dependOn(&announceybot_build_step.step);
}
