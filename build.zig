const std = @import("std");

pub fn build(b: *std.build.Builder) !void {
    const target = b.standardTargetOptions(.{});
    // Standard release options allow the person running `zig build` to select
    // between Debug, ReleaseSafe, ReleaseFast, and ReleaseSmall.
    const optimize = b.standardOptimizeOption(.{});

    var ensure_step = b.step("deps", "ensure external dependencies");
    ensure_step.makeFn = ensureDeps;

    inline for ([_]struct {
        name: []const u8,
        src: []const u8,
    }{
        .{ .name = "hello", .src = "examples/hello/hello.zig" },
        .{ .name = "hello2", .src = "examples/hello2/hello2.zig" },
        .{ .name = "routes", .src = "examples/routes/routes.zig" },
        .{ .name = "serve", .src = "examples/serve/serve.zig" },
        .{ .name = "hello_json", .src = "examples/hello_json/hello_json.zig" },
        .{ .name = "endpoint", .src = "examples/endpoint/main.zig" },
        .{ .name = "wrk", .src = "wrk/zig/main.zig" },
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

        var example = b.addExecutable(.{
            .name = ex_name,
            .root_source_file = .{ .path = ex_src },
            .target = target,
            .optimize = optimize,
        });
        const zap_module = b.createModule(.{ .source_file = .{ .path = "src/zap.zig" } });
        example.addModule("zap", zap_module);
        try addFacilio(example, "./");

        const example_run = example.run();
        example_run_step.dependOn(&example_run.step);

        // install the artifact - depending on the "example"
        // only after the ensure step
        // the step invoked via `zig build example` on the installed exe which
        // itself depends on the "ensure" step
        const example_build_step = b.addInstallArtifact(example);
        example.step.dependOn(ensure_step);
        example_step.dependOn(&example_build_step.step);
    }
}

pub fn ensureDeps(step: *std.build.Step) !void {
    _ = step;
    const allocator = std.heap.page_allocator;
    ensureGit(allocator);
    ensureGitHook(allocator);
    try ensureSubmodule(allocator, "src/deps/facilio");
    try ensurePatch(allocator, "src/deps/facilio", "../0001-microsecond-logging.patch");
    ensureMake(allocator);
    try makeFacilioLibdump(allocator);
}

pub fn addFacilio(exe: *std.build.CompileStep, comptime p: [*]const u8) !void {
    exe.linkLibC();

    // Generate flags
    var flags = std.ArrayList([]const u8).init(std.heap.page_allocator);
    if (exe.optimize != .Debug) try flags.append("-Os");
    try flags.append("-Wno-return-type-c-linkage");
    try flags.append("-fno-sanitize=undefined");
    try flags.append("-DFIO_OVERRIDE_MALLOC");
    try flags.append("-DFIO_HTTP_EXACT_LOGGING");
    exe.addIncludePath(p ++ "src/deps/facilio/libdump/all");

    // Add C
    exe.addCSourceFiles(&.{
        p ++ "src/deps/facilio/libdump/all/http.c",
        p ++ "src/deps/facilio/libdump/all/fiobj_numbers.c",
        p ++ "src/deps/facilio/libdump/all/fio_siphash.c",
        p ++ "src/deps/facilio/libdump/all/fiobj_str.c",
        p ++ "src/deps/facilio/libdump/all/http1.c",
        p ++ "src/deps/facilio/libdump/all/fiobj_ary.c",
        p ++ "src/deps/facilio/libdump/all/fiobj_data.c",
        p ++ "src/deps/facilio/libdump/all/fiobj_hash.c",
        p ++ "src/deps/facilio/libdump/all/websockets.c",
        p ++ "src/deps/facilio/libdump/all/fiobj_json.c",
        p ++ "src/deps/facilio/libdump/all/fio.c",
        p ++ "src/deps/facilio/libdump/all/fiobject.c",
        p ++ "src/deps/facilio/libdump/all/http_internal.c",
        p ++ "src/deps/facilio/libdump/all/fiobj_mustache.c",
    }, flags.items);
}

pub fn addZap(exe: *std.build.CompileStep, comptime p: [*]const u8) !void {
    try addFacilio(exe, p);
    var b = exe.builder;
    var ensure_step = b.step("zap-deps", "ensure zap's dependencies");
    ensure_step.makeFn = ensureDeps;
    exe.step.dependOn(ensure_step);
}

fn sdkPath(comptime suffix: []const u8) []const u8 {
    if (suffix[0] != '/') @compileError("suffix must be an absolute path");
    return comptime blk: {
        const root_dir = std.fs.path.dirname(@src().file) orelse ".";
        break :blk root_dir ++ suffix;
    };
}

fn ensureGit(allocator: std.mem.Allocator) void {
    const result = std.ChildProcess.exec(.{
        .allocator = allocator,
        .argv = &.{ "git", "--version" },
    }) catch { // e.g. FileNotFound
        std.log.err("error: 'git --version' failed. Is git not installed?", .{});
        std.process.exit(1);
    };
    defer {
        allocator.free(result.stderr);
        allocator.free(result.stdout);
    }
    if (result.term.Exited != 0) {
        std.log.err("error: 'git --version' failed. Is git not installed?", .{});
        std.process.exit(1);
    }
}

pub fn ensureGitHook(allocator: std.mem.Allocator) void {
    // only check if we ourselves are not part of a submodule
    var cwd = std.fs.cwd().openDir(sdkPath("/"), .{}) catch return;
    defer cwd.close();
    const dotgit = cwd.statFile(".git") catch return;
    if (dotgit.kind == .File) {
        // we are checked out as a submodule. No point in trying to install
        // a hook
        return;
    }

    var child = std.ChildProcess.init(
        &.{ "cp", "precommit-hook.sh", ".git/hooks/pre-commit" },
        allocator,
    );
    child.cwd = sdkPath("/");
    child.stderr = std.io.getStdErr();
    child.stdout = std.io.getStdOut();
    _ = child.spawnAndWait() catch {
        std.log.err("Warning: unable to install git precommit-hook ", .{});
        return;
    };
}

fn ensureSubmodule(allocator: std.mem.Allocator, path: []const u8) !void {
    if (std.process.getEnvVarOwned(allocator, "NO_ENSURE_SUBMODULES")) |no_ensure_submodules| {
        defer allocator.free(no_ensure_submodules);
        if (std.mem.eql(u8, no_ensure_submodules, "true")) return;
    } else |_| {}
    var child = std.ChildProcess.init(&.{ "git", "submodule", "update", "--init", path }, allocator);
    child.cwd = sdkPath("/");
    child.stderr = std.io.getStdErr();
    child.stdout = std.io.getStdOut();
    _ = try child.spawnAndWait();
}

fn ensurePatch(allocator: std.mem.Allocator, path: []const u8, patch: []const u8) !void {
    if (std.process.getEnvVarOwned(allocator, "NO_ENSURE_SUBMODULES")) |no_ensure_submodules| {
        defer allocator.free(no_ensure_submodules);
        if (std.mem.eql(u8, no_ensure_submodules, "true")) return;
    } else |_| {}
    var child = std.ChildProcess.init(&.{ "git", "-C", path, "am", "-3", patch }, allocator);
    child.cwd = sdkPath("/");
    child.stderr = std.io.getStdErr();
    child.stdout = std.io.getStdOut();
    _ = try child.spawnAndWait();
}

fn ensureMake(allocator: std.mem.Allocator) void {
    const result = std.ChildProcess.exec(.{
        .allocator = allocator,
        .argv = &.{ "make", "--version" },
    }) catch { // e.g. FileNotFound
        std.log.err("error: 'make --version' failed. Is make not installed?", .{});
        std.process.exit(1);
    };
    defer {
        allocator.free(result.stderr);
        allocator.free(result.stdout);
    }
    if (result.term.Exited != 0) {
        std.log.err("error: 'make --version' failed. Is make not installed?", .{});
        std.process.exit(1);
    }
}

fn makeFacilioLibdump(allocator: std.mem.Allocator) !void {
    var child = std.ChildProcess.init(&.{
        "make",
        "-C",
        "./src/deps/facilio",
        "libdump",
    }, allocator);
    child.cwd = sdkPath("/");
    child.stderr = std.io.getStdErr();
    child.stdout = std.io.getStdOut();
    _ = try child.spawnAndWait();
}
