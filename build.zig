const std = @import("std");

const facilio = std.build.Pkg{
    .name = "facilio",
    .source = std.build.FileSource{ .path = "src/deps/facilio.zig" },
};

pub fn build(b: *std.build.Builder) !void {

    // Standard release options allow the person running `zig build` to select
    // between Debug, ReleaseSafe, ReleaseFast, and ReleaseSmall.
    const mode = b.standardReleaseOptions();

    var ensure_step = b.step("deps", "ensure external dependencies");
    ensure_step.makeFn = ensureDeps;

    const example_run_step = b.step("run-example", "run the example");
    const example_step = b.step("example", "build the example");

    var example = b.addExecutable("example", "examples/hello/hello.zig");
    example.setBuildMode(mode);
    example.addPackage(facilio);
    example.addIncludePath("src/deps/facilio/libdump/all");
    _ = try addFacilio(example);

    const example_run = example.run();
    example_run_step.dependOn(&example_run.step);

    // install the artifact
    const example_build_step = b.addInstallArtifact(example);
    // only after the ensure step
    example_build_step.step.dependOn(ensure_step);
    // via `zig build example` invoked step depends on the installed exe
    example_step.dependOn(&example_build_step.step);
}

pub fn ensureDeps(step: *std.build.Step) !void {
    _ = step;
    const allocator = std.heap.page_allocator;
    ensureGit(allocator);
    try ensureSubmodule(allocator, "src/deps/facilio");
    ensureMake(allocator);
    try makeFacilioLibdump(allocator);
}

pub fn addFacilio(exe: *std.build.LibExeObjStep) !void {
    var b = exe.builder;
    exe.linkLibC();

    // Generate flags
    var flags = std.ArrayList([]const u8).init(std.heap.page_allocator);
    if (b.is_release) try flags.append("-Os");
    try flags.append("-Wno-return-type-c-linkage");
    try flags.append("-fno-sanitize=undefined");

    exe.addIncludePath("./src/deps/facilio/libdump/all");

    // Add C
    exe.addCSourceFiles(&.{
        "src/deps/facilio/libdump/all/http.c",
        "src/deps/facilio/libdump/all/fiobj_numbers.c",
        "src/deps/facilio/libdump/all/fio_siphash.c",
        "src/deps/facilio/libdump/all/fiobj_str.c",
        "src/deps/facilio/libdump/all/http1.c",
        "src/deps/facilio/libdump/all/fiobj_ary.c",
        "src/deps/facilio/libdump/all/fiobj_data.c",
        "src/deps/facilio/libdump/all/fiobj_hash.c",
        "src/deps/facilio/libdump/all/websockets.c",
        "src/deps/facilio/libdump/all/fiobj_json.c",
        "src/deps/facilio/libdump/all/fio.c",
        "src/deps/facilio/libdump/all/fiobject.c",
        "src/deps/facilio/libdump/all/http_internal.c",
        "src/deps/facilio/libdump/all/fiobj_mustache.c",
    }, flags.items);
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
        std.log.err("mach: error: 'git --version' failed. Is git not installed?", .{});
        std.process.exit(1);
    };
    defer {
        allocator.free(result.stderr);
        allocator.free(result.stdout);
    }
    if (result.term.Exited != 0) {
        std.log.err("mach: error: 'git --version' failed. Is git not installed?", .{});
        std.process.exit(1);
    }
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
