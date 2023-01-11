const std = @import("std");

const facilio = std.build.Pkg{
    .name = "facilio",
    .source = std.build.FileSource{ .path = "src/deps/facilio.zig" },
};

pub fn build(b: *std.build.Builder) !void {

    // Standard release options allow the person running `zig build` to select
    // between Debug, ReleaseSafe, ReleaseFast, and ReleaseSmall.
    const mode = b.standardReleaseOptions();

    const lib = b.addStaticLibrary("zap", "src/main.zig");
    lib.setBuildMode(mode);
    lib.addPackage(facilio);

    const lib_facilio = try addFacilioLib(lib);
    lib.linkLibrary(lib_facilio);
    lib.install();

    const main_tests = b.addTest("src/main.zig");
    main_tests.setBuildMode(mode);

    const test_step = b.step("test", "Run library tests");
    test_step.dependOn(&main_tests.step);
}

pub fn addFacilioLib(exe: *std.build.LibExeObjStep) !*std.build.LibExeObjStep {
    ensureGit(exe.builder.allocator);
    try ensureSubmodule(exe.builder.allocator, "src/deps/facilio");
    ensureMake(exe.builder.allocator);
    try makeFacilioLibdump(exe.builder.allocator);

    var b = exe.builder;
    var lib_facilio = b.addStaticLibrary("facilio", null);
    lib_facilio.linkLibC();

    // Generate flags
    var flags = std.ArrayList([]const u8).init(std.heap.page_allocator);
    if (b.is_release) try flags.append("-Os");
    try flags.append("-Wno-return-type-c-linkage");
    try flags.append("-fno-sanitize=undefined");

    lib_facilio.addIncludePath("./src/deps/facilio/libdump/all");

    // legacy for fio_mem
    lib_facilio.addIncludePath("src/deps/facilio/lib/facil/legacy");

    // Add C
    lib_facilio.addCSourceFiles(&.{
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

    return lib_facilio;
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
