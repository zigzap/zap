const std = @import("std");

pub fn build(b: *std.Build) !void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const httpz_dep = b.dependency("httpz", .{ .target = target, .optimize = optimize });

    // setup executable
    const exe = b.addExecutable(.{
        .name = "wrk_zighttpz",
        .root_source_file = .{ .path = "main.zig" },
        .target = target,
        .optimize = optimize,
    });
    exe.root_module.addImport("httpz", httpz_dep.module("httpz"));
    b.installArtifact(exe);

    const run_cmd = b.addRunArtifact(exe);
    run_cmd.step.dependOn(b.getInstallStep());
    if (b.args) |args| {
        run_cmd.addArgs(args);
    }
}
