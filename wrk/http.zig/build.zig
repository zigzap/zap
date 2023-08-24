const std = @import("std");

pub fn build(b: *std.Build) !void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const hzzp_dep = b.dependency("httpz", .{ .target = target, .optimize = optimize });

    // setup executable
    const exe = b.addExecutable(.{
        .name = "http.zig.demo",
        .root_source_file = .{ .path = "main.zig" },
        .target = target,
        .optimize = optimize,
    });
    exe.addModule("httpz", hzzp_dep.module("httpz"));
    b.installArtifact(exe);

    const run_cmd = b.addRunArtifact(exe);
    run_cmd.step.dependOn(b.getInstallStep());
    if (b.args) |args| {
        run_cmd.addArgs(args);
    }
}
