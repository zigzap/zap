const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});

    const optimize = b.standardOptimizeOption(.{});

    const exe = b.addExecutable(.{
        .name = "cpp-beast",
        .target = target,
        .optimize = optimize,
    });
    exe.addIncludePath(.{ .path = "." });
    exe.addCSourceFiles(&.{"main.cpp"}, &.{
        "-Wall",
        "-Wextra",
        "-Wshadow",
    });
    const libasio_dep = b.dependency("beast", .{
        .target = target,
        .optimize = optimize,
    });
    const libasio = libasio_dep.artifact("beast");
    for (libasio.include_dirs.items) |include| {
        exe.include_dirs.append(include) catch {};
    }
    exe.linkLibrary(libasio);
    exe.linkLibCpp();

    b.installArtifact(exe);
    const run_cmd = b.addRunArtifact(exe);
    run_cmd.step.dependOn(b.getInstallStep());
    if (b.args) |args| {
        run_cmd.addArgs(args);
    }

    const run_step = b.step("run", "Run C++ Http Server");
    run_step.dependOn(&run_cmd.step);
}
